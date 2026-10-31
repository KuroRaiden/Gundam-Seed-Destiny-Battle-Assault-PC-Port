#pragma once
#include <SDL3/SDL.h>
#include <filesystem>
namespace seed {
class TrainingMeterOverlay;
// Consume a completed Training-only capture without touching the live target.
// The destination rectangle is in physical output pixels.
bool save_pending_training_snapshot(SDL_Window* window, SDL_Renderer* renderer,
                                    TrainingMeterOverlay& overlay,
                                    const std::filesystem::path& folder,
                                    const SDL_FRect& destination);
} // namespace seed
