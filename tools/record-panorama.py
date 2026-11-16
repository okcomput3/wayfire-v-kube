#!/usr/bin/env python3
"""Record completed cube panorama frames through one encoder and frame clock."""
import argparse
import json
import os
from pathlib import Path
import shutil
import signal
import socket
import struct
import subprocess
import tempfile
import threading
import time


def read_exact(client, count):
    data = bytearray(count)
    view = memoryview(data)
    offset = 0
    while offset < count:
        received = client.recv_into(view[offset:])
        if not received:
            raise EOFError('Cube frame stream closed')
        offset += received
    return data


def ipc(path, method, data=None):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
        client.settimeout(5)
        client.connect(path)
        payload = json.dumps({'method': method, 'data': data or {}}).encode()
        client.sendall(struct.pack('<I', len(payload)) + payload)
        response = json.loads(read_exact(client, struct.unpack('<I', read_exact(client, 4))[0]))
        if 'error' in response:
            raise RuntimeError(f'{method}: {response["error"]}')
        return response


class FrameStream:
    """Keep complete frames only; never mix slices or unboundedly buffer video."""
    def __init__(self, client):
        self.client = client
        self.latest = None
        self.error = None
        self.lock = threading.Lock()
        self.available = threading.Event()
        self.thread = threading.Thread(target=self.receive, daemon=True)
        self.thread.start()

    def receive(self):
        try:
            previous = -1
            dimensions = None
            while True:
                magic, width, height, low, high = struct.unpack('<IIIII', read_exact(self.client, 20))
                stamp = low | (high << 32)
                if magic != 0x31524b56 or not 0 < width * height <= 32000000 or not width or not height:
                    raise ValueError('Invalid cube frame header')
                if dimensions is not None and dimensions != (width, height):
                    raise ValueError('Monitor dimensions changed; start a new recording')
                if stamp < previous:
                    raise ValueError('Cube frame clock went backwards')
                dimensions, previous = (width, height), stamp
                pixels = read_exact(self.client, width * height * 4)
                with self.lock:
                    self.latest = (width, height, stamp, pixels)
                self.available.set()
        except (EOFError, OSError, ValueError) as error:
            self.error = error
            self.available.set()

    def frame(self):
        with self.lock:
            return self.latest

    def close(self):
        try:
            self.client.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self.client.close()
        self.thread.join(timeout=2)


def encode_command(destination, width, height, fps, audio):
    command = ['ffmpeg', '-hide_banner', '-n', '-f', 'rawvideo', '-pixel_format', 'rgba',
               '-video_size', f'{width}x{height}', '-framerate', str(fps), '-i', 'pipe:0']
    if audio:
        command += ['-thread_queue_size', '512', '-f', 'pulse', '-i', audio]
    command += ['-map', '0:v:0']
    if audio:
        command += ['-map', '1:a:0', '-c:a', 'aac', '-af', 'aresample=async=1:first_pts=0']
    command += ['-vf', 'pad=ceil(iw/2)*2:ceil(ih/2)*2', '-c:v', 'libx264', '-preset', 'ultrafast',
                '-crf', '18', '-pix_fmt', 'yuv420p', '-shortest', str(destination)]
    return command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('file', type=Path)
    parser.add_argument('--ready-file', type=Path, help='Create this new file after writing the first video frame')
    parser.add_argument('--fps', type=int, default=30)
    parser.add_argument('--audio', action='store_true', help='Capture desktop sound from the default PulseAudio sink monitor')
    parser.add_argument('--seconds', type=float, help='Recording duration, starting with the first cube frame')
    args = parser.parse_args()
    ipc_path = os.environ.get('WAYFIRE_SOCKET') or os.environ.get('_WAYFIRE_SOCKET')
    if not ipc_path:
        parser.error('Run this in a terminal inside the simulated Wayfire desktop (WAYFIRE_SOCKET is required)')
    if not shutil.which('ffmpeg'):
        parser.error('ffmpeg is required')
    if not 1 <= args.fps <= 120 or (args.seconds is not None and args.seconds <= 0):
        parser.error('Use fps from 1 to 120 and a positive duration')
    destination = args.file.expanduser().resolve()
    if destination.exists() or not destination.parent.is_dir():
        parser.error('Choose a new filename in an existing directory')
    if args.ready_file and args.ready_file.exists():
        parser.error('Ready file already exists')
    audio_source = None
    if args.audio:
        if not shutil.which('pactl'):
            parser.error('pactl is required for desktop audio capture')
        sink = subprocess.run(['pactl', 'get-default-sink'], check=True,
                              capture_output=True, text=True).stdout.strip()
        if not sink:
            parser.error('No default audio output found')
        audio_source = sink + '.monitor'
    stopping = threading.Event()
    previous_handlers = {sig: signal.signal(sig, lambda *_: stopping.set())
                         for sig in (signal.SIGINT, signal.SIGTERM)}
    encoder = stream = None
    started = False
    try:
        with tempfile.TemporaryDirectory(prefix='vkube-record-') as directory:
            path = str(Path(directory) / 'frames.sock')
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as server:
                server.bind(path)
                server.listen(1)
                server.settimeout(5)
                ipc(ipc_path, 'vertical_kube/record_start', {'socket': path, 'fps': args.fps})
                started = True
                client, _ = server.accept()
                stream = FrameStream(client)
            print('Waiting for the cube. Activate it and press Ctrl+K for the panoramic view.\n'
                  'One complete panorama per frame; Ctrl+C stops recording.\n'
                  'Closing the cube holds the last frame until you reopen it or stop.', flush=True)
            while not stream.available.wait(.1):
                if stopping.is_set():
                    return
            if stream.error:
                raise RuntimeError(str(stream.error))
            first_frame = stream.frame()
            width, height, _, _ = first_frame
            encoder = subprocess.Popen(encode_command(destination, width, height, args.fps, audio_source),
                                       stdin=subprocess.PIPE, start_new_session=True)
            print(f'Recording {width}x{height} to {destination}', flush=True)
            epoch = time.monotonic()
            index = 0
            while not stopping.is_set():
                deadline = epoch + index / args.fps
                if args.seconds is not None and index / args.fps >= args.seconds:
                    break
                if stopping.wait(max(0, deadline - time.monotonic())):
                    break
                if stream.error:
                    raise RuntimeError(str(stream.error))
                if encoder.poll() is not None:
                    raise RuntimeError('Video encoder stopped unexpectedly')
                # A late or dropped source frame repeats the entire last panorama.
                # There is never an independently advancing monitor timeline.
                encoder.stdin.write(first_frame[3] if index == 0 else stream.frame()[3])
                if index == 0:
                    encoder.stdin.flush()
                    if args.ready_file:
                        with args.ready_file.open('x') as ready:
                            ready.write(f'{width}x{height}\n')
                index += 1
    finally:
        if stream:
            stream.close()
        if started:
            try:
                ipc(ipc_path, 'vertical_kube/record_stop')
            except (OSError, EOFError, RuntimeError) as error:
                print(f'Recorder cleanup: {error}')
        if encoder:
            try:
                encoder.stdin.close()
            except BrokenPipeError:
                pass
            try:
                result = encoder.wait(timeout=15)
            except subprocess.TimeoutExpired:
                encoder.terminate()
                result = encoder.wait(timeout=5)
            if result:
                print(f'Encoder exited with status {result}; check its output above.')
        for sig, handler in previous_handlers.items():
            signal.signal(sig, handler)
    if encoder:
        if encoder.returncode != 0:
            raise RuntimeError(f'Encoder failed with status {encoder.returncode}')
        print(f'Saved: {destination}')


if __name__ == '__main__':
    try:
        main()
    except (OSError, EOFError, RuntimeError) as error:
        raise SystemExit(str(error))
