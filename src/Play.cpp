#include "Common.hpp"
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/loader/SettingV3.hpp>

void setCascade(CCNode* node) {
    if (!node) return;
    if (auto rgba = dynamic_cast<CCRGBAProtocol*>(node)) {
        rgba->setCascadeOpacityEnabled(true);
    }
    if (!node->getChildren()) return;
    for (auto child : CCArrayExt<CCNode*>(node->getChildren())) setCascade(child);
}

namespace {
    struct LiveStart {
        Ref<StartPosObject> obj;
        bool custom = false;
        int status = 0;     // 0 not checked, 1 right, 2 fixed, 3 wrong
        bool checked = false;
    };

    StartPosObject* makeStart(SavedStart const& c) {
        auto obj = typeinfo_cast<StartPosObject*>(GameObject::createWithKey(31));
        if (!obj) {
            obj = StartPosObject::create();
            if (!obj) return nullptr;
            obj->m_objectID = 31;
        }
        if (!obj->m_startSettings) obj->setSettings(LevelSettingsObject::create());
        applyState(obj->m_startSettings, c.state);
        obj->setPosition({c.x, c.y});
        obj->m_startPosition = {c.x, c.y};
        return obj;
    }
}

class $modify(StudioPlayLayer, PlayLayer) {
    struct Fields {
        std::vector<Ref<StartPosObject>> levelStarts;
        std::vector<LiveStart> starts;
        int index = 0;
        bool managed = false;
        Ref<StartPosObject> wanted;
        bool verifyChoice = false;

        // recording
        Attempt current;
        bool recording = false;
        float attemptTime = 0;
        float sampleTimer = 0;
        int unsaved = 0;

        // ghost and death marks
        Ref<SimplePlayer> ghost;
        std::vector<Sample> ghostPath;
        size_t ghostIdx = 0;
        int ghostMode = -1;
        Ref<CCDrawNode> deathNode;

        // switcher
        Ref<CCNodeRGBA> ui;
        Ref<CCLabelBMFont> title;
        Ref<CCLabelBMFont> detail;
        Ref<CCNode> bar;
        Ref<SimplePlayer> icon;
        Ref<CCNode> marks;
        bool hinted = false;
    };

    // ------------------------------------------------------------ setup

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        this->buildSwitcher();
        this->refreshUI(true);
        return true;
    }

    void addObject(GameObject* obj) {
        PlayLayer::addObject(obj);
        if (obj && obj->m_objectID == 31) {
            if (auto sp = typeinfo_cast<StartPosObject*>(obj)) m_fields->levelStarts.push_back(sp);
        }
    }

    void createObjectsFromSetupFinished() {
        PlayLayer::createObjectsFromSetupFinished();
        this->rebuildStarts(true);
    }

    void rebuildStarts(bool applyChoice) {
        auto f = m_fields.self();
        auto& data = levelData(m_level);
        StartPosObject* current = m_startPosObject;
        f->starts.clear();
        for (auto& o : f->levelStarts) {
            LiveStart s;
            s.obj = o;
            int k = static_cast<int>(std::round(o->getPositionX()));
            if (auto fix = data.fixes.find(k); fix != data.fixes.end()) applyState(o->m_startSettings, fix->second);
            if (auto st = data.status.find(k); st != data.status.end()) s.status = st->second;
            f->starts.push_back(s);
        }
        for (auto& c : data.custom) {
            // keep the same object if this StartPos already existed (it may be the active one)
            StartPosObject* obj = nullptr;
            if (current && std::abs(current->getPositionX() - c.x) < 0.5f && std::abs(current->getPositionY() - c.y) < 0.5f) {
                obj = current;
            }
            if (!obj) obj = makeStart(c);
            if (!obj) continue;
            LiveStart s;
            s.obj = obj;
            s.custom = true;
            s.status = 1;
            f->starts.push_back(s);
        }
        std::stable_sort(f->starts.begin(), f->starts.end(), [](auto& a, auto& b) {
            return a.obj->getPositionX() < b.obj->getPositionX();
        });

        if (applyChoice && data.hasChoice) {
            StartPosObject* want = nullptr;
            bool found = data.choiceX < 0;
            if (!found) {
                float best = 3.f;
                for (auto& s : f->starts) {
                    float d = std::abs(s.obj->getPositionX() - data.choiceX);
                    if (d < best) { best = d; want = s.obj; found = true; }
                }
            }
            if (found) {
                this->setStartPosObject(want);
                m_isTestMode = want != nullptr;
                f->managed = true;
                f->wanted = want;
                f->verifyChoice = true;
            }
            if (!setting("remember-choice")) {
                data.hasChoice = false;
                data.save();
            }
        }
        f->index = this->indexOf(m_startPosObject);
        this->refreshUI(false);
    }

    int indexOf(StartPosObject* obj) {
        if (!obj) return 0;
        auto& starts = m_fields->starts;
        for (size_t i = 0; i < starts.size(); i++) {
            if (starts[i].obj == obj) return static_cast<int>(i) + 1;
        }
        return 0;
    }

    // ------------------------------------------------------------ switching

    void switchTo(int idx) {
        auto f = m_fields.self();
        int n = static_cast<int>(f->starts.size());
        if (n == 0) return;
        idx = ((idx % (n + 1)) + (n + 1)) % (n + 1);
        StartPosObject* obj = idx == 0 ? nullptr : f->starts[idx - 1].obj.data();
        f->index = idx;
        f->managed = true;
        m_currentCheckpoint = nullptr;
        this->setStartPosObject(obj);
        m_isTestMode = obj != nullptr;
        this->updateTestModeLabel();
        if (m_isPracticeMode) this->resetLevelFromStart();
        this->resetLevel();
        this->startMusic();
        this->rememberChoice(obj);
        this->refreshUI(true);
    }

    void switchToX(float x) {
        if (x < 0) return this->switchTo(0);
        auto& starts = m_fields->starts;
        int best = 0;
        float dist = 3.f;
        for (size_t i = 0; i < starts.size(); i++) {
            float d = std::abs(starts[i].obj->getPositionX() - x);
            if (d < dist) { dist = d; best = static_cast<int>(i) + 1; }
        }
        if (best == 0 && !starts.empty()) {
            // the beginning was not asked for, but no StartPos is there: keep the closest one before x
            for (size_t i = 0; i < starts.size(); i++) {
                if (starts[i].obj->getPositionX() <= x + 3) best = static_cast<int>(i) + 1;
            }
        }
        this->switchTo(best);
    }

    void rememberChoice(StartPosObject* obj) {
        if (!setting("remember-choice")) return;
        auto& data = levelData(m_level);
        data.hasChoice = true;
        data.choiceX = obj ? obj->getPositionX() : -1.f;
        data.save();
    }

    void placeHere() {
        if (setting("place-only-practice") && !m_isPracticeMode) {
            Notification::create("Turn on practice mode to place a StartPos", NotificationIcon::Info)->show();
            return;
        }
        if (!m_player1 || m_player1->m_isDead || m_hasCompletedLevel) return;
        SavedStart c;
        c.x = m_player1->getPositionX();
        c.y = m_player1->getPositionY();
        c.state = stateOf(m_player1, m_gameState.m_isDualMode);
        auto& data = levelData(m_level);
        data.custom.push_back(c);
        std::sort(data.custom.begin(), data.custom.end(), [](auto& a, auto& b) { return a.x < b.x; });
        data.save();

        auto f = m_fields.self();
        auto obj = makeStart(c);
        if (!obj) return;
        LiveStart s;
        s.obj = obj;
        s.custom = true;
        s.status = 1;
        auto at = std::find_if(f->starts.begin(), f->starts.end(), [&](auto& o) { return o.obj->getPositionX() > c.x; });
        f->starts.insert(at, s);
        // it becomes your StartPos for the next attempts, without restarting this one
        this->setStartPosObject(obj);
        f->managed = true;
        f->index = this->indexOf(obj);
        this->rememberChoice(obj);
        this->refreshUI(true);
        Notification::create(
            fmt::format("StartPos placed at {}%: {}", this->percentOf(c.x), describe(c.state)),
            NotificationIcon::Success
        )->show();
    }

    void removeSelected() {
        auto f = m_fields.self();
        if (f->index <= 0 || f->index > static_cast<int>(f->starts.size()) || !f->starts[f->index - 1].custom) {
            Notification::create("Only the StartPos you placed can be deleted", NotificationIcon::Info)->show();
            return;
        }
        float x = f->starts[f->index - 1].obj->getPositionX();
        auto& data = levelData(m_level);
        std::erase_if(data.custom, [&](auto& c) { return std::abs(c.x - x) < 0.5f; });
        data.save();
        f->starts.erase(f->starts.begin() + (f->index - 1));
        f->index -= 1;
        StartPosObject* obj = f->index > 0 ? f->starts[f->index - 1].obj.data() : nullptr;
        this->setStartPosObject(obj);
        f->managed = true;
        this->rememberChoice(obj);
        this->refreshUI(true);
        Notification::create("StartPos deleted", NotificationIcon::Success)->show();
    }

    int percentOf(float x) {
        float len = m_levelLength > 0 ? m_levelLength : 1.f;
        return std::clamp(static_cast<int>(x / len * 100.f), 0, 100);
    }

    // ------------------------------------------------------------ attempts

    void resetLevel() {
        auto f = m_fields.self();
        this->finishAttempt(false, false);
        if (f->managed) m_isTestMode = m_startPosObject != nullptr;
        PlayLayer::resetLevel();
        this->beginAttempt();
    }

    void beginAttempt() {
        auto f = m_fields.self();
        if (!m_player1) return;
        f->current = Attempt{};
        f->current.startX = m_player1->getPositionX();
        f->current.practice = m_isPracticeMode;
        f->recording = true;
        f->attemptTime = 0;
        f->sampleTimer = 1.f;
        for (auto& s : f->starts) s.checked = s.obj->getPositionX() <= f->current.startX + 2.f;

        // ghost: your best run from this same place
        f->ghostPath.clear();
        f->ghostIdx = 0;
        if (setting("show-ghost")) {
            if (auto best = runData(m_level).bestFor(f->current.startX); best && best->reach() > 60) {
                f->ghostPath = best->path;
            }
        }
        this->ensureWorldNodes();
        if (f->ghost) f->ghost->setVisible(false);
    }

    void finishAttempt(bool died, bool completed) {
        auto f = m_fields.self();
        if (!f->recording || !m_player1) return;
        f->recording = false;
        auto& a = f->current;
        a.died = died;
        a.completed = completed;
        a.endX = m_player1->getPositionX();
        a.endY = m_player1->getPositionY();
        a.time = f->attemptTime;
        this->pushSample();
        if (!died && !completed && a.time < 0.6f) return;  // quick restart, nothing to keep
        auto& runs = runData(m_level);
        float dx = a.endX, dy = a.endY;
        runs.addAttempt(std::move(a));
        if (died) this->drawDeath({dx, dy});
        if (++f->unsaved >= 15 || completed) {
            runs.save();
            f->unsaved = 0;
        }
    }

    void pushSample() {
        auto f = m_fields.self();
        if (!m_player1) return;
        auto st = stateOf(m_player1, m_gameState.m_isDualMode);
        Sample s;
        s.t = f->attemptTime;
        s.x = m_player1->getPositionX();
        s.y = m_player1->getPositionY();
        s.rot = m_player1->getRotation();
        s.mode = static_cast<uint8_t>(st.mode);
        s.speed = static_cast<uint8_t>(st.speed);
        s.flags = static_cast<uint8_t>((st.flip ? 1 : 0) | (st.mini ? 2 : 0) | (st.dual ? 4 : 0));
        if (f->current.path.size() < 30000) f->current.path.push_back(s);
    }

    void recordClick() {
        auto f = m_fields.self();
        if (!f->recording || !m_player1 || m_player1->m_isDead) return;
        if (f->current.clicks.size() < 20000) {
            f->current.clicks.push_back(m_player1->getPosition());
        }
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        bool wasDead = m_player1 && m_player1->m_isDead;
        PlayLayer::destroyPlayer(player, object);
        if (object == m_anticheatSpike) return;
        if (!wasDead && m_player1 && m_player1->m_isDead) this->finishAttempt(true, false);
    }

    void levelComplete() {
        this->finishAttempt(false, true);
        PlayLayer::levelComplete();
    }

    void onQuit() {
        this->finishAttempt(false, false);
        runData(m_level).save();
        PlayLayer::onQuit();
    }

    void togglePracticeMode(bool practice) {
        PlayLayer::togglePracticeMode(practice);
        auto f = m_fields.self();
        if (practice && !f->hinted) {
            f->hinted = true;
            auto keys = Mod::get()->getSettingValue<std::vector<Keybind>>("place-startpos");
            std::string key = keys.empty() ? "your Place key" : keys.front().toString();
            Notification::create(fmt::format("Press {} to place a StartPos where you are", key), NotificationIcon::Info)->show();
        }
    }

    // ------------------------------------------------------------ every frame

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        auto f = m_fields.self();
        if (f->verifyChoice) {
            f->verifyChoice = false;
            if (m_startPosObject != f->wanted.data()) {
                Ref<PlayLayer> self = this;
                float x = f->wanted ? f->wanted->getPositionX() : -1.f;
                queueInMainThread([self, x] {
                    if (PlayLayer::get() == self.data()) static_cast<StudioPlayLayer*>(self.data())->switchToX(x);
                });
            }
        }
        if (!m_player1 || m_isPaused) return;
        if (!f->recording) {
            if (m_player1->m_isDead || m_hasCompletedLevel) return;
            this->beginAttempt();
        }
        f->attemptTime += dt;
        f->sampleTimer += dt;
        if (f->sampleTimer >= 1.f / 30.f) {
            f->sampleTimer = 0;
            this->pushSample();
        }
        this->checkStarts();
        this->updateGhost();
    }

    // auto-fix: compare every StartPos you fly through with your real state there
    void checkStarts() {
        auto f = m_fields.self();
        if (m_player1->m_isDead) return;
        float x = m_player1->getPositionX();
        for (size_t i = 0; i < f->starts.size(); i++) {
            auto& s = f->starts[i];
            if (s.checked || s.custom) continue;
            float sx = s.obj->getPositionX();
            if (x < sx) continue;
            s.checked = true;
            if (f->current.startX > sx - 5) continue;
            auto actual = stateOf(m_player1, m_gameState.m_isDualMode);
            auto have = stateOf(s.obj->m_startSettings);
            int k = static_cast<int>(std::round(sx));
            auto& data = levelData(m_level);
            if (actual == have) {
                if (s.status != 2) s.status = 1;
            }
            else if (setting("auto-fix")) {
                applyState(s.obj->m_startSettings, actual);
                data.fixes[k] = actual;
                s.status = 2;
                Notification::create(
                    fmt::format("StartPos {} fixed: {}", i + 1, describe(actual)), NotificationIcon::Success
                )->show();
            }
            else {
                s.status = 3;
                Notification::create(
                    fmt::format("StartPos {} is wrong, it should be {}", i + 1, describe(actual)), NotificationIcon::Warning
                )->show();
            }
            if (s.status == 1 || s.status == 2) data.status[k] = s.status;
            data.save();
            this->refreshUI(false);
        }
    }

    void ensureWorldNodes() {
        auto f = m_fields.self();
        if (!m_player1 || !m_player1->getParent()) return;
        auto parent = m_player1->getParent();
        if (!f->deathNode) {
            f->deathNode = CCDrawNode::create();
            f->deathNode->setID("death-marks"_spr);
            parent->addChild(f->deathNode, m_player1->getZOrder() - 2);
            if (setting("show-deaths")) {
                auto& pts = runData(m_level).deathPoints;
                size_t from = pts.size() > 600 ? pts.size() - 600 : 0;
                for (size_t i = from; i < pts.size(); i++) this->drawDeath(pts[i]);
            }
        }
        if (!f->ghost) {
            auto gm = GameManager::get();
            f->ghost = SimplePlayer::create(gm->getPlayerFrame());
            f->ghost->setID("ghost"_spr);
            f->ghost->setColor(gm->colorForIdx(gm->getPlayerColor()));
            f->ghost->setSecondColor(gm->colorForIdx(gm->getPlayerColor2()));
            f->ghost->setVisible(false);
            parent->addChild(f->ghost, m_player1->getZOrder() - 1);
            f->ghostMode = -1;
        }
    }

    void drawDeath(CCPoint p) {
        auto f = m_fields.self();
        if (!f->deathNode || !setting("show-deaths")) return;
        ccColor4F red = {1.f, 0.2f, 0.2f, 0.55f};
        float r = 6.f;
        f->deathNode->drawSegment({p.x - r, p.y - r}, {p.x + r, p.y + r}, 1.4f, red);
        f->deathNode->drawSegment({p.x - r, p.y + r}, {p.x + r, p.y - r}, 1.4f, red);
    }

    void updateGhost() {
        auto f = m_fields.self();
        if (!f->ghost) return;
        auto& path = f->ghostPath;
        if (path.size() < 2 || f->attemptTime > path.back().t) {
            f->ghost->setVisible(false);
            return;
        }
        while (f->ghostIdx + 1 < path.size() && path[f->ghostIdx + 1].t <= f->attemptTime) f->ghostIdx++;
        auto& a = path[f->ghostIdx];
        auto& b = path[std::min(f->ghostIdx + 1, path.size() - 1)];
        float span = b.t - a.t;
        float k = span > 0.0001f ? std::clamp((f->attemptTime - a.t) / span, 0.f, 1.f) : 0.f;
        f->ghost->setPosition({a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k});
        f->ghost->setRotation(a.rot);
        if (a.mode != f->ghostMode) {
            f->ghostMode = a.mode;
            auto type = modeIcon(a.mode);
            f->ghost->updatePlayerFrame(GameManager::get()->activeIconForType(type), type);
            setCascade(f->ghost);
            f->ghost->setOpacity(110);
        }
        float scale = (a.flags & 2) ? 0.6f : 1.f;
        f->ghost->setScaleX(scale);
        f->ghost->setScaleY((a.flags & 1) ? -scale : scale);
        f->ghost->setVisible(true);
    }

    // ------------------------------------------------------------ switcher

    void buildSwitcher() {
        auto f = m_fields.self();
        if (f->ui || !m_uiLayer) return;
        auto win = CCDirector::get()->getWinSize();

        auto root = CCNodeRGBA::create();
        root->setID("switcher"_spr);
        root->setPosition({win.width / 2, CCDirector::get()->getScreenBottom() + 26.f});
        m_uiLayer->addChild(root, 50);
        f->ui = root;

        auto bg = CCScale9Sprite::create("square02_small.png");
        bg->setContentSize({240.f, 44.f});
        bg->setColor({0, 0, 0});
        bg->setOpacity(150);
        root->addChild(bg);

        auto menu = CCMenu::create();
        menu->setPosition({0, 0});
        root->addChild(menu);
        auto prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_02_001.png");
        prevSpr->setScale(0.5f);
        auto nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_02_001.png");
        nextSpr->setScale(0.5f);
        nextSpr->setFlipX(true);
        auto prev = CCMenuItemExt::createSpriteExtra(prevSpr, [this](auto) { this->switchTo(m_fields->index - 1); });
        auto next = CCMenuItemExt::createSpriteExtra(nextSpr, [this](auto) { this->switchTo(m_fields->index + 1); });
        prev->setPosition({-104.f, 0});
        next->setPosition({104.f, 0});
        menu->addChild(prev);
        menu->addChild(next);

        auto gm = GameManager::get();
        auto icon = SimplePlayer::create(gm->getPlayerFrame());
        icon->setColor(gm->colorForIdx(gm->getPlayerColor()));
        icon->setSecondColor(gm->colorForIdx(gm->getPlayerColor2()));
        icon->setScale(0.42f);
        icon->setPosition({-74.f, 2.f});
        root->addChild(icon);
        f->icon = icon;

        auto title = CCLabelBMFont::create("Start", "bigFont.fnt");
        title->setPosition({8.f, 11.f});
        root->addChild(title);
        f->title = title;

        auto detail = CCLabelBMFont::create("", "bigFont.fnt");
        detail->setPosition({8.f, -2.f});
        root->addChild(detail);
        f->detail = detail;

        auto bar = CCLayerColor::create({255, 255, 255, 70}, 140.f, 2.f);
        bar->ignoreAnchorPointForPosition(false);
        bar->setPosition({8.f, -14.f});
        root->addChild(bar);
        f->bar = bar;

        auto marks = CCNode::create();
        marks->setPosition({8.f - 70.f, -14.f});
        root->addChild(marks);
        f->marks = marks;
    }

    void refreshUI(bool flash) {
        auto f = m_fields.self();
        this->updateProgressMarks();
        if (!f->ui) return;
        int n = static_cast<int>(f->starts.size());
        f->ui->setVisible(n > 0 && setting("show-switcher"));
        if (n == 0) return;
        int idx = std::clamp(f->index, 0, n);

        SPState state;
        std::string where;
        std::string mark;
        if (idx == 0) {
            if (m_levelSettings) state = stateOf(m_levelSettings);
            where = "Start of the level";
        }
        else {
            auto& s = f->starts[idx - 1];
            state = stateOf(s.obj->m_startSettings);
            where = fmt::format("{}%", this->percentOf(s.obj->getPositionX()));
            if (s.custom) mark = "  (yours)";
            else if (s.status == 1) mark = "  (checked)";
            else if (s.status == 2) mark = "  (fixed)";
            else if (s.status == 3) mark = "  (wrong!)";
        }
        f->title->setString(idx == 0 ? fmt::format("Start  0/{}", n).c_str()
                                     : fmt::format("StartPos {}/{}{}", idx, n, mark).c_str());
        f->title->limitLabelWidth(150.f, 0.4f, 0.1f);
        f->detail->setString(fmt::format("{}  {}", where, describe(state)).c_str());
        f->detail->limitLabelWidth(150.f, 0.3f, 0.1f);
        f->detail->setColor(modeColor(state.mode));
        auto type = modeIcon(state.mode);
        f->icon->updatePlayerFrame(GameManager::get()->activeIconForType(type), type);

        f->marks->removeAllChildren();
        float len = m_levelLength > 0 ? m_levelLength : 1.f;
        for (int i = 0; i <= n; i++) {
            float x = i == 0 ? 0.f : f->starts[i - 1].obj->getPositionX();
            bool sel = i == idx;
            ccColor4B col = sel ? ccColor4B{90, 255, 90, 255}
                          : (i > 0 && f->starts[i - 1].custom) ? ccColor4B{60, 230, 255, 220}
                          : (i > 0 && f->starts[i - 1].status == 3) ? ccColor4B{255, 70, 70, 220}
                          : ccColor4B{255, 255, 255, 180};
            float w = sel ? 3.f : 2.f, h = sel ? 9.f : 6.f;
            auto m = CCLayerColor::create(col, w, h);
            m->setPosition({std::clamp(x / len, 0.f, 1.f) * 140.f - w / 2, -h / 2 + 1});
            f->marks->addChild(m);
        }

        setCascade(f->ui);
        float idle = static_cast<float>(Mod::get()->getSettingValue<int64_t>("switcher-opacity")) * 2.55f;
        f->ui->stopAllActions();
        if (flash) {
            f->ui->setOpacity(255);
            f->ui->runAction(CCSequence::create(
                CCDelayTime::create(1.8f), CCEaseInOut::create(CCFadeTo::create(0.6f, static_cast<GLubyte>(idle)), 2.f), nullptr
            ));
        }
        else if (f->ui->getOpacity() < idle) {
            f->ui->setOpacity(static_cast<GLubyte>(idle));
        }
    }

    void updateProgressMarks() {
        if (!m_progressBar) return;
        if (auto old = m_progressBar->getChildByID("marks"_spr)) old->removeFromParent();
        auto f = m_fields.self();
        if (!setting("progress-markers") || f->starts.empty() || m_levelLength <= 0) return;
        auto holder = CCNode::create();
        holder->setID("marks"_spr);
        m_progressBar->addChild(holder, 10);
        auto size = m_progressBar->getContentSize();
        float inner = size.width - 4.f;
        for (size_t i = 0; i < f->starts.size(); i++) {
            auto& s = f->starts[i];
            float pct = std::clamp(s.obj->getPositionX() / m_levelLength, 0.f, 1.f);
            bool sel = static_cast<int>(i) + 1 == f->index;
            ccColor4B col = sel ? ccColor4B{90, 255, 90, 255}
                          : s.custom ? ccColor4B{60, 230, 255, 230}
                          : ccColor4B{255, 255, 255, 200};
            auto m = CCLayerColor::create(col, sel ? 2.f : 1.f, size.height);
            m->setPosition({2.f + inner * pct, 0});
            holder->addChild(m);
        }
    }
};

class $modify(StudioBaseLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
        if (!down || button != 1 || !isPlayer1) return;
        auto pl = PlayLayer::get();
        if (pl && static_cast<GJBaseGameLayer*>(pl) == static_cast<GJBaseGameLayer*>(this)) {
            static_cast<StudioPlayLayer*>(pl)->recordClick();
        }
    }
};

// ---------------------------------------------------------------- helpers for the map

bool studioPlaying() {
    return PlayLayer::get() != nullptr;
}

void studioSwitchToX(float x) {
    if (auto pl = PlayLayer::get()) static_cast<StudioPlayLayer*>(pl)->switchToX(x);
}

void studioRebuild() {
    if (auto pl = PlayLayer::get()) static_cast<StudioPlayLayer*>(pl)->rebuildStarts(false);
}

// ---------------------------------------------------------------- keys

$on_mod(Loaded) {
    auto onKey = [](void (*action)(StudioPlayLayer*)) {
        return [action](Keybind const&, bool down, bool repeat, double) {
            if (!down || repeat) return;
            auto pl = PlayLayer::get();
            if (!pl || pl->m_isPaused) return;
            action(static_cast<StudioPlayLayer*>(pl));
        };
    };
    listenForKeybindSettingPresses("prev-startpos", onKey([](StudioPlayLayer* pl) { pl->switchTo(pl->m_fields->index - 1); }));
    listenForKeybindSettingPresses("next-startpos", onKey([](StudioPlayLayer* pl) { pl->switchTo(pl->m_fields->index + 1); }));
    listenForKeybindSettingPresses("place-startpos", onKey([](StudioPlayLayer* pl) { pl->placeHere(); }));
    listenForKeybindSettingPresses("remove-startpos", onKey([](StudioPlayLayer* pl) { pl->removeSelected(); }));
}
