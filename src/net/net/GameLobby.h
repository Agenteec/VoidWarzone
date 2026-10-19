#pragma once

#include <array>
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
    std::uint8_t color = 0;
    std::uint8_t team = 0;
    bool bot = false;
};

inline constexpr std::array<std::uint32_t, 16> kPlayerColors{0x4ce06a, 0x3f8cff, 0xf2d53c, 0xff8a2a, 0xc85cff, 0x30e0e0, 0xff5ca8, 0xf0f0f0,
                                                             0xe03c3c, 0x2a4cb0, 0xa8e048, 0x9a6a3a, 0x2a9d8f, 0xd83cd8, 0xc89a30, 0x808890};
inline constexpr std::uint8_t kLobbyTeams = 4;

struct LobbyInfo {
    LobbyPhase phase = LobbyPhase::Lobby;
    std::string serverName;
    std::string map;
    std::uint64_t mapHash = 0;
    std::uint8_t mode = 0;
    std::uint8_t difficulty = 1;
    std::uint8_t maxPlayers = 8;
    std::uint8_t leader = 0xff;
    bool cheats = false;
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
