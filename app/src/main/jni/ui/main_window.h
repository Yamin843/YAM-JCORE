// ui/main_window.h — top-level window with sidebar and content area.
//
// The window is the only ImGui window that is always present. Tabs are
// drawn in the content area. Sidebar shows one button per registered
// tab. The window supports:
//
//   * Drag from title bar (top strip, height = current font size).
//   * Custom 5-zone resize: left, right, top, bottom, bottom-right corner.
//   * Minimize-to-circle; tap to restore.
//   * Fullscreen toggle (used by the settings tab).
//
// Thread model: every method here is UI-thread only. The renderer holds
// its recursive mutex while calling draw().
#ifndef YAMGG_UI_MAIN_WINDOW_H
#define YAMGG_UI_MAIN_WINDOW_H

#include <atomic>
#include <mutex>
#include <string>

#include "ui/tab.h"

namespace yamgg {

class MainWindow {
public:
    static MainWindow& instance();

    // Draw the window for the current frame. Called once per frame from
    // Renderer::on_draw_frame, with the renderer's mutex held.
    void draw();

    // Thread-safe hit test using the rectangle cached by the last draw.
    // x, y are in screen coordinates (pixels).
    bool hit_test(float x, float y) const;

    // Thread-safe test for the title-bar strip. Used by ModView to
    // decide whether the touch starts a window drag.
    bool is_on_title_bar(float y) const;

    // Thread-safe cached rectangle accessor. Returns false if no frame
    // has been drawn yet.
    bool get_rect(float& x, float& y, float& w, float& h) const;

    // Invalidate the cached rectangle (e.g. after a fullscreen toggle
    // that moves the window off-screen).
    void invalidate_rect();

    // Show the window (default) or hide it entirely.
    void set_visible(bool visible);
    bool is_visible() const;

    // Toggle fullscreen. When fullscreen, the window fills the entire
    // viewport and drag/resize are disabled.
    void set_fullscreen(bool fullscreen);
    bool is_fullscreen() const;

    // Register a tab. Ownership transferred. Usually called once at
    // startup by register_all_default_tabs() (Reply 6).
    void register_tab(std::shared_ptr<Tab> tab);

    // Select the tab at the given index (0-based). Out-of-range indices
    // are ignored. Thread-safe.
    void select_tab(size_t index);

    // Index of the currently selected tab. -1 if no tabs are registered.
    int current_tab_index() const;

private:
    MainWindow();
    ~MainWindow() = default;

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    void draw_main_window();
    void draw_minimized();
    void draw_sidebar(const std::vector<Tab*>& tabs);
    void draw_content(const std::vector<Tab*>& tabs);
    void draw_title_bar_min_button();
    void handle_drag_and_resize();
    void update_cached_rect();

    struct Impl;
    Impl* impl_;
};

// Implemented in ui/tabs/register_all.cpp (Reply 6). Called once by
// MainWindow::draw() before the first frame is drawn, only when the
// tab registry is empty. Reply 6 adds the real implementation and the
// one-line call from MainWindow::draw.
void register_all_default_tabs();

} // namespace yamgg

#endif // YAMGG_UI_MAIN_WINDOW_H
