#pragma once
#include "live_controls.h"
struct ImGuiContext;
namespace seed {
class InGameRemap {
  public:
    ~InGameRemap();
    void initialize(SDL_Window* window, SDL_Renderer* renderer, LiveControls* controls);
    void shutdown();
    bool event(const SDL_Event& event, bool online);
    void draw(bool online);
    bool is_open() const {
        return open_;
    }
    bool pauses_game(bool online) const {
        return open_ && !online;
    }

  private:
    void toggle();
    bool save_and_close();
    void begin_capture(unsigned player, unsigned bit, int slot);
    void draw_bindings();
    void draw_capture();
    bool capture_event(const SDL_Event& event);
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    ImGuiContext* context_ = nullptr;
    LiveControls* controls_ = nullptr;
    ControlBindings edited_;
    bool open_ = false;
    std::string error_;
    struct Capture {
        int player = -1;
        unsigned bit = 0;
        int slot = -1;
        Uint64 armed_at = 0;
        bool neutral = false;
    } capture_;
};
} // namespace seed
