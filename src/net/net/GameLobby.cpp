#include "net/GameLobby.h"

namespace vw::net {

void writeLobby(Writer& w, const LobbyInfo& info) {
    w.u8(static_cast<std::uint8_t>(info.phase));
    w.str(info.serverName);
    w.str(info.map);
    w.u64(info.mapHash);
    w.u8(info.mode);
    w.u8(info.difficulty);
    w.u8(info.maxPlayers);
    w.u8(info.leader);
    w.u8(info.cheats ? 1 : 0);
    w.u8(static_cast<std::uint8_t>(info.players.size()));
    for (const auto& p : info.players) {
        w.u8(p.id);
        w.str(p.name);
        w.u8(p.ready ? 1 : 0);
        w.u8(p.color);
        w.u8(p.team);
    }
}

bool readLobby(Reader& r, LobbyInfo& info) {
    info.phase = r.u8() == 1 ? LobbyPhase::Playing : LobbyPhase::Lobby;
    info.serverName = r.str(64);
    info.map = r.str(64);
    info.mapHash = r.u64();
    info.mode = r.u8();
    info.difficulty = r.u8();
    info.maxPlayers = r.u8();
    info.leader = r.u8();
    info.cheats = r.u8() != 0;
    info.players.resize(r.u8());
    for (auto& p : info.players) {
        p.id = r.u8();
        p.name = r.str(32);
        p.ready = r.u8() != 0;
        p.color = static_cast<std::uint8_t>(r.u8() % kPlayerColors.size());
        p.team = static_cast<std::uint8_t>(r.u8() % kLobbyTeams);
    }
    return r.ok();
}

}
