#include "online_lobby.h"
#include "invite_window.h"
#include <filesystem>
#include <cstdio>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include <algorithm>
#include <stdexcept>

namespace seed {
OnlineLobby::~OnlineLobby() {
    shutdown();
}
void OnlineLobby::initialize(SDL_Window* window, SDL_Renderer* renderer, LiveControls* controls) {
    window_ = window;
    renderer_ = renderer;
    controls_ = controls;
    auto* previous = ImGui::GetCurrentContext();
    context_ = ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 10;
    style.FrameRounding = 4;
    style.WindowPadding = {20, 18};
    style.FramePadding = {8, 6};
    style.ItemSpacing = {10, 9};
    style.FontSizeBase = 17;
    style.Colors[ImGuiCol_WindowBg] = {.035f, .055f, .085f, .99f};
    if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer) ||
        !ImGui_ImplSDLRenderer3_Init(renderer)) {
        ImGui::SetCurrentContext(previous);
        throw std::runtime_error("Online lobby UI initialization failed");
    }
    ImGui::SetCurrentContext(previous);
}
void OnlineLobby::shutdown() {
    if (!context_)
        return;
    show(nullptr);
    auto* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(context_);
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(context_);
    ImGui::SetCurrentContext(previous == context_ ? nullptr : previous);
    context_ = nullptr;
}
void OnlineLobby::show(OnlineLobbyState* state) {
    state_ = state;
    if (state_ && !state_->player_name[0]) {
        const auto name = load_player_name(std::filesystem::path(SDL_GetBasePath()));
        std::snprintf(state_->player_name.data(), state_->player_name.size(), "%s", name.c_str());
    }
    if (controls_) {
        controls_->overlay_open = is_open();
        controls_->release_guard();
    }
    if (context_) {
        auto* previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(context_);
        ImGui::GetIO().ClearInputKeys();
        ImGui::SetCurrentContext(previous);
    }
    if (!is_open() && window_)
        SDL_StopTextInput(window_);
}
void OnlineLobby::cancel() {
    if (state_)
        state_->cancel();
    if (controls_) {
        controls_->overlay_open = false;
        controls_->release_guard();
    }
    SDL_StopTextInput(window_);
}
bool OnlineLobby::event(const SDL_Event& event) {
    if (!is_open() || !context_)
        return false;
    auto* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(context_);
    if ((event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE &&
         !event.key.repeat) ||
        (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
         event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST))
        cancel();
    else
        ImGui_ImplSDL3_ProcessEvent(&event);
    ImGui::SetCurrentContext(previous);
    return event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP ||
           event.type == SDL_EVENT_TEXT_INPUT || event.type == SDL_EVENT_TEXT_EDITING ||
           event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
           event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_WHEEL ||
           event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
           event.type == SDL_EVENT_GAMEPAD_BUTTON_UP || event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION;
}
void OnlineLobby::draw() {
    if (!is_open() || !context_)
        return;
    auto* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(context_);
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
        IM_COL32(0, 0, 0, 165));
    ImGui::SetNextWindowPos({view->Pos.x + view->Size.x * .5f, view->Pos.y + view->Size.y * .5f},
                            ImGuiCond_Always, {.5f, .5f});
    ImGui::SetNextWindowSize(
        {std::min(690.f, view->Size.x - 16), std::min(550.f, view->Size.y - 16)}, ImGuiCond_Always);
    bool visible = true;
    ImGui::Begin("Online VS", &visible,
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse);
    ImGui::TextColored({.3f, .83f, .94f, 1}, "SEED DESTINY / 24 MOBILE SUITS");
    ImGui::BeginDisabled(state_->hosting);
    ImGui::SetNextItemWidth(260);
    ImGui::InputText("Player name", state_->player_name.data(), state_->player_name.size());
    ImGui::EndDisabled();
    auto save_name = [&] {
        const auto name = normalize_player_name(state_->player_name.data());
        std::snprintf(state_->player_name.data(), state_->player_name.size(), "%s", name.c_str());
        return save_player_name(std::filesystem::path(SDL_GetBasePath()), name, state_->status);
    };
    ImGui::Separator();
    ImGui::TextWrapped(
        "Create a code to host, or paste your opponent's code or invitation link to join.");
    if (!state_->status.empty())
        ImGui::TextWrapped("%s", state_->status.c_str());
    ImGui::Spacing();
    if (ImGui::BeginTabBar("connection")) {
        if (ImGui::BeginTabItem("Host")) {
            if (state_->code.empty()) {
                ImGui::BeginDisabled(state_->hosting);
                if (ImGui::Button("Create code", {170, 36}) && save_name())
                    state_->action = OnlineLobbyAction::Host;
                ImGui::EndDisabled();
            }
            if (!state_->code.empty()) {
                ImGui::Spacing();
                ImGui::TextUnformatted("Your join code");
                ImGui::TextWrapped("%s", state_->code.c_str());
                if (ImGui::Button("Copy code", {140, 36}))
                    SDL_SetClipboardText(state_->code.c_str());
                ImGui::SameLine();
                if (ImGui::Button("Copy link", {140, 36}))
                    SDL_SetClipboardText(state_->invitation.c_str());
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Join")) {
            ImGui::TextWrapped("Paste the host's invitation. Your Player 1 bindings control Player "
                               "2 in the match.");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##invite", "Paste a SEED1 code or invitation link",
                                     state_->invite.data(), state_->invite.size());
            if (ImGui::Button("Join match", {170, 36}) && save_name())
                state_->action = OnlineLobbyAction::Join;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::SmallButton("Enable invitation links on this PC")) {
        register_invite_launcher(std::filesystem::path(SDL_GetBasePath()) /
                                     L"GundamSeedLauncher.exe",
                                 state_->link_status);
    }
    if (!state_->link_status.empty())
        ImGui::TextWrapped("%s", state_->link_status.c_str());
    ImGui::Spacing();
    if (ImGui::Button("Back to main menu", {190, 36}))
        cancel();
    ImGui::SameLine();
    ImGui::TextDisabled("Esc / gamepad B");
    ImGui::End();
    if (!visible)
        cancel();
    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
    SDL_SetRenderLogicalPresentation(renderer_, lw, lh, logical);
    SDL_SetRenderScale(renderer_, sx, sy);
    SDL_SetRenderViewport(renderer_, &viewport);
    ImGui::SetCurrentContext(previous);
}
} // namespace seed
