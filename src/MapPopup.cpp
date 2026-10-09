#include "MapPopup.hpp"

namespace {
    ccColor4F col(ccColor3B c, float a) {
        return {c.r / 255.f, c.g / 255.f, c.b / 255.f, a};
    }

    SPState sampleState(Sample const& s) {
        SPState st;
        st.mode = s.mode;
        st.speed = s.speed;
        st.flip = s.flags & 1;
        st.mini = s.flags & 2;
        st.dual = s.flags & 4;
        return st;
    }

    CCMenuItemSpriteExtra* textButton(char const* text, char const* texture, float scale, geode::Function<void(CCMenuItemSpriteExtra*)> cb) {
        auto spr = ButtonSprite::create(text, "bigFont.fnt", texture, 0.8f);
        spr->setScale(scale);
        return CCMenuItemExt::createSpriteExtra(spr, std::move(cb));
    }

    CCMenuItemSpriteExtra* arrowButton(bool right, float scale, geode::Function<void(CCMenuItemSpriteExtra*)> cb) {
        auto spr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
        spr->setScale(scale);
        spr->setFlipX(right);
        return CCMenuItemExt::createSpriteExtra(spr, std::move(cb));
    }
}

MapPopup* MapPopup::create(GJGameLevel* level, From from, CCNode* owner) {
    auto ret = new MapPopup();
    if (ret->init(level, from, owner)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool MapPopup::init(GJGameLevel* level, From from, CCNode* owner) {
    if (!Popup::init(440.f, 300.f)) return false;
    m_level = level;
    m_from = from;
    m_owner = owner;
    this->setTitle(fmt::format("{}: StartPos Map", std::string(level->m_levelName)));

    m_parsed = parseLevel(level);
    m_length = std::max(m_parsed->length, 300.f);

    // map area
    auto bg = CCLayerColor::create({0, 0, 0, 140}, m_mapSize.width, m_mapSize.height);
    bg->setPosition(m_mapOrigin);
    m_mainLayer->addChild(bg);
    auto stencil = CCLayerColor::create({255, 255, 255, 255}, m_mapSize.width, m_mapSize.height);
    auto clip = CCClippingNode::create(stencil);
    clip->setPosition(m_mapOrigin);
    m_mainLayer->addChild(clip, 1);
    m_canvas = CCNode::create();
    clip->addChild(m_canvas);
    m_draw = CCDrawNode::create();
    m_canvas->addChild(m_draw);
    m_overlay = CCDrawNode::create();
    m_canvas->addChild(m_overlay, 5);
    m_labels = CCNode::create();
    m_canvas->addChild(m_labels, 6);
    auto frame = CCDrawNode::create();
    CCPoint a = m_mapOrigin, b = m_mapOrigin + CCPoint(m_mapSize.width, m_mapSize.height);
    frame->drawRect(a, b, {0, 0, 0, 0}, 1.f, {1, 1, 1, 0.35f});
    m_mainLayer->addChild(frame, 2);

    m_stats = CCLabelBMFont::create("", "goldFont.fnt");
    m_stats->setPosition({220.f, 263.f});
    m_mainLayer->addChild(m_stats);

    // legend: what every thing on the map is
    auto legend = CCLabelBMFont::create(
        "Line = your best try (colour = gamemode)   White dots = your jumps   Flags = StartPos", "chatFont.fnt");
    legend->setScale(0.48f);
    legend->setPosition({220.f, 249.f});
    m_mainLayer->addChild(legend);
    auto hint = CCLabelBMFont::create("Drag to move.  Tap a flag to start there.  Tap your line or a jump to put a StartPos there.", "chatFont.fnt");
    hint->setScale(0.48f);
    hint->setColor({255, 230, 120});
    hint->setPosition({220.f, 86.f});
    m_mainLayer->addChild(hint);

    m_info = CCLabelBMFont::create("", "bigFont.fnt");
    m_info->setPosition({220.f, 72.f});
    m_mainLayer->addChild(m_info);

    // row 1: choose StartPos, play, place, delete
    m_buttonMenu->addChildAtPosition(arrowButton(false, 0.45f, [this](auto) { this->selectStart(m_sel - 1); }), Anchor::BottomLeft, {32.f, 46.f});
    m_buttonMenu->addChildAtPosition(arrowButton(true, 0.45f, [this](auto) { this->selectStart(m_sel + 1); }), Anchor::BottomLeft, {66.f, 46.f});
    m_playBtn = textButton(m_from == From::Pause ? "Go" : "Play", "GJ_button_01.png", 0.6f, [this](auto) { this->onPlay(); });
    m_buttonMenu->addChildAtPosition(m_playBtn, Anchor::BottomLeft, {130.f, 46.f});
    m_placeBtn = textButton("Place here", "GJ_button_02.png", 0.55f, [this](auto) { this->onPlace(); });
    m_buttonMenu->addChildAtPosition(m_placeBtn, Anchor::BottomLeft, {240.f, 46.f});
    m_deleteBtn = textButton("Delete", "GJ_button_06.png", 0.55f, [this](auto) { this->onDelete(); });
    m_buttonMenu->addChildAtPosition(m_deleteBtn, Anchor::BottomLeft, {360.f, 46.f});

    // row 2: which run you see, zoom, share codes
    m_showDeaths = Mod::get()->getSavedValue<bool>("map-deaths", false);
    m_deathsBtn = textButton(m_showDeaths ? "Deaths: on" : "Deaths: off", "GJ_button_04.png", 0.42f, [this](auto) { this->onToggleDeaths(); });
    m_buttonMenu->addChildAtPosition(m_deathsBtn, Anchor::BottomLeft, {48.f, 18.f});
    m_runLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_runLabel->setPosition({165.f, 18.f});
    m_mainLayer->addChild(m_runLabel);
    m_buttonMenu->addChildAtPosition(textButton("-", "GJ_button_04.png", 0.5f, [this](auto) { this->onZoom(1.f / 1.5f); }), Anchor::BottomLeft, {256.f, 18.f});
    m_buttonMenu->addChildAtPosition(textButton("+", "GJ_button_04.png", 0.5f, [this](auto) { this->onZoom(1.5f); }), Anchor::BottomLeft, {286.f, 18.f});
    m_buttonMenu->addChildAtPosition(textButton("Copy code", "GJ_button_04.png", 0.42f, [this](auto) { this->onCopy(); }), Anchor::BottomLeft, {340.f, 18.f});
    m_buttonMenu->addChildAtPosition(textButton("Paste", "GJ_button_04.png", 0.42f, [this](auto) { this->onPaste(); }), Anchor::BottomLeft, {404.f, 18.f});

    m_line = bestLine(runData(m_level));
    this->reload();
    float fx = m_sel > 0 ? m_starts[m_sel - 1].x : 0.f;
    float fy = m_sel > 0 ? m_starts[m_sel - 1].y : 105.f;
    this->focusX(fx, fy);
    return true;
}

int MapPopup::pct(float x) const {
    return std::clamp(static_cast<int>(x / m_length * 100.f), 0, 100);
}

void MapPopup::reload() {
    m_starts = mergedStarts(m_level, *m_parsed);
    auto& data = levelData(m_level);
    m_sel = 0;
    if (data.hasChoice && data.choiceX >= 0) {
        float best = 3.f;
        for (size_t i = 0; i < m_starts.size(); i++) {
            float d = std::abs(m_starts[i].x - data.choiceX);
            if (d < best) { best = d; m_sel = static_cast<int>(i) + 1; }
        }
    }
    this->updateStats();
    this->redraw();
    this->updateInfo();
}

void MapPopup::updateStats() {
    auto& runs = runData(m_level);
    std::string text = fmt::format("Attempts {}   Deaths {}   Best {}%", runs.attempts, runs.deaths, pct(runs.bestFromStart));
    if (!runs.deathPoints.empty()) {
        std::vector<int> bins(101, 0);
        for (auto& p : runs.deathPoints) bins[pct(p.x)]++;
        int worst = static_cast<int>(std::max_element(bins.begin(), bins.end()) - bins.begin());
        text += fmt::format("   Most deaths at {}%", worst);
    }
    m_stats->setString(text.c_str());
    m_stats->limitLabelWidth(400.f, 0.42f, 0.1f);

    if (m_line.path.empty()) {
        m_runLabel->setString("No tries yet: play the level!");
    }
    else {
        m_runLabel->setString(fmt::format("Best try: {}% to {}%  ({} jumps)", pct(m_line.from),
                                          m_line.completed ? 100 : pct(m_line.to), m_line.clicks.size()).c_str());
    }
    m_runLabel->limitLabelWidth(160.f, 0.35f, 0.1f);
}

void MapPopup::redraw() {
    m_draw->clear();
    m_labels->removeAllChildren();
    float s = m_zoom;

    // the level: blocks, orbs/pads/portals, hazards
    for (auto& r : m_parsed->cells) {
        ccColor4F c = r.type == 3 ? ccColor4F{0.9f, 0.3f, 0.3f, 0.45f}
                    : r.type == 2 ? ccColor4F{1.f, 0.8f, 0.25f, 0.45f}
                    : ccColor4F{0.6f, 0.65f, 0.8f, 0.22f};
        CCPoint a = {r.cx0 * 30.f * s, r.cy * 30.f * s};
        CCPoint b = {(r.cx1 + 1) * 30.f * s, (r.cy + 1) * 30.f * s};
        m_draw->drawRect(a, b, c, 0.f, c);
    }
    m_draw->drawSegment(toMap(0, LEVEL_Y_OFFSET), toMap(m_length, LEVEL_Y_OFFSET), 0.6f, {1, 1, 1, 0.5f});
    m_draw->drawSegment(toMap(m_length, LEVEL_Y_OFFSET), toMap(m_length, LEVEL_Y_OFFSET + 900), 0.8f, {0.3f, 1, 0.3f, 0.7f});
    for (int i = 1; i < 10; i++) {
        float x = m_length * i / 10.f;
        m_draw->drawSegment(toMap(x, LEVEL_Y_OFFSET - 25), toMap(x, LEVEL_Y_OFFSET), 0.4f, {1, 1, 1, 0.35f});
        auto l = CCLabelBMFont::create(fmt::format("{}%", i * 10).c_str(), "chatFont.fnt");
        l->setScale(0.4f);
        l->setOpacity(150);
        l->setPosition(toMap(x, LEVEL_Y_OFFSET - 40));
        m_labels->addChild(l);
    }

    // where you died (only if you turn it on)
    auto& runs = runData(m_level);
    if (m_showDeaths) {
        size_t from = runs.deathPoints.size() > 1500 ? runs.deathPoints.size() - 1500 : 0;
        for (size_t i = from; i < runs.deathPoints.size(); i++) {
            auto p = toMap(runs.deathPoints[i].x, runs.deathPoints[i].y);
            float r = 2.2f;
            m_draw->drawSegment({p.x - r, p.y - r}, {p.x + r, p.y + r}, 0.5f, {1, 0.25f, 0.25f, 0.6f});
            m_draw->drawSegment({p.x - r, p.y + r}, {p.x + r, p.y - r}, 0.5f, {1, 0.25f, 0.25f, 0.6f});
        }
    }

    // your best try: one thick line with a dark border, colour = gamemode, white dots = jumps
    for (int pass = 0; pass < 2; pass++) {
        CCPoint last;
        bool has = false;
        for (auto& smp : m_line.path) {
            auto p = toMap(smp.x, smp.y);
            if (smp.flags & 0x80) has = false;
            if (has && (p - last).getLength() < 1.5f && &smp != &m_line.path.back()) continue;
            if (has) {
                if (pass == 0) m_draw->drawSegment(last, p, 2.4f, {0, 0, 0, 0.8f});
                else m_draw->drawSegment(last, p, 1.4f, col(modeColor(smp.mode), 1.f));
            }
            last = p;
            has = true;
        }
    }
    for (auto& c : m_line.clicks) {
        auto p = toMap(c.x, c.y);
        m_draw->drawDot(p, 2.4f, {0, 0, 0, 0.85f});
        m_draw->drawDot(p, 1.6f, {1, 1, 1, 1});
    }

    // flags: start of the level + every StartPos
    auto flag = [&](float x, float y, ccColor4F c, std::string const& text) {
        auto p = toMap(x, y);
        m_draw->drawSegment({p.x, p.y - 3}, {p.x, p.y + 14}, 0.8f, c);
        CCPoint tri[3] = {{p.x, p.y + 14}, {p.x + 11, p.y + 10.5f}, {p.x, p.y + 7}};
        m_draw->drawPolygon(tri, 3, c, 0.f, c);
        auto l = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        l->setScale(0.3f);
        l->setPosition({p.x + 4, p.y + 21});
        m_labels->addChild(l);
    };
    flag(0, LEVEL_Y_OFFSET + 15, {0.35f, 1, 0.35f, 1}, "Start");
    for (size_t i = 0; i < m_starts.size(); i++) {
        auto& e = m_starts[i];
        ccColor4F c = e.custom ? ccColor4F{0.25f, 0.9f, 1, 1} : e.fixed ? ccColor4F{1, 0.85f, 0.2f, 1} : ccColor4F{1, 1, 1, 1};
        flag(e.x, e.y, c, fmt::format("{}", i + 1));
    }
    this->drawOverlay();
}

void MapPopup::drawOverlay() {
    m_overlay->clear();
    CCPoint p = m_sel == 0 ? toMap(0, LEVEL_Y_OFFSET + 15) : toMap(m_starts[m_sel - 1].x, m_starts[m_sel - 1].y);
    if (!m_point) {
        m_overlay->drawCircle({p.x + 3, p.y + 8}, 11.f, {0.3f, 1, 0.3f, 0.12f}, 1.2f, {0.3f, 1, 0.3f, 1}, 32);
    }
    else {
        auto q = toMap(m_point->x, m_point->y);
        m_overlay->drawCircle(q, 6.f, {1, 1, 0.2f, 0.25f}, 1.2f, {1, 1, 0.2f, 1}, 24);
    }
}

void MapPopup::updateInfo() {
    std::string text;
    bool custom = false;
    if (m_point) {
        auto st = sampleState(*m_point);
        text = m_pointJump > 0 ? fmt::format("Jump {} at {}%: {}", m_pointJump, pct(m_point->x), describe(st))
                               : fmt::format("Your run at {}%: {}", pct(m_point->x), describe(st));
    }
    else if (m_sel == 0) {
        text = fmt::format("From the start: {}", describe(m_parsed->startState));
    }
    else {
        auto& e = m_starts[m_sel - 1];
        custom = e.custom;
        text = fmt::format("StartPos {} at {}%: {}{}", m_sel, pct(e.x), describe(e.state),
                           e.custom ? " (yours)" : e.fixed ? " (fixed)" : e.status == 1 ? " (checked)" : "");
    }
    m_info->setString(text.c_str());
    m_info->limitLabelWidth(400.f, 0.36f, 0.1f);
    m_placeBtn->setVisible(m_point.has_value());
    m_deleteBtn->setVisible(!m_point && custom);
}

void MapPopup::clampCanvas() {
    float s = m_zoom;
    auto pos = m_canvas->getPosition();
    float minX = std::min(30.f, m_mapSize.width - m_length * s - 30.f);
    float top = std::max(m_parsed->maxY + 150.f, 450.f) * s;
    float minY = std::min(10.f, m_mapSize.height - top);
    pos.x = std::clamp(pos.x, minX, 30.f);
    pos.y = std::clamp(pos.y, minY, 10.f);
    m_canvas->setPosition(pos);
}

void MapPopup::focusX(float x, float y) {
    m_canvas->setPosition({m_mapSize.width * 0.3f - x * m_zoom, m_mapSize.height * 0.3f - y * m_zoom});
    this->clampCanvas();
}

void MapPopup::selectStart(int sel) {
    int n = static_cast<int>(m_starts.size());
    m_sel = ((sel % (n + 1)) + (n + 1)) % (n + 1);
    m_point.reset();
    auto& data = levelData(m_level);
    data.hasChoice = true;
    data.choiceX = m_sel == 0 ? -1.f : m_starts[m_sel - 1].x;
    data.save();
    float x = m_sel == 0 ? 0.f : m_starts[m_sel - 1].x;
    float y = m_sel == 0 ? LEVEL_Y_OFFSET + 15 : m_starts[m_sel - 1].y;
    // keep the flag on screen
    float px = m_canvas->getPositionX() + x * m_zoom;
    if (px < 20 || px > m_mapSize.width - 20) this->focusX(x, y);
    this->drawOverlay();
    this->updateInfo();
}

void MapPopup::handleTap(CCPoint mapPoint) {
    CCPoint c = mapPoint - m_canvas->getPosition();
    // a flag?
    int hit = -1;
    float best = 12.f;
    auto test = [&](int idx, float x, float y) {
        auto p = toMap(x, y) + CCPoint(3, 8);
        float d = (p - c).getLength();
        if (d < best) { best = d; hit = idx; }
    };
    test(0, 0, LEVEL_Y_OFFSET + 15);
    for (size_t i = 0; i < m_starts.size(); i++) test(static_cast<int>(i) + 1, m_starts[i].x, m_starts[i].y);
    if (hit >= 0) {
        this->selectStart(hit);
        return;
    }
    // a jump or a point of your run?
    m_point.reset();
    m_pointJump = -1;
    if (!m_line.path.empty()) {
        auto& line = m_line;
        auto nearestSample = [&](float x) -> Sample const* {
            Sample const* out = nullptr;
            float d = 1e9f;
            for (auto& s : line.path) {
                if (std::abs(s.x - x) < d) { d = std::abs(s.x - x); out = &s; }
            }
            return out;
        };
        float bestJump = 10.f;
        int jump = -1;
        for (size_t i = 0; i < line.clicks.size(); i++) {
            float d = (toMap(line.clicks[i].x, line.clicks[i].y) - c).getLength();
            if (d < bestJump) { bestJump = d; jump = static_cast<int>(i); }
        }
        if (jump >= 0) {
            if (auto s = nearestSample(line.clicks[jump].x)) {
                Sample p = *s;
                p.x = line.clicks[jump].x;
                p.y = line.clicks[jump].y;
                m_point = p;
                m_pointJump = jump + 1;
            }
        }
        else {
            float bestD = 12.f;
            for (auto& s : line.path) {
                float d = (toMap(s.x, s.y) - c).getLength();
                if (d < bestD) { bestD = d; m_point = s; }
            }
        }
        if (m_point) m_point->flags &= 0x7f;
    }
    this->drawOverlay();
    this->updateInfo();
}

void MapPopup::onPlay() {
    auto& data = levelData(m_level);
    data.hasChoice = true;
    data.choiceX = m_sel == 0 ? -1.f : m_starts[m_sel - 1].x;
    data.save();
    float x = data.choiceX;
    Ref<CCNode> owner = m_owner;
    From from = m_from;
    this->onClose(nullptr);
    if (from == From::Info) {
        if (auto l = typeinfo_cast<LevelInfoLayer*>(owner.data())) l->onPlay(nullptr);
    }
    else if (from == From::Editor) {
        if (auto l = typeinfo_cast<EditLevelLayer*>(owner.data())) l->onPlay(nullptr);
    }
    else {
        if (auto p = typeinfo_cast<PauseLayer*>(owner.data())) p->onResume(nullptr);
        studioSwitchToX(x);
    }
}

void MapPopup::onPlace() {
    if (!m_point) return;
    SavedStart c;
    c.x = m_point->x;
    c.y = m_point->y;
    c.state = sampleState(*m_point);
    auto& data = levelData(m_level);
    data.custom.push_back(c);
    std::sort(data.custom.begin(), data.custom.end(), [](auto& a, auto& b) { return a.x < b.x; });
    data.hasChoice = true;
    data.choiceX = c.x;
    data.save();
    if (m_from == From::Pause) studioRebuild();
    m_point.reset();
    this->reload();
    Notification::create(fmt::format("StartPos placed at {}%: {}", pct(c.x), describe(c.state)), NotificationIcon::Success)->show();
}

void MapPopup::onDelete() {
    if (m_sel == 0 || !m_starts[m_sel - 1].custom) return;
    float x = m_starts[m_sel - 1].x;
    auto& data = levelData(m_level);
    std::erase_if(data.custom, [&](auto& c) { return std::abs(c.x - x) < 0.5f; });
    int prev = m_sel - 1;
    data.hasChoice = true;
    data.choiceX = prev == 0 ? -1.f : m_starts[prev - 1].x;
    data.save();
    if (m_from == From::Pause) studioRebuild();
    this->reload();
    Notification::create("StartPos deleted", NotificationIcon::Success)->show();
}

void MapPopup::onCopy() {
    auto& data = levelData(m_level);
    if (data.custom.empty()) {
        Notification::create("Place some StartPos of your own first", NotificationIcon::Info)->show();
        return;
    }
    clipboard::write(makeShareCode(data));
    Notification::create(
        fmt::format("Code with {} StartPos copied! Paste it in the comments or on Discord", data.custom.size()),
        NotificationIcon::Success
    )->show();
}

void MapPopup::onPaste() {
    auto text = clipboard::read();
    LevelData tmp = levelData(m_level);
    bool other = false;
    int added = importShareCode(tmp, text, other);
    if (added < 0) {
        Notification::create("Copy a StartPos code first (it starts with SPS1:)", NotificationIcon::Error)->show();
        return;
    }
    if (added == 0) {
        Notification::create("You already have those StartPos", NotificationIcon::Info)->show();
        return;
    }
    Ref<MapPopup> self = this;
    auto custom = tmp.custom;
    auto commit = [self, custom, added]() {
        auto& data = levelData(self->m_level);
        data.custom = custom;
        data.save();
        if (self->m_from == From::Pause) studioRebuild();
        self->reload();
        Notification::create(fmt::format("{} StartPos added", added), NotificationIcon::Success)->show();
    };
    if (other) {
        createQuickPopup("Another level", "This code was made for a <cy>different level</c>. Add its StartPos here anyway?",
                         "Cancel", "Add", [commit](auto, bool yes) { if (yes) commit(); });
    }
    else {
        commit();
    }
}

void MapPopup::onZoom(float factor) {
    float old = m_zoom;
    float zoom = std::clamp(m_zoom * factor, 0.03f, 0.6f);
    if (zoom == old) return;
    auto pos = m_canvas->getPosition();
    float cx = (m_mapSize.width / 2 - pos.x) / old;
    float cy = (m_mapSize.height / 2 - pos.y) / old;
    m_zoom = zoom;
    m_canvas->setPosition({m_mapSize.width / 2 - cx * zoom, m_mapSize.height / 2 - cy * zoom});
    this->clampCanvas();
    this->redraw();
}

void MapPopup::onToggleDeaths() {
    m_showDeaths = !m_showDeaths;
    Mod::get()->setSavedValue<bool>("map-deaths", m_showDeaths);
    auto spr = ButtonSprite::create(m_showDeaths ? "Deaths: on" : "Deaths: off", "bigFont.fnt", "GJ_button_04.png", 0.8f);
    spr->setScale(0.42f);
    m_deathsBtn->setNormalImage(spr);
    this->redraw();
}

bool MapPopup::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    auto p = m_mainLayer->convertToNodeSpace(touch->getLocation());
    CCRect r = {m_mapOrigin.x, m_mapOrigin.y, m_mapSize.width, m_mapSize.height};
    if (r.containsPoint(p)) {
        m_dragging = true;
        m_moved = false;
        m_touchStart = p;
        m_canvasStart = m_canvas->getPosition();
        return true;
    }
    return Popup::ccTouchBegan(touch, event);
}

void MapPopup::ccTouchMoved(CCTouch* touch, CCEvent* event) {
    if (!m_dragging) return Popup::ccTouchMoved(touch, event);
    auto p = m_mainLayer->convertToNodeSpace(touch->getLocation());
    auto delta = p - m_touchStart;
    if (delta.getLength() > 5.f) m_moved = true;
    if (m_moved) {
        m_canvas->setPosition(m_canvasStart + delta);
        this->clampCanvas();
    }
}

void MapPopup::ccTouchEnded(CCTouch* touch, CCEvent* event) {
    if (!m_dragging) return Popup::ccTouchEnded(touch, event);
    m_dragging = false;
    if (!m_moved) {
        auto p = m_mainLayer->convertToNodeSpace(touch->getLocation());
        this->handleTap(p - m_mapOrigin);
    }
}

void MapPopup::ccTouchCancelled(CCTouch* touch, CCEvent* event) {
    m_dragging = false;
    Popup::ccTouchCancelled(touch, event);
}
