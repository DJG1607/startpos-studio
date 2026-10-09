#pragma once
#include "Common.hpp"

// The level map: StartPos flags, your runs (with every jump), your deaths.
// Tap a flag to pick where you start, tap your line to put a new StartPos there.
class MapPopup : public geode::Popup {
public:
    enum class From { Info, Editor, Pause };

    static MapPopup* create(GJGameLevel* level, From from, CCNode* owner);

protected:
    Ref<GJGameLevel> m_level;
    From m_from = From::Info;
    Ref<CCNode> m_owner;
    std::shared_ptr<ParsedLevel> m_parsed;
    std::vector<StartEntry> m_starts;
    int m_sel = 0;  // 0 = beginning, i = m_starts[i - 1]
    std::optional<Sample> m_point;
    int m_pointJump = -1;
    std::vector<Attempt const*> m_runs;
    int m_runIdx = 0;
    float m_zoom = 0.12f;
    float m_length = 1.f;

    CCSize m_mapSize = {400.f, 150.f};
    CCPoint m_mapOrigin = {20.f, 92.f};
    CCNode* m_canvas = nullptr;
    CCDrawNode* m_draw = nullptr;
    CCDrawNode* m_overlay = nullptr;
    CCNode* m_labels = nullptr;
    CCLabelBMFont* m_info = nullptr;
    CCLabelBMFont* m_stats = nullptr;
    CCLabelBMFont* m_runLabel = nullptr;
    CCMenuItemSpriteExtra* m_playBtn = nullptr;
    CCMenuItemSpriteExtra* m_placeBtn = nullptr;
    CCMenuItemSpriteExtra* m_deleteBtn = nullptr;

    bool m_dragging = false;
    bool m_moved = false;
    CCPoint m_touchStart;
    CCPoint m_canvasStart;

    bool init(GJGameLevel* level, From from, CCNode* owner);
    void reload();
    void collectRuns();
    void redraw();
    void drawOverlay();
    void updateInfo();
    void updateStats();
    void clampCanvas();
    void focusX(float x, float y);
    void selectStart(int sel);
    void handleTap(CCPoint mapPoint);

    void onPlay();
    void onPlace();
    void onDelete();
    void onCopy();
    void onPaste();
    void onZoom(float factor);
    void onRun(int dir);

    int pct(float x) const;
    CCPoint toMap(float x, float y) const { return {x * m_zoom, y * m_zoom}; }

    bool ccTouchBegan(CCTouch* touch, CCEvent* event) override;
    void ccTouchMoved(CCTouch* touch, CCEvent* event) override;
    void ccTouchEnded(CCTouch* touch, CCEvent* event) override;
    void ccTouchCancelled(CCTouch* touch, CCEvent* event) override;
};
