// ============================================================
// Charlie UI · 动效层
// 全部时间驱动 (乘 DeltaTime), 帧率无关 —— 修正参考实现里
// 「每帧固定步进」导致 60/120/144Hz 手感不一致的问题
// ============================================================
#pragma once

#include "imgui.h"

namespace charlie {

// ---------- 缓动曲线 ----------
namespace ease {
    float Linear(float t);
    float InQuad(float t);
    float OutQuad(float t);
    float InOutQuad(float t);
    float OutCubic(float t);
    float OutQuart(float t);      // 开屏主曲线: 前段猛冲, 后段贴住
    float InQuart(float t);       // 闭屏主曲线
    float OutBack(float t, float overshoot = 1.70158f);  // 带回弹过冲
    float OutExpo(float t);
    float InOutQuint(float t);
}

// ---------- 安全 DeltaTime ----------
// 从后台切回时 dt 可能是几百毫秒, 直接乘会让动画瞬移到终点。
// 统一在这里钳制, 所有动画都必须走它。
float SafeDt(float maxDt = 1.f / 20.f);

// ---------- 指数追随 ----------
// 刷新率无关的平滑逼近: cur += (target-cur) * (1 - e^(-speed*dt))
// speed 越大越快。speed=8 约 300ms 稳定, speed=16 约 150ms 稳定。
float ExpApproach(float cur, float target, float speed, float dt);

// ---------- 弹簧 ----------
// 二阶弹簧: 有真实过冲与回弹, 比缓动更"活"
struct Spring {
    float current = 0.f;
    float target  = 0.f;
    float velocity = 0.f;

    void Set(float v) { current = target = v; velocity = 0.f; }
    void To(float v)  { target = v; }
    // stiffness 越大越快, damping 越大越不弹。常用 180/16、200/18、260/20
    void Update(float dt, float stiffness = 200.f, float damping = 18.f);
    bool Settled(float eps = 0.001f) const;
};

// 带过冲钳制的弹簧 (用于宽度/高度这类不能为负的量)
struct SpringClamped {
    Spring s;
    float lo = 0.f;
    float hi = 1e9f;
    void Set(float v) { s.Set(v); }
    void To(float v)  { s.To(v < lo ? lo : (v > hi ? hi : v)); }
    void Update(float dt, float stiffness = 200.f, float damping = 18.f);
};

// ---------- 指针键动画缓存 ----------
// 同类控件共用一张表, 按控件标识指针做 key。
// 相比 std::map<const char*> 更快, 且没有指针比较陷阱:
// 调用方传的一定是稳定地址 (变量地址 / 字面量 / 显式 key)。
float Anim(const void* key, bool on, float speed = 12.f);
float AnimValue(const void* key, float target, float speed = 12.f);
void  AnimReset(const void* key);
void  AnimResetAll();

// ---------- 一次性时间轴 ----------
// 用于开屏/闭屏这种「播一次」的动画
struct Timeline {
    float startTime = -1.f;
    float duration  = 0.3f;
    bool  finished  = false;

    void Start(float now, float dur) { startTime = now; duration = dur; finished = false; }
    // 返回 0..1 的原始进度
    float Raw(float now) const;
    // 返回缓动后进度, 播完自动置 finished
    float Eval(float now, float (*curve)(float));
    bool  Done(float now) const { return startTime >= 0.f && (now - startTime) >= duration; }
    void  Reset() { startTime = -1.f; finished = false; }
    bool  Active() const { return startTime >= 0.f; }
};

} // namespace charlie
