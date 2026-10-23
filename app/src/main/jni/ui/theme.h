// ui/theme.h — black-gold palette, ImGui style, system font loader.
//
// One theme in this project: apply_black_gold(). It is applied exactly
// once, at first Renderer::on_surface_created. Fonts are reloaded only
// if the caller explicitly asks for a different size via set_font_size
// followed by reapply(), which is only used by the settings tab.
#ifndef YAMGG_UI_THEME_H
#define YAMGG_UI_THEME_H

#include <string>

namespace yamgg {

class Theme {
public:
    // Applies ImGui style and loads the font. Must be called after
    // ImGui::CreateContext() and before the first NewFrame().
    // `density_scale` comes from the Android display density:
    //   density_scale = displayMetrics.density
    // is the correct value; if unknown, pass 2.75f.
    static void apply_black_gold(float density_scale);

    // Re-applies the style using the currently loaded font size.
    // Used after set_font_size().
    static void reapply();

    // Set the logical point size for the font. reload_font() must be
    // called afterwards to actually load a new font atlas.
    static void set_font_size(float points);
    static float font_size();

    // Load the font again with the current size and rebuild the atlas.
    // Must be called before NewFrame or the atlas will be invalid.
    static void reload_font();

    // The path that was successfully loaded, or "(default)" if the
    // built-in proggy font is in use.
    static const std::string& font_path();

    // True after the first successful apply_black_gold.
    static bool is_applied();

private:
    Theme() = delete;
};

} // namespace yamgg

#endif // YAMGG_UI_THEME_H
