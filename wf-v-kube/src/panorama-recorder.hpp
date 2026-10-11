#pragma once
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <poll.h>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// One complete panoramic frame per packet. Socket I/O never blocks rendering;
// a slow encoder drops whole frames, never individual monitor slices.
class panorama_recorder_t
{
    int fd = -1;
    std::atomic<bool> connected{false};
    std::thread sender;
    std::mutex mutex;
    std::condition_variable ready;
    std::vector<unsigned char> queued;
    void *owner = nullptr;
    double interval = 1.0 / 30;
    std::chrono::steady_clock::time_point next_frame{};

  public:
    ~panorama_recorder_t() { stop(); }
    bool active() const { return connected; }
    bool start(const std::string& path, int fps)
    {
        if (active() || path.empty() || path.size() >= sizeof(sockaddr_un::sun_path)) return false;
        stop();
        fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
        if (fd < 0) return false;
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
        if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
        {
            close(fd); fd = -1; return false;
        }
        interval = 1.0 / std::clamp(fps, 1, 120);
        connected = true;
        sender = std::thread([this]
        {
            while (connected)
            {
                std::vector<unsigned char> packet;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    ready.wait(lock, [this] { return !connected || !queued.empty(); });
                    if (!connected) break;
                    packet.swap(queued);
                }
                size_t offset = 0;
                while (connected && offset < packet.size())
                {
                    auto sent = send(fd, packet.data() + offset, packet.size() - offset, MSG_NOSIGNAL);
                    if (sent > 0) { offset += sent; continue; }
                    if (sent < 0 && errno == EINTR) continue;
                    if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                    {
                        pollfd writable{fd, POLLOUT, 0};
                        if (poll(&writable, 1, 1000) > 0 && (writable.revents & POLLOUT)) continue;
                    }
                    connected = false;
                }
            }
        });
        return true;
    }
    void stop()
    {
        connected = false;
        if (fd >= 0) shutdown(fd, SHUT_RDWR);
        ready.notify_all();
        if (sender.joinable()) sender.join();
        if (fd >= 0) close(fd);
        fd = -1;
        queued.clear();
        owner = nullptr;
        next_frame = {};
    }
    bool begin_frame(void *controller)
    {
        if (!active() || (owner && owner != controller)) return false;
        owner = controller;
        const auto now = std::chrono::steady_clock::now();
        if (now < next_frame) return false;
        next_frame = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(interval));
        return true;
    }
    void release(void *controller) { if (owner == controller) owner = nullptr; }
    void submit(int width, int height, const std::vector<unsigned char>& rgba)
    {
        if (!active()) return;
        auto usec = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        std::vector<unsigned char> packet(20 + rgba.size());
        size_t header_offset = 0;
        for (uint32_t word : {uint32_t(0x31524b56), uint32_t(width), uint32_t(height),
            uint32_t(usec), uint32_t(uint64_t(usec) >> 32)})
            for (int byte = 0; byte < 4; ++byte) packet[header_offset++] = (word >> (8 * byte)) & 255;
        std::memcpy(packet.data() + 20, rgba.data(), rgba.size());
        {
            std::lock_guard<std::mutex> lock(mutex);
            queued.swap(packet);
        }
        ready.notify_one();
    }
};
