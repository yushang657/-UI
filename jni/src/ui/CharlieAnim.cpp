// ============================================================
// Charlie UI · 动效层实现
// ============================================================
#include "CharlieAnim.h"

#include <cmath>
#include <cstring>

namespace charlie {

// ---------- 缓动 ----------
namespace ease {

float Linear(float t) { return t; }

float InQuad(float t)     { return t * t; }
float OutQuad(float t)    { return 1.f - (1.f - t) * (1.f - t); }
float InOutQuad(float t)  { return t < 0.5f ? 2.f * t * t : 1.f - 2.f * (1.f - t) * (1.f - t); }

float OutCubic(float t) {
    const float u = 1.f - t;
    return 1.f - u * u * u;
}

float OutQuart(float t) {
    const float u = 1.f - t;
    return 1.f - u * u * u * u;
}

float InQuart(float t) { return t * t * t * t; }

float OutBack(float t, float overshoot) {
    const float c = overshoot;
    const float u = t - 1.f;
    return 1.f + (c + 1.f) * u * u * u + c * u * u;
}

float OutExpo(float t) {
    if (t >= 1.f) return 1.f;
    return 1.f - powf(2.f, -10.f * t);
}

float InOutQuint(float t) {
    if (t < 0.5f) return 16.f * t * t * t * t * t;
    const float u = 1.f - t;
    return 1.f - 16.f * u * u * u * u * u;
}

} // namespace ease

// ---------- SafeDt ----------
float SafeDt(float maxDt) {
    float dt = ImGui::GetIO().DeltaTime;
    if (!(dt > 0.f)) dt = 1.f / 60.f;   // NaN / 0 兜底
    if (dt > maxDt)  dt = maxDt;
    return dt;
}

// ---------- ExpApproach ----------
float ExpApproach(float cur, float target, float speed, float dt) {
    if (speed <= 0.f || dt <= 0.f) return target;
    const float k = 1.f - expf(-speed * dt);
    const float v = cur + (target - cur) * k;
    // 逼近到足够近时直接吸附, 避免长期存在亚像素差值
    if (fabsf(target - v) < 0.002f) return target;
    return v;
}

// ---------- Spring ----------
void Spring::Update(float dt, float stiffness, float damping) {
    if (dt <= 0.f) return;
    const float force = (target - current) * stiffness;
    velocity += force * dt;
    velocity *= (1.f - damping * dt);
    current  += velocity * dt;
    if (fabsf(target - current) < 0.0015f && fabsf(velocity) < 0.0015f) {
        current  = target;
        velocity = 0.f;
    }
}

bool Spring::Settled(float eps) const {
    return fabsf(target - current) < eps && fabsf(velocity) < eps;
}

void SpringClamped::Update(float dt, float stiffness, float damping) {
    s.Update(dt, stiffness, damping);
    if (s.current < lo) { s.current = lo; s.velocity = 0.f; }
    if (s.current > hi) { s.current = hi; s.velocity = 0.f; }
}

// ---------- 指针键动画表 ----------
// 256 槽开放寻址 + 线性探测。无 malloc, 无碎片。
namespace {
constexpr int kSlotCount = 256;

struct Slot {
    const void* key = nullptr;
    float value = 0.f;
};

Slot g_slots[kSlotCount];

inline int HashKey(const void* key) {
    // 指针低位普遍是 0 (对齐), 右移 4 位再取模, 分布更均匀
    size_t h = reinterpret_cast<size_t>(key) >> 4;
    return static_cast<int>(h % kSlotCount);
}

Slot* FindSlot(const void* key) {
    if (!key) return nullptr;
    const int base = HashKey(key);
    for (int i = 0; i < kSlotCount; ++i) {
        Slot& s = g_slots[(base + i) % kSlotCount];
        if (s.key == key) return &s;
        if (s.key == nullptr) {
            s.key = key;
            s.value = 0.f;
            return &s;
        }
    }
    // 表满: 返回 nullptr, 调用方退化为瞬间跳变 (不会崩, 但没动画)
    return nullptr;
}
} // namespace

float Anim(const void* key, bool on, float speed) {
    return AnimValue(key, on ? 1.f : 0.f, speed);
}

float AnimValue(const void* key, float target, float speed) {
    Slot* s = FindSlot(key);
    if (!s) return target;
    s->value = ExpApproach(s->value, target, speed, SafeDt());
    return s->value;
}

void AnimReset(const void* key) {
    if (!key) return;
    const int base = HashKey(key);
    for (int i = 0; i < kSlotCount; ++i) {
        Slot& s = g_slots[(base + i) % kSlotCount];
        if (s.key == key) { s.value = 0.f; return; }
        if (s.key == nullptr) return;
    }
}

void AnimResetAll() {
    memset(g_slots, 0, sizeof(g_slots));
}

// ---------- Timeline ----------
float Timeline::Raw(float now) const {
    if (startTime < 0.f) return 0.f;
    const float t = (now - startTime) / (duration > 0.f ? duration : 0.001f);
    return t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
}

float Timeline::Eval(float now, float (*curve)(float)) {
    if (startTime < 0.f) return 0.f;
    const float raw = Raw(now);
    if (raw >= 1.f) finished = true;
    return curve ? curve(raw) : raw;
}

} // namespace charlie
