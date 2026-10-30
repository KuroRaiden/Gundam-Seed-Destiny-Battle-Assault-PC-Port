#pragma once
#include <SDL3/SDL.h>
#include <array>
struct ImGuiContext;
namespace seed {
struct LiveControls;
class MoveListOverlay {
  public:
    ~MoveListOverlay();
    void initialize(SDL_Window*, SDL_Renderer*, LiveControls*);
    void shutdown();
    bool event(const SDL_Event&);
    void draw(bool online);
    bool is_open() const {
        return open_;
    }
    bool pauses_game(bool online) const {
        return open_ && !online;
    }

  private:
    void close();
    void input(const char* command);
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    LiveControls* controls_ = nullptr;
    ImGuiContext* context_ = nullptr;
    std::array<SDL_Texture*, 8> glyphs_{};
    bool open_ = false;
    unsigned fighter_ = 1;
    float glyph_size_ = 30;
};
} // namespace seed
