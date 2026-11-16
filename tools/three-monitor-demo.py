#!/usr/bin/env python3
"""Run three nested Wayfire outputs without changing the main session's config."""
import argparse
import configparser
import json
import os
from pathlib import Path
import shlex
import shutil
import signal
import socket
import struct
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]

def ipc(path, method, data=None):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
        client.settimeout(10)
        client.connect(str(path))
        payload = json.dumps({'method': method, 'data': data or {}}).encode()
        client.sendall(struct.pack('<I', len(payload)) + payload)
        def read(count):
            result = b''
            while len(result) < count:
                part = client.recv(count - len(result))
                if not part:
                    raise RuntimeError('Nested compositor closed its IPC connection')
                result += part
            return result
        reply = json.loads(read(struct.unpack('<I', read(4))[0]))
        if isinstance(reply, dict) and 'error' in reply:
            raise RuntimeError(f'{method}: {reply}')
        return reply

def desktop_style(config, config_path, shell_path):
    """Copy appearance only; never launch the main session's autostart services."""
    current = configparser.ConfigParser(interpolation=None, strict=False)
    if not current.read(config_path):
        raise ValueError(f'Cannot read desktop settings: {config_path}')
    bindings = {key: value for key, value in config['vertical_kube'].items()
                if key == 'activate' or key.startswith('rotate_') or key == 'mirror_outputs'}
    for section in ('vertical_kube', 'blur', 'decoration', 'wobbly', 'move', 'resize'):
        if current.has_section(section):
            config[section] = dict(current[section])
    config['vertical_kube'].update(bindings)
    for key in ('vwidth', 'vheight', 'background_color', 'preferred_decoration_mode'):
        if current.has_option('core', key):
            config['core'][key] = current['core'][key]
    plugins = config['core']['plugins'].split()
    # Native blur settings also supply the cube's glass blur implementation.
    plugins.append('blur')
    if 'wobbly' in current.get('core', 'plugins', fallback='').split():
        plugins.append('wobbly')
    config['core']['plugins'] = ' '.join(plugins)
    shell = configparser.ConfigParser(interpolation=None, strict=False)
    shell.read(shell_path)
    wallpaper = Path(shell.get('background', 'image', fallback='')).expanduser()
    if wallpaper.is_file():
        mode = shell.get('background', 'fill_mode', fallback='fill')
        if mode not in ('stretch', 'fill', 'fit', 'center', 'tile'):
            mode = 'fill'
        return ['swaybg', '-i', str(wallpaper), '-m', mode]
    return ['swaybg', '-c', '#334466']

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', type=Path, default=Path('/way/wayfire'))
    parser.add_argument('--build-dir', type=Path, help='Use an uninstalled plugin from this Meson build')
    parser.add_argument('--backend', choices=['wayland', 'headless'], help='Override the test backend')
    parser.add_argument('--software', action='store_true', help='Use software GLES if GPU nesting fails')
    parser.add_argument('--smoke', action='store_true', help='Run an automated three-output check and exit')
    parser.add_argument('--size', default='640x360', help='Size of each simulated monitor (default: 640x360)')
    parser.add_argument('--smoke-alternative', action='store_true', help='Preview alternative nebula background')
    parser.add_argument('--smoke-cosmic', action='store_true', help='Test optional cosmic accents')
    parser.add_argument('--smoke-mist', action='store_true', help='Test mist animation and settings')
    parser.add_argument('--smoke-searchlights', action='store_true', help='Test rotating hue beams')
    parser.add_argument('--smoke-cinematic', action='store_true', help='Test Ctrl+Alt+Shift camera sweep')
    parser.add_argument('--smoke-animated', action='store_true', help='Test Fox skeletal animation playback')
    parser.add_argument('--smoke-glb', action='store_true', help='Test embedded GLB materials')
    parser.add_argument('--smoke-object', action='store_true', help='Test an OBJ inside transparent cubes')
    parser.add_argument('--smoke-desktop-thickness', action='store_true', help='Exercise desktop edge extrusion')
    parser.add_argument('--smoke-thickness', action='store_true', help='Exercise border pixel extrusion')
    parser.add_argument('--smoke-window-depth', action='store_true', help='Exercise separately popped-out windows')
    parser.add_argument('--smoke-caps', action='store_true', help='Test cap pictures with 3D ripples')
    parser.add_argument('--smoke-deform', choices=['0', '1', '2'], default='0', help='Cube/cylinder/star for the smoke test')
    parser.add_argument('--smoke-blur', nargs='?', const='kawase', choices=['kawase', 'mipmap_gaussian', 'swirlblur'], help='Exercise native desktop and cap blur during the smoke recording')
    parser.add_argument('--plain', action='store_true', help='Use the minimal blue test scene instead of your appearance')
    parser.add_argument('--config', type=Path, default=Path.home() / '.config/wayfire.ini', help='Desktop settings to copy for the interactive demo')
    args = parser.parse_args()
    try:
        width, height = map(int, args.size.split('x'))
        assert 160 <= width <= 4096 and 120 <= height <= 2160
    except (ValueError, AssertionError):
        parser.error('--size must be WIDTHxHEIGHT, at least 160x120 and at most 4096x2160')
    backend = args.backend or 'wayland'
    if backend == 'wayland' and not os.environ.get('WAYLAND_DISPLAY'):
        parser.error('Run this from a terminal inside your Wayfire/Wayland desktop')
    binary = args.prefix / 'bin/wayfire'
    if not binary.is_file():
        parser.error(f'Wayfire not found: {binary}')
    run = Path(tempfile.mkdtemp(prefix='v-kube-three-monitors-'))
    config = configparser.ConfigParser(interpolation=None)
    config['core'] = {'plugins': 'ipc ipc-rules stipc command move resize decoration vertical_kube',
                      'vwidth': '4', 'vheight': '3', 'xwayland': 'false'}
    config['vertical_kube'] = {
        'mirror_outputs': 'true', 'activate': '<alt> <shift> BTN_LEFT',
        'rotate_left': '<alt> <shift> KEY_LEFT', 'rotate_right': '<alt> <shift> KEY_RIGHT',
        'rotate_up': '<alt> <shift> KEY_UP', 'rotate_down': '<alt> <shift> KEY_DOWN',
        'background_mode': 'simple', 'background': '0.094 0.133 0.22 1.0', 'deform': '0',
        'light': 'false', 'dynamic_lighting': 'true', 'enable_caps': 'false',
        'desktop_opacity': '0.6', 'desktop_blur': 'false', 'cap_blur': 'false',
        'enable_window_popout': 'true', 'popout_scale': '1.2', 'popout_opacity': '1',
        'zoom': '1', 'initial_animation': '350ms','cap_bump_mapping' : 'true'
    }
    wallpaper_command = ['swaybg', '-c', '#334466']
    if not args.plain:
        try:
            wallpaper_command = desktop_style(config, args.config, Path.home() / '.config/wf-shell.ini')
        except ValueError as error:
            parser.error(str(error))
    if shutil.which('foot'):
        config['command'] = {'binding_terminal': '<alt> <shift> KEY_ENTER', 'command_terminal': 'foot'}
    name = 'HEADLESS' if backend == 'headless' else 'WL'
    for i in range(1, 4):
        config[f'output:{name}-{i}'] = {'mode': f'{width}x{height}',
            'position': f'{(i-1)*width},0', 'scale': '1', 'transform': 'normal'}
    ini = run / 'wayfire.ini'
    with ini.open('w') as file:
        config.write(file)
    if args.smoke_blur:
        config['core']['plugins'] += ' blur'
        config['vertical_kube'].update({'desktop_blur': 'true', 'cap_blur': 'true',
            'enable_caps': 'true', 'cap_alpha': '0.6',
            # Keep sequential screenshots comparable while testing blur.
            'cap_bump_mapping': 'false'})
        config['blur'] = {'method': args.smoke_blur, 'kawase_iterations': '3', 'kawase_offset': '3'}
        with (run / 'wayfire.ini').open('w') as config_file:
            config.write(config_file)
    if args.smoke_caps:
        from PIL import Image, ImageDraw
        picture = Image.new('RGB', (128, 128), '#ff00ff')
        ImageDraw.Draw(picture).rectangle((24, 24, 104, 104), outline='#ffffff', width=8)
        picture.save(run / 'cap-test.png')
        config['vertical_kube'].update({'enable_caps': 'true', 'cap_bump_3d': 'true', 'dynamic_lighting': 'false',
            'cap_texture_top': str(run / 'cap-test.png'), 'cap_texture_bottom': str(run / 'cap-test.png')})
        with ini.open('w') as config_file:
            config.write(config_file)
    if args.smoke_object or args.smoke_glb or args.smoke_animated:
        config['vertical_kube'].update({'object_enabled': 'true',
            'object_path': str(Path(__file__).resolve().parents[1] / ('examples/animated/fox/Fox.glb' if args.smoke_animated else 'examples/textured-triangle.glb' if args.smoke_glb else 'examples/cube-object.obj')),
            'desktop_opacity': '0.35', 'cap_alpha': '0.35', 'dynamic_lighting': 'false',
            'object_animation': 'false' if args.smoke_animated else 'true', 'object_animation_clip': '2'})
        if args.smoke_animated:
            config['vertical_kube'].update({'object_rotation': '90', 'object_scale': '0.85',
                'object_flip_vertical': 'false', 'desktop_opacity': '0.35'})
        with (run / 'wayfire.ini').open('w') as config_file:
            config.write(config_file)
    # Pixel comparisons below require static lighting between sequential screenshots.
    if args.smoke:
        config['vertical_kube']['dynamic_lighting'] = 'false'
        with ini.open('w') as file: config.write(file)
    if args.smoke_desktop_thickness:
        config['vertical_kube'].update({'desktop_thickness': '0.08', 'dynamic_lighting': 'false'})
        with ini.open('w') as file: config.write(file)
    if args.smoke_thickness:
        config['vertical_kube'].update({'window_thickness': '0.08', 'dynamic_lighting': 'false'})
        config['core']['plugins'] += ' blur'
        config['blur'] = {'method': 'kawase', 'blur_by_default': 'all'}
        with ini.open('w') as file: config.write(file)
    if args.smoke_window_depth:
        config['vertical_kube'].update({'per_window_depth': 'true', 'window_depth_spacing': '0.5',
            'dynamic_lighting': 'false'})
        with ini.open('w') as config_file:
            config.write(config_file)
    env = os.environ.copy()
    lib = args.prefix / 'lib/x86_64-linux-gnu'
    env['LD_LIBRARY_PATH'] = str(lib) + os.pathsep + env.get('LD_LIBRARY_PATH', '')
    paths = [str(lib / 'wayfire')]
    if args.build_dir:
        plugin = args.build_dir.resolve() / 'src'
        if not (plugin / 'libvertical_kube.so').is_file():
            parser.error(f'Plugin missing from {plugin}')
        paths.insert(0, str(plugin))
    env['WAYFIRE_PLUGIN_PATH'] = os.pathsep.join(paths)
    env['WAYFIRE_PLUGIN_XML_PATH'] = str(ROOT / 'wf-v-kube/metadata')
    # Isolate user metadata overrides as well as the nested configuration.
    env['XDG_DATA_HOME'] = str(run / 'data')
    env['WLR_BACKENDS'] = backend
    env['WLR_HEADLESS_OUTPUTS' if backend == 'headless' else 'WLR_WL_OUTPUTS'] = '3'
    env['WLR_RENDERER'] = 'gles2'
    env['_WAYFIRE_SOCKET'] = str(run / 'ipc.socket')
    env.pop('WAYFIRE_SOCKET', None)
    if args.software:
        env.update(WLR_RENDERER_ALLOW_SOFTWARE='1', WLR_RENDERER_FORCE_SOFTWARE='1',
                   LIBGL_ALWAYS_SOFTWARE='1')
    log = (run / 'wayfire.log').open('w')
    processes = []
    print(f'Test files and log: {run}', flush=True)
    print('Three simulated monitors; arrange their windows side by side.\n'
          'Hold Alt+Shift+left mouse to open/rotate; release to close.\n'
          'Alt+Shift+arrows navigate; wheel/two-finger scroll zooms.\n'
          'Ctrl+K while open joins the monitors into one panoramic cube; press again to return.\n'
          'Alt+Shift+Enter opens another terminal. Ctrl+C here stops the test.', flush=True)
    try:
        compositor = subprocess.Popen([str(binary), '-c', str(ini)], env=env,
            stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        processes.append(compositor)
        deadline = time.monotonic() + 25
        while not (run / 'ipc.socket').exists():
            if compositor.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError(f'Nested Wayfire did not start; read {run / "wayfire.log"}')
            time.sleep(.1)
        display = ipc(run / 'ipc.socket', 'stipc/get_display')['wayland']
        child_env = env | {'WAYLAND_DISPLAY': display, 'WAYFIRE_SOCKET': str(run / 'ipc.socket')}
        outputs = ipc(run / 'ipc.socket', 'window-rules/list-outputs')
        if len(outputs) != 3:
            raise RuntimeError(f'Expected three outputs, got {outputs}')
        for command in ([wallpaper_command] if shutil.which('swaybg') else []):
            processes.append(subprocess.Popen(command, env=child_env, stdout=log,
                stderr=log, start_new_session=True))
        if shutil.which('foot') and args.smoke:
            processes.append(subprocess.Popen(['foot', '--title=Cube mirror test', '--window-size-chars=42x10',
                'sh', '-c', "printf '\033[?25lMirror test window\nActivate on this monitor.\nAll three cubes should show this window.\n'; sleep 86400"],
                env=child_env, stdout=log, stderr=log, start_new_session=True))
        if shutil.which('foot') and not args.smoke:
            for i, output in enumerate(outputs, 1):
                title = f'Simulated monitor {i}'
                processes.append(subprocess.Popen(['foot', f'--title={title}', f'--app-id=vkube-demo-{i}', '--window-size-chars=42x10'],
                    env=child_env, stdout=log, stderr=log, start_new_session=True))
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    views = ipc(run / 'ipc.socket', 'window-rules/list-views')
                    view = next((v for v in views if v.get('app-id') == f'vkube-demo-{i}'), None)
                    if view:
                        ipc(run / 'ipc.socket', 'window-rules/configure-view',
                            {'id': view['id'], 'output_id': output['id'],
                             'geometry': {'x': 30, 'y': 35, 'width': max(100, width-60),
                                          'height': max(80, height-70)}})
                        break
                    time.sleep(.1)
        if args.smoke_window_depth:
            processes.append(subprocess.Popen(['foot', '-o', 'colors.alpha=0.75', '-o', 'colors.background=103010', '--title=Depth front test', '--window-size-chars=28x8',
                'sh', '-c', "printf '\033[?25lFront depth layer\n'; sleep 86400"],
                env=child_env, stdout=log, stderr=log, start_new_session=True))
        if args.smoke:
            time.sleep(2)
            views = ipc(run / 'ipc.socket', 'window-rules/list-views')
            source = next((v['output-id'] for v in views if v.get('title') == 'Cube mirror test'), outputs[0]['id'])
            if args.smoke_window_depth:
                back = next(v for v in views if v.get('title') == 'Cube mirror test')
                front = next(v for v in views if v.get('title') == 'Depth front test')
                for view, x, y in [(back, 35, 40), (front, 130, 65)]:
                    ipc(run / 'ipc.socket', 'window-rules/configure-view',
                        {'id': view['id'], 'output_id': source,
                         'geometry': {'x': x, 'y': y, 'width': 260, 'height': 160}})
                time.sleep(.3)
            ipc(run / 'ipc.socket', 'vertical_kube/activate', {'output_id': source})
            time.sleep(1)
            # Every monitor must contain the same source scene after settling.
            from PIL import Image, ImageChops, ImageStat
            images = []
            for output in outputs:
                path = run / f'{output["name"]}.png'
                subprocess.run(['grim', '-o', output['name'], str(path)], env=child_env, check=True)
                images.append(Image.open(path).convert('RGB'))
            for image in images[1:]:
                difference = ImageStat.Stat(ImageChops.difference(images[0], image))
                assert max(difference.mean) < 2, difference.mean
            if args.smoke_animated:
                config['vertical_kube']['object_animation'] = 'true'
                with ini.open('w') as file: config.write(file)
                time.sleep(.6)
                frame_a, frame_b = run / 'fox-animation-a.png', run / 'fox-animation-b.png'
                subprocess.run(['grim', '-o', outputs[0]['name'], str(frame_a)], env=child_env, check=True)
                time.sleep(.23)
                subprocess.run(['grim', '-o', outputs[0]['name'], str(frame_b)], env=child_env, check=True)
                motion = ImageStat.Stat(ImageChops.difference(Image.open(frame_a).convert('RGB'),
                    Image.open(frame_b).convert('RGB'))).mean
                assert max(motion) > .05, ('Fox animation did not move', motion)
                config['vertical_kube']['object_animation'] = 'false'
                with ini.open('w') as file: config.write(file)
                time.sleep(.5)
                print('PASS: animated textured Fox moves; playback can be disabled', flush=True)
            if args.smoke_cinematic or args.smoke_thickness or args.smoke_desktop_thickness:
                for key in ['KEY_LEFTCTRL', 'KEY_LEFTALT', 'KEY_LEFTSHIFT', 'KEY_RIGHT']:
                    ipc(run / 'ipc.socket', 'stipc/feed_key', {'key': key, 'state': True})
                for key in ['KEY_RIGHT', 'KEY_LEFTSHIFT', 'KEY_LEFTALT', 'KEY_LEFTCTRL']:
                    ipc(run / 'ipc.socket', 'stipc/feed_key', {'key': key, 'state': False})
                time.sleep(.8)
                shot = run / 'cinematic-sweep.png'
                subprocess.run(['grim', '-o', outputs[0]['name'], str(shot)], env=child_env, check=True)
                difference = ImageStat.Stat(ImageChops.difference(images[0], Image.open(shot).convert('RGB')))
                assert max(difference.mean) > 2, 'Cinematic binding did not move camera'
                time.sleep(2.2)
                shot = run / 'cinematic-restored.png'
                subprocess.run(['grim', '-o', outputs[0]['name'], str(shot)], env=child_env, check=True)
                difference = ImageStat.Stat(ImageChops.difference(images[0], Image.open(shot).convert('RGB')))
                assert max(difference.mean) < 2, 'Camera failed to return to original view'
                print('PASS: cinematic key binding moves camera and returns smoothly', flush=True)
            if args.smoke_alternative:
                config['vertical_kube']['background_mode']='alternative_shader'
                with ini.open('w') as file: config.write(file)
                time.sleep(.5)
                subprocess.run(['grim','-o',outputs[0]['name'],str(run/'alternative-background.png')],env=child_env,check=True)
                time.sleep(.4)
                subprocess.run(['grim','-o',outputs[0]['name'],str(run/'alternative-background-b.png')],env=child_env,check=True)
                a=Image.open(run/'alternative-background.png').convert('RGB')
                b=Image.open(run/'alternative-background-b.png').convert('RGB')
                assert max(ImageStat.Stat(ImageChops.difference(a,b)).mean)>.01, 'Alternative background frozen'
                config['vertical_kube']['background_mode']='simple'
                with ini.open('w') as file: config.write(file)
                time.sleep(.3)
                print('PASS: alternative background renders and animates',flush=True)
            if args.smoke_cosmic:
                saved_caps=config['vertical_kube']['enable_caps']
                config['vertical_kube'].update({'cosmic_rings': 'true', 'cosmic_sparkles': 'true',
                    'cosmic_rims': 'true', 'cap_crystal': 'true', 'enable_caps': 'true'})
                with ini.open('w') as file: config.write(file)
                time.sleep(.6)
                a,b,off=run/'cosmic-a.png',run/'cosmic-b.png',run/'cosmic-off.png'
                subprocess.run(['grim','-o',outputs[0]['name'],str(a)],env=child_env,check=True)
                time.sleep(.5)
                subprocess.run(['grim','-o',outputs[0]['name'],str(b)],env=child_env,check=True)
                config['vertical_kube'].update({'bloom_enabled': 'true', 'bloom_strength': '2.0'})
                with ini.open('w') as file: config.write(file)
                time.sleep(.4)
                subprocess.run(['grim','-o',outputs[0]['name'],str(run/'cosmic-bloom.png')],env=child_env,check=True)
                config['vertical_kube']['bloom_quality']='1'
                with ini.open('w') as file: config.write(file)
                time.sleep(.4)
                subprocess.run(['grim','-o',outputs[0]['name'],str(run/'cosmic-bloom-high.png')],env=child_env,check=True)
                config['vertical_kube']['bloom_enabled']='false'
                change=ImageStat.Stat(ImageChops.difference(Image.open(a).convert('RGB'),Image.open(b).convert('RGB')))
                assert max(change.mean)>.05, 'Cosmic accents did not animate'
                config['vertical_kube'].update({'cosmic_rings': 'false', 'cosmic_sparkles': 'false',
                    'cosmic_rims': 'false', 'cap_crystal': 'false', 'enable_caps': saved_caps})
                with ini.open('w') as file: config.write(file)
                time.sleep(.4)
                subprocess.run(['grim','-o',outputs[0]['name'],str(off)],env=child_env,check=True)
                change=ImageStat.Stat(ImageChops.difference(images[0],Image.open(off).convert('RGB')))
                assert max(change.mean)<2, 'Cosmic toggle did not restore previous appearance'
                print('PASS: cosmic accents animation and restoring original appearance',flush=True)
            if args.smoke_mist:
                config['vertical_kube'].update({'mist_enabled': 'true', 'mist_density': '3.0'})
                with ini.open('w') as file: config.write(file)
                time.sleep(.6)
                a, b, off = run / 'mist-a.png', run / 'mist-b.png', run / 'mist-off.png'
                subprocess.run(['grim', '-o', outputs[0]['name'], str(a)], env=child_env, check=True)
                time.sleep(.5)
                subprocess.run(['grim', '-o', outputs[0]['name'], str(b)], env=child_env, check=True)
                motion = ImageStat.Stat(ImageChops.difference(Image.open(a).convert('RGB'), Image.open(b).convert('RGB')))
                assert max(motion.mean) > .05, 'Mist did not animate'
                config['vertical_kube']['mist_enabled'] = 'false'
                with ini.open('w') as file: config.write(file)
                time.sleep(.4)
                subprocess.run(['grim', '-o', outputs[0]['name'], str(off)], env=child_env, check=True)
                difference = ImageStat.Stat(ImageChops.difference(images[0], Image.open(off).convert('RGB')))
                assert max(difference.mean) < 2, 'Disabling mist failed to restore scene'
                visible = ImageStat.Stat(ImageChops.difference(Image.open(a).convert('RGB'), Image.open(off).convert('RGB')))
                assert max(visible.mean) > .1, 'Mist was not visible'
                print('PASS: mist visibility, animation and off toggle', flush=True)
            if args.smoke_searchlights:
                config['vertical_kube']['searchlights_enabled'] = 'true'
                config['vertical_kube']['searchlights_refraction'] = 'true'
                config['vertical_kube']['searchlights_length'] = '4.0'
                config['vertical_kube']['searchlights_intensity'] = '2.0'
                with ini.open('w') as file: config.write(file)
                time.sleep(.6)
                a, b = run / 'searchlights-a.png', run / 'searchlights-b.png'
                subprocess.run(['grim', '-o', outputs[0]['name'], str(a)], env=child_env, check=True)
                time.sleep(.3)
                subprocess.run(['grim', '-o', outputs[0]['name'], str(b)], env=child_env, check=True)
                motion = ImageStat.Stat(ImageChops.difference(Image.open(a).convert('RGB'), Image.open(b).convert('RGB')))
                assert max(motion.mean) > .1, 'Searchlights did not animate'
                config['vertical_kube']['searchlights_enabled'] = 'false'
                with ini.open('w') as file: config.write(file)
                time.sleep(.4)
                print('PASS: searchlight animation and settings toggle', flush=True)
            # Distinct wallpapers make it possible to verify that the panoramic
            # face contains each monitor's content, rather than repeated copies.
            paint = ['swaybg']
            for output, color in zip(sorted(outputs, key=lambda o: o['geometry']['x']),
                                     ['#cc2222', '#22cc22', '#2222cc']):
                paint += ['-o', output['name'], '-c', color]
            processes.append(subprocess.Popen(paint, env=child_env, stdout=log,
                stderr=log, start_new_session=True))
            time.sleep(.3)
            # Exercise the actual Ctrl+K binding while the cube owns input.
            for key in ['KEY_LEFTCTRL', 'KEY_K']:
                ipc(run / 'ipc.socket', 'stipc/feed_key', {'key': key, 'state': True})
            for key in ['KEY_K', 'KEY_LEFTCTRL']:
                ipc(run / 'ipc.socket', 'stipc/feed_key', {'key': key, 'state': False})
            time.sleep(1)
            panoramic_images = []
            for output in sorted(outputs, key=lambda o: o['geometry']['x']):
                path = run / f'panorama-{output["name"]}.png'
                subprocess.run(['grim', '-o', output['name'], str(path)], env=child_env, check=True)
                panoramic_images.append(Image.open(path).convert('RGB'))
            assert max(ImageStat.Stat(ImageChops.difference(panoramic_images[0], panoramic_images[1])).mean) > 3, 'Ctrl+K did not change mirrored projection'
            stitched = Image.new('RGB', (sum(i.width for i in panoramic_images), panoramic_images[0].height))
            x = 0
            for image in panoramic_images:
                stitched.paste(image, (x, 0)); x += image.width
            stitched.save(run / 'panorama.png')
            if args.smoke_caps:
                assert sum(r > g + 40 and b > g + 40 for r,g,b in stitched.getdata()) > 100, 'Cap picture did not render'

            for channel in range(3):
                assert sum(p[channel] > max(p[(channel+1)%3], p[(channel+2)%3]) + 30
                           for p in stitched.getdata()) > 100, 'Missing monitor content in panoramic face'
            # One native frame stream and one encoder: no independent output clips.
            recording_log = (run / 'recording.log').open('w')
            recorder = subprocess.Popen(['python3', str(ROOT / 'tools/record-panorama.py'),
                str(run / 'universal.mp4'), '--fps', '15', '--seconds', '2', '--ready-file', str(run/'recording.ready')],
                env=child_env, stdout=recording_log, stderr=recording_log, start_new_session=True)
            processes.append(recorder)
            deadline=time.monotonic()+15
            while not (run/'recording.ready').exists():
                assert recorder.poll() is None and time.monotonic()<deadline, 'Recorder did not produce its first frame'
                time.sleep(.05)
            time.sleep(.2)
            # Rotate the cohesive view, then return to the original face.
            follower = next(o['id'] for o in outputs if o['id'] != source)
            ipc(run / 'ipc.socket', 'vertical_kube/rotate_right', {'output_id': follower})
            time.sleep(.6)
            subprocess.run(['grim', '-o', outputs[0]['name'], str(run / 'panorama-rotated.png')], env=child_env, check=True)
            ipc(run / 'ipc.socket', 'vertical_kube/rotate_left', {'output_id': source})
            time.sleep(.6)
            recorder.wait(timeout=15)
            recording_log.close()
            assert recorder.returncode == 0, f'Universal recorder failed: {run / "recording.log"}'
            subprocess.run(['ffmpeg', '-v', 'error', '-i', str(run / 'universal.mp4'),
                '-frames:v', '1', str(run / 'universal-first.png')], check=True)
            first_recorded = Image.open(run / 'universal-first.png').convert('RGB')
            assert first_recorded.size == stitched.size, 'Universal recording dimensions differ'
            comparison = ImageChops.difference(first_recorded, stitched)
            if args.smoke_blur or args.smoke_caps:
                # Caps animate between the sequential screenshot and recording.
                # Compare the static upper desktop/window region, not moving ripples.
                comparison = comparison.crop((0, 0, stitched.width, int(stitched.height * .6)))
            assert max(ImageStat.Stat(comparison).mean) < 4, 'Universal recording is flipped or differs from the joined cube'
            decoded = subprocess.run(['ffmpeg', '-v', 'error', '-i', str(run / 'universal.mp4'),
                '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'], capture_output=True, check=True).stdout
            stride = stitched.width * stitched.height * 3
            assert len(decoded) == 30 * stride, 'Universal encoder did not produce one two-second timeline'
            assert decoded[:stride] != decoded[stride * 12:stride * 13], 'Rotation missing from recording'
            for frame in range(30):
                pixels = decoded[frame * stride:(frame + 1) * stride]
                for seam in (width, width * 2):
                    differences = 0
                    for y in range(stitched.height):
                        occupied = []
                        for x in (seam - 1, seam):
                            offset = (y * stitched.width + x) * 3
                            occupied.append(max(abs(pixels[offset + c] - (24, 34, 56)[c]) for c in range(3)) > 12)
                        differences += occupied[0] != occupied[1]
                    assert differences <= (5 if args.smoke_deform == '0' else 30), f'Cube silhouette jumps at seam {seam} in frame {frame}: {differences} pixels'

            close_log = (run / 'closing-recording.log').open('w')
            closing_recorder = subprocess.Popen(['python3', str(ROOT / 'tools/record-panorama.py'),
                str(run / 'closing.mp4'), '--fps', '30', '--seconds', '2', '--ready-file', str(run/'closing.ready')],
                env=child_env, stdout=close_log, stderr=close_log, start_new_session=True)
            processes.append(closing_recorder)
            deadline=time.monotonic()+15
            while not (run/'closing.ready').exists():
                assert closing_recorder.poll() is None and time.monotonic()<deadline, 'Closing recorder did not produce its first frame'
                time.sleep(.05)
            time.sleep(.2)
            # Closing must preserve the selected mode, including when reopened
            # from a different monitor. Only Ctrl+K switches back to mirrors.
            ipc(run / 'ipc.socket', 'stipc/feed_button', {'combo': 'BTN_LEFT', 'mode': 'release'})
            time.sleep(.9)
            closing_recorder.wait(timeout=15)
            close_log.close()
            assert closing_recorder.returncode == 0, 'Closing animation recording failed'
            subprocess.run(['ffmpeg', '-v', 'error', '-sseof', '-0.05', '-i', str(run / 'closing.mp4'),
                '-frames:v', '1', str(run / 'closing-final.png')], check=True)
            flat = Image.open(run / 'closing-final.png').convert('RGB')
            # The shared face must end with each desktop in its own monitor slice,
            # rather than crossfading full-sized copies over the joined atlas.
            for column in range(3):
                sample = flat.crop((column * width + 4 * width // 5, height // 2,
                    column * width + 9 * width // 10, 3 * height // 4))
                means = ImageStat.Stat(sample).mean
                assert means[column] > max(means[(column+1)%3], means[(column+2)%3]) + 30, 'Panoramic close did not land on the correct desktop'
            reopen_source = next(o['id'] for o in outputs if o['id'] != source)
            ipc(run / 'ipc.socket', 'vertical_kube/activate', {'output_id': reopen_source})
            time.sleep(1.2)
            remembered = []
            for output in outputs:
                path = run / f'remembered-{output["name"]}.png'
                subprocess.run(['grim', '-o', output['name'], str(path)], env=child_env, check=True)
                remembered.append(Image.open(path).convert('RGB'))
            assert max(ImageStat.Stat(ImageChops.difference(remembered[0], remembered[1])).mean) > 3, 'Panoramic mode reset after returning to desktop'
            ipc(run / 'ipc.socket', 'vertical_kube/toggle_panorama', {'output_id': source})
            time.sleep(.8)
            returned = []
            for output in outputs:
                path = run / f'returned-{output["name"]}.png'
                subprocess.run(['grim', '-o', output['name'], str(path)], env=child_env, check=True)
                returned.append(Image.open(path).convert('RGB'))
            for image in returned[1:]:
                assert max(ImageStat.Stat(ImageChops.difference(returned[0], image)).mean) < 2, 'Return to mirrors failed'
            # Navigation issued to another output must reach the same controller.
            other = next(o['id'] for o in outputs if o['id'] != source)
            ipc(run / 'ipc.socket', 'vertical_kube/rotate_right', {'output_id': other})
            time.sleep(.6)
            ipc(run / 'ipc.socket', 'stipc/feed_button', {'combo': 'BTN_LEFT', 'mode': 'release'})
            time.sleep(.7)
            states = ipc(run / 'ipc.socket', 'window-rules/list-outputs')
            workspaces = [o['workspace'] for o in states]
            assert all(w == workspaces[0] for w in workspaces), workspaces
            # Reopen from a non-origin output and validate source coordinates.
            source_output = max(outputs, key=lambda o: o['geometry']['x'])
            terminal = next((v for v in views if v.get('title') == 'Cube mirror test'), None)
            if terminal:
                ipc(run / 'ipc.socket', 'window-rules/configure-view',
                    {'id': terminal['id'], 'output_id': source_output['id'],
                     'geometry': {'x': 30, 'y': 35, 'width': 320, 'height': 180}})
                time.sleep(.3)
                subprocess.run(['grim', '-o', source_output['name'], str(run / 'source-before-cube.png')], env=child_env, check=True)
            ipc(run / 'ipc.socket', 'vertical_kube/activate', {'output_id': source_output['id']})
            time.sleep(1)
            second_images = []
            for output in outputs:
                path = run / f'reopened-{output["name"]}.png'
                subprocess.run(['grim', '-o', output['name'], str(path)], env=child_env, check=True)
                second_images.append(Image.open(path).convert('RGB'))
            if terminal:
                assert sum(20 < p[0] < 50 and max(p)-min(p) < 5 for p in second_images[0].getdata()) > 100, 'Source terminal missing from mirrored cube'
            for image in second_images[1:]:
                assert max(ImageStat.Stat(ImageChops.difference(second_images[0], image)).mean) < 2
            if backend == 'wayland':
                # Unplugging a follower while active must cancel cleanly.
                victim = next(o for o in outputs if o['id'] != source_output['id'])
                ipc(run / 'ipc.socket', 'stipc/destroy_wayland_output', {'output': victim['name']})
                time.sleep(.3)
                assert len(ipc(run / 'ipc.socket', 'window-rules/list-outputs')) == 2
                ipc(run / 'ipc.socket', 'vertical_kube/activate', {'output_id': source_output['id']})
                time.sleep(.5)
            ipc(run / 'ipc.socket', 'stipc/feed_button', {'combo': 'BTN_LEFT', 'mode': 'release'})
            time.sleep(.7)
            print('PASS: mirrors, Ctrl+K panorama, distinct desktops, panoramic rotation, universal recording with aligned seams, return transition, synchronized exit, reopening and output removal', flush=True)
        else:
            compositor.wait()
    except KeyboardInterrupt:
        pass
    finally:
        for process in reversed(processes):
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
        log.close()

if __name__ == '__main__':
    main()
