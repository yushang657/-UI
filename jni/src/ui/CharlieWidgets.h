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
// 下拉框。
//   label：用于 ID 唯一化，同时决定表头是否带前缀标签。
//   showLabelInHeader：表头是否渲染 "标签: 当前项"。
//     RowCombo 已经在左侧单独画了行标签，所以它传 false，
//     表头只显示"当前项"，避免同一行里同一个词出现两次。
bool Combo(const char* label, int* idx, const char* const items[], int count,
           bool showLabelInHeader = true);

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

// ============================================================
// 二级标签条 (页面内子页切换)
//
// 【为什么自绘, 不用 ImGui::BeginTabBar】
//   ImGui 原生 TabBar 内部走 ButtonBehavior + ItemHoverable,
//   hover 依赖 g.HoveredWindow == 当前窗口 (imgui.cpp:4593)。
//   本工程是 SurfaceComposer 图层 + 旁听 /dev/input 注入,
//   弹窗关闭后焦点一旦不干净, 原生 Tab 就会集体点不动 ——
//   这和之前 Toggle/Combo 踩到的是同一个坑。
//   所以这里改成纯坐标命中 + TouchClaim 归属, 与其它控件同一套逻辑。
//
// 【视觉】
//   下划线式: 文字 + 选中态底部 2px 强调条 + 轻微底色。
//   不用胶囊分段, 因为页面宽度有限, 中文标签胶囊在 3 个以上时
//   文字会被压得很挤; 下划线式横向占用更小, 也更贴近系统 Tab 的观感。
//
// 【用法】
//   int& t = TabIndex(PAGE_AIM);      // 取本页当前子页下标 (自动跨帧保持)
//   int  n = SubTabs("##aim", kNames, kCount, &t);
//   if (n >= 0) t = n;                // 发生切换
//   // 之后按 t 分发内容
// ============================================================

// 取某页的子页下标引用。首次访问返回 0。
// 用函数内静态数组, 页面增删不用改任何声明; PAGE_COUNT 变了也能自适应。
int& TabIndex(int pageId);

// 只重绘不返回。返回用户点中的新下标, 未点击返回 -1。
//   id      —— ImGui ID 作用域 (同一页内唯一即可)
//   labels  —— 标签文字数组 (不含 "##")
//   count   —— 标签数量
//   cur     —— 当前选中下标, 越界自动夹到 [0, count-1]
int SubTabs(const char* id, const char* const labels[], int count, int* cur);

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
