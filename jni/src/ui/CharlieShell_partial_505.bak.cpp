// ============================================================
} // namespace charlie
        // 【关键: 帧率不在岛内时, 必须真的"消失", 而不是被时间顶替】
        //
        //   原来的判定是 if (showFps && fpsPos==0) ... else if (showClock)。
        //   这导致用户选了"自由/不显示"之后, 因为 else if 命中, 岛右侧
        //   立刻冒出 HH:MM —— 观感上是"帧率被换成了时间", 而不是消失。
        //
        //   现在的语义拆开, 三个档位各自独立:
        //     fpsPos == 0   -> 右侧显示帧率
        //     fpsPos == 1/2 -> 右侧不显示帧率
        //     showClock     -> 右侧显示时间 (独立的开关, 不再当 elseif 兜底)
        //   两者都想显示时, 帧率优先 (它是实时量, 时间可以随时看系统栏)。
        //   都不显示 -> valueText 空串, 岛按内容收缩, 只剩品牌+状态点+状态。
        static char islandNum[32];
        islandNum[0] = 0;
        idata.valueText = islandNum;
        idata.unitText  = "";

        if (F().showFps && F().fpsPos == 0) {
            // 帧率在岛内
            snprintf(islandNum, sizeof(islandNum), "%s", FpsText());
            idata.valueText = islandNum;
            idata.unitText  = "FPS";
        } else if (F().showClock) {
            // 时钟是独立开关, 只有帧率让位时才显示
            time_t nowSec = time(nullptr);
            struct tm tmv;
            localtime_r(&nowSec, &tmv);
            snprintf(islandNum, sizeof(islandNum), "%02d:%02d",
                     tmv.tm_hour, tmv.tm_min);
            idata.valueText = islandNum;
            idata.unitText  = "";
        }
        // 帧率不显示 且 时钟关闭: islandNum 保持空串,
        // 岛按内容收缩, 只剩品牌 + 状态点 + 状态文字, 不留占位。

        // 品牌 + 状态点 + 状态文字, 宽度按内容收缩, 不留占位。

        if (island::Draw(idata)) {
            // 点岛 = 显隐切换, 不是"只能打开"。
            //
            // 旧实现只有 `if (g_state == Hidden) RequestOpen();`,
            // 面板打开后再点岛什么也不会发生, 用户只能去点面板右上角的
            // 关闭按钮。改成调用已有的 shell::Toggle():
            //   Opened  -> RequestClose()  走 0.22s 收起动画
            //   Hidden  -> RequestOpen()
            //   Opening/Closing 两个中间态 Toggle 里不处理, 天然防连点抖动。
            Toggle();
        }
    } else {
        island::SetVisible(false);
        island::Draw(IslandData{});   // 走一次 fade-out，不要硬切
    }

    // ---------- 自由帧率区块 ----------
    // 必须在灵动岛之后画: 它是可拖动的附属信息, 层级要比岛更高,
    // 拖动时才不会被岛盖住。
    // Draw() 内部自己判断 showFps / fpsPos 是否该显示,
    // 以及 fpsFreeDrag 关闭时完全不参与命中 (触摸穿透)。
    freefps::Draw();

    // ---------- 状态机 ----------
    switch (g_state) {
    case WinState::Opening: {
        if (!animOn) {
            g_openProgress = 1.f;
            g_state = WinState::Opened;
        } else {
            g_openProgress = g_openTL.Eval(now, ease::OutQuart);
            if (g_openTL.finished) {
                g_openProgress = 1.f;
                g_state = WinState::Opened;
            }
        }
        break;
    }
    case WinState::Closing: {
        if (!animOn) {
            g_closeProgress = 1.f;
        } else {
            // 闭屏用 InQuart: 起步慢, 末端快速收走
            g_closeProgress = g_closeTL.Eval(now, ease::InQuart);
        }
        if (g_closeProgress >= 1.f) {
            ForceClose();
            SaveConfig();
            DrawToasts();
themeTransition::Draw();
            return true;
        }
        break;
    }
    default:
        break;
    }

    // 主面板是否应该绘制
    const bool mainVisible = (g_state != WinState::Hidden);

    // 主面板已隐藏: 岛和 Toast 画完就收工
    if (!mainVisible) {
        DrawToasts();
themeTransition::Draw();
        return true;
    }

    // ---------- 内容淡入 ----------
    float targetAlpha = (g_state == WinState::Opened) ? 1.f : g_openProgress;
    if (g_state == WinState::Closing) {
        targetAlpha = 1.f - g_closeProgress;
    }
    g_contentAlpha = animOn ? ExpApproach(g_contentAlpha, targetAlpha, 14.f, dt) : targetAlpha;
    if (!animOn) g_contentAlpha = targetAlpha;

    // 灵动岛已在函数开头统一绘制，此处不再重复画。

    // ---------- 窗口几何 ----------
    // 参数对齐参考工程 查理hp源码/jni/src/悬浮窗.cpp:
    //   开屏 0.30s : progress = 1 - (1-t)^4    (OutQuart, 起步猛末端缓)
    //   闭屏 0.22s : progress = 1 - t^4        (InQuart,  起步缓末端猛)
    //   宽度 0 -> 100%
    //   高度 55% -> 100%  (纵向滞后展开, 这是参考工程的关键手感)
    float prog = (g_state == WinState::Closing) ? (1.f - g_closeProgress) : g_openProgress;
    if (!animOn) prog = 1.f;

    const float baseW = m.winW;
    const float baseH = m.winH;
    const float curW  = ImMax(2.f, baseW * prog);
    const float curH  = ImMax(2.f, baseH * (0.55f + 0.45f * prog));

    // 中心 + 拖动偏移
    ImVec2 center(disp.x * 0.5f + g_dragOffset.x, disp.y * 0.5f + g_dragOffset.y);
    ImVec2 winPos(center.x - curW * 0.5f, center.y - curH * 0.5f);

    // ---------- 样式 (按 scale 覆盖基准) ----------
    // 只在度量值变化时写一次。
    // 原来每帧无条件覆盖 8 个字段: ImGuiStyle 是全局对象, 每次赋值
    // 都是 8 次成员写入, 而这些值只在 RebuildMetrics 后才可能变。
    // 用一个缓存指纹判断, 命中就完全跳过。
    {
        const float sc = m.s;
        // 用参与赋值的几个度量拼指纹; 任一变化则整块重写
        const float fingerprint = m.radiusL + m.radius * 7.1f + m.radiusS * 13.3f +
                                  sc * 31.7f + m.knob * 57.1f + m.gap * 91.9f;
        static float s_styleFp = -1.f;
        if (fingerprint != s_styleFp) {
            s_styleFp = fingerprint;
            ImGuiStyle& st = ImGui::GetStyle();
            st.WindowRounding    = m.radiusL;
            st.ChildRounding     = m.radius;
            st.FrameRounding     = m.radiusS;
            st.PopupRounding     = m.radius;
            st.ScrollbarSize     = 5.f * sc;
            st.GrabMinSize       = m.knob;
            st.WindowPadding     = ImVec2(0.f, 0.f);
            st.ItemSpacing       = ImVec2(m.gap * 0.7f, m.gap * 0.55f);
        }
    }

    // ---------- 窗口 ----------
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImClamp(g_contentAlpha, 0.f, 1.f));
    ImGui::SetNextWindowPos(winPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(curW, curH), ImGuiCond_Always);

    ImGui::Begin("##charlie_main", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                 ImGuiWindowFlags_NoSavedSettings);
    {
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();
        ImVec2 ws = ImGui::GetWindowSize();

        // 窗口底 + 描边 + 阴影
        for (int i = 4; i >= 1; --i) {
            float sp = i * 2.4f * m.s;
            dl->AddRectFilled(ImVec2(wp.x - sp, wp.y - sp + 3.f * m.s),
                              ImVec2(wp.x + ws.x + sp, wp.y + ws.y + sp + 3.f * m.s),
                              WithAlpha(p.shadow, 0.10f / i), m.radiusL + sp);
        }
        dl->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), p.bgWin, m.radiusL);
        dl->AddRect(wp, ImVec2(wp.x + ws.x, wp.y + ws.y),
                    WithAlpha(p.border, 0.75f), m.radiusL, 0, 1.2f);

        // 顶部品牌渐变
        dl->AddRectFilledMultiColor(
            ImVec2(wp.x + 1.f, wp.y + 1.f), ImVec2(wp.x + ws.x - 1.f, wp.y + m.titleH * 2.2f),
            WithAlpha(p.accent, 0.07f), WithAlpha(p.accent2, 0.04f),
            IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));

        // ---- 侧栏 / 标题栏 / 内容区 ----
        // 导航栏位置: 0=左 (默认) 1=右
        // 布局用 sideOnRight 分流, 内容区始终占另一侧
        const bool sideOnRight = (g_cfg.navSide == 1);
        const float sideW = ImMin(m.sideW, ws.x * 0.34f);

        ImVec2 sidePos = sideOnRight ? ImVec2(wp.x + ws.x - sideW, wp.y)
                                     : ImVec2(wp.x, wp.y);
        ImVec2 titlePos = sideOnRight ? ImVec2(wp.x, wp.y)
                                      : ImVec2(wp.x + sideW, wp.y);

        DrawSidebar(sidePos, ImVec2(sideW, ws.y), sideOnRight);

        // ---- 标题栏 ----
        const float titleH = m.titleH;
        DrawTitleBar(titlePos, ImVec2(ws.x - sideW, titleH));

        // ---- 内容区 ----
        ImVec2 c0 = sideOnRight ? ImVec2(wp.x, wp.y + titleH)
                                : ImVec2(wp.x + sideW, wp.y + titleH);
        ImVec2 c1 = sideOnRight ? ImVec2(wp.x + ws.x - sideW, wp.y + ws.y)
                                : ImVec2(wp.x + ws.x, wp.y + ws.y);
        ImVec2 csize(c1.x - c0.x, c1.y - c0.y);

        ImGui::SetCursorScreenPos(ImVec2(c0.x + m.padX, c0.y + m.padY));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ToVec4(p.bgContent));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, m.radius);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(m.padX, m.padY));

        // 内容区必须可滚动: 小屏上控件会超出可视区
        ImGui::BeginChild("##content", ImVec2(csize.x - m.padX * 2.f, csize.y - m.padY * 1.4f),
                          false, ImGuiWindowFlags_None);

        // 切页时回到顶部
        if (g_pageResetScroll) {
            ImGui::SetScrollY(0.f);
            g_pageResetScroll = false;
        }

        // ============================================================
        // 触摸拖拽滚动
        //
        // 【旧实现为什么"不跟手"】
        //   1. 启动阈值 6*s (横屏 s=1.55 时接近 10px) 太大 ——
        //      手指先划了 10px 页面纹丝不动, 第一感觉就是"拖不动"。
        //   2. 越过阈值那一帧只加了当帧 MouseDelta,
        //      阈值内已经划掉的那段位移被直接丢弃 -> 画面"嗒"地跳一下。
        //   3. 抬手立刻停止, 完全没有惯性 -> 手感很硬。
        //   4. 旧判据 !IsAnyItemActive() 在触摸下恒为假(卡片里全是
        //      InvisibleButton), 等于整段逻辑从来没跑过。
        //
        // 【现在的做法】
        //   - 阈值降到 3px, 越过即锁存 s_dragging, 并把起步位移补回来
        //   - 拖动期间按 MouseDelta 逐帧跟手
        //   - 抬手按速度做指数衰减惯性, 自然滑停
        //   - 只接管"活动项属于当前滚动窗口"的情况, 不去抢 popup 里的拖动
        // ============================================================
        {
            ImGuiWindow* cw  = ImGui::GetCurrentWindow();
            ImGuiIO&     io  = ImGui::GetIO();
            ImGuiContext& g  = *ImGui::GetCurrentContext();

            const float maxScroll = ImMax(0.f, cw->ContentSize.y - cw->InnerRect.GetHeight());

            // ============================================================
            // 手势状态
            //
            // 【s_armed 是本次修复的核心】
            //   旧实现只在"在内容区内按下"时更新 s_startPos, 但后续
            //   `if (down)` 对整个按下周期都生效, 完全不检查按下位置。
            //   于是出现两个症状:
            //     a) 手指落在悬浮窗外按下 -> s_startPos 还是上一次的旧坐标,
            //        算出的位移必然巨大, 一按就越过阈值, 内容立刻滚走。
            //     b) 手指落在窗口内但按在按钮上 -> 按钮吞掉点击的同时,
            //        滚动逻辑也读同一个 MouseDelta, 页面跟着动。
            //   修法: 增加 s_armed, 只在"按下边沿且落点在内容区内"置位,
            //   抬起或按下在区外时一律为 false, 全程不参与滚动。
            // ============================================================
            static bool   s_dragging = false;
            static bool   s_armed    = false;   // 本次按下是否起于内容区内
            static ImVec2 s_startPos(0.f, 0.f);
            static float  s_velY     = 0.f;     // 惯性速度, px/s

            const bool  down = ImGui::IsMouseDown(ImGuiMouseButton_Left);

            // 起判阈值。
            // 【为什么从 3px 提到 8*s px】
            //   3px 是鼠标量级的值。手指按下时接触面积大、系统还有
            //   抖动补偿前的原始位移, 3px 会被正常点击轻易越过 ->
            //   "点一下按钮, 内容滑了一下"。8*s (横屏约 12px) 才稳。
            const float kSlop = 8.f * m.s;

            // 内容区矩形用窗口自身的 Pos/Size, 不用 InnerRect:
            // InnerRect 会随滚动偏移变化, 用它判断落点会在滚动后失真。
            const ImVec2 wpos = cw->Pos;
            const ImVec2 wsz  = cw->Size;
            const bool inContent =
                (io.MousePos.x >= wpos.x) && (io.MousePos.x <= wpos.x + wsz.x) &&
                (io.MousePos.y >= wpos.y) && (io.MousePos.y <= wpos.y + wsz.y);

            // 按下边沿: 只有落点在内容区内才装填手势
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                s_dragging = false;
                s_velY     = 0.f;
                s_armed    = inContent;
                s_startPos = io.MousePos;
            }

            // 落点不在内容区(含窗口外): 整段手势作废
            if (!inContent && s_dragging) {
                // 已经拖到窗口外时保持拖动 (手指划出边界是常见操作),
                // 这里只在"尚未开始拖动"阶段做拦截, 见下面 if 条件
            }

            if (down && s_armed) {
                if (!s_dragging) {
                    const ImVec2 d(io.MousePos.x - s_startPos.x,
                                   io.MousePos.y - s_startPos.y);
                    // 越过阈值才认定为拖动
                    if (d.x * d.x + d.y * d.y > kSlop * kSlop) {
                        s_dragging = true;
                        // 释放当前 item, 否则 ImGui 继续把移动喂给
                        // 那个 InvisibleButton, 页面不会动。
                        // 只在活动项属于本窗口时才清, 避免误伤 popup。
                        if (g.ActiveId != 0 && !g.ActiveIdIsJustActivated &&
                            g.ActiveIdWindow == cw) {
                            ImGui::ClearActiveID();
                        }
                        // 把阈值内已经划掉的位移补回来, 消除起步跳变
                        cw->Scroll.y -= d.y;
                        s_startPos = io.MousePos;
                        s_velY     = 0.f;
                    }
                } else {
                    cw->Scroll.y -= io.MouseDelta.y;
                    // 记录瞬时速度, 供抬手后做惯性
                    if (io.DeltaTime > 0.0001f) {
                        s_velY = io.MouseDelta.y / io.DeltaTime;
                    }
                }

                // 拖动/补位移后都要钳制
                if (cw->Scroll.y < 0.f)       cw->Scroll.y = 0.f;
                if (cw->Scroll.y > maxScroll) cw->Scroll.y = maxScroll;
            } else if (!down) {
                // 抬起: 释放武装状态, 此后任何 MouseDelta 都不再影响滚动
                s_armed    = false;
                s_dragging = false;
                // 惯性滑动: 指数衰减, 约 1 秒滑停
                if (ImFabs(s_velY) > 2.f) {
                    cw->Scroll.y -= s_velY * io.DeltaTime;
                    // 每帧衰减 ~5%(= 每秒 0.05 的幂), 手感接近系统滚动
                    s_velY *= ImPow(0.05f, io.DeltaTime);
                    if (ImFabs(s_velY) < 2.f) s_velY = 0.f;

                    if (cw->Scroll.y < 0.f)       cw->Scroll.y = 0.f;
                    if (cw->Scroll.y > maxScroll) cw->Scroll.y = maxScroll;
                } else {
                    s_velY = 0.f;
                }
            }
        }

        // 内容区背景 (自己画, 避免 ChildBg 溢出圆角)
        {
            ImVec2 cp = ImGui::GetWindowPos();
            ImVec2 cs = ImGui::GetWindowSize();
            dl->AddRectFilled(cp, ImVec2(cp.x + cs.x, cp.y + cs.y),
                              WithAlpha(p.bgContent, 0.55f), m.radius);
        }

        // 页面内容 (切页淡入)
        g_pageFade = animOn ? ExpApproach(g_pageFade, 1.f, 16.f, dt) : 1.f;
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
                            ImClamp(g_contentAlpha * g_pageFade, 0.f, 1.f));


        DrawPage(g_currentPage);
        ImGui::PopStyleVar();

        // 底部留白, 保证最后一格不贴边
        ImGui::Dummy(ImVec2(1.f, m.padY * 0.6f));

        // ---------- 窗口拖动 (抄 查理hp源码/jni/src/悬浮窗.cpp:3072-3099) ----------
        // 参考实现的条件是 "动画完成 且 免责声明已关", 这里对应
        // prog >= 0.999f (开屏动画收尾) 与 g_disclaimerAgreed。
        // 只在"窗口空白处"按下才起拖: IsAnyItemHovered() 为 true 说明
        // 按在按钮/滑块上, 必须让控件先吃掉这次点击。
        if (prog >= 0.999f && g_disclaimerAgreed) {
            // 【为什么改成"只在标题栏拖"】
            // 原来用 IsWindowHovered(RootAndChildWindows) + !IsAnyItemHovered():
            // 内容区的行都是 InvisibleButton, 手指按在行与行之间的缝隙时
            // IsAnyItemHovered() 可能为 false, 于是这次按下被判成"拖面板",
            // 面板跟着手指走 —— 用户想上下滑内容, 结果整个框在移动。
            //
            // 现在把拖动热区收敛到标题栏(顶部 titleH 内), 内容区一律
            // 交给滚动逻辑。这是移动端悬浮窗的通用做法: 标题栏拖框,
            // 内容区滑内容, 两者不会互相抢。
            const ImVec2 mpNow = ImGui::GetIO().MousePos;
            const bool inTitleBar =
                (mpNow.y >= wp.y) && (mpNow.y <= wp.y + m.titleH) &&
                (mpNow.x >= wp.x) && (mpNow.x <= wp.x + ws.x);

            if (inTitleBar && !ImGui::IsAnyItemHovered() &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                g_dragging       = true;
                g_dragStartMouse = mpNow;
                g_dragStartOffset = g_dragOffset;
            }
            if (g_dragging) {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    g_dragOffset.x = g_dragStartOffset.x + (mpNow.x - g_dragStartMouse.x);
                    g_dragOffset.y = g_dragStartOffset.y + (mpNow.y - g_dragStartMouse.y);
                } else {
                    // 松手: 结束拖动并落盘 (参考实现 SaveUiPosition)
                    g_dragging = false;
                    SaveConfig();
                }
            }
            // 限制窗口不超出屏幕 (参考实现 L3090-3096)
            // 以中心为基准, 保证整个窗口始终有交集落在可视区内
            const float minOffX = -disp.x * 0.5f + curW * 0.5f;
            const float maxOffX =  disp.x * 0.5f - curW * 0.5f;
            const float minOffY = -disp.y * 0.5f + curH * 0.5f;
            const float maxOffY =  disp.y * 0.5f - curH * 0.5f;
            // min > max 时 (窗口比屏幕还大) 居中, 避免 ImClamp 行为未定义
            g_dragOffset.x = (minOffX <= maxOffX)
                             ? ImClamp(g_dragOffset.x, minOffX, maxOffX)
                             : 0.f;
            g_dragOffset.y = (minOffY <= maxOffY)
                             ? ImClamp(g_dragOffset.y, minOffY, maxOffY)
                             : 0.f;
        } else {
            g_dragging = false;
        }

        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();

        // ---------- 免责弹窗 ----------
        //
        // 【为什么必须在 End() 之前调用】
        // 原来这个调用写在 ImGui::End() 之后。End() 之后当前窗口栈为空,
        // ImGui 会把后续所有调用落到它内部自动创建的
        // "Debug##Default" 隐式 fallback 窗口(imgui.cpp:5343)里。
        // 那个窗口只要被塞进任何内容就会以 400x400 渲染出来 ——
        // 这就是用户看到的"除主悬浮窗外还有一个标着 debug 的小悬浮窗"。
        //
        // 弹窗靠 OpenPopupEx 进 g.OpenPopupStack, 层级由 popup 栈决定,
        // 与它是从哪个窗口发起的无关, 放在主窗口内部不会影响它盖在最上层。

        // ---------- 明暗过渡遮罩 ----------
        //
        // 【为什么画在这里】
        //   前一版把调用放在 ImGui::End() 之后, 那时窗口矩形已经拿不到,
        //   函数内部用 GetMainViewport()->Size 兜底, 于是整屏被盖了一层
        //   黑/白蒙版 —— 用户看到的是"手机闪了一下"而不是"面板换了配色"。
        //
        //   明暗切换是悬浮窗自身的属性, 遮罩必须只落在窗口矩形内。
        //   这里在窗口块内调用, 直接传 wp/ws 与窗口圆角 m.radiusL。
        //   窗口隐藏时根本走不到这里 (上面 mainVisible 早返回),
        //   也就不会出现"没有面板却在闪"的情况。
        themeTransition::Draw(wp, ws, m.radiusL);

        DrawDisclaimer(disp);
    }
    ImGui::End();
    ImGui::PopStyleVar();

    // ---------- 弹窗关闭后恢复主窗口 hover ----------
    // 【为什么需要这段】
    //   modal 弹窗存在的那些帧, ImGui 会把 g.HoveredWindow 指向弹窗,
    //   并且 modal 期间其它窗口的 hover 全部被屏蔽 —— 这是设计如此。
    //
    //   问题在于关掉 modal 之后: ClosePopupToLevel 会尝试 FocusWindow
    //   恢复焦点, 但它恢复的是 prev_popup.RestoreNavWindow, 而我们的
    //   主窗口用了 NoBringToFrontOnFocus + NoSavedSettings, 并且是在
    //   End() 之后、Debug##Default 隐式窗口上下文里创建弹窗的。
    //   结果是 HoveredWindow 恢复不彻底, 接下来的帧里
    //   imgui.cpp:4593 `if (g.HoveredWindow != window) return false;`
    //   持续否决主面板所有控件的 hover ——
    //   Toggle / Combo / Button 全部"画得出来但点不动"。
    //
    //   修法: 检测到"弹窗刚关闭"这个边沿, 主动 FocusWindow 主窗口,
    //   把 HoveredWindow 与 NavWindow 明确拉回来。
    //
    //   只在边沿触发一次, 不是每帧调用 —— 每帧 FocusWindow 会让
    //   主窗口永远抢焦点, 反而破坏弹窗与下拉菜单的层级。
    {
        static bool s_prevPopupOpen = false;
        const bool popupOpenNow = (ImGui::GetCurrentContext()->OpenPopupStack.Size > 0);

        if (s_prevPopupOpen && !popupOpenNow) {
            // 弹窗刚刚关闭: 把焦点与 hover 拉回主面板
            if (ImGuiWindow* mw = ImGui::FindWindowByName("##charlie_main")) {
                ImGui::FocusWindow(mw);
            }
        }
        s_prevPopupOpen = popupOpenNow;
    }

    // ---------- Toast ----------
    DrawToasts();
    // 明暗过渡遮罩已经在主窗口块内按窗口矩形画过了 (见上面 themeTransition::Draw(wp, ws, ...)),
    // 这里不再重复调用 —— 无参版本是空实现, 留着只会误导后来者以为这里还在画东西。

    return true;
}

} // namespace shell
} // namespace charlie
