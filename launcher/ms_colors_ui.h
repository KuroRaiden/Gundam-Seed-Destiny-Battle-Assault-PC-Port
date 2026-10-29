#pragma once
#include "platform.h"
#include "ms_color_profiles.h"
#include "ms_color_preview.h"
#include <SDL3/SDL.h>
namespace seed::launcher {
class MsColorsUi {
  public:
    ~MsColorsUi();
    void initialize(const std::filesystem::path& folder);
    void tick();
    void draw(SDL_Renderer* renderer, bool verified, const std::string& jp_rom,
              const std::string& bios);
    bool save();
    bool busy() const {
        return import_.running();
    }
    void release_texture();

  private:
    void choose();
    void prepare(const std::string& jp_rom, const std::string& bios);
    void changed();
    void read_cache();
    std::filesystem::path folder_;
    MsColorProfiles profiles_, saved_;
    std::map<unsigned, MsColorPreview> previews_;
    std::array<std::uint16_t, kMsPaletteColors> colors_{};
    GameProcess import_;
    SDL_Texture* texture_ = nullptr;
    int player_ = 0, fighter_ = 1, palette_ = 0, selected_ = 1, sprite_ = 0;
    bool custom_ = false, repaint_ = true, tried_cache_ = false;
    std::string status_;
};
} // namespace seed::launcher
