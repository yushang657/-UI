// ============================================================
// Charlie UI · 主程序
// 纯 UI 模板：无驱动、无内存读写、无网络
//
// 启动流程:
//   1. 读屏幕信息
//   2. 创建透明悬浮层 (ANativeWindowCreator)
//   3. 初始化图形后端 (OpenGL ES 3 / Vulkan)
//   4. 初始化触摸
//   5. 加载字体 + 样式
//   6. 挂音量键监听
//   7. 主循环
// ============================================================
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#include "draw.h"
#include "AndroidImgui.h"
#include "GraphicsManager.h"
#include "ANativeWindowCreator.h"

#include "ui/CharlieTheme.h"
#include "ui/CharlieFonts.h"
#include "ui/CharlieShell.h"
#include "ui/CharlieIsland.h"
#include "ui/CharlieState.h"
#include "ui/CharlieWidgets.h"
#include "ui/CharlieAnim.h"
#include "platform/CharlieVolume.h"
#include "platform/CharlieTouch.h"
#include "platform/CharlieDisplay.h"
#include "platform/CharlieSingle.h"

// ============================================================
// draw.h 中声明的全局符号定义
// ============================================================
std::unique_ptr<AndroidImgui> graphics = nullptr;
ANativeWindow* window = nullptr;
android::ANativeWindowCreator::DisplayInfo displayInfo;
ImGuiWindow* g_window = nullptr;

int abs_ScreenX = 0, abs_ScreenY = 0;
int native_window_screen_x = 0, native_window_screen_y = 0;

TextureInfo Aekun_image = {};
ImFont* zh_font = nullptr;

Last_ImRect LastCoordinate = {};
bool permeate_record = false;
bool permeate_record_ini = false;

// ============================================================
// 屏幕信息
//
// GetDisplayInfo() 返回的 width/height 就是"当前朝向"的逻辑屏幕尺寸
// (orientation 为 1/3 时是 横x竖, 为 0/2 时是 竖x横)。
// 悬浮层的 buffer 尺寸在 Create 时固定且无法 resize, 所以旋转后
// 必须销毁旧层按新尺寸重建 —— 见 main 循环里的 display::Poll()。
//
// 层尺寸直接取 width x height (不是正方形):
//   正方形层在旋转后虽然也能覆盖, 但 EGL 后台缓冲变成正方形,
//   glViewport 与 ImGui DisplaySize 会不一致, UI 被拉伸。
//   按当前朝向建层 + 旋转时重建, 两者才严格对齐。
// ============================================================
void screen_config() {
    displayInfo = android::ANativeWindowCreator::GetDisplayInfo();

    const int32_t w = displayInfo.width;
    const int32_t h = displayInfo.height;

    // 层尺寸 = 当前朝向真实屏幕尺寸
    native_window_screen_x = w;
    native_window_screen_y = h;

    // 触摸标定基准
    abs_ScreenX = (w > h) ? w : h;
    abs_ScreenY = (w < h) ? w : h;

    printf("[Charlie] display: %dx%d orientation=%d landscape=%d\n",
           (int)w, (int)h, (int)displayInfo.orientation, (w > h) ? 1 : 0);
}

// ============================================================
// 退出信号
// ============================================================
static volatile sig_atomic_t g_sigExit = 0;
static void OnSignal(int) { g_sigExit = 1; }

// ============================================================
// 音量键 -> 打开 / 隐藏 / 关闭
//
// 音量上键        打开悬浮窗
// 音量下键 (短按)  隐藏悬浮窗（收面板, 进程与灵动岛保留）
// 音量下键 (连按两次, 1.5s 内)  关闭程序（结束进程）
//
// 注意: 免责弹窗未确认时不响应任何音量键。
// 弹窗只认「我已知晓」按钮。
// ============================================================
static double g_lastVolDownTime = 0.0;

static double NowSec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void OnVolumeKey(const charlie::VolumeKeyEvent& e) {
    using namespace charlie;
    if (!e.down) return;   // 只处理按下, 忽略抬起与长按重复
    if (e.repeat > 0) return;

    // 免责未确认时: 音量键不参与同意判定, 但也不允许把面板藏起来。
    //
    // 参考实现 查理hp源码/jni/src/悬浮窗.cpp 用 ui_first_open 同时管两件事:
    //   ui_anim_completed && !ui_first_open -> 允许拖动
    //   ui_anim_completed &&  ui_first_open -> 弹窗 + 拖动
    // 即"弹窗开着"期间面板本身就被锁住。
    //
    // 这里做一个更稳的兜底: 未同意时按任意音量键,
    // 只要面板不是 Opened 就强制 RequestOpen。这样即使有别的路径
    // 把状态推到 Hidden(动画收尾、外部调用), 一按就立刻回到可见,
    // 弹窗随之重新出现 —— 而不是"藏了就叫不回来"。
    // 注意: 这里绝不能因为"免责未确认"就把音量键整个吞掉。
    //
    // 上一版在未同意时直接 return, 结果是死锁:
    //   弹窗是 ImGui Modal -> 自带全屏遮罩 -> 吞掉所有点击
    //   若弹窗因任何原因点不到「我已知晓」, 用户就被永久锁住,
    //   连音量键都失效, 只能杀进程。
    //
    // 正确策略: 音量键保持原有的开关行为不变。
    // "弹窗不消失"由 RequestOpen 复位 g_disclaimerRequested 来保证
    // —— 面板重新打开时弹窗必然重新弹出, 而不是靠禁键。

    // ---------- 音量上键: 打开 ----------
    if (e.kind == VolumeKeyEvent::VolumeUp) {
        g_lastVolDownTime = 0.0;   // 上键打断下键的连按计数
        if (!shell::IsVisible()) {
            shell::RequestOpen();
            printf("[Charlie] 音量上键: 打开悬浮窗\n");
        }
        return;
    }

    // ---------- 音量下键 ----------
    // 面板开着 -> 隐藏 (只收面板, 进程保留)
    if (shell::IsVisible()) {
        if (shell::State() == WinState::Opened) {
            shell::RequestClose();
            printf("[Charlie] 音量下键: 隐藏悬浮窗\n");
        }
        g_lastVolDownTime = 0.0;
        return;
    }

    // 面板已隐藏 -> 连按两次关闭程序 (结束进程)
    const double now = NowSec();
    if (g_lastVolDownTime > 0.0 && (now - g_lastVolDownTime) <= 1.5) {
        g_lastVolDownTime = 0.0;
        printf("[Charlie] 音量下键连按: 关闭程序\n");
        shell::RequestExit();
    } else {
        g_lastVolDownTime = now;
        printf("[Charlie] 再按一次音量下键关闭程序\n");
    }
}

// ============================================================
// 渲染栈 (层 + 图形后端 + ImGui 上下文 + 字体 + 壳层)
//
// 旋转后必须整体重建: AndroidImgui::Init_Render 会重建 ImGui 上下文,
// 字体随之释放, 所以字体与壳层度量都要重来一遍。
// ============================================================
static bool BuildRenderStack(int w, int h) {
    // 层
    ::window = android::ANativeWindowCreator::Create("Charlie", w, h, false);
    if (!::window) {
        printf("[Charlie] 创建悬浮层失败 (%dx%d)\n", w, h);
        return false;
    }

    // 图形后端 + ImGui 上下文
    ::graphics = GraphicsManager::getGraphicsInterface(GraphicsManager::OPENGL);
    if (!::graphics) {
        printf("[Charlie] 图形后端创建失败\n");
        android::ANativeWindowCreator::Destroy(::window);
        ::window = nullptr;
        return false;
    }
    if (!::graphics->Init_Render(::window, (float)w, (float)h)) {
        printf("[Charlie] 渲染初始化失败\n");
        android::ANativeWindowCreator::Destroy(::window);
        ::window = nullptr;
        ::graphics.reset();
        return false;
    }

    // 字体 (上下文是新的, 必须清标志后重载)
    // 字体选择从配置读, 但此刻壳层还没 Init, 配置尚未加载;
    // 先用上次保存的默认值, 等 shell::Init 之后若发现配置不同再重载
    charlie::fonts::Reset();
    charlie::fonts::LoadWith(charlie::shell::Cfg().fontId);
    charlie::ApplyStyle();
    charlie::AnimResetAll();

    // 壳层度量按当前朝向重建
    charlie::shell::Init((float)w, (float)h, 1.f);

    // Init 会读配置, 若字体与配置不符则重载一次
    if (charlie::fonts::CurrentId() != charlie::shell::Cfg().fontId) {
        charlie::fonts::Reset();
        charlie::fonts::LoadWith(charlie::shell::Cfg().fontId);
    }

    printf("[Charlie] renderer: %s  layer: %dx%d\n",
           ::graphics->RenderName, w, h);
    return true;
}

static void DestroyRenderStack() {
    if (::graphics) {
        ::graphics->Shutdown();
        ::graphics.reset();
    }
    if (::window) {
        android::ANativeWindowCreator::Destroy(::window);
        ::window = nullptr;
    }
}

// ============================================================
// main
// ============================================================
int main(int argc, char** argv) {
    (void)argc; (void)argv;

    // stdout 默认全缓冲: 一旦重定向到文件/管道, 日志会全部卡在 4KB 缓冲里
    // 不落盘, 表现为"进程活着但日志是空的", 排障时看不到任何实时输出。
    // 改成行缓冲, 每行立即写出。
    setvbuf(stdout, nullptr, _IOLBF, 0);
    setvbuf(stderr, nullptr, _IOLBF, 0);

    signal(SIGINT,  OnSignal);
    signal(SIGTERM, OnSignal);
    signal(SIGHUP,  OnSignal);

    printf("\n");
    printf("==================================================\n");
    printf("  Charlie UI  ·  悬浮窗模板 v1.2.0\n");
    printf("  纯界面工程: 无驱动 / 无内存读写 / 无网络\n");
    printf("--------------------------------------------------\n");
    printf("  音量+            打开悬浮窗\n");
    printf("  音量-            隐藏悬浮窗\n");
    printf("  音量- x2          关闭程序 (1.5s 内, 隐藏状态下)\n");
    printf("  侧栏「结束进程」   连点两次关闭程序\n");
    printf("--------------------------------------------------\n");
    printf("  免责弹窗只认「我已知晓」按钮, 音量键不参与\n");
    printf("==================================================\n");
    printf("\n");

    // ---------- 0. 单实例互斥 ----------
    // 检测同名旧进程; 有则询问用户是关闭旧的还是退出本次。
    // 必须在创建悬浮层之前做, 否则两个进程会抢同一个 SurfaceFlinger 层。
    if (!charlie::single::EnsureSingleInstance("charlie_ui")) {
        return 0;   // 用户选择保留旧进程
    }

    // ---------- 1. 屏幕信息 ----------
    ::screen_config();

    const int winW = ::native_window_screen_x;
    const int winH = ::native_window_screen_y;

    // ---------- 2~4. 渲染栈 (层 + 后端 + 字体 + 壳层) ----------
    if (!BuildRenderStack(winW, winH)) return 1;

    // ---------- 5. 触摸 ----------
    // 悬浮层不属于 Android 输入焦点体系, 收不到 AInputEvent。
    // 自建只读触摸读取器: 不抢占系统触摸 (不做 EVIOCGRAB),
    // 也不注入 uinput 虚拟设备 —— 只旁路读取 /dev/input。
    if (!charlie::touch::Start(winW, winH, displayInfo.orientation)) {
        printf("[Charlie] 触摸不可用, 面板无法点击 (可检查 /dev/input 读权限)\n");
    }

    // ---------- 6. 显示监测基准 ----------
    charlie::display::Bind(winW, winH, displayInfo.orientation);

    // ---------- 7. 免责 / 开窗 / 音量键 ----------
    charlie::shell::SetDisclaimerPath("./charlie_agreed.flag");
    charlie::shell::RequestOpen();

    if (!charlie::volume::Start(OnVolumeKey)) {
        printf("[Charlie] 音量键不可用, 窗口可点击灵动岛切换\n");
    }

    // ---------- 8. 主循环 ----------
    printf("[Charlie] 进入主循环\n");

    while (true) {
        if (g_sigExit) break;
        if (charlie::shell::ExitRequested()) break;

        // 朝向变化 -> 整体重建渲染栈
        if (charlie::display::Poll()) {
            const auto& t = charlie::display::Target();

            printf("[Charlie] 重建渲染栈: %dx%d\n", t.width, t.height);

            DestroyRenderStack();

            if (!BuildRenderStack(t.width, t.height)) {
                printf("[Charlie] 重建失败, 退出\n");
                break;
            }

            // 触摸换坐标系
            charlie::touch::SetScreenSize(t.width, t.height, t.orientation);

            // 记录新基准
            ::native_window_screen_x = t.width;
            ::native_window_screen_y = t.height;
            ::displayInfo.width      = t.width;
            ::displayInfo.height     = t.height;
            ::displayInfo.orientation = t.orientation;
            charlie::display::Bind(t.width, t.height, t.orientation);

            // 重建后保持面板处于打开状态
            charlie::shell::RequestOpen();
            // 让免责弹窗在新 ImGui 上下文里重新 OpenPopup
            // (旧上下文的 popup 栈已随 DestroyRenderStack 销毁)
            charlie::shell::OnRenderStackRebuilt();
            charlie::AnimResetAll();
        }

        // 投递触摸事件
        charlie::touch::Pump();

        // 输入 -> 新帧
        ::graphics->NewFrame();

        // 壳层绘制
        charlie::shell::Frame();

        // 帧提交
        ::graphics->EndFrame();

        // ---------- 帧率控制 ----------
        // 回到固定 1ms 休眠。
        //
        // 上一版换成"按 60FPS 差值休眠"(最长 16.6ms), 结果触摸与音量键
        // 一起失灵。原因: 本工程是轮询式主循环,
        //   - touch::Pump() 每轮从 /dev/input 读事件
        //   - volume::ThreadMain 独立线程读音量键
        // 主循环周期从 1ms 拉到 16.6ms 后, 触摸事件在输入子系统里
        // 积压, ImGui 每帧只处理 Pump() 读到的部分, 表现为"点了没反应";
        // 同时 16.6ms 的 nanosleep 在 tee 管道/调度抖动下会被进一步拉长。
        // 1ms 轮询的 CPU 代价在这个体量下完全可以接受(面板静态时
        // 每帧只是 Begin/End + 少量 draw call), 稳定性优先。
        usleep(1000);
    }

    // ---------- 9. 清理 ----------
    printf("[Charlie] 退出中...\n");

    charlie::shell::SaveConfig();
    charlie::volume::Stop();
    charlie::touch::Stop();

    DestroyRenderStack();

    printf("[Charlie] 已退出\n");
    return 0;
}
