#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "net/Connection.h"
#include "net/Ice.h"
#include "net/Serialize.h"

namespace vw::net {

enum class LobbyMsg : std::uint8_t {
    Welcome = 1,
    CreateRoom,
    RoomCreated,
    UpdateRoom,
    JoinRoom,
    JoinError,
    PeerJoin,
    PeerAnswer,
    Answer,
    Candidate,
    GatheringDone,
    ListRooms,
    RoomList,
    PeerLeft,
    Report,
};

struct RoomInfo {
    std::string code;
    std::string name;
    std::string map;
    std::uint8_t players = 0;
    std::uint8_t maxPlayers = 0;
    bool listed = true;
};

inline constexpr std::size_t kRoomCodeLength = 4;

std::string makeRoomCode(std::mt19937& rng);
std::string normalizeRoomCode(std::string_view text);

void writeIceServers(Writer& w, const IceServers& s);
IceServers readIceServers(Reader& r);
void writeRoom(Writer& w, const RoomInfo& room);
RoomInfo readRoom(Reader& r);

inline Writer lobbyMessage(LobbyMsg type) {
    Writer w;
    w.u8(static_cast<std::uint8_t>(type));
    return w;
}

}
