#include "Common.hpp"
#include <Geode/utils/base64.hpp>
#include <miniz.h>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <map>
#include <sstream>
#include <unordered_set>

// ---------------------------------------------------------------- names

std::string modeName(int mode) {
    static char const* names[] = {"Cube", "Ship", "Ball", "UFO", "Wave", "Robot", "Spider", "Swing"};
    return (mode >= 0 && mode < 8) ? names[mode] : "Cube";
}

std::string speedName(int speed) {
    switch (speed) {
        case 1: return "0.5x";
        case 2: return "2x";
        case 3: return "3x";
        case 4: return "4x";
        default: return "1x";
    }
}

std::string describe(SPState const& s) {
    auto out = modeName(s.mode) + " " + speedName(s.speed);
    if (s.mini) out += " mini";
    if (s.flip) out += " upside down";
    if (s.dual) out += " dual";
    return out;
}

ccColor3B modeColor(int mode) {
    switch (mode) {
        case 1: return {255, 90, 220};   // ship
        case 2: return {255, 80, 60};    // ball
        case 3: return {255, 170, 40};   // ufo
        case 4: return {60, 230, 255};   // wave
        case 5: return {235, 235, 235};  // robot
        case 6: return {170, 90, 255};   // spider
        case 7: return {255, 240, 60};   // swing
        default: return {90, 255, 90};   // cube
    }
}

IconType modeIcon(int mode) {
    if (mode < 0 || mode > 7) return IconType::Cube;
    return static_cast<IconType>(mode);
}

// ---------------------------------------------------------------- state

SPState stateOf(PlayerObject* p, bool dual) {
    SPState s;
    if (p->m_isShip) s.mode = 1;
    else if (p->m_isBall) s.mode = 2;
    else if (p->m_isBird) s.mode = 3;
    else if (p->m_isDart) s.mode = 4;
    else if (p->m_isRobot) s.mode = 5;
    else if (p->m_isSpider) s.mode = 6;
    else if (p->m_isSwing) s.mode = 7;
    float v = p->m_playerSpeed;  // 0.7, 0.9, 1.1, 1.3, 1.6
    if (v < 0.8f) s.speed = 1;
    else if (v < 1.0f) s.speed = 0;
    else if (v < 1.2f) s.speed = 2;
    else if (v < 1.45f) s.speed = 3;
    else s.speed = 4;
    s.mini = p->m_vehicleSize < 0.9f;
    s.flip = p->m_isUpsideDown;
    s.dual = dual;
    return s;
}

SPState stateOf(LevelSettingsObject* o) {
    SPState s;
    if (!o) return s;
    s.mode = o->m_startMode;
    s.speed = static_cast<int>(o->m_startSpeed);
    s.mini = o->m_startMini;
    s.dual = o->m_startDual;
    s.flip = o->m_isFlipped;
    return s;
}

void applyState(LevelSettingsObject* o, SPState const& s) {
    if (!o) return;
    o->m_startMode = s.mode;
    o->m_startSpeed = static_cast<Speed>(s.speed);
    o->m_startMini = s.mini;
    o->m_startDual = s.dual;
    o->m_isFlipped = s.flip;
}

static int packFlags(SPState const& s) {
    return (s.flip ? 1 : 0) | (s.mini ? 2 : 0) | (s.dual ? 4 : 0);
}

static SPState unpack(int mode, int speed, int flags) {
    SPState s;
    s.mode = std::clamp(mode, 0, 7);
    s.speed = std::clamp(speed, 0, 4);
    s.flip = flags & 1;
    s.mini = flags & 2;
    s.dual = flags & 4;
    return s;
}

bool setting(char const* key) {
    return Mod::get()->getSettingValue<bool>(key);
}

bool deathsVisible() {
    return Mod::get()->getSavedValue<bool>("map-deaths", false);
}

void setDeathsVisible(bool visible) {
    Mod::get()->setSavedValue<bool>("map-deaths", visible);
}

// ---------------------------------------------------------------- keys

std::string levelKey(GJGameLevel* level) {
    if (!level) return "none";
    if (level->m_levelType == GJLevelType::Editor) {
        std::string name = level->m_levelName;
        std::string out = "local_";
        for (char c : name) out += (std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
        return out;
    }
    auto id = level->m_levelID.value();
    if (level->m_levelType == GJLevelType::Main) return fmt::format("main_{}", id);
    return fmt::format("{}", id);
}

// ---------------------------------------------------------------- level data

static std::unordered_map<std::string, std::unique_ptr<LevelData>> s_levels;
static std::unordered_map<std::string, std::unique_ptr<RunData>> s_runs;

static double num(matjson::Value const& v, size_t i) {
    if (!v.isArray() || i >= v.size()) return 0;
    return v[i].asDouble().unwrapOr(0.0);
}

LevelData& levelData(GJGameLevel* level) {
    auto key = levelKey(level);
    auto& slot = s_levels[key];
    if (slot) return *slot;
    slot = std::make_unique<LevelData>();
    auto& d = *slot;
    d.key = key;
    auto json = Mod::get()->getSavedValue<matjson::Value>("lvl/" + key, matjson::Value::object());
    if (auto c = json.get("choice"); c.isOk() && c.unwrap().isNumber()) {
        d.hasChoice = true;
        d.choiceX = static_cast<float>(c.unwrap().asDouble().unwrapOr(0.0));
    }
    if (auto arr = json.get("custom"); arr.isOk() && arr.unwrap().isArray()) {
        for (auto& e : arr.unwrap()) {
            SavedStart s;
            s.x = num(e, 0);
            s.y = num(e, 1);
            s.state = unpack(num(e, 2), num(e, 3), num(e, 4));
            d.custom.push_back(s);
        }
    }
    if (auto arr = json.get("fixes"); arr.isOk() && arr.unwrap().isArray()) {
        for (auto& e : arr.unwrap()) {
            d.fixes[static_cast<int>(num(e, 0))] = unpack(num(e, 1), num(e, 2), num(e, 3));
        }
    }
    if (auto arr = json.get("status"); arr.isOk() && arr.unwrap().isArray()) {
        for (auto& e : arr.unwrap()) {
            d.status[static_cast<int>(num(e, 0))] = static_cast<int>(num(e, 1));
        }
    }
    if (auto arr = json.get("names"); arr.isOk() && arr.unwrap().isArray()) {
        for (auto& e : arr.unwrap()) {
            if (e.size() >= 2) d.names[static_cast<int>(num(e, 0))] = e[1].asString().unwrapOr("");
        }
    }
    if (auto arr = json.get("hidden"); arr.isOk() && arr.unwrap().isArray()) {
        for (auto& e : arr.unwrap()) d.hidden.insert(static_cast<int>(e.asDouble().unwrapOr(0.0)));
    }
    if (auto arr = json.get("routes"); arr.isOk() && arr.unwrap().isArray()) {
        for (auto& e : arr.unwrap()) {
            LevelData::Route r;
            r.startX = static_cast<float>(num(e, 0));
            r.endX = static_cast<float>(num(e, 1));
            r.done = static_cast<int>(num(e, 2));
            if (e.size() >= 4) r.name = e[3].asString().unwrapOr("");
            d.routes.push_back(r);
        }
    }
    if (auto a = json.get("active"); a.isOk() && a.unwrap().isNumber()) {
        d.active = static_cast<int>(a.unwrap().asDouble().unwrapOr(-1.0));
    }
    return d;
}

void LevelData::save() {
    auto json = matjson::Value::object();
    if (hasChoice) json.set("choice", matjson::Value(static_cast<double>(choiceX)));
    std::vector<matjson::Value> c;
    for (auto& s : custom) {
        c.push_back(matjson::Value(std::vector<matjson::Value>{
            matjson::Value(static_cast<double>(s.x)), matjson::Value(static_cast<double>(s.y)),
            matjson::Value(static_cast<double>(s.state.mode)), matjson::Value(static_cast<double>(s.state.speed)),
            matjson::Value(static_cast<double>(packFlags(s.state)))
        }));
    }
    json.set("custom", matjson::Value(c));
    std::vector<matjson::Value> f;
    for (auto& [x, s] : fixes) {
        f.push_back(matjson::Value(std::vector<matjson::Value>{
            matjson::Value(static_cast<double>(x)), matjson::Value(static_cast<double>(s.mode)),
            matjson::Value(static_cast<double>(s.speed)), matjson::Value(static_cast<double>(packFlags(s)))
        }));
    }
    json.set("fixes", matjson::Value(f));
    std::vector<matjson::Value> st;
    for (auto& [x, v] : status) {
        st.push_back(matjson::Value(std::vector<matjson::Value>{
            matjson::Value(static_cast<double>(x)), matjson::Value(static_cast<double>(v))
        }));
    }
    json.set("status", matjson::Value(st));
    std::vector<matjson::Value> nm, hd, rt;
    for (auto& [k, n] : names) {
        nm.push_back(matjson::Value(std::vector<matjson::Value>{matjson::Value(static_cast<double>(k)), matjson::Value(n)}));
    }
    for (int k : hidden) hd.push_back(matjson::Value(static_cast<double>(k)));
    for (auto& r : routes) {
        rt.push_back(matjson::Value(std::vector<matjson::Value>{
            matjson::Value(static_cast<double>(r.startX)), matjson::Value(static_cast<double>(r.endX)),
            matjson::Value(static_cast<double>(r.done)), matjson::Value(r.name)
        }));
    }
    json.set("names", matjson::Value(nm));
    json.set("hidden", matjson::Value(hd));
    json.set("routes", matjson::Value(rt));
    json.set("active", matjson::Value(static_cast<double>(active)));
    Mod::get()->setSavedValue<matjson::Value>("lvl/" + key, json);
}

int startKey(float x) {
    return x < 1.f ? -1 : static_cast<int>(std::round(x));
}

std::string startName(LevelData const& data, float x, int number) {
    if (auto it = data.names.find(startKey(x)); it != data.names.end() && !it->second.empty()) return it->second;
    return number <= 0 ? "Start" : fmt::format("StartPos {}", number);
}

// ---------------------------------------------------------------- run data

static std::filesystem::path runPath(std::string const& key) {
    return Mod::get()->getSaveDir() / "runs" / (key + ".txt");
}

RunData& runData(GJGameLevel* level) {
    auto key = levelKey(level);
    auto& slot = s_runs[key];
    if (slot) return *slot;
    slot = std::make_unique<RunData>();
    auto& r = *slot;
    r.key = key;
    auto text = file::readString(runPath(key));
    if (!text) return r;
    std::istringstream in(text.unwrap());
    std::string tag;
    in >> tag;
    if (tag != "SPSRUN") return r;
    int version;
    in >> version;
    while (in >> tag) {
        if (tag == "S") {
            in >> r.attempts >> r.deaths >> r.completions >> r.bestFromStart;
        }
        else if (tag == "D") {
            size_t n;
            in >> n;
            for (size_t i = 0; i < n && in; i++) {
                float x, y;
                in >> x >> y;
                r.deathPoints.push_back({x, y});
            }
        }
        else if (tag == "A" || tag == "R") {
            Attempt a;
            int practice, completed, died;
            size_t np, nc;
            in >> a.startX >> practice >> completed >> died >> a.endX >> a.endY >> a.time >> a.number >> np >> nc;
            a.practice = practice;
            a.completed = completed;
            a.died = died;
            a.path.reserve(np);
            for (size_t i = 0; i < np && in; i++) {
                Sample s;
                int m, sp, fl;
                in >> s.t >> s.x >> s.y >> s.rot >> m >> sp >> fl;
                s.mode = m;
                s.speed = sp;
                s.flags = fl;
                a.path.push_back(s);
            }
            for (size_t i = 0; i < nc && in; i++) {
                float x, y;
                in >> x >> y;
                a.clicks.push_back({x, y});
            }
            (tag == "A" ? r.bests : r.recent).push_back(std::move(a));
        }
        else break;
    }
    return r;
}

static void writeAttempt(std::string& out, char tag, Attempt const& a) {
    out += fmt::format("{} {:.1f} {} {} {} {:.1f} {:.1f} {:.2f} {} {} {}\n", tag, a.startX, a.practice ? 1 : 0,
                       a.completed ? 1 : 0, a.died ? 1 : 0, a.endX, a.endY, a.time, a.number, a.path.size(),
                       a.clicks.size());
    for (auto& s : a.path) {
        out += fmt::format("{:.2f} {:.1f} {:.1f} {:.0f} {} {} {} ", s.t, s.x, s.y, s.rot, (int)s.mode, (int)s.speed,
                           (int)s.flags);
    }
    out += "\n";
    for (auto& c : a.clicks) out += fmt::format("{:.1f} {:.1f} ", c.x, c.y);
    out += "\n";
}

void RunData::save() {
    if (!dirty) return;
    std::string out = "SPSRUN 1\n";
    out += fmt::format("S {} {} {} {:.1f}\n", attempts, deaths, completions, bestFromStart);
    out += fmt::format("D {}\n", deathPoints.size());
    for (auto& p : deathPoints) out += fmt::format("{:.0f} {:.0f} ", p.x, p.y);
    out += "\n";
    for (auto& a : bests) writeAttempt(out, 'A', a);
    for (auto& a : recent) writeAttempt(out, 'R', a);
    auto path = runPath(key);
    (void)file::createDirectoryAll(path.parent_path());
    if (auto res = file::writeStringSafe(path, out); !res) {
        log::warn("Could not save runs for {}: {}", key, res.unwrapErr());
        return;
    }
    dirty = false;
}

void RunData::addAttempt(Attempt&& a) {
    attempts++;
    a.number = attempts;
    if (a.died) {
        deaths++;
        deathPoints.push_back({a.endX, a.endY});
        if (deathPoints.size() > 4000) deathPoints.erase(deathPoints.begin(), deathPoints.begin() + 500);
    }
    if (a.completed) completions++;
    if (!a.practice && a.startX < 60) bestFromStart = std::max(bestFromStart, a.endX);
    dirty = true;
    if (a.path.size() < 2) return;

    // best attempt for this start (StartPos / checkpoint), by distance travelled
    int bucket = static_cast<int>(std::round(a.startX / 30.f));
    auto it = std::find_if(bests.begin(), bests.end(), [&](Attempt const& b) {
        return static_cast<int>(std::round(b.startX / 30.f)) == bucket;
    });
    if (it == bests.end()) bests.push_back(a);
    else if (a.completed > it->completed || (a.completed == it->completed && a.reach() > it->reach())) *it = a;
    if (bests.size() > 40) {
        auto worst = std::min_element(bests.begin(), bests.end(), [](auto& l, auto& r) { return l.reach() < r.reach(); });
        bests.erase(worst);
    }
    recent.push_back(std::move(a));
    if (recent.size() > 8) recent.erase(recent.begin());
}

Attempt const* RunData::bestFor(float startX) const {
    Attempt const* best = nullptr;
    for (auto& a : bests) {
        if (std::abs(a.startX - startX) > 20) continue;
        if (!best || a.reach() > best->reach()) best = &a;
    }
    return best;
}

BestLine bestLine(RunData const& runs) {
    BestLine out;
    std::vector<Attempt const*> all;
    for (auto& a : runs.bests) if (a.path.size() >= 2) all.push_back(&a);
    for (auto& a : runs.recent) if (a.path.size() >= 2) all.push_back(&a);
    if (all.empty()) return out;
    auto end = [](Attempt const* a) { return a->completed ? 1e9f : a->endX; };

    float reached = -1e9f;
    bool first = true;
    while (true) {
        Attempt const* pick = nullptr;
        // attempts that start where the line is now and go further
        for (auto a : all) {
            if (!first && a->startX > reached + 15) continue;
            if (end(a) <= reached + 30) continue;
            if (first && a->startX > 60) continue;
            if (!pick || end(a) > end(pick)) pick = a;
        }
        bool gap = false;
        if (!pick) {
            // nothing continues the line: jump to the next place you started from
            float next = 1e9f;
            for (auto a : all) if (a->startX > reached && end(a) > reached + 30) next = std::min(next, a->startX);
            if (next >= 1e9f) break;
            for (auto a : all) {
                if (std::abs(a->startX - next) > 15) continue;
                if (!pick || end(a) > end(pick)) pick = a;
            }
            if (!pick) break;
            gap = !first;
        }
        bool newPiece = gap || first;
        for (auto& smp : pick->path) {
            if (smp.x <= reached) continue;
            Sample s = smp;
            s.flags &= 0x7f;
            if (newPiece) { s.flags |= 0x80; newPiece = false; }
            out.path.push_back(s);
        }
        for (auto& c : pick->clicks) if (c.x > reached && c.x <= pick->endX + 1) out.clicks.push_back(c);
        if (first) out.from = pick->startX;
        first = false;
        reached = pick->endX;
        out.to = pick->endX;
        if (pick->completed) { out.completed = true; break; }
    }
    return out;
}

// ---------------------------------------------------------------- level parsing

static std::optional<std::string> gunzip(std::string const& raw) {
    auto p = reinterpret_cast<unsigned char const*>(raw.data());
    size_t n = raw.size();
    size_t skip = 0;
    int flags = 0;
    if (n >= 10 && p[0] == 0x1f && p[1] == 0x8b) {
        unsigned char flg = p[3];
        skip = 10;
        if (flg & 4) {
            if (skip + 2 > n) return std::nullopt;
            skip += 2 + (p[skip] | (p[skip + 1] << 8));
        }
        if (flg & 8) { while (skip < n && p[skip]) skip++; skip++; }
        if (flg & 16) { while (skip < n && p[skip]) skip++; skip++; }
        if (flg & 2) skip += 2;
    }
    else if (n >= 2 && p[0] == 0x78) {
        flags = TINFL_FLAG_PARSE_ZLIB_HEADER;
    }
    if (skip >= n) return std::nullopt;
    size_t outLen = 0;
    void* out = tinfl_decompress_mem_to_heap(p + skip, n - skip, &outLen, flags);
    if (!out) return std::nullopt;
    std::string result(static_cast<char*>(out), outLen);
    mz_free(out);
    return result;
}

static std::optional<std::string> levelObjects(GJGameLevel* level) {
    std::string s = level->m_levelString;
    while (!s.empty() && (s.back() == '\0' || std::isspace(static_cast<unsigned char>(s.back())))) s.pop_back();
    if (s.empty()) return std::nullopt;
    if (s.find(';') != std::string::npos && s.find(',') != std::string::npos && !s.starts_with("H4sI")) return s;
    auto raw = utils::base64::decodeString(s, utils::base64::Base64Variant::Url);
    if (!raw) raw = utils::base64::decodeString(s, utils::base64::Base64Variant::UrlWithPad);
    if (!raw) return std::nullopt;
    return gunzip(raw.unwrap());
}

static float toFloat(std::string_view v) {
    float out = 0;
    std::from_chars(v.data(), v.data() + v.size(), out);
    return out;
}

static int toInt(std::string_view v) {
    int out = 0;
    std::from_chars(v.data(), v.data() + v.size(), out);
    return out;
}

// objects that you do not see or touch while playing
static std::unordered_set<int> const& invisibleIds() {
    static std::unordered_set<int> ids = [] {
        std::unordered_set<int> s = {22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 55, 56, 57, 58, 59, 104, 105, 221,
            717, 718, 743, 744, 899, 900, 901, 914, 915, 1006, 1007, 1049, 1268, 1346, 1347, 1520, 1585, 1595, 1611,
            1612, 1613, 1615, 1616, 1811, 1812, 1814, 1815, 1817, 1818, 1819, 1912, 1913, 1914, 1915, 1916, 1917,
            1931, 1932, 1934, 1935, 2015, 2016, 2062, 2066, 2067, 2068, 2899, 2900, 2901, 2903, 2904, 2905, 2907,
            2909, 2910, 2911, 2912, 2913, 2914, 2915, 2916, 2917, 2919, 2920, 2921, 2922, 2923, 2924, 2925, 2999,
            3006, 3007, 3008, 3009, 3010, 3011, 3012, 3013, 3014, 3015, 3016, 3017, 3018, 3019, 3020, 3021, 3022,
            3023, 3024, 3029, 3030, 3031, 3032, 3033, 3600, 3602, 3603, 3604, 3605, 3606, 3607, 3608, 3609, 3612,
            3613, 3614, 3615, 3617, 3618, 3619, 3620, 3640, 3641, 3642, 3643, 3660, 3661, 3662};
        return s;
    }();
    return ids;
}

static std::unordered_set<int> const& hazardIds() {
    static std::unordered_set<int> ids = {8, 9, 39, 61, 88, 89, 98, 103, 135, 144, 145, 177, 178, 179, 183, 184, 185,
        186, 187, 188, 191, 198, 199, 205, 216, 217, 218, 243, 244, 363, 364, 365, 366, 367, 368, 392, 397, 398, 399,
        421, 422, 446, 447, 458, 459, 667, 678, 679, 680, 720, 740, 741, 742, 768, 918, 919, 989, 991, 1327, 1328,
        1582, 1619, 1620, 1701, 1702, 1703, 1705, 1706, 1707, 1708, 1709, 1710, 1711, 1712, 1713, 1714, 1715, 1716,
        1717, 1718, 1719, 1720, 1721, 1722, 1723, 1724, 1725, 1726, 1727, 1728, 1729, 1730, 1731, 1732, 1733, 1734,
        1735, 1736, 1889, 1890, 1891, 1892, 2012};
    return ids;
}

static std::unordered_set<int> const& specialIds() {
    static std::unordered_set<int> ids = {10, 11, 12, 13, 35, 36, 45, 46, 47, 67, 84, 99, 101, 111, 140, 141, 200,
        201, 202, 203, 286, 287, 660, 745, 747, 749, 1022, 1330, 1331, 1332, 1333, 1334, 1594, 1704, 1751, 1933,
        2063, 2064, 2902, 2926, 3004, 3005, 3027};
    return ids;
}

static void readState(std::string_view key, std::string_view val, SPState& st) {
    if (key == "kA2") st.mode = std::clamp(toInt(val), 0, 7);
    else if (key == "kA3") st.mini = toInt(val) != 0;
    else if (key == "kA4") st.speed = std::clamp(toInt(val), 0, 4);
    else if (key == "kA8") st.dual = toInt(val) != 0;
    else if (key == "kA11") st.flip = toInt(val) != 0;
}

static std::unordered_map<std::string, std::pair<size_t, std::shared_ptr<ParsedLevel>>> s_parsed;

std::shared_ptr<ParsedLevel> parseLevel(GJGameLevel* level) {
    auto key = levelKey(level);
    size_t size = level->m_levelString.size();
    if (auto it = s_parsed.find(key); it != s_parsed.end() && it->second.first == size) return it->second.second;

    auto result = std::make_shared<ParsedLevel>();
    auto data = levelObjects(level);
    if (!data) return result;
    std::string_view all = *data;
    std::unordered_map<int64_t, uint8_t> cells;
    bool header = true;
    size_t pos = 0;
    while (pos < all.size()) {
        size_t end = all.find(';', pos);
        if (end == std::string_view::npos) end = all.size();
        std::string_view obj = all.substr(pos, end - pos);
        pos = end + 1;
        if (obj.empty()) continue;

        int id = 0;
        float x = 0, y = 0, scale = 1;
        SPState st;
        size_t p = 0;
        while (p < obj.size()) {
            size_t c1 = obj.find(',', p);
            if (c1 == std::string_view::npos) break;
            size_t c2 = obj.find(',', c1 + 1);
            if (c2 == std::string_view::npos) c2 = obj.size();
            auto k = obj.substr(p, c1 - p);
            auto v = obj.substr(c1 + 1, c2 - c1 - 1);
            p = c2 + 1;
            if (k == "1") id = toInt(v);
            else if (k == "2") x = toFloat(v);
            else if (k == "3") y = toFloat(v);
            else if (k == "32") scale = toFloat(v);
            else if (k.starts_with("kA")) readState(k, v, st);
        }
        if (header) {
            header = false;
            result->startState = st;
            continue;
        }
        if (id <= 0) continue;
        y += LEVEL_Y_OFFSET;
        result->length = std::max(result->length, x);
        if (id == 31) {
            StartEntry e;
            e.x = x;
            e.y = y;
            e.state = st;
            result->starts.push_back(e);
            continue;
        }
        if (invisibleIds().contains(id)) continue;
        uint8_t type = hazardIds().contains(id) ? 3 : specialIds().contains(id) ? 2 : 1;
        if (y < 0 || y > 9000) continue;
        result->maxY = std::max(result->maxY, y);
        int r = scale > 1.5f ? 1 : 0;
        int cx = static_cast<int>(std::floor(x / 30.f)), cy = static_cast<int>(std::floor(y / 30.f));
        for (int dx = -r; dx <= r; dx++) {
            for (int dy = -r; dy <= r; dy++) {
                auto& c = cells[(static_cast<int64_t>(cy + dy) << 32) | static_cast<uint32_t>(cx + dx)];
                c = std::max(c, type);
            }
        }
    }
    std::sort(result->starts.begin(), result->starts.end(), [](auto& a, auto& b) { return a.x < b.x; });

    // merge cells into horizontal runs so the map stays light
    std::vector<std::pair<int64_t, uint8_t>> sorted(cells.begin(), cells.end());
    std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) {
        int ay = static_cast<int>(a.first >> 32), by = static_cast<int>(b.first >> 32);
        if (ay != by) return ay < by;
        return static_cast<int32_t>(a.first & 0xffffffff) < static_cast<int32_t>(b.first & 0xffffffff);
    });
    for (auto& [k, type] : sorted) {
        int cy = static_cast<int>(k >> 32);
        int cx = static_cast<int32_t>(k & 0xffffffff);
        auto& runs = result->cells;
        if (!runs.empty() && runs.back().cy == cy && runs.back().cx1 == cx - 1 && runs.back().type == type) {
            runs.back().cx1 = cx;
        }
        else {
            runs.push_back({cy, cx, cx, type});
        }
    }
    s_parsed[key] = {size, result};
    return result;
}

std::vector<StartEntry> mergedStarts(GJGameLevel* level, ParsedLevel const& parsed) {
    auto& data = levelData(level);
    std::vector<StartEntry> out;
    for (auto e : parsed.starts) {
        int k = static_cast<int>(std::round(e.x));
        if (data.hidden.contains(startKey(e.x))) continue;
        if (auto f = data.fixes.find(k); f != data.fixes.end()) {
            e.state = f->second;
            e.fixed = true;
        }
        if (auto s = data.status.find(k); s != data.status.end()) e.status = s->second;
        out.push_back(e);
    }
    for (auto& c : data.custom) {
        StartEntry e;
        e.x = c.x;
        e.y = c.y;
        e.state = c.state;
        e.custom = true;
        e.status = 1;
        out.push_back(e);
    }
    std::stable_sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.x < b.x; });
    for (auto& e : out) {
        if (auto it = data.names.find(startKey(e.x)); it != data.names.end()) e.name = it->second;
    }
    return out;
}

// ---------------------------------------------------------------- share codes

std::string makeShareCode(LevelData const& data) {
    std::string body = data.key + "|";
    for (auto& s : data.custom) {
        body += fmt::format("{:.1f},{:.1f},{},{},{};", s.x, s.y, s.state.mode, s.state.speed, packFlags(s.state));
    }
    return "SPS1:" + utils::base64::encode(body, utils::base64::Base64Variant::Url);
}

int importShareCode(LevelData& data, std::string const& rawCode, bool& otherLevel) {
    otherLevel = false;
    std::string code;
    for (char c : rawCode) if (!std::isspace(static_cast<unsigned char>(c))) code += c;
    auto at = code.find("SPS1:");
    if (at == std::string::npos) return -1;
    code = code.substr(at + 5);
    auto body = utils::base64::decodeString(code, utils::base64::Base64Variant::Url);
    if (!body) body = utils::base64::decodeString(code, utils::base64::Base64Variant::UrlWithPad);
    if (!body) return -1;
    auto text = body.unwrap();
    auto bar = text.find('|');
    if (bar == std::string::npos) return -1;
    otherLevel = text.substr(0, bar) != data.key;
    int added = 0;
    std::stringstream entries(text.substr(bar + 1));
    std::string item;
    while (std::getline(entries, item, ';')) {
        float v[5] = {0, 0, 0, 0, 0};
        std::stringstream fields(item);
        std::string f;
        int i = 0;
        while (i < 5 && std::getline(fields, f, ',')) v[i++] = toFloat(f);
        if (i < 5) continue;
        bool dup = std::any_of(data.custom.begin(), data.custom.end(), [&](auto& c) { return std::abs(c.x - v[0]) < 2; });
        if (dup) continue;
        data.custom.push_back({v[0], v[1], unpack(v[2], v[3], v[4])});
        added++;
    }
    std::sort(data.custom.begin(), data.custom.end(), [](auto& a, auto& b) { return a.x < b.x; });
    return added;
}
