#pragma once
#include "imgui.h"
#include "input_bindings.h"
#include <algorithm>
#include <cstdlib>

namespace seed {
inline void draw_gamepad_picker(ControlBindings& bindings, unsigned player,
                                const InputDevices& devices) {
    const auto label_for = [&](int slot) {
        if (slot < 0)
            return std::string("No gamepad");
        auto label = "Gamepad " + std::to_string(static_cast<unsigned>(slot) + 1);
        if (auto* pad = devices.pad(slot)) {
            if (const auto* name = SDL_GetGamepadName(pad); name && *name)
                label += " - " + std::string(name);
        } else
            label += " (not connected)";
        return label;
    };
    const int selected = bindings.players[player].pad_slot;
    const auto preview = label_for(selected);
    ImGui::SetNextItemWidth(-1);
    if (!ImGui::BeginCombo("##gamepad", preview.c_str()))
        return;
    ImGui::TextDisabled("Hold a button to identify its gamepad.");
    for (int slot = -1; slot < std::max(2, devices.slot_count()); ++slot) {
        auto label = label_for(slot);
        if (auto* pad = devices.pad(slot)) {
            const auto state = capture_controls(nullptr, pad);
            if (std::any_of(state.buttons.begin(), state.buttons.end(),
                            [](bool held) { return held; }) ||
                std::any_of(state.axes.begin(), state.axes.end(),
                            [](auto value) { return std::abs(int(value)) > 16000; }))
                label += " [input detected]";
        }
        label += "###device";
        ImGui::PushID(slot);
        if (ImGui::Selectable(label.c_str(), selected == slot))
            bindings.assign_gamepad(player, slot);
        ImGui::PopID();
    }
    ImGui::EndCombo();
}
} // namespace seed
