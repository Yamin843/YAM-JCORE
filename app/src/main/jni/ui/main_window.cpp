// ui/main_window.cpp
#include "ui/main_window.h"

#include <android/log.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"  // for ImGuiWindowFlags_NoBringToFrontOnFocus

#include "ui/notification.h"
#include "ui/renderer.h"

#define LOG_TAG "YAMGG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)

namespace yamgg {

namespace {

// Layout constants. Values are in pixels (ImGui space).
constexpr float kInitialWidthFraction  = 0.92f;
constexpr float kInitialHeightFraction = 0.86f;
constexpr float kMinWidthPx            = 360.0f;
constexpr float kMinHeightPx           = 280.0f;
constexpr float kSidebarWidth          = 84.0f;
constexpr float kTabButtonHeight       = 64.0f;
constexpr float kEdgeZone              = 44.0f;   // custom resize zone
constexpr float kCornerZone            = 100.0f;  // bottom-right corner
constexpr float kMinButtonSize         = 34.0f;
constexpr float kMinimizedSize         = 72.0f;
constexpr float kDragStartThresholdPx  = 10.0f;

// Title-bar height is cached here every frame so is_on_title_bar() can
// be called from a non-UI thread without touching ImGui.
std::atomic<int> g_title_bar_height_px{66};

} // namespace

// ═══════════════════════════════════════════════════════════════
//  Impl
// ═══════════════════════════════════════════════════════════════

struct MainWindow::Impl {
    bool        visible       = true;
    bool        fullscreen    = false;
    bool        minimized     = false;
    bool        first_draw    = true;
    bool        tabs_checked  = false;

    ImVec2      last_pos       = ImVec2(0, 0);
    ImVec2      last_size      = ImVec2(0, 0);
    ImVec2      initial_size   = ImVec2(0, 0);
    bool        initial_size_set = false;
    ImVec2      minimized_pos  = ImVec2(60, 100);

    // Drag state.
    bool        drag_pending    = false;
    bool        dragging_window = false;
    ImVec2      drag_start_mouse = ImVec2(0, 0);
    ImVec2      drag_start_window = ImVec2(0, 0);

    // Current tab.
    int         active_tab = -1;
    Tab*        prev_active = nullptr;

    // Cached rect, guarded by rect_mu.
    mutable std::mutex  rect_mu;
    ImVec2              cached_pos  = ImVec2(0, 0);
    ImVec2              cached_size = ImVec2(0, 0);
    bool                cached_valid = false;

    // Resize state, one active edge at a time.
    enum class ResizeEdge { None, Left, Right, Top, Bottom, BottomRight };
    ResizeEdge  resize_edge = ResizeEdge::None;
};

MainWindow& MainWindow::instance() {
    static MainWindow m;
    return m;
}

MainWindow::MainWindow() : impl_(new Impl()) {}
MainWindow::~MainWindow() {
    delete impl_;
    impl_ = nullptr;
}

// ═══════════════════════════════════════════════════════════════
//  Public API
// ═══════════════════════════════════════════════════════════════

void MainWindow::draw() {
    // Register default tabs once, on the first frame, when the registry
    // is still empty. Reply 6 implements register_all_default_tabs().
    if (!impl_->tabs_checked) {
        impl_->tabs_checked = true;
        if (TabRegistry::instance().count() == 0) {
            register_all_default_tabs();
        }
        auto snap = TabRegistry::instance().snapshot();
        if (!snap.empty()) {
            impl_->active_tab = 0;
            snap[0]->on_activated();
        }
    }

    Notification::instance().draw();

    if (!impl_->visible) {
        std::lock_guard<std::mutex> lk(impl_->rect_mu);
        impl_->cached_valid = false;
        return;
    }

    if (impl_->minimized) {
        draw_minimized();
        return;
    }

    draw_main_window();
}

bool MainWindow::hit_test(float x, float y) const {
    std::lock_guard<std::mutex> lk(impl_->rect_mu);
    if (!impl_->cached_valid) return false;
    if (impl_->cached_size.x <= 0.0f || impl_->cached_size.y <= 0.0f) return false;
    return x >= impl_->cached_pos.x
        && x <= impl_->cached_pos.x + impl_->cached_size.x
        && y >= impl_->cached_pos.y
        && y <= impl_->cached_pos.y + impl_->cached_size.y;
}

bool MainWindow::is_on_title_bar(float y) const {
    std::lock_guard<std::mutex> lk(impl_->rect_mu);
    if (!impl_->cached_valid) return false;
    int tbh = g_title_bar_height_px.load(std::memory_order_relaxed);
    return y >= impl_->cached_pos.y
        && y <= impl_->cached_pos.y + (float)tbh;
}

bool MainWindow::get_rect(float& x, float& y, float& w, float& h) const {
    std::lock_guard<std::mutex> lk(impl_->rect_mu);
    if (!impl_->cached_valid) return false;
    if (impl_->cached_size.x <= 0.0f || impl_->cached_size.y <= 0.0f) return false;
    x = impl_->cached_pos.x;
    y = impl_->cached_pos.y;
    w = impl_->cached_size.x;
    h = impl_->cached_size.y;
    return true;
}

void MainWindow::invalidate_rect() {
    std::lock_guard<std::mutex> lk(impl_->rect_mu);
    impl_->cached_valid = false;
}

void MainWindow::set_visible(bool visible) {
    impl_->visible = visible;
}

bool MainWindow::is_visible() const {
    return impl_->visible;
}

void MainWindow::set_fullscreen(bool fullscreen) {
    impl_->fullscreen = fullscreen;
    if (fullscreen) impl_->minimized = false;
    impl_->first_draw = false;  // force re-apply position next frame
}

bool MainWindow::is_fullscreen() const {
    return impl_->fullscreen;
}

void MainWindow::register_tab(std::shared_ptr<Tab> tab) {
    TabRegistry::instance().register_tab(std::move(tab));
}

void MainWindow::select_tab(size_t index) {
    size_t n = TabRegistry::instance().count();
    if (index >= n) return;
    int new_tab = (int)index;
    if (impl_->active_tab == new_tab) return;

    // Deactivate previous, activate new.
    if (impl_->active_tab >= 0) {
        Tab* prev = TabRegistry::instance().get((size_t)impl_->active_tab);
        if (prev) prev->on_deactivated();
    }
    impl_->active_tab = new_tab;
    Tab* next = TabRegistry::instance().get((size_t)new_tab);
    if (next) next->on_activated();
}

int MainWindow::current_tab_index() const {
    return impl_->active_tab;
}

// ═══════════════════════════════════════════════════════════════
//  Drawing
// ═══════════════════════════════════════════════════════════════

void MainWindow::draw_main_window() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x <= 1.0f || io.DisplaySize.y <= 1.0f) return;

    // Cache title-bar height for is_on_title_bar().
    int tbh = (int)(ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f);
    g_title_bar_height_px.store(tbh, std::memory_order_relaxed);

    // ── Compute the initial size on the very first frame ──
    if (!impl_->initial_size_set) {
        impl_->initial_size = ImVec2(io.DisplaySize.x * kInitialWidthFraction,
                                     io.DisplaySize.y * kInitialHeightFraction);
        impl_->initial_size_set = true;
    }

    // ── Window flags ──
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (impl_->fullscreen) {
        flags |= ImGuiWindowFlags_NoResize;
        flags |= ImGuiWindowFlags_NoMove;
    }

    // ── Size constraints ──
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(kMinWidthPx, kMinHeightPx),
        ImVec2(io.DisplaySize.x, io.DisplaySize.y));

    // ── Initial position / size ──
    if (impl_->first_draw) {
        impl_->first_draw = false;
        ImGui::SetNextWindowPos(
            ImVec2((io.DisplaySize.x - impl_->initial_size.x) * 0.5f,
                   (io.DisplaySize.y - impl_->initial_size.y) * 0.5f),
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(impl_->initial_size, ImGuiCond_Always);
    }

    // ── Fullscreen toggle re-applies on the next frame ──
    if (impl_->fullscreen) {
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
    }

    // ── Begin window ──
    bool opened = ImGui::Begin("YAM-JCORE", nullptr, flags);
    if (!opened) {
        ImGui::End();
        return;
    }

    draw_title_bar_min_button();

    // ── Sidebar ──
    ImGui::BeginChild("##sidebar", ImVec2(kSidebarWidth, 0), false);
    {
        auto tabs = TabRegistry::instance().snapshot();
        draw_sidebar(tabs);
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // ── Content area ──
    ImGui::BeginChild("##content", ImVec2(0, 0), false);
    {
        auto tabs = TabRegistry::instance().snapshot();
        draw_content(tabs);
    }
    ImGui::EndChild();

    // ── Custom drag + resize ──
    if (!impl_->fullscreen) {
        handle_drag_and_resize();
    }

    // ── Update the cached rect for the next hit_test() call ──
    update_cached_rect();

    ImGui::End();
}

void MainWindow::draw_sidebar(const std::vector<Tab*>& tabs) {
    ImGui::Dummy(ImVec2(0, 4));

    int active = impl_->active_tab;
    for (size_t i = 0; i < tabs.size(); i++) {
        Tab* tab = tabs[i];
        if (!tab) continue;

        bool selected = (active == (int)i);

        ImGui::PushID((int)i);

        // Button colors depend on selection state.
        ImVec4 bg, hov, act, txt;
        if (selected) {
            bg  = ImVec4(0.18f, 0.14f, 0.04f, 1.0f);
            hov = bg;
            act = bg;
            txt = ImVec4(1.00f, 0.83f, 0.30f, 1.0f);
        } else {
            bg  = ImVec4(0.08f, 0.08f, 0.09f, 1.0f);
            hov = ImVec4(0.14f, 0.12f, 0.06f, 1.0f);
            act = ImVec4(0.20f, 0.16f, 0.05f, 1.0f);
            txt = ImVec4(0.72f, 0.72f, 0.74f, 1.0f);
        }

        ImGui::PushStyleColor(ImGuiCol_Button,        bg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hov);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  act);
        ImGui::PushStyleColor(ImGuiCol_Text,          txt);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, kTabButtonHeight * 0.24f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));

        const char* label = tab->name();
        ImGui::Button(label, ImVec2(kSidebarWidth - 16.0f, kTabButtonHeight));

        if (ImGui::IsItemClicked()) {
            select_tab(i);
        }

        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(4);
        ImGui::PopID();

        ImGui::Dummy(ImVec2(0, 6));
    }
}

void MainWindow::draw_content(const std::vector<Tab*>& tabs) {
    if (tabs.empty()) {
        ImGui::TextDisabled("No tabs registered.");
        return;
    }
    int idx = impl_->active_tab;
    if (idx < 0 || idx >= (int)tabs.size()) {
        ImGui::TextDisabled("Invalid tab selection.");
        return;
    }
    Tab* tab = tabs[(size_t)idx];
    if (!tab) {
        ImGui::TextDisabled("Tab is null.");
        return;
    }
    try {
        tab->draw();
    } catch (const std::exception& e) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                           "Tab threw: %s", e.what());
    }
}

void MainWindow::draw_title_bar_min_button() {
    ImVec2 wpos  = ImGui::GetWindowPos();
    ImVec2 wsize = ImGui::GetWindowSize();
    int    tbh   = g_title_bar_height_px.load(std::memory_order_relaxed);

    ImVec2 saved_cursor = ImGui::GetCursorPos();

    ImVec2 btn_pos(
        wpos.x + wsize.x - kMinButtonSize - 6.0f,
        wpos.y + ((float)tbh - kMinButtonSize) * 0.5f);

    ImGui::SetCursorScreenPos(btn_pos);
    ImGui::PushID("##min_button");
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, kMinButtonSize * 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.18f, 0.22f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.28f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.85f, 0.65f, 0.00f, 1.0f));

    if (ImGui::Button("##min", ImVec2(kMinButtonSize, kMinButtonSize))) {
        impl_->last_pos    = wpos;
        impl_->last_size   = wsize;
        impl_->minimized   = true;
    }

    ImVec2 bMin = ImGui::GetItemRectMin();
    ImVec2 bMax = ImGui::GetItemRectMax();
    ImVec2 ctr((bMin.x + bMax.x) * 0.5f, (bMin.y + bMax.y) * 0.5f);
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(ctr.x - 8.0f, ctr.y),
        ImVec2(ctr.x + 8.0f, ctr.y),
        IM_COL32(255, 220, 80, 255), 2.5f);

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
    ImGui::PopID();

    ImGui::SetCursorPos(saved_cursor);
}

// ═══════════════════════════════════════════════════════════════
//  Drag and resize
// ═══════════════════════════════════════════════════════════════

void MainWindow::handle_drag_and_resize() {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 wpos  = ImGui::GetWindowPos();
    ImVec2 wsize = ImGui::GetWindowSize();
    int    tbh   = g_title_bar_height_px.load(std::memory_order_relaxed);

    // ── Resize edges ──
    // Each edge is an invisible button covering a strip. When clicked
    // and dragged, we resize the window along that axis.
    auto edge_at = [&](float x, float y, float w, float h) -> bool {
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        ImGui::InvisibleButton("##edge", ImVec2(w, h),
                               ImGuiButtonFlags_MouseButtonLeft);
        return ImGui::IsItemActive();
    };

    // Left edge
    if (edge_at(wpos.x, wpos.y + kEdgeZone,
                kEdgeZone, wsize.y - 2 * kEdgeZone)) {
        float nw = wsize.x - io.MouseDelta.x;
        if (nw < kMinWidthPx) nw = kMinWidthPx;
        if (nw > io.DisplaySize.x) nw = io.DisplaySize.x;
        ImGui::SetWindowSize(ImVec2(nw, wsize.y));
    }

    // Right edge
    if (edge_at(wpos.x + wsize.x - kEdgeZone, wpos.y + kEdgeZone,
                kEdgeZone, wsize.y - 2 * kEdgeZone)) {
        float nw = wsize.x + io.MouseDelta.x;
        if (nw < kMinWidthPx) nw = kMinWidthPx;
        if (nw > io.DisplaySize.x) nw = io.DisplaySize.x;
        ImGui::SetWindowSize(ImVec2(nw, wsize.y));
    }

    // Top edge
    if (edge_at(wpos.x + kEdgeZone, wpos.y,
                wsize.x - 2 * kEdgeZone, kEdgeZone)) {
        float nh = wsize.y - io.MouseDelta.y;
        if (nh < kMinHeightPx) nh = kMinHeightPx;
        if (nh > io.DisplaySize.y) nh = io.DisplaySize.y;
        ImGui::SetWindowSize(ImVec2(wsize.x, nh));
    }

    // Bottom edge
    if (edge_at(wpos.x + kEdgeZone, wpos.y + wsize.y - kEdgeZone,
                wsize.x - 2 * kEdgeZone, kEdgeZone)) {
        float nh = wsize.y + io.MouseDelta.y;
        if (nh < kMinHeightPx) nh = kMinHeightPx;
        if (nh > io.DisplaySize.y) nh = io.DisplaySize.y;
        ImGui::SetWindowSize(ImVec2(wsize.x, nh));
    }

    // Bottom-right corner (both axes)
    if (edge_at(wpos.x + wsize.x - kCornerZone,
                wpos.y + wsize.y - kCornerZone,
                kCornerZone, kCornerZone)) {
        float nw = wsize.x + io.MouseDelta.x;
        float nh = wsize.y + io.MouseDelta.y;
        if (nw < kMinWidthPx)  nw = kMinWidthPx;
        if (nh < kMinHeightPx) nh = kMinHeightPx;
        if (nw > io.DisplaySize.x) nw = io.DisplaySize.x;
        if (nh > io.DisplaySize.y) nh = io.DisplaySize.y;
        ImGui::SetWindowSize(ImVec2(nw, nh));
    }

    // ── Drag from title bar ──
    bool hovered = ImGui::IsWindowHovered(
        ImGuiHoveredFlags_ChildWindows
        | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    bool on_title = io.MousePos.y >= wpos.y
                 && io.MousePos.y <= wpos.y + (float)tbh;

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)
        && hovered && on_title
        && !ImGui::IsAnyItemActive()
        && !impl_->dragging_window) {
        impl_->drag_pending        = true;
        impl_->drag_start_mouse    = io.MousePos;
        impl_->drag_start_window   = wpos;
    }

    if (impl_->drag_pending) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            impl_->drag_pending = false;
        } else {
            float dx = io.MousePos.x - impl_->drag_start_mouse.x;
            float dy = io.MousePos.y - impl_->drag_start_mouse.y;
            if (dx * dx + dy * dy > kDragStartThresholdPx * kDragStartThresholdPx) {
                impl_->dragging_window = true;
                impl_->drag_pending    = false;
            }
        }
    }

    if (impl_->dragging_window) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            ImVec2 d(io.MousePos.x - impl_->drag_start_mouse.x,
                     io.MousePos.y - impl_->drag_start_mouse.y);
            ImGui::SetWindowPos(ImVec2(impl_->drag_start_window.x + d.x,
                                       impl_->drag_start_window.y + d.y));
        } else {
            impl_->dragging_window = false;
        }
    }
}

void MainWindow::update_cached_rect() {
    std::lock_guard<std::mutex> lk(impl_->rect_mu);
    impl_->cached_pos    = ImGui::GetWindowPos();
    impl_->cached_size   = ImGui::GetWindowSize();
    impl_->cached_valid  = (impl_->cached_size.x > 0.0f && impl_->cached_size.y > 0.0f);
}

// ═══════════════════════════════════════════════════════════════
//  Minimized mode
// ═══════════════════════════════════════════════════════════════

void MainWindow::draw_minimized() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x <= 1.0f || io.DisplaySize.y <= 1.0f) return;

    // Clamp within the viewport.
    if (impl_->minimized_pos.x + kMinimizedSize > io.DisplaySize.x)
        impl_->minimized_pos.x = io.DisplaySize.x - kMinimizedSize - 8;
    if (impl_->minimized_pos.y + kMinimizedSize > io.DisplaySize.y)
        impl_->minimized_pos.y = io.DisplaySize.y - kMinimizedSize - 8;
    if (impl_->minimized_pos.x < 0) impl_->minimized_pos.x = 0;
    if (impl_->minimized_pos.y < 0) impl_->minimized_pos.y = 0;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::SetNextWindowPos(impl_->minimized_pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kMinimizedSize, kMinimizedSize), ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, kMinimizedSize * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.02f, 0.02f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(0.85f, 0.65f, 0.00f, 1.0f));

    ImGui::Begin("##yamgg_minimized", nullptr, flags);

    {
        std::lock_guard<std::mutex> lk(impl_->rect_mu);
        impl_->cached_pos   = impl_->minimized_pos;
        impl_->cached_size  = ImVec2(kMinimizedSize, kMinimizedSize);
        impl_->cached_valid = true;
    }

    ImVec2 cur = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##icon", ImVec2(kMinimizedSize, kMinimizedSize));
    bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    bool active  = ImGui::IsItemActive();

    ImVec2 ctr(cur.x + kMinimizedSize * 0.5f, cur.y + kMinimizedSize * 0.5f);
    ImVec2 text_sz = ImGui::CalcTextSize("YG");
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(ctr.x - text_sz.x * 0.5f, ctr.y - text_sz.y * 0.5f),
        IM_COL32(255, 215, 0, 255), "YG");

    static ImVec2 s_press_start(0, 0);
    static bool   s_pressed = false;

    if (clicked) {
        s_press_start = io.MousePos;
        s_pressed = true;
    }

    if (active) {
        impl_->minimized_pos.x += io.MouseDelta.x;
        impl_->minimized_pos.y += io.MouseDelta.y;
    }

    // Detect tap vs drag on release.
    if (s_pressed && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        s_pressed = false;
        float dx = io.MousePos.x - s_press_start.x;
        float dy = io.MousePos.y - s_press_start.y;
        if (dx * dx + dy * dy < 25.0f) {  // < 5 px
            impl_->minimized = false;
            if (impl_->last_size.x > 100.0f && impl_->last_size.y > 100.0f) {
                ImGui::SetNextWindowPos(impl_->last_pos, ImGuiCond_Always);
                ImGui::SetNextWindowSize(impl_->last_size, ImGuiCond_Always);
            }
        }
    }

    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

} // namespace yamgg
