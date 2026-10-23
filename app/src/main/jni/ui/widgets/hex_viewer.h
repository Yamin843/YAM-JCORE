// ui/widgets/hex_viewer.h — hex dump viewer for byte arrays.
//
// Renders a std::vector<uint8_t> as 16-byte rows: offset | hex bytes |
// ASCII. Uses ImGuiListClipper for virtual scrolling, so a 1 MB buffer
// scrolls smoothly. Copy-on-click copies the current row's hex to the
// Android clipboard.
#ifndef YAMGG_UI_WIDGETS_HEX_VIEWER_H
#define YAMGG_UI_WIDGETS_HEX_VIEWER_H

#include <cstdint>
#include <string>
#include <vector>

namespace yamgg {

class HexViewer {
public:
    HexViewer() = default;

    // Replace the current buffer.
    void set_data(std::vector<uint8_t> bytes);
    void set_data(const uint8_t* data, size_t length);

    // Decode a hex string. Separators (space, colon, dash) are stripped
    // before decoding. Odd-length or invalid input sets an error state
    // that draw() reports.
    bool set_data_from_hex(const std::string& hex, std::string& error);

    // Clear the current buffer.
    void clear();

    // Draw the viewer. `height` is the available vertical space in
    // pixels, 0 to fill. `id` must be unique per draw site.
    void draw(const char* id, float height);

    // Statistics for callers that want to display them.
    size_t size() const;

private:
    std::vector<uint8_t>  data_;
    std::string           error_;
    bool                  truncated_note_ = false;
};

} // namespace yamgg

#endif // YAMGG_UI_WIDGETS_HEX_VIEWER_H
