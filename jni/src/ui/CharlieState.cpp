// ============================================================
// Charlie UI · 业务状态实现
// ============================================================
#include "CharlieState.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <dirent.h>

namespace charlie {

Features& F() {
    static Features f;
    return f;
}

Stats& S() {
    static Stats s;
    return s;
}

// ============================================================
// 帧率统计（0.5s 刷新）
//
// 为什么不在每帧刷新:
//   1. 帧率是浮点瞬时量，每帧变一次 -> 灵动岛数字狂跳，看不清
//   2. 数字一变就触发滚动动画 -> 动画永远播不完
//   3. 每帧 snprintf + 文本测量 -> 白烧 CPU
//
// 做法: 每帧只把 dt 累加进采样窗口，到 0.5s 才结算一次，
//       结算结果写进 S().fps / frameMs，其它模块只在
//       PerfUpdated() 为 true 时才更新显示。
// ============================================================
namespace {

constexpr float kDefaultPerfInterval = 0.5f;

// ============================================================
// 资源采集 (抄 查理hp源码/jni/src/悬浮窗.cpp GetSelfMemoryMB /
//           GetSystemCpuUsage / GetSelfCpuUsage)
//
// 设计约束:
//   1. 全部只在 0.5s 结算点调用, 绝不逐帧读 /proc
//   2. 只在匿名 namespace 内可见, 不污染对外头文件
//   3. 读失败返回 0, 调用方保留上一次值, 不清零
// ============================================================

// 本进程常驻内存 (RSS, MB)
static float GetSelfMemoryMB() {
    FILE* fp = fopen("/proc/self/statm", "r");
    if (!fp) return 0.f;
    long total = 0, resident = 0;
    const int n = fscanf(fp, "%ld %ld", &total, &resident);
    fclose(fp);
    if (n != 2 || resident <= 0) return 0.f;
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) return 0.f;
    return (float)resident * (float)pageSize / (1024.f * 1024.f);
}

// 系统可用 / 总内存 (MB)
static void ReadMemInfo(float* availMb, float* totalMb) {
    if (availMb) *availMb = 0.f;
    if (totalMb) *totalMb = 0.f;
    FILE* fp = fopen("/proc/meminfo", "r");
    if (!fp) return;
    char line[128];
    long avail = -1, total = -1;
    while (fgets(line, sizeof(line), fp)) {
        if (avail < 0 && strncmp(line, "MemAvailable:", 13) == 0)
            sscanf(line + 13, "%ld", &avail);
        else if (total < 0 && strncmp(line, "MemTotal:", 9) == 0)
            sscanf(line + 9, "%ld", &total);
        if (avail >= 0 && total >= 0) break;
    }
    fclose(fp);
    if (availMb && avail >= 0) *availMb = (float)avail / 1024.f;
    if (totalMb && total >= 0) *totalMb = (float)total / 1024.f;
}

static float GetMemAvailMB() {
    float a = 0.f, t = 0.f;
    ReadMemInfo(&a, &t);
    return a;
}

static float GetMemTotalMB() {
    float a = 0.f, t = 0.f;
    ReadMemInfo(&a, &t);
    return t;
}

// 系统 CPU 占用 (%) —— 两次 /proc/stat 采样差分
static float GetSystemCpuUsage() {
    static unsigned long long prevTotal = 0, prevIdle = 0;
    FILE* fp = fopen("/proc/stat", "r");
    if (!fp) return 0.f;
    char line[256];
    if (!fgets(line, sizeof(line), fp)) { fclose(fp); return 0.f; }
    fclose(fp);

    unsigned long long v[8] = {0};
    // cpu user nice system idle iowait irq softirq steal
    const int n = sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                         &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
    if (n < 4) return 0.f;

    unsigned long long total = 0;
    for (int i = 0; i < n; ++i) total += v[i];
    const unsigned long long idle = v[3] + (n > 4 ? v[4] : 0);

    if (prevTotal == 0) { prevTotal = total; prevIdle = idle; return 0.f; }

    const unsigned long long dTotal = total - prevTotal;
    const unsigned long long dIdle  = idle  - prevIdle;
    prevTotal = total;
    prevIdle  = idle;
    if (dTotal == 0) return 0.f;
    return (float)(dTotal - dIdle) * 100.f / (float)dTotal;
}

// 本进程 CPU 占用 (%) —— 读 /proc/self/stat 的 utime+stime 差分
// 注意: comm 字段可能含空格与括号, 必须先定位最后一个 ')' 再切分
static float GetSelfCpuUsage() {
    static unsigned long long prevJiffies = 0;
    static long long prevWallMs = 0;

    FILE* fp = fopen("/proc/self/stat", "r");
    if (!fp) return 0.f;
    char buf[512];
    const size_t got = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    if (got == 0) return 0.f;
    buf[got] = '\0';

    const char* rp = strrchr(buf, ')');
    if (!rp || rp[1] == '\0') return 0.f;

    // ')' 之后是 state(第3) 后续字段, utime 是第14、stime 第15
    const char* p = rp + 1;
    unsigned long long utime = 0, stime = 0;
    int idx = 3;   // 当前解析到第几字段
    while (*p && idx < 15) {
        while (*p == ' ') ++p;
        if (!*p) break;
        if (idx == 14) { utime = strtoull(p, (char**)&p, 10); }
        else if (idx == 15) { stime = strtoull(p, (char**)&p, 10); }
        else { while (*p && *p != ' ') ++p; }
        ++idx;
    }
    if (utime == 0 && stime == 0) return 0.f;

    const unsigned long long jiffies = utime + stime;
    const long long hz = sysconf(_SC_CLK_TCK);
    if (hz <= 0) return 0.f;

    long long wallMs = 0;
    {
        FILE* up = fopen("/proc/uptime", "r");
        if (!up) return 0.f;
        double upSec = 0.0;
        if (fscanf(up, "%lf", &upSec) != 1) { fclose(up); return 0.f; }
        fclose(up);
        wallMs = (long long)(upSec * 1000.0);
    }

    if (prevJiffies == 0 || prevWallMs == 0) {
        prevJiffies = jiffies;
        prevWallMs  = wallMs;
        return 0.f;
    }

    const unsigned long long dJiffies = jiffies - prevJiffies;
    const long long dWallMs = wallMs - prevWallMs;
    prevJiffies = jiffies;
    prevWallMs  = wallMs;
    if (dWallMs <= 0) return 0.f;

    // CPU% = 消耗的 CPU 时间 / 墙钟时间 * 100
    const double cpuMs = (double)dJiffies * 1000.0 / (double)hz;
    return (float)(cpuMs * 100.0 / (double)dWallMs);
}

// 线程数
static int GetThreadCount() {
    DIR* d = opendir("/proc/self/task");
    if (!d) return 0;
    int n = 0;
    while (struct dirent* e = readdir(d)) {
        if (e->d_name[0] != '.') ++n;
    }
    closedir(d);
    return n;
}

float g_perfInterval = kDefaultPerfInterval;
float g_perfAccum    = 0.f;    // 本窗口累计时长
int   g_perfFrames   = 0;      // 本窗口帧数
bool  g_perfUpdated  = false;  // 本帧是否刚结算
float g_smoothFrameMs = 0.f;   // 逐帧平滑帧时（参考工程 0.85/0.15）
float g_peakFrameMs   = 0.f;   // 本结算窗口内的峰值帧时（参考工程 g_frame_ms_max）

char  g_fpsText[32]  = "--";
char  g_msText[32]   = "--";

} // namespace

void SetPerfInterval(float sec) {
    if (sec < 0.1f) sec = 0.1f;
    if (sec > 5.f)  sec = 5.f;
    g_perfInterval = sec;
}

float GetPerfInterval() { return g_perfInterval; }

bool PerfUpdated() { return g_perfUpdated; }

const char* FpsText() { return g_fpsText; }

const char* FrameMsText() { return g_msText; }

void UpdateStats(float dt) {
    g_perfUpdated = false;

    if (dt <= 0.f) return;
    // 对齐参考工程 悬浮窗.cpp UpdateAdaptiveFps():
    //   单帧 dt > 1s 视为切后台/长卡顿, 丢弃
    //   平滑系数 0.85/0.15 —— 比之前 0.92/0.08 更跟手
    if (dt > 1.f) return;

    Stats& s = S();

    // ---------- 逐帧平滑（参考工程做法）----------
    // 参考工程是每帧都更新一个平滑帧时, 用它驱动自适应调度。
    // 这里保留逐帧平滑, 但只用来内部累积,
    // 对外显示的 fps 仍按 0.5s 结算, 避免数字狂跳。
    const float frameMs = dt * 1000.f;
    if (g_smoothFrameMs <= 0.f) g_smoothFrameMs = frameMs;
    else g_smoothFrameMs = g_smoothFrameMs * 0.85f + frameMs * 0.15f;

    // 峰值帧时 (抄参考工程 L927 `if (dt_ms > g_frame_ms_max) g_frame_ms_max = dt_ms;`)
    if (frameMs > g_peakFrameMs) g_peakFrameMs = frameMs;

    g_perfAccum  += dt;
    g_perfFrames += 1;

    if (g_perfAccum < g_perfInterval) return;

    // ---------- 0.5s 结算: 用平滑后的帧时反推 fps ----------
    const float curMs  = g_smoothFrameMs;
    const float curFps = (curMs > 0.0001f) ? (1000.f / curMs) : 0.f;

    // 窗口内极值（用瞬时值统计，反映真实抖动）
    const float instMs  = g_perfAccum / (float)g_perfFrames;
    const float instFps = (instMs > 0.f) ? (1.f / instMs) : 0.f;

    if (s.fps <= 0.f) {
        s.fps     = curFps;
        s.frameMs = curMs;
        s.fpsMin  = instFps;
        s.fpsMax  = instFps;
    } else {
        s.fps     = curFps;
        s.frameMs = curMs;
        if (instFps < s.fpsMin) s.fpsMin = instFps;
        if (instFps > s.fpsMax) s.fpsMax = instFps;
    }

    snprintf(g_fpsText, sizeof(g_fpsText), "%.0f", s.fps);
    snprintf(g_msText,  sizeof(g_msText),  "%.1fms", s.frameMs);

    // ---------- 资源采集 (抄 查理hp源码/jni/src/悬浮窗.cpp UpdatePerfStats) ----------
    // 只在 0.5s 结算点读一次 /proc, 不做逐帧采样 —— 每帧读文件会显著拉高 CPU。
    // 采集失败时保留上一次的值, 不清零 (避免面板闪 0)。
    {
        const float rawMem  = GetSelfMemoryMB();
        const float rawAvail = GetMemAvailMB();
        const float rawTotal = GetMemTotalMB();
        const float rawSelf = GetSelfCpuUsage();
        const float rawSys  = GetSystemCpuUsage();

        // EMA: 内存 0.7/0.3, CPU 0.6/0.4 (与参考实现同系数)
        s.memMb      = (s.memMb      > 0.f) ? (s.memMb      * 0.7f + rawMem   * 0.3f) : rawMem;
        s.cpuSelf    = (s.cpuSelf    > 0.f) ? (s.cpuSelf    * 0.6f + rawSelf  * 0.4f) : rawSelf;
        s.cpuSys     = (s.cpuSys     > 0.f) ? (s.cpuSys     * 0.6f + rawSys   * 0.4f) : rawSys;
        s.memAvailMb = (rawAvail > 0.f) ? rawAvail : s.memAvailMb;
        s.memTotalMb = (rawTotal > 0.f) ? rawTotal : s.memTotalMb;

        // 峰值帧时: 逐帧跟踪, 结算后复位。
        // 原来这里算的是 g_perfAccum/g_perfFrames (平均值), 与 "峰值" 不符。
        s.frameMsMax = g_peakFrameMs;

        s.threadCnt = GetThreadCount();
    }

    g_perfAccum  = 0.f;
    g_perfFrames = 0;
    g_peakFrameMs = 0.f;   // 峰值复位 (参考工程 L940)
    g_perfUpdated = true;
}

} // namespace charlie
