// ============================================================
// Charlie UI · 显示与朝向
//
// 悬浮层的 buffer 尺寸在 Create 时固定, SurfaceControl 没有暴露
// resize 接口。设备旋转后层尺寸必须跟着换, 否则:
//   竖屏建 1080x2400 -> 转横屏后层还是 1080 宽, UI 被裁
//
// 本模块每帧轮询真实屏幕尺寸, 变化时通知调用方重建渲染栈。
// 重建动作放在 main.cpp (需要重载字体 / 重建 ImGui 上下文),
// 这里只负责检测与去抖。
// ============================================================
#pragma once

#include <cstdint>

namespace charlie {
namespace display {

struct Info {
    int32_t width       = 0;    // 当前朝向逻辑屏幕宽
    int32_t height      = 0;    // 当前朝向逻辑屏幕高
    int32_t orientation = 0;    // 0/1/2/3
    bool    landscape   = false;
};

// 读一次当前屏幕信息 (不缓存)
Info Query();

// 绑定当前层尺寸基准 (层建好后调用一次)
void Bind(int32_t w, int32_t h, int32_t orientation);

// 每帧调用。检测到尺寸稳定变化时返回 true,
// 此时 info() 给出新尺寸, 调用方应重建渲染栈。
bool Poll();

// 最近一次 Poll 认为已稳定的目标尺寸
Info const& Target();

// 缺省 350ms: 旋转过程中系统会连抛中间尺寸, 稳定后再重建
void SetSettleDelay(float seconds);

} // namespace display
} // namespace charlie
