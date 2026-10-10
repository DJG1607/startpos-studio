#pragma once
#include "Common.hpp"

// The level map: your StartPos as flags, your best try as one line (with every jump and
// the % along it) and your active route. Tap a flag to choose it, tap the line to add a StartPos.
class MapPopup : public geode::Popup {
public:
    enum class From { Info, Editor, Pause };

    static MapPopup* create(GJGameLevel* level, From from, CCNode* owner);

    // used by the small popups (routes, names, share)
    GJGameLevel* level() const { return m_level; }
    int pct(float x) const;
    std::string selName() const;
    float selStart() const;
    void beginNewRoute();
    void playRoute(int index);
    void refreshAll();
    void doCopy();
    void doPaste();
    void restoreHidden();

protected:
    Ref<GJGameLevel> m_level;
    From m_from = From::Info;
    Ref<CCNode> m_owner;
    std::shared_ptr<ParsedLevel> m_parsed;
    std::vector<StartEntry> m_starts;
    int m_sel = 0;  // 0 = start of the level, i = m_starts[i - 1]
    std::optional<Sample> m_point;
    int m_pointJump = -1;
    BestLine m_line;
    bool m_showDeaths = false;
    bool m_pickingEnd = false;  // making a new route: next tap is its end
    float m_zoom = 0.15f;
    float m_length = 1.f;

    CCSize m_mapSize = {400.f, 150.f};
    CCPoint m_mapOrigin = {20.f, 92.f};
    CCNode* m_canvas = nullptr;
    CCDrawNode* m_draw = nullptr;
    CCDrawNode* m_overlay = nullptr;
    CCNode* m_labels = nullptr;
    CCLabelBMFont* m_endLabel = nullptr;
    CCLabelBMFont* m_info = nullptr;
    CCLabelBMFont* m_hint = nullptr;
    CCLabelBMFont* m_stats = nullptr;

    CCMenuItemSpriteExtra* m_playBtn = nullptr;
    CCMenuItemSpriteExtra* m_renameBtn = nullptr;
    CCMenuItemSpriteExtra* m_deleteBtn = nullptr;
    CCMenuItemSpriteExtra* m_placeBtn = nullptr;
    CCMenuItemSpriteExtra* m_deathsBtn = nullptr;
    CCMenuItemSpriteExtra* m_watchBtn = nullptr;

    // Watch: your best try played on the map
    bool m_watching = false;
    float m_watchT = 0;
    size_t m_watchIdx = 0;
    std::vector<float> m_watchTimes;
    SimplePlayer* m_watchIcon = nullptr;
    int m_watchMode = -1;

    bool m_dragging = false;
    bool m_moved = false;
    CCPoint m_touchStart;
    CCPoint m_canvasStart;

    bool init(GJGameLevel* level, From from, CCNode* owner);
    void reload();
    void redraw();
    void drawOverlay();
    void updateInfo();
    void updateStats();
    void clampCanvas();
    void focusX(float x, float y);
    void selectStart(int sel);
    void handleTap(CCPoint mapPoint);
    void setText(CCMenuItemSpriteExtra* item, char const* text);

    void onPlay();
    void onPlace();
    void onRename();
    void onDelete();
    void onZoom(float factor);
    void onToggleDeaths();
    void onWatch();
    void stopWatch();
    void onTick(float dt);

    CCPoint toMap(float x, float y) const { return {x * m_zoom, y * m_zoom}; }

    bool ccTouchBegan(CCTouch* touch, CCEvent* event) override;
    void ccTouchMoved(CCTouch* touch, CCEvent* event) override;
    void ccTouchEnded(CCTouch* touch, CCEvent* event) override;
    void ccTouchCancelled(CCTouch* touch, CCEvent* event) override;
};

// Type a name (StartPos, route).
class NamePopup : public geode::Popup {
public:
    static NamePopup* create(std::string const& title, std::string const& current, geode::Function<void(std::string)> done);

protected:
    TextInput* m_input = nullptr;
    geode::Function<void(std::string)> m_done;
    bool init(std::string const& title, std::string const& current, geode::Function<void(std::string)> done);
};

// Your routes: play, rename, delete, make a new one.
class RoutesPopup : public geode::Popup {
public:
    static RoutesPopup* create(MapPopup* map);

protected:
    Ref<MapPopup> m_map;
    CCNode* m_list = nullptr;
    int m_page = 0;
    bool init(MapPopup* map);
    void build();
};

// Share codes and bringing back deleted StartPos.
class SharePopup : public geode::Popup {
public:
    static SharePopup* create(MapPopup* map);

protected:
    Ref<MapPopup> m_map;
    bool init(MapPopup* map);
};
