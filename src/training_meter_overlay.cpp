#include "training_meter_overlay.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "training_frame_meter.h"
#include "training_tools.h"
#include <algorithm>
#include <cstdio>

namespace seed {
namespace {
ImU32 color(FramePhase phase) {
    switch (phase) {
    case FramePhase::Startup:
        return IM_COL32(41, 190, 116, 255);
    case FramePhase::Active:
        return IM_COL32(246, 71, 80, 255);
    case FramePhase::Recovery:
        return IM_COL32(24, 135, 222, 255);
    case FramePhase::Hitstun:
        return IM_COL32(251, 208, 55, 255);
    case FramePhase::Gap:
        return IM_COL32(128, 133, 144, 255);
    case FramePhase::Hitstop:
        return IM_COL32(220, 224, 235, 255);
    case FramePhase::Blockstun:
        return IM_COL32(106, 100, 126, 255);
    case FramePhase::Unknown:
        return IM_COL32(80, 66, 70, 255);
    default:
        return IM_COL32(44, 51, 64, 255);
    }
}
void row(unsigned seat, float width) {
    const auto& meter = training_frame_meter();
    const auto& summary = meter.summary(seat);
    char startup[24] = "--";
    if (summary.startup)
        std::snprintf(startup, sizeof(startup), "%u", *summary.startup);
    ImGui::Text("%s   Startup %s  Active %u  Recovery %u  Hitstun %u%s",
                seat ? "Opponent" : "Player", startup, summary.active, summary.recovery,
                summary.hitstun, summary.interrupted ? "  (interrupted)" : "");
    const auto origin = ImGui::GetCursorScreenPos();
    auto* draw = ImGui::GetWindowDrawList();
    const auto columns = std::clamp(int(width / 6), 30, 120);
    const auto visible = std::min<std::size_t>(meter.size(), columns);
    const auto first = meter.size() - visible;
    const float step = width / columns;
    for (int column = 0; column < columns; ++column) {
        const auto fill = column < int(visible) ? color(meter.cell(seat, first + column).phase)
                                                : color(FramePhase::Idle);
        const float x = origin.x + column * step;
        draw->AddRectFilled({x, origin.y}, {x + std::max(1.f, step - 1), origin.y + 9}, fill);
        if (column < int(visible) && meter.cell(seat, first + column).projectile)
            draw->AddRectFilled({x, origin.y + 11}, {x + std::max(1.f, step - 1), origin.y + 13},
                                color(FramePhase::Active));
        if (column % 10 == 0)
            draw->AddLine({x, origin.y + 14}, {x, origin.y + 17}, IM_COL32(150, 158, 174, 180));
    }
    ImGui::Dummy({width, 19});
}
} // namespace
TrainingMeterOverlay::~TrainingMeterOverlay() {
    shutdown();
}
void TrainingMeterOverlay::initialize(SDL_Window* window, SDL_Renderer* renderer) {
    window_ = window;
    renderer_ = renderer;
}
bool TrainingMeterOverlay::prepare() {
    if (context_)
        return true;
    auto* previous = ImGui::GetCurrentContext();
    context_ = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().LogFilename = nullptr;
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowPadding = {10, 8};
    style.ItemSpacing = {4, 3};
    style.FontSizeBase = 13;
    const bool platform = ImGui_ImplSDL3_InitForSDLRenderer(window_, renderer_);
    const bool renderer = platform && ImGui_ImplSDLRenderer3_Init(renderer_);
    if (!renderer) {
        if (platform)
            ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext(context_);
        context_ = nullptr;
    }
    ImGui::SetCurrentContext(previous);
    return renderer;
}
void TrainingMeterOverlay::shutdown() {
    if (!context_)
        return;
    auto* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(context_);
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(context_);
    ImGui::SetCurrentContext(previous == context_ ? nullptr : previous);
    context_ = nullptr;
}
bool TrainingMeterOverlay::event(const SDL_Event& event, bool online) {
    auto& meter = training_frame_meter();
    if (online || !meter.allowed() ||
        (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP) ||
        event.key.scancode != SDL_SCANCODE_F5)
        return false;
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
        meter.toggle();
    return true;
}
void TrainingMeterOverlay::draw(bool online) {
    if (online ||
        (!training_frame_meter().visible() && !training_boxes_visible() &&
         training_tools_message().empty()) ||
        !prepare())
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
    const float width = std::max(220.f, std::min(960.f, view->Size.x - 16));
    if (training_boxes_visible() || !training_tools_message().empty()) {
        ImGui::SetNextWindowPos({view->Pos.x + 8, view->Pos.y + 8}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(.9f);
        ImGui::Begin("Training tools", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize);
        if (training_boxes_visible()) {
            ImGui::TextColored({1, .22f, .28f, 1}, "Red: hitboxes");
            ImGui::SameLine();
            ImGui::TextColored({.24f, .94f, .45f, 1}, "Green: hurtboxes");
            ImGui::TextUnformatted("F6: hide  |  F2: save  |  F3: load");
        }
        if (!training_tools_message().empty())
            ImGui::TextUnformatted(training_tools_message().c_str());
        ImGui::End();
    }
    if (training_frame_meter().visible()) {
        ImGui::SetNextWindowPos({view->Pos.x + view->Size.x * .5f, view->Pos.y + view->Size.y - 6},
                                ImGuiCond_Always, {.5f, 1});
        ImGui::SetNextWindowSize({width, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(.94f);
        ImGui::Begin("Training frame meter / F5", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::TextWrapped("TRAINING FRAME METER   F5: hide   1 square = 1 game frame");
        row(0, ImGui::GetContentRegionAvail().x);
        row(1, ImGui::GetContentRegionAvail().x);
        const struct {
            FramePhase phase;
            const char* label;
        } legend[] = {{FramePhase::Startup, "Startup"},
                      {FramePhase::Active, "Active"},
                      {FramePhase::Recovery, "Recovery"},
                      {FramePhase::Hitstun, "Hitstun"}};
        for (unsigned n = 0; n < 4; ++n) {
            if (n)
                ImGui::SameLine();
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color(legend[n].phase)), "%s",
                               legend[n].label);
        }
        ImGui::TextWrapped("Grey: gap/idle | White: hitstop | Thin red: projectile "
                           "| Startup includes first active");
        ImGui::End();
    }
    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
    SDL_SetRenderLogicalPresentation(renderer_, lw, lh, logical);
    SDL_SetRenderScale(renderer_, sx, sy);
    SDL_SetRenderViewport(renderer_, &viewport);
    ImGui::SetCurrentContext(previous);
}
} // namespace seed
