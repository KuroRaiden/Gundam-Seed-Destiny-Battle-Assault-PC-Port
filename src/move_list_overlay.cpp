#include "move_list_overlay.h"
#include "move_list.h"
#include "live_controls.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include <algorithm>
#include <filesystem>
#include <string>
#include <stdexcept>
namespace seed {
namespace {
constexpr const char* glyph_names[]{"switch_dpad_up",    "switch_dpad_down", "switch_dpad_left",
                                    "switch_dpad_right", "switch_button_a",  "switch_button_b",
                                    "switch_button_l",   "switch_button_r"};
constexpr std::string_view tokens = "UDBFablr";
bool input_event(Uint32 type) {
    return type == SDL_EVENT_KEY_DOWN || type == SDL_EVENT_KEY_UP || type == SDL_EVENT_TEXT_INPUT ||
           type == SDL_EVENT_MOUSE_BUTTON_DOWN || type == SDL_EVENT_MOUSE_BUTTON_UP ||
           type == SDL_EVENT_MOUSE_MOTION || type == SDL_EVENT_MOUSE_WHEEL ||
           type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || type == SDL_EVENT_GAMEPAD_BUTTON_UP ||
           type == SDL_EVENT_GAMEPAD_AXIS_MOTION;
}
} // namespace
MoveListOverlay::~MoveListOverlay() {
    shutdown();
}
void MoveListOverlay::initialize(SDL_Window* window, SDL_Renderer* renderer,
                                 LiveControls* controls) {
    window_ = window;
    renderer_ = renderer;
    controls_ = controls;
    auto* previous = ImGui::GetCurrentContext();
    context_ = ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 9;
    style.FrameRounding = 4;
    style.WindowPadding = {16, 14};
    style.Colors[ImGuiCol_WindowBg] = {.035f, .05f, .075f, .99f};
    if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer) ||
        !ImGui_ImplSDLRenderer3_Init(renderer))
        throw std::runtime_error("Movelist UI initialization failed");
    const auto folder = std::filesystem::path(SDL_GetBasePath()) / "resource/input-prompts";
    for (unsigned i = 0; i < glyphs_.size(); ++i) {
        auto* image =
            SDL_LoadPNG((folder / (std::string(glyph_names[i]) + ".png")).string().c_str());
        if (!image)
            continue; // A missing resource remains readable using its button label.
        glyphs_[i] = SDL_CreateTextureFromSurface(renderer, image);
        SDL_DestroySurface(image);
        if (glyphs_[i])
            SDL_SetTextureScaleMode(glyphs_[i], SDL_SCALEMODE_LINEAR);
    }
    ImGui::SetCurrentContext(previous);
}
void MoveListOverlay::close() {
    open_ = false;
    if (controls_) {
        controls_->overlay_open = false;
        controls_->release_guard();
    }
}
void MoveListOverlay::shutdown() {
    if (!context_)
        return;
    if (open_)
        close();
    for (auto*& texture : glyphs_) {
        if (texture)
            SDL_DestroyTexture(texture);
        texture = nullptr;
    }
    auto* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(context_);
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(context_);
    ImGui::SetCurrentContext(previous == context_ ? nullptr : previous);
    context_ = nullptr;
}
bool MoveListOverlay::event(const SDL_Event& event) {
    if (!context_)
        return false;
    auto* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(context_);
    const bool was_open = open_;
    const bool f1 = (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) &&
                    event.key.scancode == SDL_SCANCODE_F1;
    if (f1 && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        if (open_)
            close();
        else {
            fighter_ = movelist_default_fighter();
            open_ = true;
            if (controls_) {
                controls_->overlay_open = true;
                controls_->release_guard();
            }
            ImGui::GetIO().ClearInputKeys();
        }
    } else if (open_) {
        if ((event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE) ||
            (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
             event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST))
            close();
        else
            ImGui_ImplSDL3_ProcessEvent(&event);
    }
    ImGui::SetCurrentContext(previous);
    return f1 || (input_event(event.type) && (was_open || open_));
}
void MoveListOverlay::input(const char* command) {
    bool first = true;
    for (const char* c = command; *c; ++c) {
        if (!first) {
            ImGui::SameLine(0, 5);
            ImGui::TextUnformatted(*c == '>' ? ">" : "+");
            ImGui::SameLine(0, 5);
        }
        if (*c == '>') {
            first = true;
            continue;
        }
        const auto index = tokens.find(*c);
        if (index != std::string_view::npos && glyphs_[index]) {
            const float w = glyph_size_ * (index >= 6 ? 1.25f : 1.f);
            ImGui::Image((ImTextureID)(intptr_t)glyphs_[index], {w, glyph_size_});
        } else {
            constexpr const char* labels[]{"Up", "Down", "Back", "Forward", "A", "B", "L", "R"};
            ImGui::TextUnformatted(index < 8 ? labels[index] : "?");
        }
        first = false;
    }
}
void MoveListOverlay::draw(bool online) {
    if (!open_ || !context_)
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
    const auto screen = ImGui::GetIO().DisplaySize;
    const ImVec2 size{std::min(900.f, screen.x - 16), std::min(720.f, screen.y - 16)};
    ImGui::GetStyle().FontSizeBase = screen.x < 650 ? 13.f : 16.f;
    glyph_size_ = screen.y < 480 ? 24.f : 30.f;
    ImGui::SetNextWindowPos({screen.x / 2, screen.y / 2}, ImGuiCond_Always, {.5f, .5f});
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    bool shown = true;
    ImGui::Begin("Movelist / F1", &shown, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::TextColored({.25f, .85f, .95f, 1}, "SEED DESTINY  /  SIMPLIFIED CONTROLS");
    ImGui::TextWrapped(
        "Directions face right. A / B / L / R are game buttons, independent of your remapping.");
    if (!movelist_japanese())
        ImGui::TextDisabled(
            "JP roster reference. Enter Local VS, Online VS or Training to use these moves.");
    const auto ids = movelist_fighters();
    for (unsigned seat = 0; seat < 2; ++seat) {
        if (seat)
            ImGui::SameLine();
        const auto label = std::string(seat ? "Player 2" : "Player 1") + "##fighter";
        if (ImGui::Button(label.c_str()) && fighter_moves(ids[seat]))
            fighter_ = ids[seat];
    }
    const auto* selected = fighter_moves(fighter_);
    const auto label = std::string(selected->name) + " - " + std::string(selected->pilot);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##roster", label.c_str())) {
        for (const auto& fighter : fighter_movelist()) {
            const auto name = std::string(fighter.name) + " - " + std::string(fighter.pilot);
            if (ImGui::Selectable(name.c_str(), fighter_ == fighter.id))
                fighter_ = fighter.id;
        }
        ImGui::EndCombo();
    }
    selected = fighter_moves(fighter_);
    ImGui::Text("Berserker type: %s", selected->berserker.data());
    ImGui::Separator();
    ImGui::BeginChild("commands", {0, -70});
    ImGui::TextDisabled("COMMON INPUTS");
    for (const auto command : {"a", "b", "r", "Dl"}) {
        input(command);
        if (std::string_view(command) == "Dl") {
            ImGui::SameLine();
            ImGui::TextDisabled("Berserker / Ultimate");
        }
    }
    ImGui::Separator();
    for (const auto& move : selected->moves) {
        input(move.command.data());
        if (move.air != MoveAir::Ground) {
            ImGui::SameLine();
            ImGui::TextUnformatted(move.air == MoveAir::Only ? "(AIR)" : " (AIR OK)");
        }
        if (!move.condition.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", move.condition.data());
        }
    }
    ImGui::EndChild();
    ImGui::Separator();
    ImGui::TextDisabled("Inputs: kira_yamato_88 / GameFAQs 34177. Glyphs: Kenney (CC0).");
    ImGui::TextWrapped(online ? "Online play continues. F1 / Esc: close"
                              : "Game paused. F1 / Esc: close");
    ImGui::End();
    if (!shown)
        close();
    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
    SDL_SetRenderLogicalPresentation(renderer_, lw, lh, logical);
    SDL_SetRenderScale(renderer_, sx, sy);
    SDL_SetRenderViewport(renderer_, &viewport);
    ImGui::SetCurrentContext(previous);
}
} // namespace seed
