// ============================================================
// Charlie UI · 触摸输入实现
// ============================================================
#include "CharlieTouch.h"

#include "imgui.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/select.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <atomic>
#include <thread>
#include <vector>
#include <mutex>
#include <deque>
#include <string>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>

namespace charlie {
namespace touch {

namespace {

struct TouchDev {
    int  fd = -1;
    int  absMaxX = 0;
    int  absMaxY = 0;
    int  absMinX = 0;
    int  absMinY = 0;
};

std::vector<TouchDev> g_devs;
std::thread           g_thread;
std::atomic<bool>     g_running{false};
std::atomic<bool>     g_stop{false};

// 触摸状态 (回调线程写, 主线程读)
std::atomic<bool>  g_down{false};
std::atomic<float> g_posX{0.f};
std::atomic<float> g_posY{0.f};
std::atomic<bool>  g_dirty{false};   // 有新事件待投递

// ---------- 点击事件队列 ----------
// 只投递"当前状态"会丢失快速点击(按下+抬起同批次),
// 因此把状态边沿排成队列, 每帧按序投递。
struct TouchEvent {
    float x = 0.f;
    float y = 0.f;
    bool  down = false;
};

constexpr size_t kEvQueueMax = 64;

std::deque<TouchEvent> g_evQueue;
std::mutex             g_evMutex;
bool                   g_lastPushed = false;   // 线程内使用, 无需加锁

int g_screenW = 1080;
int g_screenH = 1920;
int g_orientation = 0;

std::function<void(float, float, bool)> g_cb;
std::mutex g_cbMutex;

// ---------- 坐标换算 ----------
// 设备原始坐标 -> 屏幕逻辑坐标
// 竖屏时触摸面板通常仍以"横屏坐标系"上报, 需要旋转
void RawToScreen(int rawX, int rawY, int absMaxX, int absMaxY,
                 int absMinX, int absMinY,
                 float* outX, float* outY) {
    const float spanX = (absMaxX > absMinX) ? (float)(absMaxX - absMinX) : 1.f;
    const float spanY = (absMaxY > absMinY) ? (float)(absMaxY - absMinY) : 1.f;

    // 归一化到 0..1
    float nx = ((float)(rawX - absMinX)) / spanX;
    float ny = ((float)(rawY - absMinY)) / spanY;
    if (nx < 0.f) nx = 0.f; else if (nx > 1.f) nx = 1.f;
    if (ny < 0.f) ny = 0.f; else if (ny > 1.f) ny = 1.f;

    const float sw = (float)g_screenW;
    const float sh = (float)g_screenH;

    float x, y;
    switch (g_orientation) {
    case 1:   // 旋转 90
        x = ny * sw;
        y = (1.f - nx) * sh;
        break;
    case 2:   // 旋转 180
        x = (1.f - nx) * sw;
        y = (1.f - ny) * sh;
        break;
    case 3:   // 旋转 270
        x = (1.f - ny) * sw;
        y = nx * sh;
        break;
    case 0:
    default:  // 不旋转
        x = nx * sw;
        y = ny * sh;
        break;
    }

    *outX = x;
    *outY = y;
}

// ---------- 设备探测 ----------
bool IsTouchDevice(int fd, TouchDev* out) {
    unsigned long absbit[ABS_MAX / (sizeof(unsigned long) * 8) + 1];
    memset(absbit, 0, sizeof(absbit));
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbit)), absbit) < 0) return false;

    const size_t bits = sizeof(absbit) * 8;
    auto hasAbs = [&](int code) -> bool {
        if (code < 0 || (size_t)code >= bits) return false;
        return (absbit[code / (sizeof(unsigned long) * 8)] &
                (1UL << (code % (sizeof(unsigned long) * 8)))) != 0;
    };

    // 必须同时有 MT 的 X/Y, 才认为是多点触摸屏
    if (!hasAbs(ABS_MT_POSITION_X) || !hasAbs(ABS_MT_POSITION_Y)) return false;

    struct input_absinfo ax{}, ay{};
    if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &ax) != 0) return false;
    if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &ay) != 0) return false;
    if (ax.maximum <= ax.minimum || ay.maximum <= ay.minimum) return false;

    if (out) {
        out->absMaxX = ax.maximum;
        out->absMaxY = ay.maximum;
        out->absMinX = ax.minimum;
        out->absMinY = ay.minimum;
    }
    return true;
}

int OpenDevices() {
    g_devs.clear();

    DIR* dir = opendir("/dev/input");
    if (!dir) return 0;

    std::vector<std::string> names;
    dirent* ent = nullptr;
    while ((ent = readdir(dir)) != nullptr) {
        if (strncmp(ent->d_name, "event", 5) != 0) continue;
        const char* num = ent->d_name + 5;
        if (!*num) continue;
        bool digits = true;
        for (const char* q = num; *q; ++q) {
            if (*q < '0' || *q > '9') { digits = false; break; }
        }
        if (digits) names.push_back(ent->d_name);
    }
    closedir(dir);

    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        return atoi(a.c_str() + 5) < atoi(b.c_str() + 5);
    });

    for (const auto& name : names) {
        char path[128];
        snprintf(path, sizeof(path), "/dev/input/%s", name.c_str());

        // 只读打开: 不抢系统触摸
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        TouchDev dev;
        if (IsTouchDevice(fd, &dev)) {
            dev.fd = fd;
            g_devs.push_back(dev);
            printf("[Charlie] touch: %s  max=(%d,%d)\n",
                   path, dev.absMaxX, dev.absMaxY);
        } else {
            close(fd);
        }
    }
    return (int)g_devs.size();
}

void CloseDevices() {
    for (auto& d : g_devs) {
        if (d.fd >= 0) close(d.fd);
    }
    g_devs.clear();
}

// ---------- 事件处理 ----------
struct SlotState {
    int  trackingId = -1;
    bool down = false;
    int  rawX = 0;
    int  rawY = 0;
};

// 每个设备一份 slot 表
std::vector<std::vector<SlotState>> g_slots;

// 收集本批事件后统一提交一次 (避免 SYN_REPORT 之间抖动)
//
// 关键: 不能只写"当前状态"。
// 用户快速点击时, 按下+抬起可能落在同一个 select 批次里,
// 若只把 g_down 置为最终值(false), ImGui 永远收不到 Down(true),
// 表现为"按钮点了没反应"。
// 因此这里把状态"边沿"记成有序事件队列, Pump() 逐一投递。
void PushState(bool down, float sx, float sy) {
    const float px = g_posX.load(std::memory_order_relaxed);
    const float py = g_posY.load(std::memory_order_relaxed);

    g_posX.store(sx, std::memory_order_relaxed);
    g_posY.store(sy, std::memory_order_relaxed);
    g_down.store(down, std::memory_order_relaxed);

    // 【必须标脏】Pump() 第一行就是
    //     if (!g_dirty.exchange(false)) return;
    // 不置位的话 Pump() 永远在第一行返回, io.AddMousePosEvent
    // 一次都不会被调用 —— io.MousePos 会永远停在 ImGui 的初始值
    // -FLT_MAX, 表现为"坐标读得到但所有控件点不动"。
    //
    // 坐标微小变动也标脏: ImGui 的 hover 判定依赖 MousePos 逐帧更新,
    // 只在按下/抬起时标脏会让"移动过程中的悬停"完全失效。
    const bool moved = (sx != px || sy != py);

    // 只在按下状态真的变化时入队, 避免同一状态重复投递
    if (down != g_lastPushed) {
        g_lastPushed = down;
        std::lock_guard<std::mutex> lk(g_evMutex);
        if (g_evQueue.size() < kEvQueueMax) {
            g_evQueue.push_back(TouchEvent{ sx, sy, down });
        }
        g_dirty.store(true, std::memory_order_release);
        return;
    }

    if (moved) g_dirty.store(true, std::memory_order_release);
}

void Commit(int devIdx, const TouchDev& dev) {
    // 采用"第一个按下的手指"作为主指针 —— 本模板是单点交互
    for (auto& s : g_slots[devIdx]) {
        if (s.down) {
            float sx, sy;
            RawToScreen(s.rawX, s.rawY, dev.absMaxX, dev.absMaxY,
                        dev.absMinX, dev.absMinY, &sx, &sy);
            PushState(true, sx, sy);
            return;
        }
    }
    // 没有手指按下: 位置保持最后已知, 仅把状态置为抬起
    PushState(false, g_posX.load(std::memory_order_relaxed),
                     g_posY.load(std::memory_order_relaxed));
}

void ThreadMain() {
    if (g_devs.empty()) { g_running = false; return; }

    const size_t n = g_devs.size();
    g_slots.assign(n, std::vector<SlotState>(16));

    while (!g_stop.load(std::memory_order_acquire)) {
        fd_set rfds;
        FD_ZERO(&rfds);
        int maxFd = -1;
        for (size_t i = 0; i < n; ++i) {
            if (g_devs[i].fd < 0) continue;
            FD_SET(g_devs[i].fd, &rfds);
            if (g_devs[i].fd > maxFd) maxFd = g_devs[i].fd;
        }
        if (maxFd < 0) break;

        // 8ms 超时: 屏幕触摸上报率通常 120-240Hz, 8ms 足够且不吃 CPU
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 8000;

        int ret = select(maxFd + 1, &rfds, nullptr, nullptr, &tv);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) continue;

        for (size_t i = 0; i < n; ++i) {
            int fd = g_devs[i].fd;
            if (fd < 0 || !FD_ISSET(fd, &rfds)) continue;

            input_event ev;
            int curSlot = 0;

            while (read(fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
                if (ev.type == EV_ABS) {
                    if (ev.code == ABS_MT_SLOT) {
                        curSlot = ev.value;
                        if (curSlot < 0) curSlot = 0;
                        if (curSlot >= (int)g_slots[i].size())
                            g_slots[i].resize(curSlot + 1);
                        continue;
                    }
                    if (curSlot >= (int)g_slots[i].size())
                        g_slots[i].resize(curSlot + 1);

                    SlotState& s = g_slots[i][curSlot];
                    switch (ev.code) {
                    case ABS_MT_TRACKING_ID:
                        if (ev.value < 0) {
                            s.down = false;
                            s.trackingId = -1;
                        } else {
                            s.trackingId = ev.value;
                            s.down = true;
                        }
                        break;
                    case ABS_MT_POSITION_X:
                        s.rawX = ev.value;
                        break;
                    case ABS_MT_POSITION_Y:
                        s.rawY = ev.value;
                        break;
                    default:
                        break;
                    }
                } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                    Commit((int)i, g_devs[i]);
                }
            }

            // EAGAIN 是正常结束; 其它错误说明设备失效
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != 0) {
                close(fd);
                g_devs[i].fd = -1;
            }
        }
    }

    g_running = false;
}

} // namespace

// ============================================================
// 对外接口
// ============================================================
bool Start(int screenW, int screenH, int orientation) {
    if (g_running.load()) return true;

    g_screenW = (screenW > 0) ? screenW : 1080;
    g_screenH = (screenH > 0) ? screenH : 1920;
    g_orientation = orientation;

    const int n = OpenDevices();
    if (n == 0) {
        printf("[Charlie] touch: 无可用触摸设备 (权限不足或 /dev/input 不可读)\n");
        return false;
    }

    g_stop.store(false, std::memory_order_release);
    g_running.store(true, std::memory_order_release);
    g_thread = std::thread(ThreadMain);
    printf("[Charlie] touch: 已监听 %d 个触摸设备, orientation=%d\n", n, orientation);
    return true;
}

void Stop() {
    g_stop.store(true, std::memory_order_release);
    if (g_thread.joinable()) g_thread.join();
    g_running.store(false, std::memory_order_release);
    CloseDevices();
}

bool IsRunning() { return g_running.load(std::memory_order_acquire); }
bool IsDown()    { return g_down.load(std::memory_order_relaxed); }

// 运行期更新坐标换算基准 (系统旋转时调用)
// 只改基准, 不重开设备, 不动线程
void SetScreenSize(int screenW, int screenH, int orientation) {
    if (screenW > 0) g_screenW = screenW;
    if (screenH > 0) g_screenH = screenH;
    g_orientation = orientation;
    printf("[Charlie] touch: 重标定 %dx%d orientation=%d\n",
           g_screenW, g_screenH, g_orientation);
}

void GetPos(float* x, float* y) {
    if (x) *x = g_posX.load(std::memory_order_relaxed);
    if (y) *y = g_posY.load(std::memory_order_relaxed);
}

void SetMoveCallback(std::function<void(float, float, bool)> cb) {
    std::lock_guard<std::mutex> lk(g_cbMutex);
    g_cb = std::move(cb);
}

// ============================================================
// 每帧调用: 把累积的触摸状态投递给 ImGui
// ============================================================
void Pump() {
    if (!g_dirty.exchange(false, std::memory_order_acquire)) return;

    ImGuiIO& io = ImGui::GetIO();
    const float x = g_posX.load(std::memory_order_relaxed);
    const float y = g_posY.load(std::memory_order_relaxed);
    const bool  d = g_down.load(std::memory_order_relaxed);

    // 先按序投递点击边沿事件。同一帧内允许出现 Down -> Up,
    // 这样快速点击不会被合并掉。
    std::deque<TouchEvent> evs;
    {
        std::lock_guard<std::mutex> lk(g_evMutex);
        evs.swap(g_evQueue);
    }

    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);

    if (evs.empty()) {
        // 只有移动: 只投递位置
        io.AddMousePosEvent(x, y);
    } else {
        for (const TouchEvent& e : evs) {
            io.AddMousePosEvent(e.x, e.y);
            io.AddMouseButtonEvent(0, e.down);
        }
        // 让最终位置与全局状态一致
        io.AddMousePosEvent(x, y);
    }

    std::function<void(float, float, bool)> cb;
    {
        std::lock_guard<std::mutex> lk(g_cbMutex);
        cb = g_cb;
    }
    if (cb) cb(x, y, d);
}

} // namespace touch
} // namespace charlie
