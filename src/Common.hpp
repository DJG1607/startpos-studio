#pragma once

#include <Geode/Geode.hpp>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace geode::prelude;

// Everything is stored in game coordinates (the ones PlayerObject and the
// objects use while playing). The level string keeps y without the 90 units
// of ground, so parsing adds LEVEL_Y_OFFSET.
constexpr float LEVEL_Y_OFFSET = 90.f;

// What a StartPos sets when you spawn on it.
struct SPState {
    int mode = 0;   // 0 cube, 1 ship, 2 ball, 3 ufo, 4 wave, 5 robot, 6 spider, 7 swing
    int speed = 0;  // Speed enum: 0 1x, 1 0.5x, 2 2x, 3 3x, 4 4x
    bool mini = false;
    bool dual = false;
    bool flip = false;

    bool operator==(SPState const&) const = default;
};

std::string modeName(int mode);
std::string speedName(int speed);
std::string describe(SPState const& s);
ccColor3B modeColor(int mode);
IconType modeIcon(int mode);

SPState stateOf(PlayerObject* player, bool dual);
SPState stateOf(LevelSettingsObject* settings);
void applyState(LevelSettingsObject* settings, SPState const& s);

// One StartPos as shown in the menus.
struct StartEntry {
    float x = 0, y = 0;
    SPState state;
    bool custom = false;   // placed with this mod
    bool fixed = false;    // settings corrected by the auto-fix
    int status = 0;        // 0 not checked, 1 checked and right, 2 fixed
};

// Saved per level: your own StartPos, the auto-fix results and your choice.
struct SavedStart {
    float x = 0, y = 0;
    SPState state;
};

struct LevelData {
    std::string key;
    bool hasChoice = false;       // false = start from the beginning
    float choiceX = 0;
    std::vector<SavedStart> custom;
    std::unordered_map<int, SPState> fixes;  // key: rounded x of a level StartPos
    std::unordered_map<int, int> status;     // key: rounded x, 1 right, 2 fixed

    void save();
};

// One sample of your run.
struct Sample {
    float t, x, y, rot;
    uint8_t mode, speed, flags;  // flags: 1 upside down, 2 mini, 4 dual
};

struct Attempt {
    float startX = 0;
    bool practice = false;
    bool completed = false;
    bool died = false;
    float endX = 0, endY = 0;
    float time = 0;
    int number = 0;
    std::vector<Sample> path;
    std::vector<CCPoint> clicks;

    float reach() const { return endX - startX; }
};

struct RunData {
    std::string key;
    int attempts = 0;
    int deaths = 0;
    int completions = 0;
    float bestFromStart = 0;   // furthest x reached starting from the beginning (normal mode)
    std::vector<CCPoint> deathPoints;
    std::vector<Attempt> bests;   // best attempt for every start x
    std::vector<Attempt> recent;  // last attempts
    bool dirty = false;

    void addAttempt(Attempt&& a);
    Attempt const* bestFor(float startX) const;
    void save();
};

std::string levelKey(GJGameLevel* level);
LevelData& levelData(GJGameLevel* level);
RunData& runData(GJGameLevel* level);

// Parsed level for the map and the menu.
struct ParsedLevel {
    float length = 0;
    float maxY = 0;
    SPState startState;
    std::vector<StartEntry> starts;  // level StartPos only
    // occupied map cells (30 units), type: 1 solid/deco, 2 orb/pad/portal, 3 hazard
    struct Run { int cy, cx0, cx1; uint8_t type; };
    std::vector<Run> cells;
};

std::shared_ptr<ParsedLevel> parseLevel(GJGameLevel* level);

// The list of StartPos for the menu: level ones with fixes + your own, sorted.
std::vector<StartEntry> mergedStarts(GJGameLevel* level, ParsedLevel const& parsed);

// Share codes
std::string makeShareCode(LevelData const& data);
int importShareCode(LevelData& data, std::string const& code, bool& otherLevel);

bool setting(char const* key);

// In-level helpers (Play.cpp). They do nothing when you are not in a level.
bool studioPlaying();
void studioSwitchToX(float x);  // x < 0 = from the beginning
void studioRebuild();           // reload your own StartPos after a change in the map
void setCascade(CCNode* node);  // makes a whole node tree fade together
