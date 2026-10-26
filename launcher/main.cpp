#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "imgui_stdlib.h"
#include "platform.h"
#include "settings.h"
#include "../src/controller_db.h"
#include "input_bindings.h"
#include "invite_code.h"
#include "invite_window.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace sl = seed::launcher;
namespace {
constexpr ImVec4 kAccent{0.20f, 0.80f, 0.88f, 1};
constexpr ImVec4 kMuted{0.55f, 0.62f, 0.72f, 1};
constexpr ImVec4 kGreen{0.42f, 0.84f, 0.62f, 1};
constexpr ImVec4 kGold{0.96f, 0.74f, 0.36f, 1};
constexpr ImVec4 kRed{0.96f, 0.48f, 0.48f, 1};
enum class Page { Play, Files, Controllers, About };
struct App {
    sl::Settings settings;
    sl::GameProcess game;
    std::filesystem::path folder;
    seed::InputDevices devices;
    seed::ControlBindings controls, saved_controls;
    struct Capture {
        int player = -1;
        unsigned bit = 0;
        int slot = -1;
        Uint64 armed_at = 0;
        bool neutral = false;
    } capture;
    Page page = Page::Play;
    std::string message, join_code;
    bool error = false;
    SDL_Window* window = nullptr;
    void* native_window = nullptr;
    bool hidden = false;
    seed::ControllerDbResult controller_db;

    void save() {
        if (!settings.save(folder / L"launcher-settings.ini", message))
            error = true;
    }
    void verify_all() {
        for (auto& file : settings.files)
            file.verify(folder);
        message.clear();
        error = false;
        save();
    }
    void refresh_pads() {
        devices.refresh();
    }
    bool save_controls() {
        if (!controls.save(folder / L"controls.ini", message)) {
            error = true;
            return false;
        }
        saved_controls = controls;
        message = "Controller bindings saved.";
        error = false;
        return true;
    }
    void begin_capture(unsigned player, unsigned bit, int slot) {
        capture = {static_cast<int>(player), bit, slot, SDL_GetTicks() + 180, false};
    }
    void launch() {
        if (controls != saved_controls && !save_controls())
            return;
        // Recheck identities immediately before launch, including files changed outside the UI.
        verify_all();
        if (!settings.files[0].verified || !settings.files[1].verified ||
            !settings.files[2].verified) {
            error = true;
            message = "Select and verify both cartridges and the BIOS first.";
            return;
        }
        std::vector<std::wstring> args{L"--rom",
                                       sl::wide(settings.files[0].path),
                                       L"--bios",
                                       sl::wide(settings.files[2].path),
                                       L"--jp-rom",
                                       sl::wide(settings.files[1].path),
                                       L"--save",
                                       (folder / L"game-us.sav").wstring(),
                                       L"--window",
                                       L"--scale",
                                       std::to_wstring(settings.scale)};
        if (!join_code.empty()) {
            args.push_back(L"--join-code");
            args.push_back(sl::wide(join_code));
        }
        if (!game.start(folder / L"GundamSeedRecomp.exe", args, folder / L"last-game.log",
                        message)) {
            error = true;
            return;
        }
        join_code.clear();
        error = false;
        message = "Game running";
        SDL_HideWindow(window);
        hidden = true;
    }
};

void theme() {
    ImGui::StyleColorsDark();
    auto& s = ImGui::GetStyle();
    s.FontSizeBase = 17;
    s.WindowPadding = {24, 24};
    s.FramePadding = {12, 9};
    s.ItemSpacing = {12, 12};
    s.WindowRounding = 0;
    s.ChildRounding = 10;
    s.FrameRounding = 6;
    s.GrabRounding = 5;
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 1;
    s.FrameBorderSize = 0;
    s.Colors[ImGuiCol_WindowBg] = {0.045f, 0.060f, 0.085f, 1};
    s.Colors[ImGuiCol_ChildBg] = {0.075f, 0.095f, 0.13f, 1};
    s.Colors[ImGuiCol_Border] = {0.16f, 0.20f, 0.26f, 1};
    s.Colors[ImGuiCol_FrameBg] = {0.10f, 0.13f, 0.18f, 1};
    s.Colors[ImGuiCol_FrameBgHovered] = {0.15f, 0.20f, 0.26f, 1};
    s.Colors[ImGuiCol_Button] = {0.13f, 0.19f, 0.25f, 1};
    s.Colors[ImGuiCol_ButtonHovered] = {0.18f, 0.30f, 0.37f, 1};
    s.Colors[ImGuiCol_ButtonActive] = {0.12f, 0.38f, 0.44f, 1};
    s.Colors[ImGuiCol_CheckMark] = kAccent;
    s.Colors[ImGuiCol_SliderGrab] = kAccent;
    s.Colors[ImGuiCol_HeaderHovered] = {0.16f, 0.30f, 0.36f, 1};
    s.Colors[ImGuiCol_Text] = {0.89f, 0.92f, 0.97f, 1};
    s.Colors[ImGuiCol_TextDisabled] = kMuted;
}
void title(const char* heading, const char* caption) {
    ImGui::PushFont(nullptr, 30);
    ImGui::TextUnformatted(heading);
    ImGui::PopFont();
    ImGui::TextColored(kMuted, "%s", caption);
    ImGui::Spacing();
}
void card_begin(const char* id, float height) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 18));
    ImGui::BeginChild(id, ImVec2(0, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
}
void card_end() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
}
void primary_begin() {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.11f, 0.53f, 0.61f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.65f, 0.72f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.43f, 0.51f, 1));
}
void primary_end() {
    ImGui::PopStyleColor(3);
}

void play_page(App& app) {
    title("Play", "Launch the game and choose a mode from its main menu.");
    card_begin("brand", 145);
    ImGui::TextColored(kAccent, "GUNDAM SEED");
    ImGui::PushFont(nullptr, 27);
    ImGui::TextUnformatted("DESTINY / BATTLE ASSAULT");
    ImGui::PopFont();
    ImGui::TextColored(kMuted, "PC edition");
    card_end();
    card_begin("baseline", 200);
    ImGui::TextUnformatted("Start playing");
    ImGui::TextWrapped(
        "Choose your game mode in the game. Press F4 during play to remap controls.");
    const bool host = std::filesystem::is_regular_file(app.folder / L"GundamSeedRecomp.exe");
    const bool art =
        std::filesystem::is_regular_file(app.folder / L"assets" / L"destiny-selection.bin");
    const bool combat = std::filesystem::is_regular_file(app.folder / L"SeedJpCombat.dll");
    const bool ready = host && combat && app.settings.files[0].verified &&
                       app.settings.files[1].verified && app.settings.files[2].verified;
    ImGui::BeginDisabled(!ready || app.game.running());
    primary_begin();
    if (ImGui::Button("Launch game", ImVec2(195, 40)))
        app.launch();
    primary_end();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Game files", ImVec2(125, 40)))
        app.page = Page::Files;
    if (!host)
        ImGui::TextColored(kGold, "The game executable is missing from this folder.");
    else if (!combat)
        ImGui::TextColored(kGold, "The JP combat module is missing. Rebuild the game host.");
    else if (!ready)
        ImGui::TextColored(kMuted, "Select and verify both cartridges and the BIOS to launch.");
    else if (!art)
        ImGui::TextColored(kMuted, "JP selection art will be generated locally on first launch.");
    else
        ImGui::TextColored(kGreen, "Both cartridges and BIOS verified");
    card_end();
    if (!app.message.empty()) {
        ImGui::TextColored(app.error ? kRed : kMuted, "%s", app.message.c_str());
        if (std::filesystem::is_regular_file(app.folder / L"last-game.log") &&
            ImGui::SmallButton("Open last game log"))
            sl::open_file(app.folder / L"last-game.log");
    }
}

void files_page(App& app) {
    title("Game files", "Your files stay on this PC. Original ROMs are never modified.");
    const char* labels[] = {"US cartridge", "Japanese cartridge", "GBA BIOS"};
    const char* captions[] = {"Battle Assault (USA) / required for the baseline",
                              "SEED Destiny (Japan) / donor for the expanded roster and selection",
                              "Extracted 16 KB BIOS image / required for the baseline"};
    for (unsigned index = 0; index < 3; ++index) {
        ImGui::PushID(static_cast<int>(index));
        card_begin("file", 150);
        auto& file = app.settings.files[index];
        ImGui::TextUnformatted(labels[index]);
        ImGui::SameLine();
        ImGui::TextColored(file.verified ? kGreen : (!file.path.empty() ? kGold : kMuted), "   %s",
                           file.message.c_str());
        ImGui::TextColored(kMuted, "%s", captions[index]);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 100);
        if (ImGui::InputText("##path", &file.path))
            file.edited();
        ImGui::SameLine();
        if (ImGui::Button("Browse", ImVec2(88, 0))) {
            try {
                if (auto selected = sl::pick_file(app.native_window, index == 2)) {
                    file.path = *selected;
                    file.verify(app.folder);
                    app.save();
                }
            } catch (const std::exception& e) {
                app.message = e.what();
                app.error = true;
            }
        }
        card_end();
        ImGui::PopID();
    }
    primary_begin();
    if (ImGui::Button("Verify and save", ImVec2(190, 40)))
        app.verify_all();
    primary_end();
    ImGui::SameLine();
    ImGui::TextColored(kMuted, "Saved beside the launcher");
    ImGui::TextWrapped("The US cartridge supplies the menus. Local VS loads JP combat and its full "
                       "selection grid from your verified Japanese cartridge.");
    if (!app.message.empty())
        ImGui::TextColored(app.error ? kRed : kMuted, "%s", app.message.c_str());
}

bool capture_binding_event(App& app, const SDL_Event& event) {
    auto& capture = app.capture;
    if (capture.player < 0)
        return false;
    const bool key = event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP;
    const bool pad = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
                     event.type == SDL_EVENT_GAMEPAD_BUTTON_UP ||
                     event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION;
    if (!key && !pad)
        return false;
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE) {
        capture.player = -1;
        return true;
    }
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_F4) {
        app.message = "F4 is reserved for in-game remapping. Choose another key.";
        return true;
    }
    if (!capture.neutral || SDL_GetTicks() < capture.armed_at)
        return true;
    auto& profile = app.controls.players[capture.player];
    if (capture.slot < 0 && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        profile.keyboard[capture.bit] = event.key.scancode;
        capture.player = -1;
    } else if (capture.slot >= 0) {
        const auto id = app.devices.id(profile.pad_slot);
        if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && event.gbutton.which == id) {
            profile.gamepad[capture.bit][capture.slot] = {seed::PadBindingKind::Button,
                                                          event.gbutton.button};
            capture.player = -1;
        } else if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && event.gaxis.which == id &&
                   std::abs(int(event.gaxis.value)) > 22000) {
            profile.gamepad[capture.bit][capture.slot] = {event.gaxis.value > 0
                                                              ? seed::PadBindingKind::AxisPositive
                                                              : seed::PadBindingKind::AxisNegative,
                                                          event.gaxis.axis};
            capture.player = -1;
        }
    }
    return true;
}
void controllers_page(App& app) {
    title("Controllers", "Remap keyboard, buttons and sticks independently for each player.");
    app.devices.refresh();
    ImGui::TextWrapped(
        "Bindings apply to menus and battles. Press F4 in game to change them immediately.");
    ImGui::TextColored(app.controller_db.loaded ? kMuted : kGold, "%s",
                       app.controller_db.message.c_str());
    const bool dirty = app.controls != app.saved_controls;
    primary_begin();
    if (ImGui::Button("Save bindings", ImVec2(180, 36)))
        app.save_controls();
    primary_end();
    ImGui::SameLine();
    if (ImGui::Button("Revert", ImVec2(105, 36)))
        app.controls = app.saved_controls;
    ImGui::SameLine();
    ImGui::TextColored(dirty ? kGold : kGreen, "%s", dirty ? "Unsaved changes" : "Saved");
    if (ImGui::BeginTabBar("players")) {
        for (unsigned player = 0; player < 2; ++player)
            if (ImGui::BeginTabItem(player ? "Player 2" : "Player 1")) {
                ImGui::PushID(static_cast<int>(player));
                auto& profile = app.controls.players[player];
                card_begin("bindings", 590);
                ImGui::Checkbox("Enable keyboard", &profile.keyboard_enabled);
                ImGui::SameLine();
                if (ImGui::SmallButton("Reset this player"))
                    profile = seed::ControlBindings{}.players[player];
                std::array<std::string, 3> choices{"No gamepad", "Gamepad 1", "Gamepad 2"};
                for (int slot = 0; slot < 2; ++slot)
                    if (auto* pad = app.devices.pad(slot))
                        choices[slot + 1] += " - " + std::string(SDL_GetGamepadName(pad));
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##gamepad", choices[profile.pad_slot + 1].c_str())) {
                    for (int choice = -1; choice < 2; ++choice)
                        if (ImGui::Selectable(choices[choice + 1].c_str(),
                                              profile.pad_slot == choice)) {
                            const auto old = profile.pad_slot;
                            if (choice >= 0 && app.controls.players[1 - player].pad_slot == choice)
                                app.controls.players[1 - player].pad_slot = old;
                            profile.pad_slot = choice;
                        }
                    ImGui::EndCombo();
                }
                ImGui::TextColored(kMuted,
                                   "Click a binding to change it. Right-click to clear it.");
                constexpr unsigned order[]{6, 7, 5, 4, 0, 1, 8, 9, 3, 2};
                if (ImGui::BeginTable("bindings-table", 4,
                                      ImGuiTableFlags_BordersInnerH |
                                          ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 70);
                    ImGui::TableSetupColumn("Keyboard");
                    ImGui::TableSetupColumn("Gamepad");
                    ImGui::TableSetupColumn("Alternate");
                    ImGui::TableHeadersRow();
                    for (const auto bit : order) {
                        ImGui::PushID(static_cast<int>(bit));
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(seed::kControlNames[bit]);
                        ImGui::TableNextColumn();
                        if (ImGui::Button(seed::key_binding_name(profile.keyboard[bit]).c_str(),
                                          ImVec2(-1, 30)))
                            app.begin_capture(player, bit, -1);
                        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                            profile.keyboard[bit] = SDL_SCANCODE_UNKNOWN;
                        for (unsigned slot = 0; slot < 2; ++slot) {
                            ImGui::TableNextColumn();
                            ImGui::PushID(static_cast<int>(slot));
                            ImGui::BeginDisabled(profile.pad_slot < 0);
                            if (ImGui::Button(
                                    seed::binding_name(profile.gamepad[bit][slot]).c_str(),
                                    ImVec2(-1, 30)))
                                app.begin_capture(player, bit, slot);
                            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                                profile.gamepad[bit][slot] = {};
                            ImGui::EndDisabled();
                            ImGui::PopID();
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                const auto mask = seed::sample_controls(profile, SDL_GetKeyboardState(nullptr),
                                                        app.devices.pad(profile.pad_slot));
                ImGui::TextUnformatted("Input check:");
                ImGui::SameLine();
                for (unsigned bit = 0; bit < 10; ++bit) {
                    if (bit)
                        ImGui::SameLine(0, 8);
                    ImGui::TextColored(mask & (1u << bit) ? kAccent : kMuted, "%s",
                                       seed::kControlNames[bit]);
                }
                card_end();
                ImGui::PopID();
                ImGui::EndTabItem();
            }
        ImGui::EndTabBar();
    }
    ImGui::TextColored(kMuted,
                       "%u gamepad(s) connected. Unplugging one player preserves the other seat.",
                       app.devices.count());
    if (!app.message.empty())
        ImGui::TextColored(app.error ? kRed : kMuted, "%s", app.message.c_str());
    if (app.capture.player >= 0 && !ImGui::IsPopupOpen("Remap input"))
        ImGui::OpenPopup("Remap input");
    if (ImGui::BeginPopupModal("Remap input", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        auto& capture = app.capture;
        if (capture.player < 0)
            ImGui::CloseCurrentPopup();
        else {
            auto* pad = app.devices.pad(app.controls.players[capture.player].pad_slot);
            ImGui::Text("Player %u / %s", capture.player + 1, seed::kControlNames[capture.bit]);
            ImGui::TextUnformatted(
                capture.slot < 0
                    ? "Release keys, then press the new key."
                    : "Release controls, then press a button or move a stick/trigger.");
            if (capture.slot >= 0 && !pad)
                ImGui::TextColored(kGold, "Connect the selected gamepad to continue.");
            if (!capture.neutral) {
                const auto state = seed::capture_controls(
                    capture.slot < 0 ? SDL_GetKeyboardState(nullptr) : nullptr,
                    capture.slot >= 0 ? pad : nullptr);
                const bool keys = std::any_of(state.keyboard.begin(), state.keyboard.end(),
                                              [](bool v) { return v; });
                const bool buttons = std::any_of(state.buttons.begin(), state.buttons.end(),
                                                 [](bool v) { return v; });
                const bool axes = std::any_of(state.axes.begin(), state.axes.end(),
                                              [](auto v) { return std::abs(int(v)) > 12000; });
                capture.neutral = !keys && !buttons && !axes && (capture.slot < 0 || pad);
            }
            ImGui::TextColored(kMuted,
                               "Esc cancels. Changes remain pending until saved or launched.");
            if (ImGui::Button("Cancel", ImVec2(130, 32))) {
                capture.player = -1;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}
void about_page(App& app) {
    title("Project status", "Gundam SEED Destiny / Battle Assault PC port");
    card_begin("status", 300);
    ImGui::TextColored(kGreen, "READY");
    ImGui::BulletText("ROM and BIOS verification");
    ImGui::BulletText("Native baseline launcher");
    ImGui::BulletText("Two-player input model: 8,192 original-ROM calls matched");
    ImGui::BulletText("JP importer: 223 compressed blocks matched");
    ImGui::BulletText("Local VS: independent selection and shared battles");
    ImGui::Spacing();
    ImGui::TextColored(kGold, "IN PROGRESS");
    ImGui::BulletText("Expanded JP combat roster: broader gameplay testing");
    ImGui::BulletText("Rollback networking and broader gameplay testing");
    card_end();
    card_begin("display", 115);
    ImGui::TextUnformatted("Baseline window size");
    ImGui::SetNextItemWidth(350);
    if (ImGui::SliderInt("Scale", &app.settings.scale, 2, 6, "%dx"))
        app.save();
    card_end();
    ImGui::TextColored(kMuted, "Dear ImGui + SDL3  /  Licenses included in this folder");
}

void draw(App& app) {
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("SEED launcher", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);
    ImGui::BeginChild("sidebar", ImVec2(204, 0));
    ImGui::PushFont(nullptr, 24);
    ImGui::TextColored(kAccent, "SEED / PC");
    ImGui::PopFont();
    ImGui::TextColored(kMuted, "DESTINY\nBATTLE ASSAULT");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    const char* pages[] = {"Play", "Game files", "Controllers", "Project status"};
    for (int index = 0; index < int(std::size(pages)); ++index) {
        const bool selected = static_cast<int>(app.page) == index;
        if (selected)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.32f, 0.38f, 1));
        if (ImGui::Button(pages[index], ImVec2(184, 44)))
            app.page = static_cast<Page>(index);
        if (selected)
            ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::TextColored(kGold, "DEVELOPMENT BUILD");
    ImGui::TextWrapped("F4 in game: controls");
    ImGui::EndChild();
    ImGui::SameLine(0, 28);
    ImGui::BeginChild("content", ImVec2(0, 0));
    switch (app.page) {
    case Page::Play:
        play_page(app);
        break;
    case Page::Files:
        files_page(app);
        break;
    case Page::Controllers:
        controllers_page(app);
        break;
    case Page::About:
        about_page(app);
        break;
    }
    ImGui::EndChild();
    ImGui::End();
}
} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    try {
        SetProcessDPIAware();
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        App app;
        app.folder = sl::executable_directory();
        app.settings.load(app.folder / L"launcher-settings.ini");
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        int quit_after = 0;
        std::filesystem::path capture;
        std::string page, invite_error;
        const bool invite_request = argc > 1 && std::wstring(argv[1]) == L"--join";
        if (invite_request) {
            try {
                if (argc != 3)
                    throw std::runtime_error("Invalid invitation link.");
                app.join_code = seed::encode_join_code(seed::decode_join_code(sl::utf8(argv[2])));
                const auto delivered =
                    seed::forward_game_invite(app.folder / L"GundamSeedRecomp.exe", app.join_code);
                if (delivered != seed::InviteDelivery::NoGame) {
                    if (delivered == seed::InviteDelivery::Queued)
                        MessageBoxW(nullptr,
                                    L"Invitation received. Leave your current match to join it.",
                                    L"Gundam SEED invitation", MB_OK);
                    else if (delivered == seed::InviteDelivery::Busy)
                        MessageBoxW(
                            nullptr,
                            L"The game is busy. Paste the code in Online VS when it is ready.",
                            L"Gundam SEED invitation", MB_OK);
                    if (argv)
                        LocalFree(argv);
                    if (SUCCEEDED(com))
                        CoUninitialize();
                    return 0;
                }
            } catch (const std::exception& error) {
                invite_error = error.what();
            }
        }
        for (int i = 1; !invite_request && argv && i < argc; ++i) {
            const std::wstring flag = argv[i];
            if (i + 1 >= argc)
                continue;
            if (flag == L"--rom")
                app.settings.files[0].path = sl::utf8(argv[++i]);
            else if (flag == L"--jp-rom")
                app.settings.files[1].path = sl::utf8(argv[++i]);
            else if (flag == L"--bios")
                app.settings.files[2].path = sl::utf8(argv[++i]);
            else if (flag == L"--quit-after-frames")
                quit_after = std::max(0, _wtoi(argv[++i]));
            else if (flag == L"--capture")
                capture = argv[++i];
            else if (flag == L"--page")
                page = sl::utf8(argv[++i]);
        }
        if (argv)
            LocalFree(argv);
        if (page == "files")
            app.page = Page::Files;
        else if (page == "controllers")
            app.page = Page::Controllers;
        else if (page == "status")
            app.page = Page::About;
        if (page.empty() && std::all_of(app.settings.files.begin(), app.settings.files.end(),
                                        [](const auto& file) { return file.path.empty(); }))
            app.page = Page::Files;
        app.verify_all();
        if (!invite_error.empty()) {
            app.error = true;
            app.message = invite_error;
        }
        SDL_SetMainReady();
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
            throw std::runtime_error(SDL_GetError());
        app.controller_db = seed::load_controller_db(app.folder);
        app.controls.load(app.folder / L"controls.ini");
        app.saved_controls = app.controls;
        app.window = SDL_CreateWindow("Gundam SEED Destiny / Battle Assault - PC Launcher", 1100,
                                      820, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!app.window)
            throw std::runtime_error(SDL_GetError());
        SDL_SetWindowMinimumSize(app.window, 1020, 760);
        app.native_window = SDL_GetPointerProperty(SDL_GetWindowProperties(app.window),
                                                   SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        SDL_Renderer* renderer = SDL_CreateRenderer(app.window, nullptr);
        if (!renderer)
            renderer = SDL_CreateRenderer(app.window, "software");
        if (!renderer)
            throw std::runtime_error(SDL_GetError());
        SDL_SetRenderVSync(renderer, 1);
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        theme();
        ImGui_ImplSDL3_InitForSDLRenderer(app.window, renderer);
        ImGui_ImplSDLRenderer3_Init(renderer);
        app.refresh_pads();
        if (!app.join_code.empty() && invite_error.empty())
            app.launch();
        bool done = false;
        int frames = 0;
        while (!done) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (!capture_binding_event(app, event) || event.type == SDL_EVENT_KEY_UP ||
                    event.type == SDL_EVENT_GAMEPAD_BUTTON_UP)
                    ImGui_ImplSDL3_ProcessEvent(&event);
                if (event.type == SDL_EVENT_QUIT ||
                    (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                     event.window.windowID == SDL_GetWindowID(app.window)))
                    done = true;
                if (event.type == SDL_EVENT_GAMEPAD_ADDED ||
                    event.type == SDL_EVENT_GAMEPAD_REMOVED)
                    app.refresh_pads();
            }
            if (const auto code = app.game.poll()) {
                if (app.controls == app.saved_controls) {
                    app.controls.load(app.folder / L"controls.ini");
                    app.saved_controls = app.controls;
                }
                app.hidden = false;
                SDL_ShowWindow(app.window);
                SDL_RaiseWindow(app.window);
                app.error = *code != 0;
                app.message = app.error ? "The game closed unexpectedly (code " +
                                              std::to_string(*code) + "). Check the game log."
                                        : "Game closed.";
            }
            if (app.hidden) {
                SDL_Delay(30);
                continue;
            }
            ImGui_ImplSDLRenderer3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            draw(app);
            ImGui::Render();
            SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x,
                               io.DisplayFramebufferScale.y);
            SDL_SetRenderDrawColor(renderer, 11, 15, 22, 255);
            SDL_RenderClear(renderer);
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
            ++frames;
            if (!capture.empty() && frames == (quit_after ? quit_after : 30)) {
                if (auto* surface = SDL_RenderReadPixels(renderer, nullptr)) {
                    if (capture.extension() == L".png")
                        SDL_SavePNG(surface, sl::utf8(capture.wstring()).c_str());
                    else
                        SDL_SaveBMP(surface, sl::utf8(capture.wstring()).c_str());
                    SDL_DestroySurface(surface);
                }
            }
            SDL_RenderPresent(renderer);
            if (quit_after && frames >= quit_after)
                done = true;
        }
        if (app.controls != app.saved_controls)
            app.save_controls();
        app.save();
        app.devices.close();
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(app.window);
        SDL_Quit();
        if (SUCCEEDED(com))
            CoUninitialize();
        return 0;
    } catch (const std::exception& error) {
        MessageBoxW(nullptr, sl::wide(error.what()).c_str(), L"Gundam SEED Launcher",
                    MB_OK | MB_ICONERROR);
        return 1;
    }
}
