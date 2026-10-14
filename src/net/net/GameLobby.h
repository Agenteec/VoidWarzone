#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "net/Serialize.h"

namespace vw::net {

enum class LobbyPhase : std::uint8_t { Lobby, Playing };

struct LobbyPlayer {
    std::uint8_t id = 0;
    std::string name;
    bool ready = false;
};

struct LobbyInfo {
    LobbyPhase phase = LobbyPhase::Lobby;
    std::string serverName;
    std::string map;
    std::uint64_t mapHash = 0;
    std::uint8_t mode = 0;
    std::uint8_t difficulty = 1;
    std::uint8_t maxPlayers = 8;
    std::uint8_t leader = 0xff;
    std::vector<LobbyPlayer> players;
};

struct ChatLine {
    std::uint8_t player = 0;
    std::string name;
    std::string text;
};

inline constexpr std::size_t kMaxChatLength = 120;

void writeLobby(Writer& w, const LobbyInfo& info);
bool readLobby(Reader& r, LobbyInfo& info);

}
