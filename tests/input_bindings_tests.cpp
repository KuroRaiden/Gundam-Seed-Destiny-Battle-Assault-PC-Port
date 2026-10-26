#include "input_bindings.h"
#include <cstdio>
#include <fstream>
#include <stdexcept>

void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Usage: seed_controls_tests SCRATCH_DIRECTORY");
        const auto root = std::filesystem::path(argv[1]) / L"bindings 日本語";
        std::filesystem::create_directories(root);
        seed::ControlBindings config;
        seed::ControlSnapshot input;
        input.keyboard[SDL_SCANCODE_X] = true;
        require(seed::evaluate_controls(config.players[0], input) == 1,
                "P1 default confirm changed");
        require(seed::evaluate_controls(config.players[1], input) == 0, "P1 key leaked into P2");
        config.players[0].keyboard[0] = SDL_SCANCODE_Q;
        require(seed::evaluate_controls(config.players[0], input) == 0,
                "Old keyboard binding remained active");
        input.keyboard[SDL_SCANCODE_Q] = true;
        require(seed::evaluate_controls(config.players[0], input) == 1,
                "New keyboard binding was ignored");
        config.players[0].keyboard_enabled = false;
        require(seed::evaluate_controls(config.players[0], input) == 0,
                "Disabled keyboard still produced input");
        input = {};
        input.buttons[SDL_GAMEPAD_BUTTON_SOUTH] = true;
        config.players[0].gamepad[0][0] = {seed::PadBindingKind::Button, SDL_GAMEPAD_BUTTON_NORTH};
        require(seed::evaluate_controls(config.players[0], input) == 0,
                "Old gamepad button remained active");
        input.buttons[SDL_GAMEPAD_BUTTON_NORTH] = true;
        require(seed::evaluate_controls(config.players[0], input) == 1,
                "New gamepad button was ignored");
        config.players[0].gamepad[0][0] = {};
        config.players[0].gamepad[0][1] = {seed::PadBindingKind::AxisPositive,
                                           SDL_GAMEPAD_AXIS_RIGHT_TRIGGER};
        input = {};
        input.axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] = 22000;
        require(seed::evaluate_controls(config.players[0], input) == 1, "Trigger remapping failed");
        input.axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] = 12000;
        require(seed::evaluate_controls(config.players[0], input) == 0,
                "Axis threshold was ignored");
        input = {};
        input.axes[SDL_GAMEPAD_AXIS_LEFTX] = -22000;
        require(seed::evaluate_controls(config.players[0], input) == 32,
                "Alternate stick direction failed");
        config.players[0].pad_slot = -1;
        require(seed::evaluate_controls(config.players[0], input) == 0,
                "Disabled gamepad still produced input");
        config.players[1].keyboard[1] = SDL_SCANCODE_E;
        std::string error;
        require(config.save(root / L"controls.ini", error), "Bindings save failed");
        seed::ControlBindings restored;
        require(restored.load(root / L"controls.ini") && restored == config,
                "Bindings did not survive persistence");
        config.players[1].pad_slot = 0;
        require(config.save(root / L"controls.ini", error), "Bindings replacement failed");
        require(restored.load(root / L"controls.ini") && restored == config,
                "Existing bindings were not replaced");
        {
            std::ofstream invalid(root / L"invalid.ini");
            invalid << "version 1\np1.key.A 999999\np2.pad1.B 1 999999\np1.pad -40\n";
        }
        require(restored.load(root / L"invalid.ini") && restored == seed::ControlBindings{},
                "Invalid fields did not preserve safe defaults");
        {
            std::ofstream invalid(root / L"future.ini");
            invalid << "version 99\np1.key.A 20\n";
        }
        require(!restored.load(root / L"future.ini") && restored == seed::ControlBindings{},
                "Unsupported config version was used");
        require(SDL_Init(SDL_INIT_GAMEPAD), "SDL gamepad initialization failed");
        SDL_VirtualJoystickDesc desc{};
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.name = "SEED remap validation";
        const auto first = SDL_AttachVirtualJoystick(&desc),
                   second = SDL_AttachVirtualJoystick(&desc);
        auto* j1 = SDL_OpenJoystick(first);
        auto* j2 = SDL_OpenJoystick(second);
        auto* p1 = SDL_OpenGamepad(first);
        auto* p2 = SDL_OpenGamepad(second);
        require(j1 && j2 && p1 && p2, "Virtual gamepads failed");
        for (auto* joystick : {j1, j2}) {
            SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768);
            SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
        }
        seed::ControlBindings virtual_config;
        virtual_config.players[0].gamepad[0][0] = {seed::PadBindingKind::Button,
                                                   SDL_GAMEPAD_BUTTON_WEST};
        virtual_config.players[1].gamepad[1][0] = {seed::PadBindingKind::Button,
                                                   SDL_GAMEPAD_BUTTON_NORTH};
        virtual_config.players[1].gamepad[0][1] = {seed::PadBindingKind::AxisPositive,
                                                   SDL_GAMEPAD_AXIS_RIGHT_TRIGGER};
        require(virtual_config.save(root / L"virtual.ini", error), "Virtual bindings save failed");
        require(restored.load(root / L"virtual.ini"), "Virtual bindings reload failed");
        SDL_SetJoystickVirtualButton(j1, SDL_GAMEPAD_BUTTON_SOUTH, true);
        SDL_UpdateJoysticks();
        require(seed::sample_controls(restored.players[0], nullptr, p1) == 0,
                "Old pad button remained active");
        SDL_SetJoystickVirtualButton(j1, SDL_GAMEPAD_BUTTON_SOUTH, false);
        SDL_SetJoystickVirtualButton(j1, SDL_GAMEPAD_BUTTON_WEST, true);
        SDL_UpdateJoysticks();
        require(seed::sample_controls(restored.players[0], nullptr, p1) == 1 &&
                    seed::sample_controls(restored.players[1], nullptr, p2) == 0,
                "Remapped P1 pad leaked into P2");
        SDL_SetJoystickVirtualButton(j2, SDL_GAMEPAD_BUTTON_NORTH, true);
        SDL_UpdateJoysticks();
        require(seed::sample_controls(restored.players[1], nullptr, p2) == 2,
                "Remapped P2 pad button failed");
        SDL_SetJoystickVirtualButton(j2, SDL_GAMEPAD_BUTTON_NORTH, false);
        SDL_SetJoystickVirtualAxis(j2, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767);
        SDL_UpdateJoysticks();
        require(seed::sample_controls(restored.players[1], nullptr, p2) == 1,
                "Remapped physical trigger sample failed");
        SDL_CloseGamepad(p1);
        SDL_CloseGamepad(p2);
        SDL_CloseJoystick(j1);
        SDL_CloseJoystick(j2);
        SDL_DetachVirtualJoystick(first);
        SDL_DetachVirtualJoystick(second);
        SDL_Quit();
        std::puts("PASS: per-player remapping, old-binding removal, axes, disabled sources, "
                  "Unicode/atomic persistence, invalid configs and two real SDL virtual gamepads.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
