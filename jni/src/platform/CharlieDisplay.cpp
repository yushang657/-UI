// ============================================================
// Charlie UI · 显示与朝向实现
// ============================================================
#include "CharlieDisplay.h"

#include "ANativeWindowCreator.h"
#include "imgui.h"

#include <cstdio>
#include <cstdlib>

namespace charlie {
namespace display {

namespace {

int32_t g_layerW = 0;
int32_t g_layerH = 0;
int32_t g_layerOrient = -1;

Info g_target;

float   g_settleDelay = 0.35f;
float   g_pendingSince = 0.f;
int32_t g_pendingW = 0;
int32_t g_pendingH = 0;

} // namespace

Info Query() {
    auto di = android::ANativeWindowCreator::GetDisplayInfo();
    Info out;
    out.width       = di.width  > 0 ? di.width  : 1080;
    out.height      = di.height > 0 ? di.height : 1920;
    out.orientation = di.orientation;
    out.landscape   = out.width > out.height;
    return out;
}

void Bind(int32_t w, int32_t h, int32_t orientation) {
    g_layerW      = w;
    g_layerH      = h;
    g_layerOrient = orientation;
    g_pendingSince = 0.f;
    g_pendingW = g_pendingH = 0;

    g_target = Query();
}

bool Poll() {
    // 恢复每帧轮询。
    // 上一版加了 200ms 节流, 但那会改变主循环的固定开销节奏,
    // 且 ImGui::GetTime() 在 Poll 里被额外调用。本工程层尺寸固定,
    // Query() 开销可接受, 稳定性优先于这点优化。
    Info now = Query();

    // 与当前层完全一致: 无事
    if (now.width == g_layerW && now.height == g_layerH &&
        now.orientation == g_layerOrient) {
        g_pendingSince = 0.f;
        g_pendingW = g_pendingH = 0;
        return false;
    }

    // 尺寸变了: 起去抖
    if (now.width != g_pendingW || now.height != g_pendingH) {
        g_pendingW = now.width;
        g_pendingH = now.height;
        g_pendingSince = ImGui::GetTime();
        return false;
    }

    if (ImGui::GetTime() - g_pendingSince < g_settleDelay) return false;

    printf("[Charlie] 屏幕变化: %dx%d(orient %d) -> %dx%d(orient %d)\n",
           (int)g_layerW, (int)g_layerH, (int)g_layerOrient,
           (int)now.width, (int)now.height, (int)now.orientation);

    g_target = now;
    g_pendingSince = 0.f;
    g_pendingW = g_pendingH = 0;
    return true;
}

Info const& Target() { return g_target; }

void SetSettleDelay(float seconds) {
    if (seconds > 0.f) g_settleDelay = seconds;
}

} // namespace display
} // namespace charlie
