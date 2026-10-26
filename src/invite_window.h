#pragma once
#include <filesystem>
#include <string>
namespace seed {
enum class InviteDelivery { NoGame, Delivered, Queued, Busy };
void queue_game_invite(const std::string& code);
bool take_game_invite(std::string& code);
void set_invite_busy(bool busy);
void attach_invite_window(void* window);
void detach_invite_window(void* window);
InviteDelivery forward_game_invite(const std::filesystem::path& game, const std::string& code);
bool register_invite_launcher(const std::filesystem::path& launcher, std::string& error);
} // namespace seed
