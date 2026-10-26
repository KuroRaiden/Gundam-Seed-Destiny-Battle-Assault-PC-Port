#pragma once
#include "live_controls.h"
#include "player_profile.h"
#include <array>
#include <string>
struct ImGuiContext;
namespace seed {
enum class OnlineLobbyAction { None, Host, Join };
struct OnlineLobbyState {
    bool open = false, cancelled = false;
    std::array<char, 256> address{};
    std::array<char, 512> invite{};
    std::array<char, kPlayerNameBytes + 1> player_name{};
    std::string code, invitation, status, link_status;
    bool hosting = false;
    OnlineLobbyAction action = OnlineLobbyAction::None;
    int port = 0; // Developer fixture only; the player chooses no address or port.
    void begin() {
        open = true;
        cancelled = false;
        hosting = false;
        action = OnlineLobbyAction::None;
        address = {};
        port = 0;
        code.clear();
        invitation.clear();
        status.clear();
    }
    void cancel() {
        open = false;
        cancelled = true;
    }
};
class OnlineLobby {
  public:
    ~OnlineLobby();
    void initialize(SDL_Window* window, SDL_Renderer* renderer, LiveControls* controls);
    void shutdown();
    void show(OnlineLobbyState* state);
    bool event(const SDL_Event& event);
    void draw();
    bool is_open() const {
        return state_ && state_->open;
    }

  private:
    void cancel();
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    LiveControls* controls_ = nullptr;
    ImGuiContext* context_ = nullptr;
    OnlineLobbyState* state_ = nullptr;
};
// Connect the game policy to the single existing host window. Headless tests
// retain the same model without creating an extra window or network listener.
void show_online_lobby(OnlineLobbyState* state);
} // namespace seed
