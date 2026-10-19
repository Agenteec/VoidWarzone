#pragma once

#include <cstddef>
#include <cstdint>

namespace vw::net {

inline constexpr std::size_t kMaxPacketSize = 1200;
inline constexpr std::uint16_t kGamePort = 27450;
inline constexpr std::uint16_t kMasterPort = 27400;
inline constexpr std::uint16_t kStunPort = 3478;

inline constexpr std::uint32_t kMagic = 0x315a5756;
inline constexpr double kTimeout = 10.0;
inline constexpr double kConnectRetry = 0.2;
inline constexpr double kConnectTimeout = 5.0;
inline constexpr std::uint8_t kNoOwner = 0xff;

enum class PacketType : std::uint8_t {
    ConnectRequest = 1,
    ConnectAccept,
    ConnectDeny,
    Payload,
    Disconnect,
    DiscoveryRequest,
    DiscoveryResponse,
};

enum class DenyReason : std::uint8_t {
    Version = 1,
    Data,
    Full,
};

enum class MessageType : std::uint8_t {
    Command = 1,
    Lobby,
    Ready,
    Settings,
    Start,
    Chat,
    ReturnToLobby,
    PlayerSetup,
    Kick,
    SaveGame,
    LoadGame,
    Cheats,
    Bot,
    Fog,
};

enum class NetEventType : std::uint8_t {
    Hit = 1,
    SlamWarn,
    Slam,
    Summon,
    Enrage,
    WaveStart,
    PointCaptured,
    MatchEnd,
    SkillCast,
    Promote,
    LevelUp,
    Loot,
    Fire,
};

const char* denyReasonText(DenyReason reason);

}
