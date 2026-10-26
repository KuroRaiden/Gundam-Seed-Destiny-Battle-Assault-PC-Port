#include "input_bindings.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace seed {
namespace {
bool valid(PadBinding binding) {
    switch (binding.kind) {
    case PadBindingKind::None:
        return binding.index == 0;
    case PadBindingKind::Button:
        return binding.index < SDL_GAMEPAD_BUTTON_COUNT;
    case PadBindingKind::AxisPositive:
    case PadBindingKind::AxisNegative:
        return binding.index < SDL_GAMEPAD_AXIS_COUNT;
    default:
        return false;
    }
}
bool down(PadBinding binding, const ControlSnapshot& state) {
    switch (binding.kind) {
    case PadBindingKind::Button:
        return binding.index < state.buttons.size() && state.buttons[binding.index];
    case PadBindingKind::AxisPositive:
        return binding.index < state.axes.size() && state.axes[binding.index] > 16000;
    case PadBindingKind::AxisNegative:
        return binding.index < state.axes.size() && state.axes[binding.index] < -16000;
    default:
        return false;
    }
}
} // namespace
ControlBindings::ControlBindings() {
    players[0].keyboard = {SDL_SCANCODE_X,      SDL_SCANCODE_Z,     SDL_SCANCODE_RSHIFT,
                           SDL_SCANCODE_RETURN, SDL_SCANCODE_RIGHT, SDL_SCANCODE_LEFT,
                           SDL_SCANCODE_UP,     SDL_SCANCODE_DOWN,  SDL_SCANCODE_V,
                           SDL_SCANCODE_C};
    players[1].keyboard = {SDL_SCANCODE_KP_1,     SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_0,
                           SDL_SCANCODE_KP_ENTER, SDL_SCANCODE_L,    SDL_SCANCODE_J,
                           SDL_SCANCODE_I,        SDL_SCANCODE_K,    SDL_SCANCODE_KP_6,
                           SDL_SCANCODE_KP_4};
    constexpr std::array<unsigned, 10> buttons{
        SDL_GAMEPAD_BUTTON_SOUTH,          SDL_GAMEPAD_BUTTON_EAST,
        SDL_GAMEPAD_BUTTON_BACK,           SDL_GAMEPAD_BUTTON_START,
        SDL_GAMEPAD_BUTTON_DPAD_RIGHT,     SDL_GAMEPAD_BUTTON_DPAD_LEFT,
        SDL_GAMEPAD_BUTTON_DPAD_UP,        SDL_GAMEPAD_BUTTON_DPAD_DOWN,
        SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER};
    for (unsigned player = 0; player < 2; ++player) {
        players[player].pad_slot = player;
        for (unsigned bit = 0; bit < 10; ++bit)
            players[player].gamepad[bit][0] = {PadBindingKind::Button, buttons[bit]};
        players[player].gamepad[4][1] = {PadBindingKind::AxisPositive, SDL_GAMEPAD_AXIS_LEFTX};
        players[player].gamepad[5][1] = {PadBindingKind::AxisNegative, SDL_GAMEPAD_AXIS_LEFTX};
        players[player].gamepad[6][1] = {PadBindingKind::AxisNegative, SDL_GAMEPAD_AXIS_LEFTY};
        players[player].gamepad[7][1] = {PadBindingKind::AxisPositive, SDL_GAMEPAD_AXIS_LEFTY};
    }
}
bool ControlBindings::load(const std::filesystem::path& path, std::string* error) {
    *this = ControlBindings{};
    if (error)
        error->clear();
    std::ifstream input(path);
    if (!input)
        return false;
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream row(line);
        std::string key;
        int value = 0;
        if (!(row >> key >> value) || key.starts_with('#'))
            continue;
        if (key == "version") {
            if (value != 1) {
                *this = ControlBindings{};
                if (error)
                    *error = "Unsupported controls file version; using defaults.";
                return false;
            }
            continue;
        }
        if (key.size() < 4 || key[0] != 'p' || (key[1] != '1' && key[1] != '2') || key[2] != '.')
            continue;
        auto& player = players[key[1] - '1'];
        const auto property = key.substr(3);
        if (property == "keyboard" && (value == 0 || value == 1))
            player.keyboard_enabled = value != 0;
        else if (property == "pad" && value >= -1 && value <= 1)
            player.pad_slot = value;
        else
            for (unsigned bit = 0; bit < 10; ++bit) {
                if (property == std::string("key.") + kControlNames[bit] && value >= 0 &&
                    value < SDL_SCANCODE_COUNT)
                    player.keyboard[bit] = static_cast<SDL_Scancode>(value);
                for (unsigned slot = 0; slot < 2; ++slot)
                    if (property == "pad" + std::to_string(slot + 1) + "." + kControlNames[bit]) {
                        unsigned index = 0;
                        if (row >> index) {
                            const PadBinding binding{static_cast<PadBindingKind>(value), index};
                            if (valid(binding))
                                player.gamepad[bit][slot] = binding;
                        }
                    }
            }
    }
    return true;
}
bool ControlBindings::save(const std::filesystem::path& path, std::string& error) const {
    error.clear();
    auto staging = path;
    staging += L".pending";
    std::ofstream output(staging, std::ios::trunc);
    if (!output) {
        error = "Cannot write controller bindings in the launcher folder.";
        return false;
    }
    output << "version 1\n";
    for (unsigned seat = 0; seat < 2; ++seat) {
        const auto& player = players[seat];
        const auto prefix = "p" + std::to_string(seat + 1) + ".";
        output << prefix << "keyboard " << player.keyboard_enabled << '\n'
               << prefix << "pad " << player.pad_slot << '\n';
        for (unsigned bit = 0; bit < 10; ++bit) {
            output << prefix << "key." << kControlNames[bit] << ' '
                   << unsigned(player.keyboard[bit]) << '\n';
            for (unsigned slot = 0; slot < 2; ++slot) {
                const auto binding = player.gamepad[bit][slot];
                output << prefix << "pad" << slot + 1 << '.' << kControlNames[bit] << ' '
                       << unsigned(binding.kind) << ' ' << binding.index << '\n';
            }
        }
    }
    output.close();
    if (!output) {
        error = "Controller bindings could not be written completely.";
        return false;
    }
#if defined(_WIN32)
    if (!MoveFileExW(staging.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "Controller bindings could not replace the previous file.";
        return false;
    }
#else
    std::error_code ec;
    std::filesystem::rename(staging, path, ec);
    if (ec) {
        error = ec.message();
        return false;
    }
#endif
    return true;
}
std::uint16_t evaluate_controls(const PlayerBindings& bindings, const ControlSnapshot& state) {
    std::uint16_t result = 0;
    for (unsigned bit = 0; bit < 10; ++bit) {
        const auto key = unsigned(bindings.keyboard[bit]);
        const bool keyboard = bindings.keyboard_enabled && key > 0 && key < state.keyboard.size() &&
                              state.keyboard[key];
        const bool pad = bindings.pad_slot >= 0 && (down(bindings.gamepad[bit][0], state) ||
                                                    down(bindings.gamepad[bit][1], state));
        if (keyboard || pad)
            result |= 1u << bit;
    }
    return result;
}
ControlSnapshot capture_controls(const bool* keyboard, SDL_Gamepad* pad) {
    ControlSnapshot result;
    if (keyboard)
        std::copy_n(keyboard, result.keyboard.size(), result.keyboard.begin());
    if (pad && SDL_GamepadConnected(pad)) {
        for (unsigned i = 0; i < result.buttons.size(); ++i)
            result.buttons[i] = SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(i));
        for (unsigned i = 0; i < result.axes.size(); ++i)
            result.axes[i] = SDL_GetGamepadAxis(pad, static_cast<SDL_GamepadAxis>(i));
    }
    return result;
}
std::uint16_t sample_controls(const PlayerBindings& bindings, const bool* keyboard,
                              SDL_Gamepad* pad) {
    return evaluate_controls(bindings, capture_controls(keyboard, pad));
}
std::string binding_name(PadBinding binding) {
    if (binding.kind == PadBindingKind::None)
        return "Unbound";
    if (binding.kind == PadBindingKind::Button) {
        if (binding.index == SDL_GAMEPAD_BUTTON_SOUTH)
            return "South / A";
        if (binding.index == SDL_GAMEPAD_BUTTON_EAST)
            return "East / B";
        if (binding.index == SDL_GAMEPAD_BUTTON_WEST)
            return "West / X";
        if (binding.index == SDL_GAMEPAD_BUTTON_NORTH)
            return "North / Y";
        if (const auto* name =
                SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(binding.index)))
            return name;
    } else if (const auto* name =
                   SDL_GetGamepadStringForAxis(static_cast<SDL_GamepadAxis>(binding.index)))
        return std::string(name) + (binding.kind == PadBindingKind::AxisPositive ? " +" : " -");
    return "Invalid binding";
}
std::string key_binding_name(SDL_Scancode key) {
    if (key == SDL_SCANCODE_UNKNOWN)
        return "Unbound";
    const auto* name = SDL_GetScancodeName(key);
    return name && *name ? name : "Unknown key";
}
std::filesystem::path controls_path() {
    const auto* base = SDL_GetBasePath();
    return base ? std::filesystem::u8path(base) / "controls.ini"
                : std::filesystem::path("controls.ini");
}
void InputDevices::refresh() {
    if (!SDL_WasInit(SDL_INIT_GAMEPAD))
        return;
    for (auto& seat : seats_)
        if (seat.handle && !SDL_GamepadConnected(seat.handle)) {
            SDL_CloseGamepad(seat.handle);
            seat = {};
        }
    int count = 0;
    auto* ids = SDL_GetGamepads(&count);
    for (int i = 0; i < count; ++i) {
        if (std::any_of(seats_.begin(), seats_.end(),
                        [id = ids[i]](const auto& seat) { return seat.id == id; }))
            continue;
        const auto empty = std::find_if(seats_.begin(), seats_.end(),
                                        [](const auto& seat) { return !seat.handle; });
        if (empty == seats_.end())
            break;
        if (auto* pad = SDL_OpenGamepad(ids[i]))
            *empty = {pad, ids[i]};
    }
    SDL_free(ids);
}
void InputDevices::close() {
    for (auto& seat : seats_) {
        if (seat.handle && SDL_WasInit(SDL_INIT_GAMEPAD))
            SDL_CloseGamepad(seat.handle);
        seat = {};
    }
}
} // namespace seed
