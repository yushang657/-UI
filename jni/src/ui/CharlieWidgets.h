// ============================================================
// Charlie UI · 控件库
// 全部自绘 (ItemSize + ItemAdd + ButtonBehavior + DrawList),
// 不依赖 ImGui 原生控件的样式, 触摸目标统一走 m.rowH
// ============================================================
#pragma once

#include "imgui.h"

namespace charlie {
namespace ui {

// ---------- 布局 ----------
void Gap(float h = -1.f);                    // h<0 用默认 gap
void Section(const char* title);             // 分组标题 (左侧强调竖条)
void Label(const char* text, bool sub = false);
void Divider();
void Spacer(float w);                        // 横向间距 (SameLine 后调用)

// 卡片容器: 自动开 Child, 返回是否可见。必须配对 EndCard()
bool BeginCard(const char* id, float height);
void EndCard();

// ---------- 开关 ----------
// 整行可点, 右侧轨道。返回是否被改变
bool Toggle(const char* label, bool* v);
// 一行两个开关, 均分宽度
bool Toggle2(const char* l1, bool* v1, const char* l2, bool* v2);
// 一行四个开关 (紧凑, 仅短标签)
bool Toggle4(const char* l1, bool* v1, const char* l2, bool* v2,
             const char* l3, bool* v3, const char* l4, bool* v4);

// 勾选框 (方块 + 勾)
bool Check(const char* label, bool* v);

// ---------- 滑块 ----------
// 标签在左, 数值在右, 轨道在下。返回是否被改变
bool SliderFloat(const char* label, float* v, float lo, float hi,
                 const char* fmt = "%.0f");
bool SliderInt(const char* label, int* v, int lo, int hi);

// 双列滑块: 调用前 BeginGroup, 本函数只负责宽度自适应
bool SliderCompact(const char* label, float* v, float lo, float hi,
                   const char* fmt, float width);

// ---------- 选择 ----------
// 下拉: 内联展开, 不用 ImGui popup (原因见 .cpp 顶部注释)
bool Combo(const char* label, int* idx, const char* const items[], int count);

// 强制收起当前展开的下拉 (面板关闭 / 切页 / 屏幕旋转时调用)
void CloseOpenCombo();

// ---------- 按钮 ----------
enum class BtnKind { Primary, Ghost, Danger };
bool Button(const char* label, BtnKind kind = BtnKind::Primary, float width = 0.f);
// 胶囊按钮 (用于页面顶部的核心操作)
bool Pill(const char* label, BtnKind kind, float width);

// ---------- 色点选择 ----------
bool ColorDots(const char* id, int* idx, const ImU32* colors, int count, float dotR = 0.f);

// ---------- 折叠 ----------
bool Collapse(const char* label, bool* open);

// ---------- 设置行 (标签在左, 控件在右) ----------
// 统一的一列多控件排布: 每行一个控件, 标签左对齐、控件右对齐,
// 行间有细分隔线。触摸目标高度 = m.rowH, 不易误触。
// 便捷封装: 一行一个开关
bool RowToggle(const char* label, bool* v, const char* hint = nullptr);
// 便捷封装: 一行一个滑块 (轨道在标签行下方, 占满整行)
bool RowSlider(const char* label, float* v, float lo, float hi,
               const char* fmt = "%.0f", const char* hint = nullptr);
bool RowSliderInt(const char* label, int* v, int lo, int hi,
                  const char* hint = nullptr);
// 便捷封装: 一行一个下拉
bool RowCombo(const char* label, int* idx, const char* const items[], int count,
              const char* hint = nullptr);
// 便捷封装: 一行一个按钮 (整行宽)
bool RowButton(const char* label, BtnKind kind = BtnKind::Primary,
               const char* hint = nullptr);
// 便捷封装: 只读数值行 (标签左 / 数值右 / 可选说明行), 不响应点击
void RowInfo(const char* label, const char* value,
             const char* hint = nullptr, ImU32 valueColor = 0);

// ---------- Toast ----------
void Toast(const char* msg, bool warn = false);

// ============================================================
// 明暗切换全屏过渡
//
// 【为什么需要它】
//   SetDark() 会一次性重建调色板, 所有元素在同一帧从深色跳到浅色。
//   对整块面板来说这是一次"闪白", 很刺眼。
//   这里加一层全屏遮罩: 切换瞬间遮罩从 0 快速升到峰值(用新主题色),
//   再缓慢淡出, 视觉上就是一次平滑的"光圈掠过", 遮住了跳变。
//
//   实现只用 GetForegroundDrawList(), 不依赖任何窗口,
//   所以在 End() 之后调用也不会制造 Debug##Default 窗口。
// ============================================================
namespace themeTransition {
    void Start(bool toDark);   // 触发一次过渡

    // 每帧调用; 用前景 drawlist 画遮罩。
    // wp/ws 是悬浮窗的屏幕矩形, rounding 是窗口圆角。
    // 【必须传矩形】前版按全屏绘制, 导致整个屏幕闪一下;
    // 明暗是面板自身属性, 遮罩只能落在面板范围内。
    void Draw(const ImVec2& wp, const ImVec2& ws, float rounding);

    // 面板不可见时不画 (留空实现, 省得调用点都要写 if)
    void Draw();

    bool Active();
}
void DrawToasts();

} // namespace ui
} // namespace charlie
