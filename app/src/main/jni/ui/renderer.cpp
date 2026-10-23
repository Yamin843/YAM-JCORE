// ui/renderer.cpp
#include "ui/renderer.h"

#include <android/log.h>

#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

#include <GLES3/gl3.h>

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"

#include "core/thread_sync.h"
#include "ui/main_window.h"
#include "ui/theme.h"

#define LOG_TAG "YAMGG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace yamgg {

namespace {

// Android key codes we care about. Values match android.view.KeyEvent.
constexpr int AK_BACK         = 4;
constexpr int AK_DPAD_UP      = 19;
constexpr int AK_DPAD_DOWN    = 20;
constexpr int AK_DPAD_LEFT    = 21;
constexpr int AK_DPAD_RIGHT   = 22;
constexpr int AK_DPAD_CENTER  = 23;
constexpr int AK_ALT_LEFT     = 57;
constexpr int AK_ALT_RIGHT    = 58;
constexpr int AK_SHIFT_LEFT   = 59;
constexpr int AK_SHIFT_RIGHT  = 60;
constexpr int AK_TAB          = 61;
constexpr int AK_ENTER        = 66;
constexpr int AK_DEL          = 67;
constexpr int AK_ESCAPE       = 111;
constexpr int AK_FORWARD_DEL  = 112;
constexpr int AK_CTRL_LEFT    = 113;
constexpr int AK_CTRL_RIGHT   = 114;
constexpr int AK_MOVE_HOME    = 122;
constexpr int AK_MOVE_END     = 123;
constexpr int AK_PAGE_UP      = 92;
constexpr int AK_PAGE_DOWN    = 93;
constexpr int AK_INSERT       = 124;

ImGuiKey to_imgui_key(int code) {
    switch (code) {
        case AK_BACK:        return ImGuiKey_Escape;
        case AK_DPAD_UP:     return ImGuiKey_UpArrow;
        case AK_DPAD_DOWN:   return ImGuiKey_DownArrow;
        case AK_DPAD_LEFT:   return ImGuiKey_LeftArrow;
        case AK_DPAD_RIGHT:  return ImGuiKey_RightArrow;
        case AK_DPAD_CENTER: return ImGuiKey_Enter;
        case AK_ALT_LEFT:    return ImGuiKey_LeftAlt;
        case AK_ALT_RIGHT:   return ImGuiKey_RightAlt;
        case AK_SHIFT_LEFT:  return ImGuiKey_LeftShift;
        case AK_SHIFT_RIGHT: return ImGuiKey_RightShift;
        case AK_TAB:         return ImGuiKey_Tab;
        case AK_ENTER:       return ImGuiKey_Enter;
        case AK_DEL:         return ImGuiKey_Backspace;
        case AK_ESCAPE:      return ImGuiKey_Escape;
        case AK_FORWARD_DEL: return ImGuiKey_Delete;
        case AK_CTRL_LEFT:   return ImGuiKey_LeftCtrl;
        case AK_CTRL_RIGHT:  return ImGuiKey_RightCtrl;
        case AK_MOVE_HOME:   return ImGuiKey_Home;
        case AK_MOVE_END:    return ImGuiKey_End;
        case AK_PAGE_UP:     return ImGuiKey_PageUp;
        case AK_PAGE_DOWN:   return ImGuiKey_PageDown;
        case AK_INSERT:      return ImGuiKey_Insert;
        default:             return ImGuiKey_None;
    }
}

} // namespace

// ═══════════════════════════════════════════════════════════════
//  Impl
// ═══════════════════════════════════════════════════════════════

struct Renderer::Impl {
    // Guards ImGui state. Recursive because on_draw_frame calls into
    // MainWindow which may re-enter through notification draw.
    std::recursive_mutex      mu;

    bool                      initialized       = false;
    bool                      backend_initialized = false;
    int                       width             = 0;
    int                       height            = 0;
    float                     density           = 2.75f;

    std::chrono::steady_clock::time_point last_frame_time;

    // Queued chars from the hidden EditText (UI thread writes, GL
    // thread drains each frame).
    std::mutex                char_mu;
    std::vector<uint32_t>     char_queue;

    // Accumulated scroll (smoothing applied in on_draw_frame).
    std::mutex                scroll_mu;
    float                     scroll_vx = 0.0f;
    float                     scroll_vy = 0.0f;

    // Last-frame ImGui state, cached for lock-free queries.
    std::atomic<bool>         want_mouse{false};
    std::atomic<bool>         want_text{false};
};

Renderer& Renderer::instance() {
    static Renderer r;
    return r;
}

Renderer::Renderer() : impl_(new Impl()) {}

// ═══════════════════════════════════════════════════════════════
//  Lifecycle
// ═══════════════════════════════════════════════════════════════

void Renderer::on_surface_created() {
    std::lock_guard<std::recursive_mutex> lk(impl_->mu);
    if (impl_->initialized) {
        LOGI("renderer: already initialized");
        return;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    Theme::apply_black_gold(impl_->density);

    if (!ImGui_ImplOpenGL3_Init("#version 300 es")) {
        LOGE("renderer: ImGui_ImplOpenGL3_Init failed");
        ImGui::DestroyContext();
        return;
    }
    impl_->backend_initialized = true;

    impl_->last_frame_time = std::chrono::steady_clock::now();
    impl_->initialized     = true;
    LOGI("renderer: initialized");
}

void Renderer::on_surface_changed(int width, int height) {
    std::lock_guard<std::recursive_mutex> lk(impl_->mu);
    if (!impl_->initialized) return;
    impl_->width  = width;
    impl_->height = height;
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);
    LOGI("renderer: surface changed %dx%d", width, height);
}

void Renderer::on_draw_frame(int width, int height) {
    std::lock_guard<std::recursive_mutex> lk(impl_->mu);
    if (!impl_->initialized) return;

    impl_->width  = width;
    impl_->height = height;

    // Delta time.
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - impl_->last_frame_time).count();
    if (dt <= 0.0f)  dt = 1.0f / 60.0f;
    if (dt > 0.25f)  dt = 0.25f;
    impl_->last_frame_time = now;

    begin_frame(dt);

    // Drain UI-thread-marshaled tasks now, before drawing. This is the
    // single integration point between JS-thread callbacks and ImGui.
    UiThreadQueue::instance().drain();

    MainWindow::instance().draw();

    // Snapshot ImGui's capture state for lock-free queries.
    ImGuiIO& io = ImGui::GetIO();
    impl_->want_mouse.store(io.WantCaptureMouse, std::memory_order_relaxed);
    impl_->want_text .store(io.WantTextInput,   std::memory_order_relaxed);

    ImGui::Render();

    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Renderer::shutdown() {
    std::lock_guard<std::recursive_mutex> lk(impl_->mu);
    if (!impl_->initialized) return;
    if (impl_->backend_initialized) {
        ImGui_ImplOpenGL3_Shutdown();
        impl_->backend_initialized = false;
    }
    ImGui::DestroyContext();
    impl_->initialized = false;
    LOGI("renderer: shutdown");
}

// ═══════════════════════════════════════════════════════════════
//  Input
// ═══════════════════════════════════════════════════════════════

void Renderer::on_touch(TouchAction action, float x, float y, int pointer_id) {
    (void)pointer_id;
    std::lock_guard<std::recursive_mutex> lk(impl_->mu);
    if (!impl_->initialized) return;
    ImGuiIO& io = ImGui::GetIO();

    switch (action) {
        case TouchAction::Down:
        case TouchAction::PointerDown:
            io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
            io.AddMousePosEvent(x, y);
            io.AddMouseButtonEvent(0, true);
            break;

        case TouchAction::Up:
        case TouchAction::PointerUp:
            io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
            io.AddMousePosEvent(x, y);
            io.AddMouseButtonEvent(0, false);
            break;

        case TouchAction::Move:
            io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
            io.AddMousePosEvent(x, y);
            break;

        case TouchAction::Cancel:
        case TouchAction::Outside:
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            io.AddMouseButtonEvent(0, false);
            break;
    }
}

void Renderer::on_key(int32_t code, KeyAction action) {
    std::lock_guard<std::recursive_mutex> lk(impl_->mu);
    if (!impl_->initialized) return;
    ImGuiKey k = to_imgui_key((int)code);
    if (k == ImGuiKey_None) return;
    ImGui::GetIO().AddKeyEvent(k, action == KeyAction::Down);
}

void Renderer::on_char(uint32_t codepoint) {
    std::lock_guard<std::mutex> lk(impl_->char_mu);
    impl_->char_queue.push_back(codepoint);
}

void Renderer::on_scroll(float dx, float dy) {
    std::lock_guard<std::mutex> lk(impl_->scroll_mu);
    impl_->scroll_vx += dx;
    impl_->scroll_vy += dy;
}

// ═══════════════════════════════════════════════════════════════
//  State queries
// ═══════════════════════════════════════════════════════════════

bool Renderer::want_capture_mouse() const {
    return impl_->want_mouse.load(std::memory_order_relaxed);
}

bool Renderer::want_text_input() const {
    return impl_->want_text.load(std::memory_order_relaxed);
}

bool Renderer::is_initialized() const {
    std::lock_guard<std::recursive_mutex> lk(impl_->mu);
    return impl_->initialized;
}

int Renderer::display_width() const {
    std::lock_guard<std::recursive_mutex> lk(impl_->mu);
    return impl_->width;
}

int Renderer::display_height() const {
    std::lock_guard<std::recursive_mutex> lk(impl_->mu);
    return impl_->height;
}

void Renderer::set_density(float density) {
    std::lock_guard<std::recursive_mutex> lk(impl_->mu);
    if (density > 0.5f && density < 8.0f) impl_->density = density;
}

float Renderer::density() const {
    std::lock_guard<std::recursive_mutex> lk(impl_->mu);
    return impl_->density;
}

// ═══════════════════════════════════════════════════════════════
//  Frame helpers
// ═══════════════════════════════════════════════════════════════

void Renderer::begin_frame(float delta_seconds) {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)impl_->width, (float)impl_->height);
    io.DeltaTime   = delta_seconds;

    drain_chars();
    drain_scroll();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
}

void Renderer::drain_chars() {
    std::vector<uint32_t> local;
    {
        std::lock_guard<std::mutex> lk(impl_->char_mu);
        std::swap(local, impl_->char_queue);
    }
    if (local.empty()) return;

    ImGuiIO& io = ImGui::GetIO();
    for (uint32_t cp : local) {
        io.AddInputCharacter(cp);
    }
}

void Renderer::drain_scroll() {
    // Smooth the scroll: emit the accumulated velocity, then decay it.
    // This matches the feel of ImGui's native mouse wheel when the user
    // releases the two-finger gesture.
    std::lock_guard<std::mutex> lk(impl_->scroll_mu);
    constexpr float kThreshold = 0.001f;

    if (std::fabs(impl_->scroll_vx) < kThreshold &&
        std::fabs(impl_->scroll_vy) < kThreshold) {
        impl_->scroll_vx = 0.0f;
        impl_->scroll_vy = 0.0f;
        return;
    }

    ImGui::GetIO().AddMouseWheelEvent(impl_->scroll_vx, impl_->scroll_vy);

    impl_->scroll_vx *= 0.55f;
    impl_->scroll_vy *= 0.55f;

    if (std::fabs(impl_->scroll_vx) < kThreshold) impl_->scroll_vx = 0.0f;
    if (std::fabs(impl_->scroll_vy) < kThreshold) impl_->scroll_vy = 0.0f;
}

// ═══════════════════════════════════════════════════════════════
//  C-like entry points (JNI)
// ═══════════════════════════════════════════════════════════════

void yamgg_renderer_on_surface_created() {
    Renderer::instance().on_surface_created();
}

void yamgg_renderer_on_surface_changed(int width, int height) {
    Renderer::instance().on_surface_changed(width, height);
}

void yamgg_renderer_on_draw_frame(int width, int height) {
    Renderer::instance().on_draw_frame(width, height);
}

void yamgg_renderer_on_touch(int action, float x, float y, int pointer_id) {
    Renderer::instance().on_touch((TouchAction)action, x, y, pointer_id);
}

void yamgg_renderer_on_key(int android_key_code, int action) {
    Renderer::instance().on_key(android_key_code,
        action == (int)KeyAction::Down ? KeyAction::Down : KeyAction::Up);
}

void yamgg_renderer_on_char(int codepoint) {
    if (codepoint <= 0) return;
    Renderer::instance().on_char((uint32_t)codepoint);
}

void yamgg_renderer_on_scroll(float dx, float dy) {
    Renderer::instance().on_scroll(dx, dy);
}

bool yamgg_renderer_want_capture_mouse() {
    return Renderer::instance().want_capture_mouse();
}

bool yamgg_renderer_want_text_input() {
    return Renderer::instance().want_text_input();
}

void yamgg_renderer_set_density(float density) {
    Renderer::instance().set_density(density);
}

} // namespace yamgg
