#include "in_game_remap.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace seed {
InGameRemap::~InGameRemap() {
    shutdown();
}
void InGameRemap::initialize(SDL_Window* window, SDL_Renderer* renderer, LiveControls* controls) {
    window_ = window;
    renderer_ = renderer;
    controls_ = controls;
    auto* previous = ImGui::GetCurrentContext();
    context_ = ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 10;
    style.FrameRounding = 4;
    style.WindowPadding = {18, 16};
    style.FramePadding = {8, 6};
    style.ItemSpacing = {8, 7};
    style.FontSizeBase = 16;
    style.Colors[ImGuiCol_WindowBg] = {.045f, .06f, .085f, .98f};
    if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer) ||
        !ImGui_ImplSDLRenderer3_Init(renderer)) {
        ImGui::SetCurrentContext(previous);
        throw std::runtime_error("In-game controls UI initialization failed");
    }
    ImGui::SetCurrentContext(previous);
}
void InGameRemap::shutdown() {
    if (!context_)
        return;
    if (controls_)
        controls_->overlay_open = false;
    auto* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(context_);
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(context_);
    ImGui::SetCurrentContext(previous == context_ ? nullptr : previous);
    context_ = nullptr;
    open_ = false;
}
bool InGameRemap::save_and_close() {
    if (edited_ != controls_->bindings && !controls_->apply(edited_, error_))
        return false;
    open_ = controls_->overlay_open = false;
    controls_->release_guard();
    capture_ = {};
    return true;
}
void InGameRemap::toggle() {
    if (open_) {
        save_and_close();
        return;
    }
    edited_ = controls_->bindings;
    error_.clear();
    capture_ = {};
    open_ = controls_->overlay_open = true;
    controls_->release_guard();
    ImGui::GetIO().ClearInputKeys();
}
void InGameRemap::begin_capture(unsigned player, unsigned bit, int slot) {
    capture_ = {int(player), bit, slot, SDL_GetTicks() + 180, false};
}
bool InGameRemap::capture_event(const SDL_Event& event) {
    if (capture_.player < 0)
        return false;
    const bool key = event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP;
    const bool pad = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
                     event.type == SDL_EVENT_GAMEPAD_BUTTON_UP ||
                     event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION;
    if (!key && !pad)
        return false;
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE) {
        capture_ = {};
        return true;
    }
    if (!capture_.neutral || SDL_GetTicks() < capture_.armed_at)
        return true;
    auto& profile = edited_.players[capture_.player];
    if (capture_.slot < 0 && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
        event.key.scancode != SDL_SCANCODE_F4 && event.key.scancode != SDL_SCANCODE_F1) {
        profile.keyboard[capture_.bit] = event.key.scancode;
        capture_ = {};
    } else if (capture_.slot >= 0) {
        const auto id = controls_->devices.id(profile.pad_slot);
        if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && event.gbutton.which == id) {
            profile.gamepad[capture_.bit][capture_.slot] = {PadBindingKind::Button,
                                                            event.gbutton.button};
            capture_ = {};
        } else if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && event.gaxis.which == id &&
                   std::abs(int(event.gaxis.value)) > 22000) {
            profile.gamepad[capture_.bit][capture_.slot] = {
                event.gaxis.value > 0 ? PadBindingKind::AxisPositive : PadBindingKind::AxisNegative,
                event.gaxis.axis};
            capture_ = {};
        }
    }
    return true;
}
bool InGameRemap::event(const SDL_Event& event, bool) {
    if (!context_)
        return false;
    auto* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(context_);
    const bool was_open = open_;
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_F4 &&
        !event.key.repeat)
        toggle();
    else if (open_) {
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE &&
            capture_.player < 0)
            save_and_close();
        else if (!capture_event(event) || event.type == SDL_EVENT_KEY_UP ||
                 event.type == SDL_EVENT_GAMEPAD_BUTTON_UP)
            ImGui_ImplSDL3_ProcessEvent(&event);
    }
    ImGui::SetCurrentContext(previous);
    const bool input =
        event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP ||
        event.type == SDL_EVENT_TEXT_INPUT || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        event.type == SDL_EVENT_MOUSE_BUTTON_UP || event.type == SDL_EVENT_MOUSE_MOTION ||
        event.type == SDL_EVENT_MOUSE_WHEEL || event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
        event.type == SDL_EVENT_GAMEPAD_BUTTON_UP || event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION;
    return input && (was_open || open_);
}
void InGameRemap::draw_bindings() {
    if (!ImGui::BeginTabBar("players"))
        return;
    for (unsigned p = 0; p < 2; ++p)
        if (ImGui::BeginTabItem(p ? "Player 2" : "Player 1")) {
            ImGui::PushID(int(p));
            auto& profile = edited_.players[p];
            ImGui::Checkbox("Keyboard", &profile.keyboard_enabled);
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset player"))
                profile = ControlBindings{}.players[p];
            std::array<std::string, 3> choices{"No gamepad", "Gamepad 1", "Gamepad 2"};
            for (int slot = 0; slot < 2; ++slot)
                if (auto* pad = controls_->devices.pad(slot))
                    choices[slot + 1] += " - " + std::string(SDL_GetGamepadName(pad));
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##pad", choices[profile.pad_slot + 1].c_str())) {
                for (int slot = -1; slot < 2; ++slot)
                    if (ImGui::Selectable(choices[slot + 1].c_str(), slot == profile.pad_slot)) {
                        const int old = profile.pad_slot;
                        if (slot >= 0 && edited_.players[1 - p].pad_slot == slot)
                            edited_.players[1 - p].pad_slot = old;
                        profile.pad_slot = slot;
                    }
                ImGui::EndCombo();
            }
            ImGui::TextDisabled("Click to remap. Right-click to clear.");
            if (ImGui::BeginTable("bindings", 4,
                                  ImGuiTableFlags_BordersInnerH |
                                      ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 58);
                ImGui::TableSetupColumn("Keyboard");
                ImGui::TableSetupColumn("Gamepad");
                ImGui::TableSetupColumn("Alternate");
                ImGui::TableHeadersRow();
                for (const auto bit : {6u, 7u, 5u, 4u, 0u, 1u, 8u, 9u, 3u, 2u}) {
                    ImGui::PushID(int(bit));
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(kControlNames[bit]);
                    ImGui::TableNextColumn();
                    if (ImGui::Button(key_binding_name(profile.keyboard[bit]).c_str(), {-1, 0}))
                        begin_capture(p, bit, -1);
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                        profile.keyboard[bit] = SDL_SCANCODE_UNKNOWN;
                    for (unsigned slot = 0; slot < 2; ++slot) {
                        ImGui::TableNextColumn();
                        ImGui::PushID(int(slot));
                        ImGui::BeginDisabled(profile.pad_slot < 0);
                        if (ImGui::Button(binding_name(profile.gamepad[bit][slot]).c_str(),
                                          {-1, 0}))
                            begin_capture(p, bit, slot);
                        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                            profile.gamepad[bit][slot] = {};
                        ImGui::EndDisabled();
                        ImGui::PopID();
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::PopID();
            ImGui::EndTabItem();
        }
    ImGui::EndTabBar();
}
void InGameRemap::draw_capture() {
    if (capture_.player >= 0 && !ImGui::IsPopupOpen("Remap input"))
        ImGui::OpenPopup("Remap input");
    if (!ImGui::BeginPopupModal("Remap input", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    if (capture_.player < 0)
        ImGui::CloseCurrentPopup();
    else {
        auto* pad = controls_->devices.pad(edited_.players[capture_.player].pad_slot);
        ImGui::Text("Player %d / %s", capture_.player + 1, kControlNames[capture_.bit]);
        ImGui::TextUnformatted(capture_.slot < 0
                                   ? "Release keys, then press a key."
                                   : "Release controls, then press a button or move a stick.");
        if (capture_.slot >= 0 && !pad)
            ImGui::TextUnformatted("Connect the selected gamepad.");
        if (!capture_.neutral) {
            const auto state =
                capture_controls(capture_.slot < 0 ? SDL_GetKeyboardState(nullptr) : nullptr,
                                 capture_.slot >= 0 ? pad : nullptr);
            capture_.neutral = std::none_of(state.keyboard.begin(), state.keyboard.end(),
                                            [](bool v) { return v; }) &&
                               std::none_of(state.buttons.begin(), state.buttons.end(),
                                            [](bool v) { return v; }) &&
                               std::none_of(state.axes.begin(), state.axes.end(),
                                            [](auto v) { return std::abs(int(v)) > 12000; }) &&
                               (capture_.slot < 0 || pad);
        }
        ImGui::TextDisabled("Esc cancels. F1 and F4 are reserved.");
        if (ImGui::Button("Cancel")) {
            capture_ = {};
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
}
void InGameRemap::draw(bool online) {
    if (!open_ || !context_)
        return;
    auto* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(context_);
    controls_->devices.refresh();
    int lw = 0, lh = 0;
    SDL_RendererLogicalPresentation logical;
    SDL_GetRenderLogicalPresentation(renderer_, &lw, &lh, &logical);
    float sx = 1, sy = 1;
    SDL_GetRenderScale(renderer_, &sx, &sy);
    SDL_Rect viewport{};
    SDL_GetRenderViewport(renderer_, &viewport);
    SDL_SetRenderLogicalPresentation(renderer_, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_SetRenderScale(renderer_, 1, 1);
    SDL_SetRenderViewport(renderer_, nullptr);
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    const auto* view = ImGui::GetMainViewport();
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        view->Pos, {view->Pos.x + view->Size.x, view->Pos.y + view->Size.y},
        IM_COL32(0, 0, 0, 145));
    ImGui::SetNextWindowPos({view->Pos.x + view->Size.x * .5f, view->Pos.y + view->Size.y * .5f},
                            ImGuiCond_Always, {.5f, .5f});
    ImGui::SetNextWindowSize(
        {std::min(760.f, view->Size.x - 20), std::min(660.f, view->Size.y - 20)}, ImGuiCond_Always);
    ImGui::Begin("Controls / F4", nullptr,
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse);
    ImGui::TextColored(online ? ImVec4{1, .75f, .35f, 1} : ImVec4{.35f, .85f, .72f, 1}, "%s",
                       online ? "ONLINE - MATCH RUNNING" : "OFFLINE - GAME PAUSED");
    ImGui::TextWrapped("Your inputs are held neutral while editing. Changes apply when you close.");
    if (ImGui::Button("Save and close", {150, 0}))
        save_and_close();
    ImGui::SameLine();
    if (ImGui::Button("Discard changes")) {
        edited_ = controls_->bindings;
        open_ = controls_->overlay_open = false;
        controls_->release_guard();
        capture_ = {};
    }
    ImGui::SameLine();
    ImGui::TextDisabled("F4 / Esc: save and close");
    if (!error_.empty())
        ImGui::TextWrapped("%s", error_.c_str());
    ImGui::Separator();
    draw_bindings();
    draw_capture();
    ImGui::End();
    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
    SDL_SetRenderLogicalPresentation(renderer_, lw, lh, logical);
    SDL_SetRenderScale(renderer_, sx, sy);
    SDL_SetRenderViewport(renderer_, &viewport);
    ImGui::SetCurrentContext(previous);
}
} // namespace seed
