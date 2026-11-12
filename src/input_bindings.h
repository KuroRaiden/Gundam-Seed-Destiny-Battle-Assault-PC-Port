#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace seed {
inline constexpr std::array<const char*, 10> kControlNames{"A",    "B",  "Select", "Start", "Right",
                                                           "Left", "Up", "Down",   "R",     "L"};
enum class PadBindingKind : unsigned { None, Button, AxisPositive, AxisNegative };
struct PadBinding {
    PadBindingKind kind = PadBindingKind::None;
    unsigned index = 0;
    bool operator==(const PadBinding&) const = default;
};
struct PlayerBindings {
    bool keyboard_enabled = true;
    int pad_slot = 0;
    std::array<SDL_Scancode, 10> keyboard{};
    std::array<std::array<PadBinding, 2>, 10> gamepad{};
    bool operator==(const PlayerBindings&) const = default;
};
struct ControlBindings {
    std::array<PlayerBindings, 2> players;
    ControlBindings();
    void assign_gamepad(unsigned player, int slot);
    bool load(const std::filesystem::path& path, std::string* error = nullptr);
    bool save(const std::filesystem::path& path, std::string& error) const;
    bool operator==(const ControlBindings&) const = default;
};
struct ControlSnapshot {
    std::array<bool, SDL_SCANCODE_COUNT> keyboard{};
    std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> buttons{};
    std::array<Sint16, SDL_GAMEPAD_AXIS_COUNT> axes{};
};
std::uint16_t evaluate_controls(const PlayerBindings& bindings, const ControlSnapshot& state);
ControlSnapshot capture_controls(const bool* keyboard, SDL_Gamepad* pad);
std::uint16_t sample_controls(const PlayerBindings& bindings, const bool* keyboard,
                              SDL_Gamepad* pad);
std::string binding_name(PadBinding binding);
std::string key_binding_name(SDL_Scancode key);
std::filesystem::path controls_path();

class InputDevices {
  public:
    InputDevices() = default;
    InputDevices(const InputDevices&) = delete;
    InputDevices& operator=(const InputDevices&) = delete;
    ~InputDevices() {
        close();
    }
    void refresh();
    void close();
    SDL_Gamepad* pad(int slot) const {
        return slot >= 0 && slot < slot_count() ? slots_[slot].handle : nullptr;
    }
    SDL_JoystickID id(int slot) const {
        return slot >= 0 && slot < slot_count() ? slots_[slot].id : 0;
    }
    int slot_count() const {
        return static_cast<int>(slots_.size());
    }
    unsigned count() const;

  private:
    struct Slot {
        SDL_Gamepad* handle = nullptr;
        SDL_JoystickID id = 0;
    };
    std::vector<Slot> slots_;
};
} // namespace seed
