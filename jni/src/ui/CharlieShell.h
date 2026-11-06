// ============================================================
// Charlie UI · 壳层
// 开屏 / 闭屏动画 + 页面导航 + 免责弹窗 + 品牌区
// ============================================================
#pragma once

#include "imgui.h"

namespace charlie {

// ---------- 窗口生命周期 ----------
enum class WinState {
    Hidden,     // 完全隐藏, 不绘制
    Opening,    // 开屏动画中
    Opened,     // 稳定显示
    Closing,    // 闭屏动画中
};

namespace shell {

// 初始化 (字体加载后调用一次)
void Init(float screenW, float screenH, float userScale);

// 请求开屏 / 闭屏 (音量键等外部触发调用)
void RequestOpen();
void RequestClose();
void Toggle();

// 当前状态
WinState State();
bool IsVisible();

// 立即销毁 (强退时用, 不走动画)
void ForceClose();

// 每帧绘制
// 返回 false 表示壳层希望退出主循环 (用户点了退出)
bool Frame();

// 用户点击了「退出」按钮
void RequestExit();
bool ExitRequested();

// ---------- 结束进程按钮（可复用组件）----------
// 侧栏底部与主页共用同一套两段式确认状态,
// 任意一处点击都会让两处同时进入确认态, 不会出现状态不一致。
// 返回 true 表示本帧完成了确认, 调用方通常无需再做事
// （RequestExit 已在内部发出）。
bool QuitButton(const char* id, float width, float height);

// 当前是否处于确认态 (排障/联动用)
bool QuitArmed();
void ResetQuitArm();

// ---------- 免责声明 ----------
// 是否已同意 (带持久化)
bool DisclaimerAgreed();
void SetDisclaimerPath(const char* path);   // 传 nullptr 用默认路径

// 渲染栈重建后调用 (旋转屏幕 / ImGui 上下文重建)。
// 不重置用户已确认的免责状态, 但要让弹窗在当前上下文里重新 OpenPopup,
// 否则 popup 栈随旧上下文销毁, 弹窗再也弹不出来。
void OnRenderStackRebuilt();

// 免责弹窗不接受任何音量键输入。
// 关闭路径只有一条：「我已知晓」按钮。
// (接口已整体移除, 此处不再声明 DisclaimerVolumeKey / ConsumeDisclaimerScroll 等)

// ---------- 布局参数 (供设置页调节) ----------
struct LayoutCfg {
    float uiScale    = 1.f;    // 用户缩放 0.8~1.4
    int   themeIdx   = 0;      // 主题索引 (预留多主题)
    bool  showIsland = true;   // 【已废弃】灵动岛常驻, 此字段不再参与判断, 仅保留兼容旧配置
    int   islandStyle= 0;      // 灵动岛样式 (预留)
    float islandTop  = 0.f;    // 灵动岛纵向位置 0~1
    int   navSide    = 0;      // 导航栏位置: 0=左 (默认) 1=右
    int   fontId     = 0;      // 字体: 0=凯撒金 1=思源黑 2=内置默认

    // 音量键控制开关
    //   true  = 音量键参与交互 (上键开窗 / 下键藏窗 / 下键连按退出)
    //   false = 音量键完全交给系统, 程序不响应
    //
    // 【为什么需要这个开关】
    //   监听 /dev/input 读音量键时, 事件只被我们消费,
    //   系统的音量调节收不到按键 —— 用户调不了音量也没法静音。
    //   玩游戏/看视频时这是硬伤, 所以必须能整个关掉。
    //   关掉时监听线程会退出 (见 volume::Stop), 不占 CPU 也不抢输入。
    bool  volKey     = true;
};
LayoutCfg& Cfg();

// 保存 / 读取位置与配置
void SaveConfig();
void LoadConfig();

} // namespace shell
} // namespace charlie
