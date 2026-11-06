// ============================================================
// Charlie UI · 音量键监听实现
// ============================================================
#include "CharlieVolume.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/select.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>

#include <atomic>
#include <thread>
#include <vector>
#include <cstdio>

namespace charlie {
namespace volume {

namespace {

std::vector<int>     g_fds;
std::atomic<bool>    g_running{false};
std::atomic<bool>    g_stopRequested{false};
std::thread          g_thread;
Callback             g_cb;

// 单个设备的事件抽取: 把内核队列一次读空
// 返回 false 表示设备不可读 (已拔出/权限变化)
bool DrainDevice(int fd, const Callback& cb) {
    input_event ev;
    bool ok = true;

    while (true) {
        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n == (ssize_t)sizeof(ev)) {
            if (ev.type == EV_KEY &&
                (ev.code == KEY_VOLUMEUP || ev.code == KEY_VOLUMEDOWN)) {
                if (cb) {
                    VolumeKeyEvent e;
                    e.kind   = (ev.code == KEY_VOLUMEUP)
                               ? VolumeKeyEvent::VolumeUp
                               : VolumeKeyEvent::VolumeDown;
                    e.down   = (ev.value != 0);
                    e.repeat = (ev.value == 2) ? 1 : 0;
                    cb(e);
                }
            }
            continue;   // 继续抽干
        }
        if (n == 0) break;                      // EOF
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;  // 队列空, 正常
        if (errno == EINTR) continue;           // 被信号打断, 重试
        ok = false;                             // 真错误
        break;
    }
    return ok;
}

// 探测某设备是否支持音量键
bool SupportsVolumeKey(int fd) {
    unsigned long evbit[EV_MAX / (sizeof(unsigned long) * 8) + 1];
    unsigned long keybit[KEY_MAX / (sizeof(unsigned long) * 8) + 1];
    memset(evbit, 0, sizeof(evbit));
    memset(keybit, 0, sizeof(keybit));

    if (ioctl(fd, EVIOCGBIT(0, sizeof(evbit)), evbit) < 0) return false;

    const size_t evBytes = sizeof(evbit);
    const size_t evBits  = evBytes * 8;
    if (EV_KEY >= (long)evBits) return false;
    if (!(evbit[EV_KEY / (sizeof(unsigned long) * 8)] &
          (1UL << (EV_KEY % (sizeof(unsigned long) * 8))))) {
        return false;
    }

    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybit)), keybit) < 0) return false;

    const size_t keyBits = sizeof(keybit) * 8;
    auto hasKey = [&](int code) -> bool {
        if (code < 0 || (size_t)code >= keyBits) return false;
        return (keybit[code / (sizeof(unsigned long) * 8)] &
                (1UL << (code % (sizeof(unsigned long) * 8)))) != 0;
    };
    return hasKey(KEY_VOLUMEUP) || hasKey(KEY_VOLUMEDOWN);
}

int OpenDevices() {
    g_fds.clear();

    DIR* dir = opendir("/dev/input");
    if (!dir) return 0;

    std::vector<std::string> names;
    dirent* ent = nullptr;
    while ((ent = readdir(dir)) != nullptr) {
        // 严格匹配 "eventN", readdir 顺序不可靠, 先收集再排序
        if (strncmp(ent->d_name, "event", 5) != 0) continue;
        const char* num = ent->d_name + 5;
        if (!*num) continue;
        bool allDigits = true;
        for (const char* q = num; *q; ++q) {
            if (*q < '0' || *q > '9') { allDigits = false; break; }
        }
        if (allDigits) names.push_back(ent->d_name);
    }
    closedir(dir);

    // 按事件号数值排序, 保证日志可读
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        return atoi(a.c_str() + 5) < atoi(b.c_str() + 5);
    });

    for (const auto& name : names) {
        char path[128];
        snprintf(path, sizeof(path), "/dev/input/%s", name.c_str());

        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        if (!SupportsVolumeKey(fd)) {
            close(fd);
            continue;
        }
        g_fds.push_back(fd);
    }
    return (int)g_fds.size();
}

void CloseDevices() {
    for (int fd : g_fds) {
        if (fd >= 0) close(fd);
    }
    g_fds.clear();
}

// 监听主循环
void ThreadMain() {
    const int n = (int)g_fds.size();
    if (n == 0) { g_running = false; return; }

    std::vector<int> alive = g_fds;

    while (!g_stopRequested.load(std::memory_order_acquire)) {
        if (alive.empty()) break;

        fd_set rfds;
        FD_ZERO(&rfds);
        int maxFd = -1;
        for (int fd : alive) {
            FD_SET(fd, &rfds);
            if (fd > maxFd) maxFd = fd;
        }

        // 50ms 超时: 无事件时进入内核等待, 不占 CPU
        struct timeval tv;
        tv.tv_sec  = 0;
        tv.tv_usec = 50000;

        int ret = select(maxFd + 1, &rfds, nullptr, nullptr, &tv);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) continue;   // 超时, 回循环顶部检查停止标志

        // 抽干所有就绪设备
        for (size_t i = 0; i < alive.size(); ) {
            int fd = alive[i];
            if (!FD_ISSET(fd, &rfds)) { ++i; continue; }

            if (!DrainDevice(fd, g_cb)) {
                // 设备失效, 摘除
                close(fd);
                alive.erase(alive.begin() + i);
                continue;
            }
            ++i;
        }
    }

    g_running = false;
}

} // namespace

bool Start(Callback cb) {
    if (g_running.load()) return true;

    g_cb = std::move(cb);
    g_stopRequested.store(false, std::memory_order_release);

    const int n = OpenDevices();
    if (n == 0) {
        printf("[Charlie] volume: 无可用输入设备 (权限不足或 /dev/input 不可读)\n");
        return false;
    }
    printf("[Charlie] volume: 已监听 %d 个输入设备\n", n);

    g_running.store(true, std::memory_order_release);
    g_thread = std::thread(ThreadMain);
    return true;
}

void Stop() {
    if (!g_running.load() && !g_thread.joinable()) {
        CloseDevices();
        return;
    }
    g_stopRequested.store(true, std::memory_order_release);
    if (g_thread.joinable()) g_thread.join();
    g_running.store(false, std::memory_order_release);
    CloseDevices();
}

bool IsRunning() {
    return g_running.load(std::memory_order_acquire);
}

int OpenDeviceCount() {
    return (int)g_fds.size();
}

} // namespace volume
} // namespace charlie
