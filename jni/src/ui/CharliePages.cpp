// ============================================================
// Charlie UI · 页面实现（全部占位内容）
// ============================================================
#include "CharliePages.h"
#include "CharlieState.h"
#include "CharlieTheme.h"
#include "CharlieWidgets.h"
#include "CharlieAnim.h"
#include "CharlieShell.h"
#include "CharlieFonts.h"

#include <cstdio>
#include <cmath>

namespace charlie {

using namespace charlie::ui;

namespace {

// 顶部页面标题条
void PageHeader(const char* title, const char* desc) {
    const Metrics& m = M();
    const Palette& p = P();

    ImVec2 cp = ImGui::GetCursorScreenPos();
    float avail = ImGui::GetContentRegionAvail().x;

    ImGui::SetWindowFontScale(m.fontL / ImGui::GetFontSize());
    ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(p.textHi));
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.f);

    if (desc && desc[0]) {
        ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(p.textLow));
        ImGui::SetWindowFontScale(m.fontXS / ImGui::GetFontSize());
        ImGui::TextUnformatted(desc);
        ImGui::SetWindowFontScale(1.f);
        ImGui::PopStyleColor();
    }

    // 底部渐隐线
    ImVec2 after = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilledMultiColor(
        ImVec2(cp.x, after.y + 2.f), ImVec2(cp.x + avail, after.y + 3.f),
        p.accent, WithAlpha(p.accent, 0.f),
        WithAlpha(p.accent, 0.f), p.accent);

    Gap(m.gapS);
}
// PlaceholderHint 已移除。
// 原来它画一条 accentGlow 底的青色提示条, 用于说明"这是模板/不含业务逻辑"。
// 这类说明在界面里是噪音: 既占纵向空间, 又在游戏画面上显得突兀。
// 主页「模板说明」区块与关于页末尾各有一处, 现已全部删除。

// ============================================================
// 总览
// ============================================================
void PageDashboard() {
    const Metrics& m = M();
    const Palette& p = P();
    Features& f = F();
    Stats& s = S();

    PageHeader("总览", "Charlie 运行状态与快捷入口");

    // ---- 品牌卡片 ----
    {
        BeginCard("##brandCard", -1.f);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 cp = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;

        // 左侧品牌标识: 圆角方块 + C 字
        const float logo = 56.f * m.s;
        ImVec2 l0(cp.x, cp.y + 4.f * m.s);
        ImVec2 l1(l0.x + logo, l0.y + logo);

        dl->AddRectFilled(ImVec2(l0.x, l0.y + 2.f), ImVec2(l1.x, l1.y + 2.f),
                          WithAlpha(p.accent, 0.18f), m.radius);
        dl->AddRectFilled(l0, l1, Mix(p.accentDim, p.accent, 0.55f), m.radius);
        dl->AddRect(l0, l1, WithAlpha(p.accent, 0.75f), m.radius, 0, 1.f);

        // "C" —— 用两段弧近似
        {
            ImVec2 c(l0.x + logo * 0.5f, l0.y + logo * 0.5f);
            float rr = logo * 0.26f;
            dl->PathArcTo(c, rr, 0.62f, 5.68f, 28);
            dl->PathStroke(IM_COL32(255, 255, 255, 240), 0, 3.4f * m.s);
        }

        // 右侧品牌文字
        ImGui::SetWindowFontScale(m.fontXL / ImGui::GetFontSize());
        ImVec2 tpos(l1.x + m.gap, l0.y);
        ImGui::SetCursorScreenPos(tpos);
        ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(p.textHi));
        ImGui::TextUnformatted("Charlie");
        ImGui::PopStyleColor();
        ImGui::SetWindowFontScale(1.f);

        // 副标题
        ImGui::SetCursorScreenPos(ImVec2(tpos.x + 1.f, tpos.y + m.fontXL * 1.18f));
        ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(p.accent));
        ImGui::SetWindowFontScale(m.fontS / ImGui::GetFontSize());
        ImGui::TextUnformatted("UI 悬浮窗 · 模板");
        ImGui::PopStyleColor();
        ImGui::SetWindowFontScale(1.f);

        // 版本徽标 (右上)
        {
            const char* ver = "v1.0.0";
            ImVec2 vs = ImGui::CalcTextSize(ver);
            float bw = vs.x + m.gap * 1.2f;
            float bh = vs.y + m.gapS * 1.2f;
            ImVec2 b0(cp.x + w - bw, cp.y + 4.f * m.s);
            ImVec2 b1(b0.x + bw, b0.y + bh);
            dl->AddRectFilled(b0, b1, WithAlpha(p.accent, 0.14f), bh * 0.5f);
            dl->AddRect(b0, b1, WithAlpha(p.accent, 0.40f), bh * 0.5f, 0, 1.f);
            dl->AddText(ImVec2(b0.x + (bw - vs.x) * 0.5f, b0.y + (bh - vs.y) * 0.5f),
                        p.accent, ver);
        }

        // 分隔
        ImGui::SetCursorScreenPos(ImVec2(cp.x, cp.y + logo + m.gap * 1.2f));
        Divider();

        // 三格指标
        ImGui::SetCursorScreenPos(ImVec2(cp.x, cp.y + logo + m.gap * 2.0f));
        {
            const float colW = (w - m.gap * 2.f) / 3.f;
            // 帧率/帧时由 UpdateStats 每 0.5s 结算一次并预格式化,
            // 这里直接取现成字符串, 不再每帧 snprintf。
            struct Metric { const char* label; const char* value; ImU32 color; };
            Metric items[3] = {
                {"帧率", FpsText(),  p.accent},
                {"帧时", FrameMsText(), p.accent2},
                {"状态", s.status,   s.warn ? p.danger : p.ok},
            };

            for (int i = 0; i < 3; ++i) {
                float x = cp.x + i * (colW + m.gap);
                ImVec2 vp(x, cp.y + logo + m.gap * 2.1f);
                ImDrawList* d = ImGui::GetWindowDrawList();

                ImGui::SetWindowFontScale(m.fontM / ImGui::GetFontSize());
                ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(items[i].color));
                ImGui::SetCursorScreenPos(vp);
                ImGui::TextUnformatted(items[i].value);
                ImGui::PopStyleColor();
                ImGui::SetWindowFontScale(1.f);

                ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(p.textLow));
                ImGui::SetWindowFontScale(m.fontXS / ImGui::GetFontSize());
                ImGui::SetCursorScreenPos(ImVec2(vp.x, vp.y + m.fontM * 1.32f));
                ImGui::TextUnformatted(items[i].label);
                ImGui::PopStyleColor();
                ImGui::SetWindowFontScale(1.f);
                (void)d;
            }
        }

        EndCard();
    }

    Gap(m.gapS);

    // ---- 快捷开关 (单列行式) ----
    PageHeader("快捷开关", nullptr);
    {
        BeginCard("##quickCard", -1.f);
        // 帧率改成"位置三选一": 灵动岛内 / 自由 / 不显示。
        // 原来的布尔开关关掉之后灵动岛会留一个 "--" 占位,
        // 有占位没信息是最难看的状态, 所以直接换成位置选择。
        {
            static const char* kFpsPos[] = { "灵动岛内", "自由", "不显示" };
            if (RowCombo("帧率位置", &f.fpsPos, kFpsPos, 3,
                         "自由 = 屏幕左上角独立区块, 可拖动")) {
                // showFps 与 fpsPos 保持同步, 老代码路径 (灵动岛取值处)
                // 仍然只看 showFps, 这样不用改那一处
                f.showFps = (f.fpsPos != 2);
                shell::SaveConfig();
            }
            // 老配置里只有 showFps, 迁移时补上 fpsPos, 保证两者一致
            if (f.showFps && f.fpsPos == 2) f.showFps = false;
        }

        // 自由区块的拖动开关。
        // 只在"自由"档下才有意义, 其他档直接隐藏, 少一个无效控件。
        if (f.fpsPos == 1) {
            if (RowToggle("自由移动", &f.fpsFreeDrag,
                          "打开后可拖动帧率区块; 关闭则触摸穿透, 不影响点屏幕")) {
                shell::SaveConfig();
            }
        }

        RowToggle("显示时钟", &f.showClock, "灵动岛右侧显示时间");
        RowToggle("动画特效", &f.animEnable, "关闭后开屏/闭屏立即完成");
        // 灵动岛开关已移除: 它是常驻组件, 始终显示, 不提供关闭入口。
        EndCard();
    }

    Gap(m.gapS);

    // ---- 结束进程（主页快捷入口，与侧栏共用确认状态）----
    PageHeader("进程", nullptr);
    {
        const float bh = m.rowH * 1.05f;
        // 卡片内要装下: 说明行 + 间距 + 按钮,
        // 卡片内容: 仅一个整宽按钮 (说明行已移除)
        BeginCard("##quitCard", -1.f);

        // 说明行已移除(原为 textLow 灰色小字), 卡片只剩按钮

        Gap(m.gapS * 0.8f);

        // 整宽按钮，与侧栏用的是同一个组件、同一份确认状态
        float bw = ImGui::GetContentRegionAvail().x;
        shell::QuitButton("##quitHome", bw, bh);

        EndCard();
    }

}

// ============================================================
// 视觉
// ============================================================
void PageVisual() {
    const Metrics& m = M();
    Features& f = F();

    PageHeader("视觉", "显示相关开关与样式");

    Section("绘制项");
    {
        BeginCard("##visCard1", -1.f);
        RowToggle("方框", &f.espBox);
        RowToggle("射线", &f.espLine);
        RowToggle("名称", &f.espName);
        RowToggle("血量", &f.espHealth);
        RowToggle("距离", &f.espDistance);
        RowToggle("骨骼", &f.espSkeleton);
        EndCard();
    }

    Gap(m.gapS);

    Section("样式");
    {
        BeginCard("##visCard2", -1.f);

        static const char* styles[] = {"矩形", "圆角矩形", "四角", "圆形"};
        RowCombo("框体样式", &f.espStyle, styles, 4);
        RowSlider("框线粗细", &f.boxWidth, 0.5f, 5.f, "%.1f");

        EndCard();
    }

    Gap(m.gapS);

    Section("范围");
    {
        BeginCard("##visCard3", -1.f);
        RowSlider("绘制距离", &f.espRange, 50.f, 800.f, "%.0f m");
        EndCard();
    }
}

// ============================================================
// 操控
// ============================================================
void PageAim() {
    const Metrics& m = M();
    Features& f = F();

    PageHeader("操控", "操作与灵敏度（占位）");

    Section("基础");
    {
        BeginCard("##aimCard1", -1.f);
        RowToggle("启用", &f.aimEnable);
        RowToggle("平滑", &f.aimSmooth);
        RowToggle("预判", &f.aimPredict);
        RowToggle("仅可见", &f.aimVisible);
        EndCard();
    }

    Gap(m.gapS);

    Section("参数");
    {
        BeginCard("##aimCard2", -1.f);
        RowSlider("跟随速度", &f.aimSpeed, 1.f, 100.f, "%.0f");
        RowSlider("视野角度", &f.aimFov, 10.f, 360.f, "%.0f°");
        EndCard();
    }

    Gap(m.gapS);

    Section("部位");
    {
        BeginCard("##aimCard3", -1.f);
        static const char* parts[] = {"头部", "胸部", "自动"};
        RowCombo("优先部位", &f.aimPart, parts, 3);
        EndCard();
    }
}

// ============================================================
// 杂项
// ============================================================
void PageMisc() {
    const Metrics& m = M();
    Features& f = F();

    PageHeader("杂项", "辅助功能占位");

    Section("开关");
    {
        BeginCard("##miscCard1", -1.f);
        RowToggle("防抖", &f.antiShake);
        RowToggle("快速切枪", &f.fastSwap);
        RowToggle("无后座", &f.noRecoil);
        RowToggle("自动开火", &f.autoFire);
        EndCard();
    }

    Gap(m.gapS);

    Section("性能");
    {
        BeginCard("##miscCard2", -1.f);
        static const char* modes[] = {"省电", "均衡", "性能"};
        RowCombo("性能模式", &f.perfMode, modes, 3);
        RowSlider("帧率上限", &f.fpsLimit, 30.f, 144.f, "%.0f");
        EndCard();
    }
}

// ============================================================
// 性能 —— 实时资源占用
// 数据源: CharlieState.cpp 的 0.5s 结算点 (抄参考工程 UpdatePerfStats)
// 本页只读展示, 不改任何状态, 因此不写回配置。
// ============================================================
void PagePerf() {
    const Metrics& m = M();
    const Palette& p = P();
    const Stats& s = S();

    PageHeader("性能", "实时资源占用");

    // 数值格式化: 只在数值变化时才 snprintf (PerfUpdated 已经节流到 0.5s,
    // 这里再缓存一层, 避免每帧重复格式化 8 个数字)
    static char bufMem[32] = "--";
    static char bufAvail[48] = "--";
    static char bufCpuSelf[32] = "--";
    static char bufCpuSys[32] = "--";
    static char bufFps[32] = "--";
    static char bufFrame[48] = "--";
    static char bufThread[32] = "--";

    static float lastMem = -1.f, lastAvail = -1.f, lastCpuSelf = -1.f;
    static float lastCpuSys = -1.f, lastFps = -1.f, lastFrame = -1.f;
    static int   lastThread = -1;

    if (s.memMb      != lastMem)     { lastMem     = s.memMb;      snprintf(bufMem,     sizeof(bufMem),     "%.1f MB", s.memMb); }
    if (s.memAvailMb != lastAvail)   { lastAvail   = s.memAvailMb; snprintf(bufAvail,   sizeof(bufAvail),   "%.0f / %.0f MB", s.memAvailMb, s.memTotalMb); }
    if (s.cpuSelf    != lastCpuSelf) { lastCpuSelf = s.cpuSelf;    snprintf(bufCpuSelf, sizeof(bufCpuSelf), "%.1f %%", s.cpuSelf); }
    if (s.cpuSys     != lastCpuSys)  { lastCpuSys  = s.cpuSys;     snprintf(bufCpuSys,  sizeof(bufCpuSys),  "%.1f %%", s.cpuSys); }
    if (s.fps        != lastFps)     { lastFps     = s.fps;        snprintf(bufFps,     sizeof(bufFps),     "%.0f FPS", s.fps); }
    if (s.frameMs    != lastFrame)   { lastFrame   = s.frameMs;    snprintf(bufFrame,   sizeof(bufFrame),   "%.1f ms (峰值 %.1f)", s.frameMs, s.frameMsMax); }
    if (s.threadCnt  != lastThread)  { lastThread  = s.threadCnt;  snprintf(bufThread,  sizeof(bufThread),  "%d", s.threadCnt); }

    // 阈值配色: 超过红线标红, 提醒用户
    const ImU32 cpuCol   = (s.cpuSelf > 25.f) ? p.warn : p.textHi;
    const ImU32 memCol   = (s.memTotalMb > 0.f && s.memMb > s.memTotalMb * 0.3f)
                           ? p.warn : p.textHi;
    const ImU32 fpsCol   = (s.fps > 0.f && s.fps < 30.f) ? p.warn : p.accent;

    Section("进程");
    {
                          + (ImGui::GetTextLineHeight() + m.gapS * 1.2f) * 3.f
                          + m.gapS;
        BeginCard("##perfProc", -1.f);
        RowInfo("内存占用", bufMem, "本进程常驻内存 (RSS)", memCol);
        RowInfo("CPU 占用", bufCpuSelf, "本进程占总 CPU 的百分比", cpuCol);
        RowInfo("线程数",   bufThread, "进程内活动线程", 0);
        EndCard();
    }

    Gap(m.gapS);

    Section("帧率");
    {
                          + (ImGui::GetTextLineHeight() + m.gapS * 1.2f) * 2.f
                          + m.gapS;
        BeginCard("##perfFps", -1.f);
        RowInfo("当前帧率", bufFps,   "0.5s 平滑值", fpsCol);
        RowInfo("帧耗时",   bufFrame, "当前 / 本窗口峰值", 0);
        EndCard();
    }

    Gap(m.gapS);

    Section("系统");
    {
                          + (ImGui::GetTextLineHeight() + m.gapS * 1.2f) * 2.f
                          + m.gapS;
        BeginCard("##perfSys", -1.f);
        RowInfo("可用 / 总内存", bufAvail, "/proc/meminfo MemAvailable", 0);
        RowInfo("系统 CPU",      bufCpuSys, "/proc/stat 全局占用", 0);
        EndCard();
    }

    Gap(m.gapS);
    Label("采样间隔 0.5s, 与灵动岛帧率同步刷新", true);
}

// ============================================================
// 设置
// ============================================================
void PageSettings() {
    const Metrics& m = M();
    const Palette& p = P();
    Features& f = F();
    shell::LayoutCfg& cfg = shell::Cfg();

    PageHeader("设置", "界面与外观");

    // ---------- 外观 ----------
    Section("外观");
    {
        BeginCard("##setLook", -1.f);

        // ---------- 明暗模式 ----------
        // themeIdx: 0 = 暗色, 1 = 浅色。
        // 切换后 SetDark 会重建整套调色板 + 重刷 ImGui 内建颜色,
        // 下一帧全界面（含灵动岛、Toast、弹窗）一起换色。
        {
            bool dark = (cfg.themeIdx != 1);
            if (RowToggle("暗色模式", &dark, dark ? "当前为深色界面" : "当前为浅色界面")) {
                cfg.themeIdx = dark ? 0 : 1;
                SetDark(dark);
                shell::SaveConfig();
                ui::Toast(dark ? "已切换到暗色模式" : "已切换到浅色模式");
            }
        }

        if (RowSlider("界面缩放", &f.uiScaleUser, 0.8f, 1.4f, "%.2fx",
                      "调整整个面板的尺寸")) {
            RebuildMetrics(ImGui::GetMainViewport()->Size.x,
                           ImGui::GetMainViewport()->Size.y, f.uiScaleUser);
        }

        static const char* kNavSides[] = { "左侧", "右侧" };
        if (RowCombo("导航栏位置", &cfg.navSide, kNavSides, 2, "切换页面列表的左右位置")) {
            shell::SaveConfig();
            ui::Toast(cfg.navSide == 0 ? "导航栏已移到左侧" : "导航栏已移到右侧");
        }

        // 字体档位
        //
        // 【为什么要按机型过滤】
        //   雅黑/宋体/楷体 读的是 /system/fonts/ 下的系统字体。
        //   不同 ROM 装的字体不一样, 直接全列出来, 用户选了没字的档位
        //   会看到界面回退到凯撒金却以为"切换失败"。
        //   这里先用 fonts::SysAvailable() 探测文件是否可读, 不可用就不列出。
        //
        // 【为什么每次进来都要重建列表】
        //   列表内容依赖运行时机型的字体文件, 不是编译期常量,
        //   所以不能写成 static 常量数组 —— 那会把不可用的档位也列出来。
        static const char* kFontLabels[8];
        static int         kFontIds[8];
        int fontN = 0;
        kFontLabels[fontN] = "凯撒金"; kFontIds[fontN] = fonts::FONT_CAESAR;   ++fontN;
        kFontLabels[fontN] = "思源黑"; kFontIds[fontN] = fonts::FONT_NOTO;     ++fontN;
        kFontLabels[fontN] = "苹方";   kFontIds[fontN] = fonts::FONT_PINGFANG;++fontN;
        if (fonts::SysAvailable(fonts::FONT_SYS_YAHEI)) { kFontLabels[fontN] = "雅黑(系统)"; kFontIds[fontN] = fonts::FONT_SYS_YAHEI; ++fontN; }
        if (fonts::SysAvailable(fonts::FONT_SYS_SONG))  { kFontLabels[fontN] = "宋体(系统)"; kFontIds[fontN] = fonts::FONT_SYS_SONG;  ++fontN; }
        if (fonts::SysAvailable(fonts::FONT_SYS_KAI))   { kFontLabels[fontN] = "楷体(系统)"; kFontIds[fontN] = fonts::FONT_SYS_KAI;   ++fontN; }
        kFontLabels[fontN] = "内置默认"; kFontIds[fontN] = fonts::FONT_DEFAULT; ++fontN;

        // 把配置里的 fontId 映射成列表下标
        int fontSel = 0;
        for (int i = 0; i < fontN; ++i) if (kFontIds[i] == cfg.fontId) { fontSel = i; break; }

        if (RowCombo("字体", &fontSel, kFontLabels, fontN, "切换后立即生效")) {
            cfg.fontId = kFontIds[fontSel];
            // 换字体必须重建 ImGui 上下文级别的字体图集,
            // 用 Reset() 清标志后 LoadWith() 才会真正重新 AddFontFrom*。
            fonts::Reset();
            fonts::LoadWith(cfg.fontId);
            shell::SaveConfig();
            ui::Toast("字体已切换");
        }

        // 灵动岛开关已移除: 它是常驻组件, 始终显示, 不提供关闭入口。

        EndCard();
    }

    Gap(m.gapS);

    // ---------- 动效 ----------
    Section("动效");
    {
        BeginCard("##setAnim", -1.f);

        RowToggle("动画特效", &f.animEnable, "关闭后开屏/闭屏立即完成");

        RowSlider("灵动岛位置", &cfg.islandTop, 0.f, 1.f, "%.0f%%",
                  "纵向位置, 0 为最上");
        cfg.islandTop = ImClamp(cfg.islandTop, 0.f, 1.f);

        EndCard();
    }

    Gap(m.gapS);

    // ---------- 性能 ----------
    Section("性能");
    {
        BeginCard("##setPerf", -1.f);

        static const char* kModes[] = { "省电", "均衡", "性能" };
        RowCombo("运行模式", &f.perfMode, kModes, 3, "改善流畅度");

        // 帧率位置 (与主页快捷开关是同一个字段, 两处都可改)
        {
            static const char* kFpsPos[] = { "灵动岛内", "自由", "不显示" };
            if (RowCombo("帧率位置", &f.fpsPos, kFpsPos, 3,
                         "自由 = 屏幕左上角独立区块, 可拖动")) {
                f.showFps = (f.fpsPos != 2);
                shell::SaveConfig();
            }
        }
        if (f.fpsPos == 1) {
            if (RowToggle("自由移动", &f.fpsFreeDrag,
                          "打开后可拖动; 关闭则触摸穿透, 不影响点屏幕")) {
                shell::SaveConfig();
            }
        }

        RowToggle("显示时钟", &f.showClock);

        EndCard();
    }

    Gap(m.gapS);

    // ---------- 快捷键 ----------
    Section("快捷键");
    {
        BeginCard("##setKey", -1.f);

        // 一行一条说明, 用行式排布保持视觉统一
        struct HintRow { const char* k; const char* v; };
        HintRow rows[4] = {
            { "音量 +",   "打开悬浮窗" },
            { "音量 -",   "隐藏悬浮窗" },
            { "音量 - ×2", "关闭程序 (隐藏时, 1.5s 内)" },
            { "侧栏按钮",  "结束进程 (连点两次)" },
        };
        const float lineH = ImGui::GetTextLineHeight();
        for (int i = 0; i < 4; ++i) {
            ImVec2 cp = ImGui::GetCursorScreenPos();
            float rowW = ImGui::GetContentRegionAvail().x;

            ImGui::SetCursorScreenPos(ImVec2(cp.x + m.gapS, cp.y + (m.rowH - lineH) * 0.5f));
            ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(p.textHi));
            ImGui::TextUnformatted(rows[i].k);
            ImGui::PopStyleColor();

            ImVec2 vs = ImGui::CalcTextSize(rows[i].v);
            ImGui::SetCursorScreenPos(ImVec2(cp.x + rowW - vs.x - m.gapS,
                                            cp.y + (m.rowH - lineH) * 0.5f));
            ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(p.textMid));
            ImGui::TextUnformatted(rows[i].v);
            ImGui::PopStyleColor();

            // 分隔线
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(cp.x, cp.y + m.rowH - 1.f),
                ImVec2(cp.x + rowW, cp.y + m.rowH),
                WithAlpha(p.divider, 0.45f));

            ImGui::SetCursorScreenPos(ImVec2(cp.x, cp.y + m.rowH));
        }

        EndCard();
    }
}

// ============================================================
// 关于
// ============================================================
void PageAbout() {
    const Metrics& m = M();
    const Palette& p = P();

    PageHeader("关于", "版本信息与声明");

    {
        BeginCard("##aboutCard", -1.f);

        struct Row { const char* k; const char* v; };
        Row rows[5] = {
            {"项目名称", "Charlie"},
            {"模块类型", "UI 悬浮窗模板"},
            {"版本号",   "1.0.0"},
            {"渲染后端", "OpenGL ES 3 / Vulkan"},
            {"构建日期", __DATE__},
        };
        for (int i = 0; i < 5; ++i) {
            ImVec2 cp = ImGui::GetCursorScreenPos();
            float w = ImGui::GetContentRegionAvail().x;
            float rh = m.rowH * 0.88f;

            if (i % 2 == 0) {
                ImGui::GetWindowDrawList()->AddRectFilled(
                    cp, ImVec2(cp.x + w, cp.y + rh),
                    WithAlpha(p.bgItem, 0.42f), m.radiusS);
            }
            ImGui::SetCursorScreenPos(ImVec2(cp.x + m.gapS * 0.8f,
                                             cp.y + (rh - ImGui::GetTextLineHeight()) * 0.5f));
            ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(p.textMid));
            ImGui::TextUnformatted(rows[i].k);
            ImGui::PopStyleColor();

            ImVec2 vs = ImGui::CalcTextSize(rows[i].v);
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(cp.x + w - vs.x - m.gapS * 0.8f,
                       cp.y + (rh - vs.y) * 0.5f),
                i == 1 ? p.accent : p.textHi, rows[i].v);

            ImGui::Dummy(ImVec2(1.f, rh - ImGui::GetTextLineHeight()));
        }
        EndCard();
    }

    Gap(m.gapS);
}

} // namespace

// ============================================================
// 分发
// ============================================================
void DrawPage(int pageId) {
    switch (pageId) {
    case PAGE_DASHBOARD: PageDashboard(); break;
    case PAGE_VISUAL:    PageVisual();    break;
    case PAGE_AIM:       PageAim();       break;
    case PAGE_MISC:      PageMisc();      break;
    case PAGE_PERF:      PagePerf();      break;
    case PAGE_SETTINGS:  PageSettings();  break;
    case PAGE_ABOUT:     PageAbout();     break;
    default:             PageDashboard(); break;
    }
}

} // namespace charlie
