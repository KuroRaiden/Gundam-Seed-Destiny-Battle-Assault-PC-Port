#pragma once
#include "training_view.h"
#include <SDL3/SDL.h>
#include <cstdint>
#include <filesystem>
#include <string>
namespace gbarecomp {
struct NativeFrameControl;
}
namespace seed {
void configure_training_tools(bool training, bool online, const std::filesystem::path& save,
                              const std::filesystem::path& art = {});
bool training_tools_allowed();
bool training_boxes_visible();
bool training_tools_event(const SDL_Event&, bool online);
void training_request_state(bool load);
bool training_checkpoint_pending();
bool training_service_checkpoint(gbarecomp::NativeFrameControl&);
const std::string& training_tools_message();
void training_presentation_restored();
void training_snapshot_notice(std::string message);
const std::uint8_t* training_snapshot_display_pixels(const std::uint8_t* pixels);
TrainingView training_view();
const std::uint8_t* training_display_pixels(const std::uint8_t* pixels, unsigned w, unsigned h);
} // namespace seed
