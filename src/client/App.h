#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL_events.h>
#include <imgui.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "Prediction.h"
#include "Visuals.h"
#include "core/GameData.h"
#include "core/Map.h"
#include "net/MasterClient.h"
#include "net/NetClient.h"
#include "net/NetLibrary.h"
#include "net/PortMapper.h"
#include "net/Protocol.h"
#include "render/Camera.h"
#include "render/Renderer.h"
#include "ui/ImGuiLayer.h"
#include "server/Server.h"

namespace vw::client {

struct EditorState;

struct LaunchOptions {
    bool host = false;
    std::optional<std::string> editor;
    bool editorTest = false;
    std::string connect;
    std::string join;
    std::string name = "Player";
    std::string map = "void_siege";
    std::uint16_t port = net::kGamePort;
    Difficulty difficulty = Difficulty::Normal;
    GameMode mode = GameMode::Defense;
    bool cheats = false;
    int windowMode = -1;
    std::string master = "77.83.86.108:27400";
    float uiScale = 0.0f;
    bool touch = false;
    std::string renderer;
    int guide = -1;
    std::optional<glm::vec2> camera;
    float zoom = 0.0f;
    int shadows = -1;
    int light = -1;
};

const char* shadowQualityName(int quality);
const char* lightPresetName(int preset);
int parseShadowQuality(std::string_view text);
int parseLightPreset(std::string_view text);
std::string prefDir();
std::string savesDir();
std::string userMapsDir();
bool validMapName(const std::string& name);
std::string readMap(const std::string& name);
std::vector<std::string> listJson(const std::string& dir);

class App {
public:
    App();
    ~App();
    bool init(SDL_Window* window, LaunchOptions options);
    void shutdown();
    void handleEvent(const SDL_Event& e);
    void frame(double dt);
    bool running() const { return running_; }

private:
    enum class Screen { Menu, Lan, Address, Code, Online, Joining, Connecting, Lobby, Game, Editor };

    struct Effect {
        Mesh mesh;
        glm::vec3 pos;
        glm::vec4 color;
        float from;
        float to;
        float life;
        float t = 0.0f;
        float rise = 0.0f;
        float delay = 0.0f;
    };
    struct Floater {
        glm::vec3 pos;
        std::string text;
        std::uint32_t color;
        float t = 0.0f;
        float life = 2.2f;
    };
    struct Known {
        std::uint8_t rank = 0;
        std::uint8_t level = 0;
    };
    std::vector<Floater> floaters_;
    struct BeamFx {
        glm::vec3 a;
        glm::vec3 b;
        glm::vec4 color;
        float width;
        float life;
        float t = 0.0f;
    };
    std::vector<BeamFx> beams_;
    void renderLegs(const net::InterpolatedEntity& e, const UnitVisual& vis, glm::vec3 base, float hullYaw, float scale, const glm::vec4& color);
    void renderVoidFx();
    void crystalFire(const net::InterpolatedEntity& shooter);
    float walkBob(const net::InterpolatedEntity& e) const;
    int anchorsAlive() const;
    std::unordered_map<std::uint32_t, Known> known_;
    std::set<std::uint32_t> unseen_;
    bool refitDot_ = false;
    bool modulesDot_ = false;
    bool guideOpen_ = false;
    int guideTab_ = 0;
    void trackLevelUps();
    void levelUpEffect(glm::vec2 pos, const std::string& label);
    void drawFloaters();
    void drawLevelUps();
    void drawGuide();
    static void redDot(ImDrawList* dl, ImVec2 at, float radius);

    void loadContent();
    void setScreen(Screen screen);
    void startHost(bool multiplayer);
    void enterGame();
    void returnToLobby();
    void buildMinimap();
    void applyVideo();
    void applyWindow();
    bool touchLayout() const;
    void startConnect(const net::Address& address);
    void startJoin(const std::string& code);
    void updateLobby();
    void leaveGame(std::string status);
    bool onConnected();

    void handleMenuKey(SDL_Keycode key);
    void handleGameEvent(const SDL_Event& e);
    void handleGameKey(SDL_Keycode key, bool shift);
    void handleFinger(const SDL_Event& e);
    void tap(glm::vec2 p);
    void pressKey(SDL_Keycode key, bool shift);
    void castSlot(std::uint8_t slot, std::optional<glm::vec2> ground);
    void updateInputScale();
    void updateGame(float dt);
    void updateCamera(float dt);

    void clickSelect(glm::vec2 screen, bool add);
    void boxSelect(glm::vec2 a, glm::vec2 b, bool add);
    int selectPriority(const net::InterpolatedEntity& e) const;
    enum class Arm { None, Move, Attack, Patrol, Unload };
    void issueCommand(glm::vec2 screen, bool attackMove, bool patrol = false);
    void issueCommandAt(glm::vec2 ground, bool attackMove, bool patrol = false);
    void issueArmed(glm::vec2 screen);
    void armOrder(Arm arm);
    void renderWaypoints();
    static glm::vec4 orderColor(std::uint8_t type);
    std::vector<entt::entity> selectedBuilders() const;
    std::vector<entt::entity> selectedTransports() const;
    float liftOf(const net::InterpolatedEntity& e) const;
    void updateAirLift(float dt);
    std::unordered_map<std::uint32_t, float> airLift_;
    bool hidden(const net::InterpolatedEntity& e) const { return (e.flags & net::kStateCarried) != 0; }
    bool hadBuilders_ = false;
    bool lineBuild_ = false;
    glm::vec2 lineStart_{};
    std::vector<glm::vec2> buildLine(glm::vec2 from, glm::vec2 to) const;
    std::uint32_t inspected_ = 0;
    struct PendingBuild {
        UnitDefId def;
        glm::vec2 pos;
        double time;
    };
    mutable std::vector<PendingBuild> pendingBuilds_;
    void collectPhantoms(std::vector<PendingBuild>& out) const;
    void renderPhantoms();
    void drawInspect();
    void pruneSelection();
    const net::InterpolatedEntity* entityAt(glm::vec2 p, float pad) const;
    const net::InterpolatedEntity* findEntity(std::uint32_t id) const;
    std::vector<entt::entity> selectedEntities() const;
    bool isOwn(const net::InterpolatedEntity& e) const { return e.owner == net_.player(); }
    glm::vec4 unitColor(const net::InterpolatedEntity& e) const;
    glm::vec4 playerColor(std::uint8_t owner) const;

    glm::vec3 muzzleOf(const net::InterpolatedEntity& e) const;
    glm::vec3 projectilePos(const net::InterpolatedEntity& e);
    void addEffect(Mesh mesh, glm::vec3 pos, glm::vec4 color, float from, float to, float life);
    void handleNetEvent(const net::NetEvent& ev);
    void showBanner(std::string text, float seconds);
    void renderResourcePoints();
    void handlePanelKey(SDL_Keycode key, bool shift);
    const net::OwnUnit* focusedOwn() const;
    std::string modsText(const StatMods& m) const;

    void loadSettings();
    void saveSettings() const;
    void applyGraphics();
    void cycleShadows();
    void cycleLight();

    void render();
    void renderWorld();
    glm::vec4 visibleRect(float margin) const;

    void drawUi();
    void drawMenu();
    void drawMenuPage();
    void drawBrowser();
    void drawLobby();
    void drawSettings();
    void drawHud();
    void drawDesktopHud();
    void drawTouchHud();
    void drawResourceBar();
    void drawMinimap(float size);
    void drawSelection();
    void drawSelectionTiles(float width, int perRow, bool horizontal);
    void drawDock();
    enum class Dock { None, Orders, Production, Skills, Build, Refit, Modules };
    struct ModuleLook {
        const Model* mount = nullptr;
        const Model* weapon = nullptr;
    };
    std::vector<ModuleLook> moduleLooks_;
    void renderModules(const net::InterpolatedEntity& e, const UnitVisual& vis, glm::vec3 base, float grow, const glm::vec4& color);
    void renderSkillPreview();
    Dock dock_ = Dock::None;
    void drawGroupBar();
    void renderBuildGhost();
    int buildKeyIndex(SDL_Keycode key) const;
    void armBuild(std::size_t index);
    bool buildValid(UnitDefId def, glm::vec2 pos) const;
    void placeBuild(glm::vec2 screen, bool keep);
    void controlGroup(int n, bool assign, bool add);
    void drawChat();
    void drawTopBar();
    void drawBossBar();
    void drawUnitPanel();
    void drawSidePanel();
    void drawStats();
    void drawPauseMenu();
    void drawEndScreen();
    void trackWindow();
    bool overUi(glm::vec2 p) const;
    float uiScale() const;

    SDL_Window* window_ = nullptr;
    LaunchOptions options_;
    net::NetLibrary netLibrary_;
    net::PortMapper mapper_;
    Renderer renderer_;
    Camera camera_;
    std::unique_ptr<GameData> data_;
    std::vector<UnitVisual> visuals_;
    std::unique_ptr<ModelLibrary> models_;
    MapVisuals mapVisuals_;
    std::uint64_t mapVisualsHash_ = 0;

    std::unique_ptr<Map> hostMap_;
    std::unique_ptr<server::Server> host_;
    std::unique_ptr<Map> map_;
    net::NetClient net_;
    net::LanBrowser lan_;
    net::MasterClient lobby_;
    double lobbyRefresh_ = 0.0;
    Prediction prediction_;

    std::vector<net::InterpolatedEntity> entities_;
    std::vector<std::uint32_t> selected_;
    std::vector<Effect> effects_;

    struct Shell {
        glm::vec3 muzzle;
        glm::vec2 start;
        float lob;
        bool seen;
        bool frost = false;
        float frostScale = 1.0f;
        glm::vec3 last{};
    };
    enum class ParticleKind : std::uint8_t { Glow, Smoke, Debris, Shard, Decal };
    struct Particle {
        glm::vec3 pos;
        glm::vec3 vel;
        glm::vec4 color;
        float size0;
        float size1;
        float life;
        float t;
        float gravity;
        float drag;
        float spin;
        ParticleKind kind;
    };
    std::vector<Particle> particles_;
    std::mt19937 fxRng_{12345u};
    float fxRandom(float lo, float hi);
    glm::vec3 fxDirection(float up);
    int fxCount(float n) const;
    void addParticle(const Particle& p);
    void spawnExplosion(glm::vec3 at, float size, bool structure, bool voidSide, glm::vec4 hull);
    void spawnFrostBurst(glm::vec3 at, float scale);
    void renderFrostShot(const net::InterpolatedEntity& e, glm::vec3 p, float scale);
    void updateParticles(float dt);
    void renderParticles();
    void onUnitDeath(const net::UnitDeath& d);
    std::vector<glm::vec4> snow_;
    void updateSnow(float dt);
    void renderSnow();
    void updateFocus();
    std::unique_ptr<EditorState> editor_;
    void openEditor(const std::string& mapId);
    void newEditorMap(int width, int height, bool winter);
    void closeEditor();
    void editorWriteCells();
    bool editorReparse(bool visuals);
    void editorChange();
    void editorUndo();
    void editorSave();
    void editorPlay();
    void loadUserMaps();
    void updateEditor(float dt);
    void editorApply(glm::vec2 p, bool start);
    void editorRelease(glm::vec2 p);
    void editorDelete();
    void handleEditorEvent(const SDL_Event& e);
    void handleEditorKey(SDL_Keycode key, bool shift);
    void renderEditorOverlay();
    void drawEditor();
    void editorSelfTest();
    char saveName_[49] = "";
    std::vector<std::string> saves_;
    int saveChoice_ = 0;
    double savesListed_ = -100.0;
    std::unordered_map<std::uint32_t, Shell> shells_;
    std::string banner_;
    float bannerTime_ = 0.0f;

    Screen screen_ = Screen::Menu;
    std::string status_;
    std::array<char, 64> addressBuf_{};
    std::array<char, 24> codeBuf_{};
    std::array<char, 32> nameBuf_{};
    std::array<char, 96> masterBuf_{};
    bool running_ = true;
    bool initialized_ = false;
    bool cameraPlaced_ = false;
    double now_ = 0.0;
    float fps_ = 0.0f;
    float frameMs_ = 0.0f;
    double simMs_ = 0.0;
    double simTotal_ = 0.0;
    std::uint64_t simSteps_ = 0;
    float pixelDensity_ = 1.0f;
    glm::vec2 mouse_{};
    glm::vec2 dragStart_{};
    bool dragging_ = false;
    Arm orderArm_ = Arm::None;

    enum class Panel { None, Shop, Stash, Learn };
    Panel panel_ = Panel::None;

    struct Finger {
        glm::vec2 start;
        glm::vec2 pos;
    };
    ImGuiLayer ui_;
    std::vector<glm::vec4> uiRects_;
    std::set<SDL_FingerID> uiFingers_;
    bool showStats_ = false;
    bool pauseOpen_ = false;
    bool settingsOpen_ = false;
    float topBarHeight_ = 0.0f;
    float hudBottom_ = 0.0f;
    float minimapRight_ = 0.0f;
    float commandCardLeft_ = 0.0f;
    float frameDt_ = 0.0f;
    std::map<SDL_FingerID, Finger> fingers_;
    bool touchUi_ = false;
    bool gesture_ = false;
    glm::vec2 gestureCenter_{};
    float gestureSpread_ = 0.0f;
    int armedSkill_ = -1;
    int buildArmed_ = -1;
    std::array<std::vector<std::uint32_t>, 10> groups_;
    int lastGroup_ = -1;
    double lastGroupTime_ = 0.0;
    bool groupHeld_ = false;
    bool paused_ = false;
    int shadowQuality_ = 2;
    int lightPreset_ = 0;

    int windowMode_ = 0;
    int resWidth_ = 0;
    int resHeight_ = 0;
    bool vsync_ = true;
    int msaa_ = 4;
    float uiScaleUser_ = 1.0f;
    int renderScale_ = 45;
    int layout_ = 0;
    int preferredColor_ = -1;
    bool upnp_ = true;
    int fpsLimit_ = 0;
    bool limitBackground_ = true;
    void startUpnp();

public:
    double frameBudget() const;

private:
    bool setupSent_ = false;
    bool endDismissed_ = false;
    std::set<std::uint8_t> lobbySelected_;
    int settingsTab_ = 0;
    bool autoStart_ = false;
    TextureId minimap_ = 0;
    std::uint64_t minimapHash_ = 0;

    struct ChatEntry {
        net::ChatLine line;
        double time;
    };
    std::vector<ChatEntry> chatLog_;

    struct CatalogEntry {
        std::string id;
        std::string name;
        std::string players;
        std::string description;
        bool custom = false;
    };
    std::vector<CatalogEntry> maps_;
    std::vector<CatalogEntry> modes_;
    std::array<char, 128> chatBuf_{};
    bool chatOpen_ = false;
};

}
