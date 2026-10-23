// ui/theme.cpp — palette, style, font loading.
#include "ui/theme.h"

#include <android/log.h>
#include <unistd.h>

#include <cstdio>

#include "imgui.h"

#define LOG_TAG "YAMGG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)

namespace yamgg {

namespace {

// ── Palette (black-gold) ─────────────────────────────────────
constexpr float kBgDeep        [4] = {0x08 / 255.0f, 0x08 / 255.0f, 0x0A / 255.0f, 1.00f};
constexpr float kBgWindow      [4] = {0x0E / 255.0f, 0x0E / 255.0f, 0x11 / 255.0f, 1.00f};
constexpr float kBgChild       [4] = {0x0B / 255.0f, 0x0B / 255.0f, 0x0E / 255.0f, 1.00f};
constexpr float kBgCard        [4] = {0x1A / 255.0f, 0x1A / 255.0f, 0x1F / 255.0f, 1.00f};
constexpr float kBgCardHover   [4] = {0x24 / 255.0f, 0x24 / 255.0f, 0x2A / 255.0f, 1.00f};
constexpr float kBgCardActive  [4] = {0x2E / 255.0f, 0x2E / 255.0f, 0x36 / 255.0f, 1.00f};
constexpr float kGoldBase      [4] = {0xD4 / 255.0f, 0xA0 / 255.0f, 0x17 / 255.0f, 1.00f};
constexpr float kGoldBright    [4] = {0xF0 / 255.0f, 0xBE / 255.0f, 0x3C / 255.0f, 1.00f};
constexpr float kGoldMuted     [4] = {0xA8 / 255.0f, 0x80 / 255.0f, 0x1A / 255.0f, 1.00f};
constexpr float kGoldSoft      [4] = {0xD4 / 255.0f, 0xA0 / 255.0f, 0x17 / 255.0f, 0.35f};
constexpr float kText          [4] = {0xF2 / 255.0f, 0xF2 / 255.0f, 0xF4 / 255.0f, 1.00f};
constexpr float kTextDim       [4] = {0xA8 / 255.0f, 0xA8 / 255.0f, 0xAE / 255.0f, 1.00f};
constexpr float kTextFaint     [4] = {0x62 / 255.0f, 0x62 / 255.0f, 0x68 / 255.0f, 1.00f};

inline ImVec4 rgba(const float c[4]) { return ImVec4(c[0], c[1], c[2], c[3]); }

struct State {
    bool        applied   = false;
    float       points    = 15.0f;
    float       density   = 2.75f;
    std::string font_path = "(default)";
};

State g_state;

// Candidate fonts to try. The first one that is readable wins.
const char* const kFontCandidates[] = {
    "/system/fonts/Roboto-Regular.ttf",
    "/system/fonts/RobotoCondensed-Regular.ttf",
    "/system/fonts/DroidSans.ttf",
    "/system/fonts/NotoSans-Regular.ttf",
    "/product/fonts/Roboto-Regular.ttf",
    "/product/fonts/NotoSans-Regular.ttf",
};

bool file_readable(const char* p) {
    return p != nullptr && ::access(p, R_OK) == 0;
}

void apply_style() {
    ImGuiStyle& s = ImGui::GetStyle();
    ImVec4* c = s.Colors;

    // ── Rounding ──
    s.WindowRounding     = 14.0f;
    s.ChildRounding      = 10.0f;
    s.FrameRounding      = 10.0f;
    s.PopupRounding      = 12.0f;
    s.TabRounding        = 10.0f;
    s.GrabRounding       = 10.0f;
    s.ScrollbarRounding  = 10.0f;

    // ── Borders ──
    s.WindowBorderSize   = 2.0f;
    s.ChildBorderSize    = 0.0f;
    s.FrameBorderSize    = 0.5f;
    s.PopupBorderSize    = 2.0f;
    s.TabBorderSize      = 0.0f;

    // ── Padding ──
    s.WindowPadding      = ImVec2(18.0f, 16.0f);
    s.FramePadding       = ImVec2(16.0f, 10.0f);
    s.CellPadding        = ImVec2(14.0f, 9.0f);
    s.ItemSpacing        = ImVec2(9.0f, 10.0f);
    s.ItemInnerSpacing   = ImVec2(7.0f, 5.0f);
    s.IndentSpacing      = 22.0f;
    s.ScrollbarSize      = 30.0f;
    s.GrabMinSize        = 48.0f;

    // ── Alignment ──
    s.WindowTitleAlign          = ImVec2(0.5f, 0.5f);
    s.ButtonTextAlign           = ImVec2(0.5f, 0.5f);
    s.SelectableTextAlign       = ImVec2(0.0f, 0.5f);
    s.WindowMenuButtonPosition  = ImGuiDir_None;
    s.ColorButtonPosition       = ImGuiDir_Right;

    // ── Anti-aliasing ──
    s.AntiAliasedLines          = true;
    s.AntiAliasedLinesUseTex    = true;
    s.AntiAliasedFill           = true;
    s.CircleTessellationMaxError = 0.15f;

    // ── Colors ──
    c[ImGuiCol_Text]                    = rgba(kText);
    c[ImGuiCol_TextDisabled]            = rgba(kTextFaint);

    c[ImGuiCol_WindowBg]                = rgba(kBgWindow);
    c[ImGuiCol_ChildBg]                 = rgba(kBgChild);
    c[ImGuiCol_PopupBg]                 = ImVec4(0x12 / 255.0f, 0x12 / 255.0f, 0x16 / 255.0f, 0.98f);
    c[ImGuiCol_MenuBarBg]               = rgba(kBgWindow);

    c[ImGuiCol_Border]                  = rgba(kGoldMuted);
    c[ImGuiCol_BorderShadow]            = ImVec4(0, 0, 0, 0);

    c[ImGuiCol_FrameBg]                 = rgba(kBgCard);
    c[ImGuiCol_FrameBgHovered]          = rgba(kBgCardHover);
    c[ImGuiCol_FrameBgActive]           = rgba(kBgCardActive);

    c[ImGuiCol_TitleBg]                 = rgba(kBgDeep);
    c[ImGuiCol_TitleBgActive]           = ImVec4(0x14 / 255.0f, 0x11 / 255.0f, 0x08 / 255.0f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]        = rgba(kBgDeep);

    c[ImGuiCol_ScrollbarBg]             = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]           = ImVec4(1.0f, 1.0f, 1.0f, 0.20f);
    c[ImGuiCol_ScrollbarGrabHovered]    = rgba(kGoldSoft);
    c[ImGuiCol_ScrollbarGrabActive]     = rgba(kGoldBase);

    c[ImGuiCol_CheckMark]               = rgba(kGoldBright);
    c[ImGuiCol_SliderGrab]              = rgba(kGoldBase);
    c[ImGuiCol_SliderGrabActive]        = rgba(kGoldBright);

    c[ImGuiCol_Button]                  = rgba(kBgCard);
    c[ImGuiCol_ButtonHovered]           = ImVec4(0x25 / 255.0f, 0x1E / 255.0f, 0x0A / 255.0f, 1.00f);
    c[ImGuiCol_ButtonActive]            = ImVec4(0x3A / 255.0f, 0x2C / 255.0f, 0x0E / 255.0f, 1.00f);

    c[ImGuiCol_Header]                  = ImVec4(0x1A / 255.0f, 0x1A / 255.0f, 0x1E / 255.0f, 1.00f);
    c[ImGuiCol_HeaderHovered]           = ImVec4(0x25 / 255.0f, 0x20 / 255.0f, 0x10 / 255.0f, 1.00f);
    c[ImGuiCol_HeaderActive]            = ImVec4(0x3A / 255.0f, 0x2E / 255.0f, 0x12 / 255.0f, 1.00f);

    c[ImGuiCol_Separator]               = ImVec4(0x8A / 255.0f, 0x68 / 255.0f, 0x10 / 255.0f, 0.55f);
    c[ImGuiCol_SeparatorHovered]        = rgba(kGoldBase);
    c[ImGuiCol_SeparatorActive]         = rgba(kGoldBright);

    c[ImGuiCol_ResizeGrip]              = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered]       = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripActive]        = ImVec4(0, 0, 0, 0);

    c[ImGuiCol_Tab]                     = ImVec4(0x12 / 255.0f, 0x12 / 255.0f, 0x14 / 255.0f, 1.00f);
    c[ImGuiCol_TabHovered]              = ImVec4(0x1E / 255.0f, 0x19 / 255.0f, 0x0A / 255.0f, 1.00f);
    c[ImGuiCol_TabSelected]             = ImVec4(0x2A / 255.0f, 0x22 / 255.0f, 0x0E / 255.0f, 1.00f);
    c[ImGuiCol_TabSelectedOverline]     = rgba(kGoldBright);
    c[ImGuiCol_TabDimmed]               = ImVec4(0x0E / 255.0f, 0x0E / 255.0f, 0x10 / 255.0f, 1.00f);
    c[ImGuiCol_TabDimmedSelected]       = ImVec4(0x1A / 255.0f, 0x14 / 255.0f, 0x08 / 255.0f, 1.00f);
    c[ImGuiCol_TabDimmedSelectedOverline] = rgba(kGoldMuted);

    c[ImGuiCol_PlotLines]               = rgba(kGoldBase);
    c[ImGuiCol_PlotLinesHovered]        = rgba(kGoldBright);
    c[ImGuiCol_PlotHistogram]           = rgba(kGoldBase);
    c[ImGuiCol_PlotHistogramHovered]    = rgba(kGoldBright);

    c[ImGuiCol_TableHeaderBg]           = ImVec4(0x14 / 255.0f, 0x12 / 255.0f, 0x0A / 255.0f, 1.00f);
    c[ImGuiCol_TableBorderStrong]       = ImVec4(0x8A / 255.0f, 0x68 / 255.0f, 0x10 / 255.0f, 0.45f);
    c[ImGuiCol_TableBorderLight]        = ImVec4(0x2A / 255.0f, 0x2A / 255.0f, 0x2E / 255.0f, 1.00f);
    c[ImGuiCol_TableRowBg]              = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]           = ImVec4(1.0f, 1.0f, 1.0f, 0.02f);

    c[ImGuiCol_TextSelectedBg]          = rgba(kGoldSoft);
    c[ImGuiCol_DragDropTarget]          = rgba(kGoldBright);
    c[ImGuiCol_NavHighlight]            = rgba(kGoldBright);
    c[ImGuiCol_NavWindowingHighlight]   = ImVec4(1.0f, 1.0f, 1.0f, 0.70f);
    c[ImGuiCol_NavWindowingDimBg]       = ImVec4(0, 0, 0, 0.60f);
    c[ImGuiCol_ModalWindowDimBg]        = ImVec4(0, 0, 0, 0.70f);
}

void load_font() {
    ImGuiIO& io = ImGui::GetIO();

    // Effective pixel size = logical points × density. ImGui works in
    // pixels; a 15pt font at density 2.75 renders as 41 px tall, which
    // is comfortable on a modern 400+ dpi phone.
    const float pixel_size = g_state.points * g_state.density;

    ImFontConfig cfg;
    cfg.OversampleH        = 2;
    cfg.OversampleV        = 1;
    cfg.PixelSnapH         = false;
    cfg.RasterizerMultiply = 1.10f;
    cfg.FontDataOwnedByAtlas = true;

    ImFont* loaded = nullptr;
    for (const char* path : kFontCandidates) {
        if (!file_readable(path)) continue;
        loaded = io.Fonts->AddFontFromFileTTF(path, pixel_size, &cfg);
        if (loaded != nullptr) {
            g_state.font_path = path;
            LOGI("theme: loaded font %s at %.2fpx", path, pixel_size);
            break;
        }
    }

    if (loaded == nullptr) {
        LOGW("theme: no system font readable, falling back to builtin");
        ImFontConfig dc;
        dc.SizePixels  = pixel_size * 0.75f;
        dc.OversampleH = 2;
        dc.OversampleV = 2;
        io.Fonts->AddFontDefault(&dc);
        g_state.font_path = "(default)";
    }

    io.Fonts->Build();
}

} // namespace

void Theme::apply_black_gold(float density_scale) {
    if (density_scale > 0.5f) g_state.density = density_scale;
    apply_style();
    load_font();
    g_state.applied = true;
    LOGI("theme: applied (density=%.2f, points=%.2f, font=%s)",
         g_state.density, g_state.points, g_state.font_path.c_str());
}

void Theme::reapply() {
    if (!g_state.applied) return;
    apply_style();
}

void Theme::set_font_size(float points) {
    if (points < 8.0f)   points = 8.0f;
    if (points > 48.0f)  points = 48.0f;
    g_state.points = points;
}

float Theme::font_size() {
    return g_state.points;
}

void Theme::reload_font() {
    if (!g_state.applied) return;
    load_font();
}

const std::string& Theme::font_path() {
    return g_state.font_path;
}

bool Theme::is_applied() {
    return g_state.applied;
}

} // namespace yamgg
