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
    w.u8(info.fog ? 1 : 0);
    w.u8(static_cast<std::uint8_t>(info.players.size()));
    for (const auto& p : info.players) {
        w.u8(p.id);
        w.str(p.name);
        w.u8(p.ready ? 1 : 0);
        w.u8(p.color);
        w.u8(p.team);
        w.u8(p.bot ? 1 : 0);
        w.u8(p.aiLevel);
        w.u8(p.aiStyle);
    }
}

void writeStats(Writer& w, const MatchStats& stats) {
    w.f32(stats.seconds);
    w.u32(static_cast<std::uint32_t>(stats.winner));
    w.u8(static_cast<std::uint8_t>(stats.players.size()));
    for (const auto& p : stats.players) {
        w.u8(p.id);
        w.u8(p.team);
        w.u8(p.ai ? 1 : 0);
        w.u8(p.aiLevel);
        w.u8(p.aiStyle);
        for (const auto v : {p.produced, p.lost, p.kills, p.built, p.structuresLost, p.researched})
            w.u32(static_cast<std::uint32_t>(v));
        for (const auto v : {p.damageDealt, p.damageTaken, p.earned, p.spent, p.valueKilled, p.valueLost})
            w.f32(v);
    }
}

bool readStats(Reader& r, MatchStats& stats) {
    stats.seconds = r.f32();
    stats.winner = static_cast<std::int32_t>(r.u32());
    stats.players.resize(r.u8());
    for (auto& p : stats.players) {
        p.id = r.u8();
        p.team = r.u8();
        p.ai = r.u8() != 0;
        p.aiLevel = r.u8();
        p.aiStyle = r.u8();
        for (auto* v : {&p.produced, &p.lost, &p.kills, &p.built, &p.structuresLost, &p.researched})
            *v = static_cast<std::int32_t>(r.u32());
        for (auto* v : {&p.damageDealt, &p.damageTaken, &p.earned, &p.spent, &p.valueKilled, &p.valueLost})
            *v = r.f32();
    }
    return r.ok();
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
    info.fog = r.u8() != 0;
    info.players.resize(r.u8());
    for (auto& p : info.players) {
        p.id = r.u8();
        p.name = r.str(32);
        p.ready = r.u8() != 0;
        p.color = static_cast<std::uint8_t>(r.u8() % kPlayerColors.size());
        p.team = static_cast<std::uint8_t>(r.u8() % kLobbyTeams);
        p.bot = r.u8() != 0;
        p.aiLevel = static_cast<std::uint8_t>(r.u8() % 4);
        p.aiStyle = r.u8();
    }
    return r.ok();
}

}
