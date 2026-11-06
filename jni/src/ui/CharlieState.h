// ============================================================
// Charlie UI · 业务状态（占位）
// 项目模板: 这里只有 UI 状态, 不含任何游戏逻辑。
// 后续接入业务时, 在各自项目里替换/扩展这份结构即可。
// ============================================================
#pragma once

namespace charlie {

// 页面索引 —— 新增页面在这里加, 导航与内容区自动跟随
enum PageId {
    PAGE_DASHBOARD = 0,   // 总览
    PAGE_VISUAL,          // 视觉
    PAGE_AIM,             // 操控
    PAGE_MISC,            // 杂项
    PAGE_PERF,            // 性能 (实时 CPU/内存/帧率)
    PAGE_SETTINGS,        // 设置
    PAGE_ABOUT,           // 关于
    PAGE_COUNT
};

inline const char* PageName(int i) {
    static const char* names[PAGE_COUNT] = {
        "总览", "视觉", "操控", "杂项", "性能", "设置", "关于"
    };
    if (i < 0 || i >= PAGE_COUNT) return "";
    return names[i];
}

inline const char* PageDesc(int i) {
    static const char* desc[PAGE_COUNT] = {
        "Charlie 状态面板", "显示相关开关", "操作与灵敏度", "辅助功能占位",
        "实时资源占用", "界面与外观", "版本与声明"
    };
    if (i < 0 || i >= PAGE_COUNT) return "";
    return desc[i];
}

// ---------- 占位业务状态 ----------
// 全部是可点但空转的开关/滑块, 用于验证控件与排版。
// 接入真实业务时, 把这些字段替换成你自己的配置结构。
struct Features {
    // 视觉
    bool  espBox      = false;
    bool  espLine     = false;
    bool  espName     = false;
    bool  espHealth   = false;
    bool  espDistance = false;
    bool  espSkeleton = false;
    int   espStyle    = 0;
    float espRange    = 300.f;
    float boxWidth    = 1.5f;

    // 操控
    bool  aimEnable   = false;
    bool  aimSmooth   = true;
    bool  aimPredict  = false;
    bool  aimVisible  = false;
    float aimSpeed    = 55.f;
    float aimFov      = 120.f;
    int   aimPart     = 0;

    // 杂项
    bool  antiShake   = false;
    bool  fastSwap    = false;
    bool  noRecoil    = false;
    bool  autoFire    = false;
    int   perfMode    = 1;
    float fpsLimit    = 60.f;

    // 设置
    bool  showFps     = true;
    bool  showClock   = false;
    bool  animEnable  = true;
    bool  islandOn    = true;   // 【已废弃】灵动岛常驻, 此字段不再参与判断
    float uiScaleUser = 1.f;
    int   themeIdx    = 0;

    // ============================================================
    // 帧率绘制位置
    //
    // 【为什么需要三档】
    //   帧率是调试信息, 不同场景下想要的位置不一样:
    //     Island —— 贴着灵动岛, 不占额外视觉面积, 但会和状态文字抢空间
    //     Free   —— 屏幕左上角独立区块, 完全不干扰灵动岛
    //     Off    —— 不显示
    //   原来只有 showFps 一个布尔, 关掉之后灵动岛右侧会留一个 "--" 占位,
    //   那是最难看的状态: 既没信息又占着宽度。
    // ============================================================
    int   fpsPos      = 0;      // 0=灵动岛 1=自由 2=不显示

    // 自由区块位置, 归一化到 [0,1] 的屏幕比例。
    //
    // 【为什么存比例不存像素】
    //   横竖屏切换时显示尺寸会变 (2412x1080 <-> 1080x2412),
    //   存绝对像素的话区块会跑到屏幕外。
    //   比例坐标天然跟随屏幕变化, 不需要额外的旋转处理。
    float fpsFreeX    = 0.035f;
    float fpsFreeY    = 0.02f;

    // 自由区块是否可交互 (可拖动)。
    // 关闭后区块仍显示, 但不参与任何命中测试 —— 触摸直接落到下层,
    // 不会挡住用户正常点击屏幕。这一点在本工程里是天然成立的:
    // 窗口是 SurfaceComposerClient 图层, 输入由我们旁听 /dev/input 得到,
    // 不是 Android InputFlinger 分发, 只要不认领触摸就不会拦截。
    bool  fpsFreeDrag = false;
};

Features& F();

// 实时统计
// 分两块, 刷新节奏不同:
//   Stats      —— 帧率/帧时, 每 kPerfInterval 秒(默认 0.5s)刷新一次
//   IslandData —— 灵动岛状态文字, 每帧可更新(轻量)
// 分离原因: 帧率是浮动的瞬时量, 每帧刷新会导致灵动岛数字狂跳、
//           文字反复触发滚动动画, 且白白增加布局重算。
struct Stats {
    // ---- 帧率区 (0.5s 刷新) ----
    float fps        = 0.f;    // 平滑后的帧率
    float frameMs    = 0.f;    // 平滑后的帧耗时
    float fpsMin     = 0.f;    // 本窗口内最低
    float fpsMax     = 0.f;    // 本窗口内最高

    // ---- 灵动岛区 (每帧可更新) ----
    float islandNum  = 0.f;    // 灵动岛右侧数值占位
    const char* status = "就绪";
    bool  warn       = false;
    bool  busy       = false;

    // ---- 资源占用区 (0.5s 刷新, 抄 查理hp源码/jni/src/悬浮窗.cpp UpdatePerfStats) ----
    // 全部走 EMA 平滑, 避免数字跳变
    float memMb      = 0.f;    // 本进程 RSS (MB)
    float memAvailMb = 0.f;    // 系统可用内存 (MB)
    float memTotalMb = 0.f;    // 系统总内存 (MB)
    float cpuSelf    = 0.f;    // 本进程 CPU 占用 (%)
    float cpuSys     = 0.f;    // 系统 CPU 占用 (%)
    float frameMsMax = 0.f;    // 本窗口内最大帧时 (ms)
    int   threadCnt  = 0;      // 线程数
};

Stats& S();

// 帧率刷新间隔 (秒), 默认 0.5
void SetPerfInterval(float sec);
float GetPerfInterval();

// 每帧更新。
// 内部按 GetPerfInterval() 节流: 未到间隔只累积采样,
// 到点才把 fps/frameMs 写进 S() 并置 PerfUpdated() 为 true。
void UpdateStats(float dt);

// 本帧是否刚好刷新了帧率 (供 UI 决定要不要重绘数字)
bool PerfUpdated();

// 帧率文字 (已格式化, 无需每帧 snprintf)
const char* FpsText();
// 帧时文字 (已格式化, 如 "16.7ms")
const char* FrameMsText();

} // namespace charlie
