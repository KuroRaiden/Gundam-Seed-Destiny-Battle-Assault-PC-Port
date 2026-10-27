#pragma once
#include <SDL3/SDL.h>
struct ImGuiContext;
namespace seed {
class TrainingMeterOverlay {
  public:
    ~TrainingMeterOverlay();
    void initialize(SDL_Window* window, SDL_Renderer* renderer);
    void shutdown();
    bool event(const SDL_Event& event, bool online);
    void draw(bool online);

  private:
    bool prepare();
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    ImGuiContext* context_ = nullptr;
};
} // namespace seed
