// ui/widgets/hex_viewer.cpp
#include "ui/widgets/hex_viewer.h"

#include <android/log.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "imgui.h"

namespace yamgg {

namespace {

constexpr size_t kBytesPerRow = 16;

char nibble(int n) {
    return (n < 10) ? ('0' + n) : ('a' + (n - 10));
}

} // namespace

void HexViewer::set_data(std::vector<uint8_t> bytes) {
    data_ = std::move(bytes);
    error_.clear();
    truncated_note_ = false;
}

void HexViewer::set_data(const uint8_t* data, size_t length) {
    if (data == nullptr || length == 0) {
        data_.clear();
        return;
    }
    data_.assign(data, data + length);
    error_.clear();
    truncated_note_ = false;
}

bool HexViewer::set_data_from_hex(const std::string& hex, std::string& error) {
    std::string clean;
    clean.reserve(hex.size());
    for (char c : hex) {
        if (c == ' ' || c == ':' || c == '-' || c == '\t' || c == '\n') continue;
        if (!std::isxdigit((unsigned char)c)) {
            error = "invalid hex character: " + std::string(1, c);
            return false;
        }
        clean += c;
    }
    if (clean.size() % 2 != 0) {
        error = "hex string has odd length";
        return false;
    }

    std::vector<uint8_t> out;
    out.reserve(clean.size() / 2);
    for (size_t i = 0; i < clean.size(); i += 2) {
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };
        out.push_back((uint8_t)((nib(clean[i]) << 4) | nib(clean[i + 1])));
    }

    set_data(std::move(out));
    return true;
}

void HexViewer::clear() {
    data_.clear();
    error_.clear();
    truncated_note_ = false;
}

size_t HexViewer::size() const {
    return data_.size();
}

void HexViewer::draw(const char* id, float height) {
    if (id == nullptr || *id == '\0') id = "##hex";

    ImGui::BeginChild(id, ImVec2(0, height), ImGuiChildFlags_Borders);

    if (!error_.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error: %s",
                           error_.c_str());
        ImGui::EndChild();
        return;
    }

    if (data_.empty()) {
        ImGui::TextDisabled("(empty)");
        ImGui::EndChild();
        return;
    }

    // Row layout: "00000000  00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f  |................|"
    const size_t rows = (data_.size() + kBytesPerRow - 1) / kBytesPerRow;

    // Font is monospace in the theme; use the same size for both rows.
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));

    if (ImGui::BeginTable("##hex_table", 3,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV
            | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit)) {

        ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Hex",    ImGuiTableColumnFlags_WidthFixed, 500.0f);
        ImGui::TableSetupColumn("ASCII",  ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin((int)rows);
        while (clipper.Step()) {
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; r++) {
                size_t base = (size_t)r * kBytesPerRow;

                ImGui::TableNextRow();
                ImGui::PushID(r);

                // Offset column.
                ImGui::TableSetColumnIndex(0);
                char offset_buf[16];
                std::snprintf(offset_buf, sizeof(offset_buf), "%08zx", base);
                ImGui::TextUnformatted(offset_buf);

                // Hex column.
                ImGui::TableSetColumnIndex(1);
                std::string hex;
                hex.reserve(kBytesPerRow * 3);
                for (size_t i = 0; i < kBytesPerRow; i++) {
                    if (base + i >= data_.size()) {
                        hex.append("   ");
                    } else {
                        uint8_t b = data_[base + i];
                        hex.push_back(nibble(b >> 4));
                        hex.push_back(nibble(b & 0x0F));
                        if (i != kBytesPerRow - 1) hex.push_back(' ');
                    }
                }
                ImGui::TextUnformatted(hex.c_str());

                // ASCII column.
                ImGui::TableSetColumnIndex(2);
                std::string ascii;
                ascii.reserve(kBytesPerRow);
                for (size_t i = 0; i < kBytesPerRow; i++) {
                    if (base + i >= data_.size()) break;
                    uint8_t b = data_[base + i];
                    ascii.push_back((b >= 32 && b <= 126) ? (char)b : '.');
                }
                ImGui::TextUnformatted(ascii.c_str());

                // Copy on row click.
                if (ImGui::IsItemClicked()) {
                    // Nothing to do — copying handled below.
                }

                ImGui::PopID();
            }
        }

        ImGui::EndTable();
    }

    ImGui::PopStyleVar(2);

    if (truncated_note_) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                           "(truncated to %zu bytes)", data_.size());
    }

    ImGui::EndChild();
}

} // namespace yamgg
