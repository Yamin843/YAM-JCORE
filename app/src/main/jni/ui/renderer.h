// ui/renderer.h — OpenGL ES 3.0 + ImGui backend + input handling.
//
// Renderer owns:
//   * the ImGui context,
//   * the OpenGL ES 3 backend,
//   * the input queue (touch, key, char, scroll),
//   * the frame timing.
//
// Threading model:
//   The Android GLSurfaceView calls onSurfaceCreated/Changed/DrawFrame
//   on the GL thread. dispatchTouchEvent, onKeyDown, onKeyUp run on the
//   UI thread. Native methods (JNI) forward into the C-like entry
//   points below. All entry points lock a single recursive mutex so
//   that input arriving mid-frame waits for the current frame to finish
//   (up to one frame of latency).
//
// Availability flags:
//   want_capture_mouse and want_text_input reflect ImGui's last frame
//   state. The Java side queries them from ModView's syncSoftKeyboard()
//   and from dispatchTouchEvent() to decide whether to forward events.
#ifndef YAMGG_UI_RENDERER_H
#define YAMGG_UI_RENDERER_H

#include <atomic>
#include <cstdint>

namespace yamgg {

// Android MotionEvent action codes, mirrored here so C++ does not
// include <android/input.h>.
enum class TouchAction : int {
    Down         = 0,
    Up           = 1,
    Move         = 2,
    Cancel       = 3,
    Outside      = 4,
    PointerDown  = 5,
    PointerUp    = 6,
};

// Android key action codes, same reason.
enum class KeyAction : int {
    Down = 0,
    Up   = 1,
};

class Renderer {
public:
    static Renderer& instance();

    // ── Lifecycle (GL thread) ───────────────────────────────
    void on_surface_created();
    void on_surface_changed(int width, int height);
    void on_draw_frame(int width, int height);
    void shutdown();

    // ── Input (UI thread) ───────────────────────────────────
    void on_touch(TouchAction action, float x, float y, int pointer_id);
    void on_key(int32_t android_key_code, KeyAction action);
    void on_char(uint32_t codepoint);
    void on_scroll(float dx, float dy);

    // ── State queries (any thread) ──────────────────────────
    bool want_capture_mouse() const;
    bool want_text_input() const;
    bool is_initialized() const;

    // ── Display info (for tabs that need it) ────────────────
    int  display_width() const;
    int  display_height() const;

    // ── Density (set once from ModView) ─────────────────────
    void set_density(float density);
    float density() const;

private:
    Renderer();
    ~Renderer() = default;

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Drain the char queue and the scroll accumulator, then call
    // ImGui_ImplOpenGL3_NewFrame() and ImGui::NewFrame().
    void begin_frame(float delta_seconds);

    // Called from begin_frame — hands queued chars to ImGui.
    void drain_chars();
    void drain_scroll();

    struct Impl;
    Impl* impl_;
};

// ── C-like entry points for JNI ─────────────────────────────
// These are implemented in renderer.cpp and called by native_methods.cpp
// (Reply 7) which wraps them in the JNINativeMethod table registered
// with ModView.java.

void yamgg_renderer_on_surface_created();
void yamgg_renderer_on_surface_changed(int width, int height);
void yamgg_renderer_on_draw_frame(int width, int height);
void yamgg_renderer_on_touch(int action, float x, float y, int pointer_id);
void yamgg_renderer_on_key(int android_key_code, int action);
void yamgg_renderer_on_char(int codepoint);
void yamgg_renderer_on_scroll(float dx, float dy);
bool yamgg_renderer_want_capture_mouse();
bool yamgg_renderer_want_text_input();
void yamgg_renderer_set_density(float density);

} // namespace yamgg

#endif // YAMGG_UI_RENDERER_H
