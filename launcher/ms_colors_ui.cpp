#include "ms_colors_ui.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace seed::launcher {
MsColorsUi::~MsColorsUi() {
    release_texture();
}
void MsColorsUi::release_texture() {
    if (texture_)
        SDL_DestroyTexture(texture_);
    texture_ = nullptr;
}
void MsColorsUi::initialize(const std::filesystem::path& folder) {
    folder_ = folder;
    profiles_.load(folder_ / "ms-colors.ini", status_);
    saved_ = profiles_;
}
void MsColorsUi::read_cache() {
    try {
        previews_ = load_ms_previews(folder_ / "assets" / "ms-color-previews.bin");
        choose();
        status_ = "Preview ready. Colors are shared with your opponent online.";
    } catch (const std::exception& error) {
        status_ = error.what();
    }
}
void MsColorsUi::tick() {
    if (const auto result = import_.poll()) {
        if (*result == 0)
            read_cache();
        else
            status_ =
                "Preview preparation failed. Check ms-preview-import.log and your game files.";
    }
}
void MsColorsUi::choose() {
    if (previews_.empty())
        return;
    const auto value = profiles_.get(player_, fighter_, palette_);
    custom_ = value.enabled;
    colors_ = custom_ ? value.colors : previews_.at(fighter_).originals[palette_];
    repaint_ = true;
}
void MsColorsUi::changed() {
    CustomPalette value;
    value.enabled = custom_;
    if (custom_)
        value.colors = colors_;
    profiles_.set(player_, fighter_, palette_, value);
    repaint_ = true;
}
bool MsColorsUi::save() {
    if (profiles_ == saved_)
        return true;
    if (!profiles_.save(folder_ / "ms-colors.ini", status_))
        return false;
    saved_ = profiles_;
    status_ = "MS colors saved. They apply the next time you launch the game.";
    return true;
}
void MsColorsUi::draw(SDL_Renderer* renderer, bool verified, const std::string& jp_rom,
                      const std::string& bios) {
    ImGui::PushFont(nullptr, 30);
    ImGui::TextUnformatted("MS Colors");
    ImGui::PopFont();
    ImGui::TextWrapped("Customize each player's suits. Online play uses your Player 1 colors and "
                       "shares them with your opponent.");
    if (!tried_cache_) {
        tried_cache_ = true;
        if (std::filesystem::is_regular_file(folder_ / "assets" / "ms-color-previews.bin"))
            read_cache();
    }
    if (previews_.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("Prepare the battle-sprite previews from your own Japanese ROM and "
                           "BIOS. This only needs to run once.");
        ImGui::BeginDisabled(!verified || busy());
        if (ImGui::Button("Prepare previews", ImVec2(210, 42))) {
            const auto output = folder_ / "assets" / "ms-color-previews.bin";
            if (import_.start(folder_ / "GundamSeedRecomp.exe",
                              {L"--import-jp-color-previews", output.wstring(), L"--jp-rom",
                               wide(jp_rom), L"--bios", wide(bios)},
                              folder_ / "ms-preview-import.log", status_))
                status_ = "Preparing 27 suit previews... You can use the other launcher pages "
                          "while this runs.";
        }
        ImGui::EndDisabled();
        if (!verified)
            ImGui::TextWrapped("Verify your Japanese ROM and BIOS in Game files first.");
        ImGui::TextWrapped("%s", status_.c_str());
        return;
    }
    bool select = false;
    select |= ImGui::RadioButton("Player 1", &player_, 0);
    ImGui::SameLine();
    select |= ImGui::RadioButton("Player 2", &player_, 1);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(155);
    const auto label = std::string("MS ") + std::to_string(fighter_);
    if (ImGui::BeginCombo("##suit", label.c_str())) {
        for (int id = 1; id <= 27; ++id) {
            const auto name =
                "Mobile suit " + std::to_string(id) + (id == 24 || id >= 26 ? " (variant)" : "");
            if (ImGui::Selectable(name.c_str(), id == fighter_)) {
                fighter_ = id;
                select = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160);
    select |= ImGui::Combo("##palette", &palette_, "Color 1\0Color 2\0");
    if (select)
        choose();
    const auto& preview = previews_.at(fighter_);
    if (repaint_) {
        release_texture();
        texture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                     preview.width, preview.height);
        if (texture_) {
            const auto pixels = preview.rgba(colors_);
            SDL_UpdateTexture(texture_, nullptr, pixels.data(), preview.width * 4);
            SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST);
        }
        repaint_ = false;
    }
    ImGui::BeginChild("MS preview", ImVec2(345, 490), ImGuiChildFlags_Borders);
    ImGui::TextUnformatted("LIVE PREVIEW");
    const auto origin = ImGui::GetCursorScreenPos();
    const ImVec2 area{300, 285};
    auto* draw = ImGui::GetWindowDrawList();
    for (int y = 0; y < 285; y += 15)
        for (int x = 0; x < 300; x += 15)
            draw->AddRectFilled(
                {origin.x + x, origin.y + y}, {origin.x + x + 15, origin.y + y + 15},
                ((x / 15 + y / 15) & 1) ? IM_COL32(27, 35, 49, 255) : IM_COL32(20, 27, 39, 255));
    if (texture_) {
        const float zoom =
            std::max(1.0f, std::floor(std::min(area.x / preview.width, area.y / preview.height)));
        const ImVec2 size{preview.width * zoom, preview.height * zoom};
        const ImVec2 pos{origin.x + (area.x - size.x) / 2, origin.y + (area.y - size.y) / 2};
        draw->AddImage((ImTextureID)(intptr_t)texture_, pos, {pos.x + size.x, pos.y + size.y});
    }
    ImGui::Dummy(area);
    ImGui::Text("Swatch %u / %s", selected_, custom_ ? "Custom" : "Original");
    const auto color = colors_[selected_];
    float rgb[3]{float(color & 31) / 31, float((color >> 5) & 31) / 31,
                 float((color >> 10) & 31) / 31};
    ImGui::SetNextItemWidth(290);
    if (ImGui::ColorEdit3("##picked color", rgb,
                          ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_Uint8)) {
        auto channel = [&](unsigned i) {
            return unsigned(std::lround(std::clamp(rgb[i], 0.0f, 1.0f) * 31));
        };
        colors_[selected_] = std::uint16_t(channel(0) | (channel(1) << 5) | (channel(2) << 10));
        custom_ = true;
        changed();
    }
    ImGui::TextWrapped("Click a swatch to edit its RGB values or open the color picker.");
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("MS swatches", ImVec2(0, 490), ImGuiChildFlags_Borders);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 6));
    const char* groups[]{"Main colors", "Details / effects 1", "Details / effects 2"};
    for (unsigned bank = 0; bank < 3; ++bank) {
        ImGui::TextUnformatted(groups[bank]);
        for (unsigned entry = 1; entry < 16; ++entry) {
            const unsigned index = bank * 16 + entry;
            const auto c = colors_[index];
            ImGui::PushID(index);
            const bool active = index == selected_;
            if (active)
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.2f, 0.85f, 1, 1));
            if (ImGui::ColorButton(
                    "##swatch",
                    {float(c & 31) / 31, float((c >> 5) & 31) / 31, float((c >> 10) & 31) / 31, 1},
                    ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, {30, 30}))
                selected_ = index;
            if (active)
                ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Swatch %u%s", index,
                                  std::find(preview.indices.begin(), preview.indices.end(),
                                            index) == preview.indices.end()
                                      ? " (used in other poses/effects)"
                                      : "");
            ImGui::PopID();
            if (entry % 5)
                ImGui::SameLine(0, 6);
        }
        ImGui::Spacing();
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
    if (ImGui::Button("Save colors", {145, 38}))
        save();
    ImGui::SameLine();
    if (ImGui::Button("Reset to original", {170, 38})) {
        custom_ = false;
        colors_ = preview.originals[palette_];
        changed();
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert saved", {145, 38})) {
        profiles_ = saved_;
        choose();
    }
    if (profiles_ != saved_)
        ImGui::TextColored({1, 0.8f, 0.35f, 1},
                           "Unsaved color changes. Launching also saves them.");
    if (!status_.empty())
        ImGui::TextWrapped("%s", status_.c_str());
}
} // namespace seed::launcher
