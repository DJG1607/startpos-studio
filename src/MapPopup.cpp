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

    CCMenu* fixedMenu(CCNode* parent) {
        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        parent->addChild(menu, 10);
        return menu;
    }

    void put(CCMenu* menu, CCMenuItemSpriteExtra* item, float x, float y) {
        item->setPosition({x, y});
        menu->addChild(item);
    }

    void routeName(LevelData::Route& r, MapPopup const* map) {
        if (r.name.empty()) r.name = fmt::format("{}% to {}%", map->pct(r.startX), map->pct(r.endX));
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

    // the map
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
    m_endLabel = CCLabelBMFont::create("END", "bigFont.fnt");
    m_endLabel->setScale(0.3f);
    m_endLabel->setColor({255, 90, 90});
    m_canvas->addChild(m_endLabel, 7);
    auto frame = CCDrawNode::create();
    CCPoint a = m_mapOrigin, b = m_mapOrigin + CCPoint(m_mapSize.width, m_mapSize.height);
    frame->drawRect(a, b, {0, 0, 0, 0}, 1.f, {1, 1, 1, 0.35f});
    m_mainLayer->addChild(frame, 2);

    m_stats = CCLabelBMFont::create("", "goldFont.fnt");
    m_stats->setPosition({220.f, 262.f});
    m_mainLayer->addChild(m_stats);
    auto legend = CCLabelBMFont::create("Line = your best try (with %)   Dots = your jumps   Flags = StartPos   Green = your route", "chatFont.fnt");
    legend->setScale(0.46f);
    legend->setOpacity(200);
    legend->setPosition({220.f, 249.f});
    m_mainLayer->addChild(legend);

    m_info = CCLabelBMFont::create("", "bigFont.fnt");
    m_info->setPosition({220.f, 80.f});
    m_mainLayer->addChild(m_info);
    m_hint = CCLabelBMFont::create("", "chatFont.fnt");
    m_hint->setColor({255, 230, 120});
    m_hint->setPosition({220.f, 64.f});
    m_mainLayer->addChild(m_hint);

    auto menu = fixedMenu(m_mainLayer);
    // row 1: what to do with the chosen StartPos (or the point of your line)
    put(menu, arrowButton(false, 0.42f, [this](auto) { this->selectStart(m_sel - 1); }), 30.f, 40.f);
    put(menu, arrowButton(true, 0.42f, [this](auto) { this->selectStart(m_sel + 1); }), 62.f, 40.f);
    m_playBtn = textButton(m_from == From::Pause ? "Go" : "Play", "GJ_button_01.png", 0.6f, [this](auto) { this->onPlay(); });
    put(menu, m_playBtn, 120.f, 40.f);
    m_renameBtn = textButton("Rename", "GJ_button_05.png", 0.5f, [this](auto) { this->onRename(); });
    put(menu, m_renameBtn, 205.f, 40.f);
    m_deleteBtn = textButton("Delete", "GJ_button_06.png", 0.5f, [this](auto) { this->onDelete(); });
    put(menu, m_deleteBtn, 285.f, 40.f);
    m_placeBtn = textButton("Add StartPos here", "GJ_button_02.png", 0.5f, [this](auto) { this->onPlace(); });
    put(menu, m_placeBtn, 245.f, 40.f);

    // row 2: routes, watch, deaths, zoom, share
    put(menu, textButton("Routes", "GJ_button_01.png", 0.45f, [this](auto) {
        if (auto p = RoutesPopup::create(this)) p->show();
    }), 52.f, 14.f);
    m_watchBtn = textButton("Watch", "GJ_button_04.png", 0.45f, [this](auto) { this->onWatch(); });
    put(menu, m_watchBtn, 124.f, 14.f);
    m_showDeaths = deathsVisible();
    m_deathsBtn = textButton(m_showDeaths ? "Deaths: on" : "Deaths: off", "GJ_button_04.png", 0.42f, [this](auto) { this->onToggleDeaths(); });
    put(menu, m_deathsBtn, 208.f, 14.f);
    put(menu, textButton("-", "GJ_button_04.png", 0.5f, [this](auto) { this->onZoom(1.f / 1.5f); }), 276.f, 14.f);
    put(menu, textButton("+", "GJ_button_04.png", 0.5f, [this](auto) { this->onZoom(1.5f); }), 304.f, 14.f);
    put(menu, textButton("Share", "GJ_button_04.png", 0.45f, [this](auto) {
        if (auto p = SharePopup::create(this)) p->show();
    }), 372.f, 14.f);

    m_line = bestLine(runData(m_level));
    this->reload();
    float fx = m_sel > 0 ? m_starts[m_sel - 1].x : 0.f;
    float fy = m_sel > 0 ? m_starts[m_sel - 1].y : 105.f;
    this->focusX(fx, fy);
    handleTouchPriority(this);
    return true;
}

int MapPopup::pct(float x) const {
    return std::clamp(static_cast<int>(x / m_length * 100.f), 0, 100);
}

float MapPopup::selStart() const {
    return m_sel == 0 ? 0.f : m_starts[m_sel - 1].x;
}

std::string MapPopup::selName() const {
    return startName(levelData(m_level), this->selStart(), m_sel);
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

void MapPopup::refreshAll() {
    this->reload();
}

void MapPopup::updateStats() {
    auto& runs = runData(m_level);
    std::string text = fmt::format("Attempts {}   Best {}%", runs.attempts, pct(runs.bestFromStart));
    if (m_showDeaths) {
        text += fmt::format("   Deaths {}", runs.deaths);
        if (!runs.deathPoints.empty()) {
            std::vector<int> bins(101, 0);
            for (auto& p : runs.deathPoints) bins[pct(p.x)]++;
            text += fmt::format(" (most at {}%)", static_cast<int>(std::max_element(bins.begin(), bins.end()) - bins.begin()));
        }
    }
    if (m_line.path.empty()) text += "   No tries yet";
    else text += fmt::format("   Best try: {}% to {}% ({} jumps)", pct(m_line.from), m_line.completed ? 100 : pct(m_line.to),
                             m_line.clicks.size());
    m_stats->setString(text.c_str());
    m_stats->limitLabelWidth(410.f, 0.42f, 0.1f);
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

    // your best try: thick line with a dark border, colour = gamemode
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

    // the % on your line (every 10%, every 5% when you zoom in)
    int step = m_zoom >= 0.2f ? 5 : 10;
    if (!m_line.path.empty()) {
        float minX = m_line.path.front().x, maxX = m_line.path.back().x;
        for (int pc = step; pc < 100; pc += step) {
            float x = m_length * pc / 100.f;
            if (x < minX || x > maxX) continue;
            Sample const* closest = nullptr;
            float d = 1e9f;
            for (auto& smp : m_line.path) {
                if (std::abs(smp.x - x) < d) { d = std::abs(smp.x - x); closest = &smp; }
            }
            if (!closest || d > 60.f) continue;
            auto p = toMap(closest->x, closest->y);
            m_draw->drawDot(p, 2.6f, {1, 0.85f, 0.2f, 1});
            auto l = CCLabelBMFont::create(fmt::format("{}%", pc).c_str(), "bigFont.fnt");
            l->setScale(0.26f);
            l->setColor({255, 220, 60});
            l->setPosition({p.x, p.y - 9});
            m_labels->addChild(l);
        }
    }
    else {
        for (int pc = 10; pc < 100; pc += 10) {
            float x = m_length * pc / 100.f;
            m_draw->drawSegment(toMap(x, LEVEL_Y_OFFSET - 25), toMap(x, LEVEL_Y_OFFSET), 0.4f, {1, 1, 1, 0.35f});
            auto l = CCLabelBMFont::create(fmt::format("{}%", pc).c_str(), "chatFont.fnt");
            l->setScale(0.4f);
            l->setOpacity(150);
            l->setPosition(toMap(x, LEVEL_Y_OFFSET - 40));
            m_labels->addChild(l);
        }
    }

    // flags: start of the level + every StartPos, with their names
    auto& data = levelData(m_level);
    auto flag = [&](float x, float y, ccColor4F c, std::string const& text) {
        auto p = toMap(x, y);
        m_draw->drawSegment({p.x, p.y - 3}, {p.x, p.y + 14}, 0.8f, c);
        CCPoint tri[3] = {{p.x, p.y + 14}, {p.x + 11, p.y + 10.5f}, {p.x, p.y + 7}};
        m_draw->drawPolygon(tri, 3, c, 0.f, c);
        auto l = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        l->setScale(0.28f);
        l->setPosition({p.x + 4, p.y + 21});
        m_labels->addChild(l);
    };
    flag(0, LEVEL_Y_OFFSET + 15, {0.35f, 1, 0.35f, 1}, startName(data, 0, 0));
    for (size_t i = 0; i < m_starts.size(); i++) {
        auto& e = m_starts[i];
        ccColor4F c = e.custom ? ccColor4F{0.25f, 0.9f, 1, 1} : e.fixed ? ccColor4F{1, 0.85f, 0.2f, 1} : ccColor4F{1, 1, 1, 1};
        flag(e.x, e.y, c, e.name.empty() ? fmt::format("{}", i + 1) : e.name);
    }
    this->drawOverlay();
}

void MapPopup::drawOverlay() {
    m_overlay->clear();
    // your active route: green from its StartPos to its end, with a chequered flag
    auto& data = levelData(m_level);
    m_endLabel->setVisible(false);
    if (auto r = data.activeRoute()) {
        float top = std::max(m_parsed->maxY + 150.f, 450.f);
        m_overlay->drawRect(toMap(r->startX, 0), toMap(r->endX, top), {0.3f, 1, 0.3f, 0.09f}, 0.f, {0, 0, 0, 0});
        auto e0 = toMap(r->endX, LEVEL_Y_OFFSET), e1 = toMap(r->endX, LEVEL_Y_OFFSET + 300);
        m_overlay->drawSegment(e0, e1, 1.f, {1, 0.35f, 0.35f, 0.9f});
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 2; j++) {
                bool white = (i + j) % 2 == 0;
                CCPoint a = {e1.x + i * 3.f, e1.y - (j + 1) * 3.f};
                CCPoint b = {a.x + 3.f, a.y + 3.f};
                ccColor4F c = white ? ccColor4F{1, 1, 1, 1} : ccColor4F{0.1f, 0.1f, 0.1f, 1};
                m_overlay->drawRect(a, b, c, 0.f, c);
            }
        }
        m_endLabel->setString(fmt::format("END {}%", pct(r->endX)).c_str());
        m_endLabel->setPosition({e1.x + 8, e1.y + 7});
        m_endLabel->setVisible(true);
    }
    // what you chose
    if (m_point) {
        auto q = toMap(m_point->x, m_point->y);
        m_overlay->drawCircle(q, 6.f, {1, 1, 0.2f, 0.25f}, 1.2f, {1, 1, 0.2f, 1}, 24);
    }
    else {
        CCPoint p = m_sel == 0 ? toMap(0, LEVEL_Y_OFFSET + 15) : toMap(m_starts[m_sel - 1].x, m_starts[m_sel - 1].y);
        m_overlay->drawCircle({p.x + 3, p.y + 8}, 11.f, {0.3f, 1, 0.3f, 0.12f}, 1.2f, {0.3f, 1, 0.3f, 1}, 32);
    }
}

void MapPopup::updateInfo() {
    std::string text, hint;
    bool custom = false;
    if (m_pickingEnd) {
        text = fmt::format("New route from {} ({}%)", this->selName(), pct(this->selStart()));
        hint = "Tap the map where the route should end";
    }
    else if (m_point) {
        auto st = sampleState(*m_point);
        text = m_pointJump > 0 ? fmt::format("Jump {} at {}%: {}", m_pointJump, pct(m_point->x), describe(st))
                               : fmt::format("Your line at {}%: {}", pct(m_point->x), describe(st));
        hint = "Press Add StartPos here to put one exactly there";
    }
    else if (m_sel == 0) {
        text = fmt::format("{}: {}", this->selName(), describe(m_parsed->startState));
        hint = "Tap a flag to choose it, tap your line to add a StartPos, drag to move";
    }
    else {
        auto& e = m_starts[m_sel - 1];
        custom = e.custom;
        text = fmt::format("{} at {}%: {}{}", this->selName(), pct(e.x), describe(e.state),
                           e.custom ? " (yours)" : e.fixed ? " (fixed)" : e.status == 1 ? " (checked)" : "");
        hint = "Play starts here. Routes lets you practise from here to where you want.";
    }
    (void)custom;
    m_info->setString(text.c_str());
    m_info->limitLabelWidth(400.f, 0.36f, 0.1f);
    m_hint->setString(hint.c_str());
    m_hint->limitLabelWidth(400.f, 0.5f, 0.1f);
    bool onLine = m_point.has_value();
    m_placeBtn->setVisible(onLine && !m_pickingEnd);
    m_renameBtn->setVisible(!onLine && !m_pickingEnd);
    m_deleteBtn->setVisible(!onLine && !m_pickingEnd && m_sel > 0);
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
    m_pickingEnd = false;
    auto& data = levelData(m_level);
    data.hasChoice = true;
    data.choiceX = m_sel == 0 ? -1.f : m_starts[m_sel - 1].x;
    data.save();
    float x = this->selStart();
    float y = m_sel == 0 ? LEVEL_Y_OFFSET + 15 : m_starts[m_sel - 1].y;
    float px = m_canvas->getPositionX() + x * m_zoom;
    if (px < 20 || px > m_mapSize.width - 20) this->focusX(x, y);
    this->drawOverlay();
    this->updateInfo();
}

void MapPopup::handleTap(CCPoint mapPoint) {
    CCPoint c = mapPoint - m_canvas->getPosition();
    if (m_pickingEnd) {
        float x = std::min(c.x / m_zoom, m_length);
        m_pickingEnd = false;
        if (x <= this->selStart() + 30) {
            Notification::create("The end has to be after the start", NotificationIcon::Info)->show();
            this->updateInfo();
            return;
        }
        auto& data = levelData(m_level);
        LevelData::Route r;
        r.startX = this->selStart();
        r.endX = x;
        routeName(r, this);
        data.routes.push_back(r);
        data.active = static_cast<int>(data.routes.size()) - 1;
        data.save();
        Notification::create(fmt::format("Route \"{}\" ready: press Play", r.name), NotificationIcon::Success)->show();
        this->drawOverlay();
        this->updateInfo();
        return;
    }
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
    // a jump or a point of your line?
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

void MapPopup::setText(CCMenuItemSpriteExtra* item, char const* text) {
    if (auto spr = typeinfo_cast<ButtonSprite*>(item->getNormalImage())) {
        spr->setString(text);
        item->setContentSize(spr->getScaledContentSize());
        spr->setPosition(item->getContentSize() / 2);
    }
}

// ---------------------------------------------------------------- buttons

void MapPopup::onPlay() {
    auto& data = levelData(m_level);
    data.hasChoice = true;
    data.choiceX = m_sel == 0 ? -1.f : m_starts[m_sel - 1].x;
    // a route only applies when you start on its StartPos
    if (auto r = data.activeRoute(); r && std::abs(r->startX - this->selStart()) > 20) data.active = -1;
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

void MapPopup::playRoute(int index) {
    auto& data = levelData(m_level);
    if (index < 0 || index >= static_cast<int>(data.routes.size())) return;
    float sx = data.routes[index].startX;
    this->reload();
    int sel = 0;
    float best = 3.f;
    for (size_t i = 0; i < m_starts.size(); i++) {
        float d = std::abs(m_starts[i].x - sx);
        if (d < best) { best = d; sel = static_cast<int>(i) + 1; }
    }
    if (sx >= 1.f && sel == 0) {
        Notification::create("The StartPos of this route is gone", NotificationIcon::Error)->show();
        return;
    }
    m_sel = sel;
    data.active = index;
    data.save();
    this->onPlay();
}

void MapPopup::beginNewRoute() {
    m_pickingEnd = true;
    m_point.reset();
    if (m_watching) this->stopWatch();
    this->drawOverlay();
    this->updateInfo();
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
    Notification::create(fmt::format("StartPos added at {}%: {}", pct(c.x), describe(c.state)), NotificationIcon::Success)->show();
}

void MapPopup::onRename() {
    Ref<MapPopup> self = this;
    float x = this->selStart();
    auto popup = NamePopup::create(fmt::format("Name for {}", this->selName()), this->selName(), [self, x](std::string name) {
        auto& data = levelData(self->m_level);
        if (name.empty()) data.names.erase(startKey(x));
        else data.names[startKey(x)] = name;
        data.save();
        self->reload();
    });
    if (popup) popup->show();
}

void MapPopup::onDelete() {
    if (m_sel == 0) return;
    auto e = m_starts[m_sel - 1];
    auto& data = levelData(m_level);
    if (e.custom) std::erase_if(data.custom, [&](auto& c) { return std::abs(c.x - e.x) < 0.5f; });
    else data.hidden.insert(startKey(e.x));  // a StartPos of the level: hidden for you
    data.names.erase(startKey(e.x));
    int prev = m_sel - 1;
    data.hasChoice = true;
    data.choiceX = prev == 0 ? -1.f : m_starts[prev - 1].x;
    data.save();
    if (m_from == From::Pause) studioRebuild();
    this->reload();
    Notification::create(e.custom ? "StartPos deleted" : "StartPos hidden (Share > Bring back to see it again)",
                         NotificationIcon::Success)->show();
}

void MapPopup::restoreHidden() {
    auto& data = levelData(m_level);
    size_t n = data.hidden.size();
    data.hidden.clear();
    data.save();
    if (m_from == From::Pause) studioRebuild();
    this->reload();
    Notification::create(fmt::format("{} StartPos back", n), NotificationIcon::Success)->show();
}

void MapPopup::doCopy() {
    auto& data = levelData(m_level);
    if (data.custom.empty()) {
        Notification::create("Add some StartPos of your own first", NotificationIcon::Info)->show();
        return;
    }
    clipboard::write(makeShareCode(data));
    Notification::create(fmt::format("Code with {} StartPos copied! Paste it in the comments or on Discord", data.custom.size()),
                         NotificationIcon::Success)->show();
}

void MapPopup::doPaste() {
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
    setDeathsVisible(m_showDeaths);
    studioDeathsChanged();
    this->setText(m_deathsBtn, m_showDeaths ? "Deaths: on" : "Deaths: off");
    this->updateStats();
    this->redraw();
}

// ---------------------------------------------------------------- watch

void MapPopup::onWatch() {
    if (m_watching) return this->stopWatch();
    if (m_line.path.size() < 2) {
        Notification::create("Play the level first: then you can watch your best try", NotificationIcon::Info)->show();
        return;
    }
    m_watchTimes.clear();
    float offset = 0, last = 0;
    for (auto& smp : m_line.path) {
        if (smp.flags & 0x80) offset = (m_watchTimes.empty() ? 0.f : last + 0.4f) - smp.t;
        last = smp.t + offset;
        m_watchTimes.push_back(last);
    }
    auto gm = GameManager::get();
    m_watchIcon = SimplePlayer::create(gm->getPlayerFrame());
    m_watchIcon->setColor(gm->colorForIdx(gm->getPlayerColor()));
    m_watchIcon->setSecondColor(gm->colorForIdx(gm->getPlayerColor2()));
    m_canvas->addChild(m_watchIcon, 8);
    m_watchMode = -1;
    m_watchT = 0;
    m_watchIdx = 0;
    m_watching = true;
    m_point.reset();
    m_pickingEnd = false;
    this->setText(m_watchBtn, "Stop");
    this->schedule(schedule_selector(MapPopup::onTick));
    this->onTick(0);
}

void MapPopup::stopWatch() {
    m_watching = false;
    this->unschedule(schedule_selector(MapPopup::onTick));
    if (m_watchIcon) m_watchIcon->removeFromParent();
    m_watchIcon = nullptr;
    this->setText(m_watchBtn, "Watch");
    this->updateInfo();
}

void MapPopup::onTick(float dt) {
    if (!m_watching || !m_watchIcon) return;
    m_watchT += dt;
    auto& path = m_line.path;
    while (m_watchIdx + 1 < path.size() && m_watchTimes[m_watchIdx + 1] <= m_watchT) m_watchIdx++;
    if (m_watchIdx + 1 >= path.size()) {
        this->stopWatch();
        return;
    }
    auto& a = path[m_watchIdx];
    auto& b = path[m_watchIdx + 1];
    float span = m_watchTimes[m_watchIdx + 1] - m_watchTimes[m_watchIdx];
    float k = (b.flags & 0x80) || span <= 0.0001f ? 0.f : std::clamp((m_watchT - m_watchTimes[m_watchIdx]) / span, 0.f, 1.f);
    float x = a.x + (b.x - a.x) * k, y = a.y + (b.y - a.y) * k;
    m_watchIcon->setPosition(toMap(x, y));
    m_watchIcon->setRotation(a.rot);
    if (a.mode != m_watchMode) {
        m_watchMode = a.mode;
        auto type = modeIcon(a.mode);
        m_watchIcon->updatePlayerFrame(GameManager::get()->activeIconForType(type), type);
    }
    float scale = std::clamp(m_zoom * 2.4f, 0.2f, 0.8f) * ((a.flags & 2) ? 0.6f : 1.f);
    m_watchIcon->setScaleX(scale);
    m_watchIcon->setScaleY((a.flags & 1) ? -scale : scale);
    auto pos = m_canvas->getPosition();
    CCPoint want = {m_mapSize.width * 0.4f - x * m_zoom, m_mapSize.height * 0.45f - y * m_zoom};
    m_canvas->setPosition({want.x, pos.y + (want.y - pos.y) * std::min(1.f, dt * 4.f)});
    this->clampCanvas();
    m_info->setString(fmt::format("Watching your best try: {}%   {}", pct(x), describe(sampleState(a))).c_str());
    m_info->limitLabelWidth(400.f, 0.36f, 0.1f);
    m_hint->setString("Press Stop or tap the map to stop");
}

// ---------------------------------------------------------------- touches

bool MapPopup::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    auto p = m_mainLayer->convertToNodeSpace(touch->getLocation());
    CCRect r = {m_mapOrigin.x, m_mapOrigin.y, m_mapSize.width, m_mapSize.height};
    if (r.containsPoint(p)) {
        if (m_watching) this->stopWatch();
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

// ================================================================ NamePopup

NamePopup* NamePopup::create(std::string const& title, std::string const& current, geode::Function<void(std::string)> done) {
    auto ret = new NamePopup();
    if (ret->init(title, current, std::move(done))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool NamePopup::init(std::string const& title, std::string const& current, geode::Function<void(std::string)> done) {
    if (!Popup::init(280.f, 140.f)) return false;
    m_done = std::move(done);
    this->setTitle(title);
    m_input = TextInput::create(230.f, "Name");
    m_input->setCommonFilter(CommonFilter::Any);
    m_input->setMaxCharCount(24);
    m_input->setString(current);
    m_input->setPosition({140.f, 78.f});
    m_mainLayer->addChild(m_input);
    auto menu = fixedMenu(m_mainLayer);
    put(menu, textButton("OK", "GJ_button_01.png", 0.7f, [this](auto) {
        std::string name = m_input->getString();
        auto done = std::move(m_done);
        this->onClose(nullptr);
        done(name);
    }), 140.f, 30.f);
    handleTouchPriority(this);
    return true;
}

// ================================================================ RoutesPopup

RoutesPopup* RoutesPopup::create(MapPopup* map) {
    auto ret = new RoutesPopup();
    if (ret->init(map)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool RoutesPopup::init(MapPopup* map) {
    if (!Popup::init(380.f, 270.f)) return false;
    m_map = map;
    this->setTitle("Routes");
    m_list = CCNode::create();
    m_mainLayer->addChild(m_list);
    this->build();
    return true;
}

void RoutesPopup::build() {
    m_list->removeAllChildren();
    auto& data = levelData(m_map->level());
    int perPage = 5;
    int pages = std::max(1, (static_cast<int>(data.routes.size()) + perPage - 1) / perPage);
    m_page = std::clamp(m_page, 0, pages - 1);
    auto menu = fixedMenu(m_list);

    if (data.routes.empty()) {
        auto l = CCLabelBMFont::create("No routes yet.\nChoose a flag on the map and press New route.", "chatFont.fnt");
        l->setAlignment(kCCTextAlignmentCenter);
        l->setScale(0.7f);
        l->setPosition({190.f, 160.f});
        m_list->addChild(l);
    }
    for (int i = m_page * perPage; i < std::min(static_cast<int>(data.routes.size()), (m_page + 1) * perPage); i++) {
        auto& r = data.routes[i];
        float y = 212.f - (i - m_page * perPage) * 34.f;
        bool active = i == data.active;
        auto bg = CCLayerColor::create(active ? ccColor4B{60, 200, 60, 70} : ccColor4B{0, 0, 0, 70}, 350.f, 30.f);
        bg->setPosition({15.f, y - 15.f});
        m_list->addChild(bg);
        auto name = CCLabelBMFont::create(r.name.c_str(), "bigFont.fnt");
        name->setAnchorPoint({0.f, 0.5f});
        name->setPosition({22.f, y + 5.f});
        name->limitLabelWidth(150.f, 0.38f, 0.1f);
        m_list->addChild(name);
        auto info = CCLabelBMFont::create(fmt::format("{}% to {}%   done x{}{}", m_map->pct(r.startX), m_map->pct(r.endX), r.done,
                                                      active ? "   (active)" : "").c_str(), "chatFont.fnt");
        info->setAnchorPoint({0.f, 0.5f});
        info->setScale(0.5f);
        info->setPosition({22.f, y - 7.f});
        m_list->addChild(info);
        put(menu, textButton("Play", "GJ_button_01.png", 0.45f, [this, i](auto) {
            Ref<MapPopup> map = m_map;
            this->onClose(nullptr);
            map->playRoute(i);
        }), 220.f, y);
        put(menu, textButton("Rename", "GJ_button_05.png", 0.4f, [this, i](auto) {
            Ref<RoutesPopup> self = this;
            auto& d = levelData(m_map->level());
            if (i >= static_cast<int>(d.routes.size())) return;
            auto p = NamePopup::create("Route name", d.routes[i].name, [self, i](std::string name) {
                auto& d2 = levelData(self->m_map->level());
                if (i >= static_cast<int>(d2.routes.size())) return;
                if (!name.empty()) d2.routes[i].name = name;
                d2.save();
                self->build();
            });
            if (p) p->show();
        }), 280.f, y);
        put(menu, textButton("X", "GJ_button_06.png", 0.45f, [this, i](auto) {
            auto& d = levelData(m_map->level());
            if (i >= static_cast<int>(d.routes.size())) return;
            d.routes.erase(d.routes.begin() + i);
            if (d.active == i) d.active = -1;
            else if (d.active > i) d.active--;
            d.save();
            m_map->refreshAll();
            this->build();
        }), 345.f, y);
    }
    if (pages > 1) {
        put(menu, arrowButton(false, 0.4f, [this](auto) { m_page--; this->build(); }), 25.f, 120.f);
        put(menu, arrowButton(true, 0.4f, [this](auto) { m_page++; this->build(); }), 355.f, 120.f);
    }
    put(menu, textButton(fmt::format("New route from {}", m_map->selName()).c_str(), "GJ_button_02.png", 0.45f, [this](auto) {
        Ref<MapPopup> map = m_map;
        this->onClose(nullptr);
        map->beginNewRoute();
    }), 130.f, 28.f);
    put(menu, textButton("No route", "GJ_button_04.png", 0.45f, [this](auto) {
        auto& d = levelData(m_map->level());
        d.active = -1;
        d.save();
        m_map->refreshAll();
        this->build();
    }), 300.f, 28.f);
    handleTouchPriority(this);
}

// ================================================================ SharePopup

SharePopup* SharePopup::create(MapPopup* map) {
    auto ret = new SharePopup();
    if (ret->init(map)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool SharePopup::init(MapPopup* map) {
    if (!Popup::init(300.f, 170.f)) return false;
    m_map = map;
    this->setTitle("Share");
    auto text = CCLabelBMFont::create("Your own StartPos fit in one code.\nPost it in the comments or on Discord.", "chatFont.fnt");
    text->setAlignment(kCCTextAlignmentCenter);
    text->setScale(0.6f);
    text->setPosition({150.f, 118.f});
    m_mainLayer->addChild(text);
    auto menu = fixedMenu(m_mainLayer);
    put(menu, textButton("Copy code", "GJ_button_01.png", 0.55f, [this](auto) { m_map->doCopy(); }), 85.f, 75.f);
    put(menu, textButton("Paste code", "GJ_button_02.png", 0.55f, [this](auto) {
        Ref<MapPopup> map = m_map;
        this->onClose(nullptr);
        map->doPaste();
    }), 215.f, 75.f);
    auto& data = levelData(map->level());
    if (!data.hidden.empty()) {
        put(menu, textButton(fmt::format("Bring back deleted ({})", data.hidden.size()).c_str(), "GJ_button_04.png", 0.45f, [this](auto) {
            Ref<MapPopup> map = m_map;
            this->onClose(nullptr);
            map->restoreHidden();
        }), 150.f, 32.f);
    }
    handleTouchPriority(this);
    return true;
}
