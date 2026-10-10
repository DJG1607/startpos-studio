#include "Common.hpp"
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <deque>

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
        obj->m_startPosition = CCPoint(c.x, c.y);
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

        Ref<CCDrawNode> deathNode;

        // info at the top left: percent (and route goal), clicks per second, clicks this try
        Ref<CCNode> hud;
        Ref<CCLabelBMFont> hudPercent;
        Ref<CCLabelBMFont> hudRoute;
        Ref<CCLabelBMFont> hudCps;
        Ref<CCLabelBMFont> hudClicks;
        Ref<CCLabelBMFont> hudInfo;
        float clock = 0;
        float hudTimer = 1.f;
        std::deque<float> clickTimes;
        int attemptClicks = 0;

        // route: from its StartPos up to its end, then you start again
        bool routeOn = false;
        bool routeReached = false;
        float routeStart = 0;
        float routeEnd = 0;
        bool hinted = false;
    };

    // ------------------------------------------------------------ setup

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        this->buildHud();
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
            if (data.hidden.contains(startKey(o->getPositionX()))) continue;
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

    bool playerIsDead() {
        return m_playerDied || (m_player1 && m_player1->m_isDead);
    }

    void resetLevel() {
        auto f = m_fields.self();
        this->finishAttempt(this->playerIsDead(), false);
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

        f->attemptClicks = 0;
        this->ensureWorldNodes();
        this->setupRoute();
    }

    // the route applies when you start on its StartPos
    void setupRoute() {
        auto f = m_fields.self();
        auto& data = levelData(m_level);
        f->routeOn = false;
        f->routeReached = false;
        f->hudTimer = 1.f;
        auto r = data.activeRoute();
        if (!r) return;
        if (std::abs(f->current.startX - r->startX) > 20.f) return;
        f->routeOn = true;
        f->routeStart = r->startX;
        f->routeEnd = r->endX;
    }

    // end of the route: it counts as done and you go back to its StartPos
    void routeDone(bool restart) {
        auto f = m_fields.self();
        if (f->routeReached) return;
        f->routeReached = true;
        auto& data = levelData(m_level);
        if (auto r = data.activeRoute()) {
            r->done++;
            data.save();
        }
        f->hudTimer = 1.f;
        if (!restart) return;
        Ref<PlayLayer> self = this;
        queueInMainThread([self] {
            if (PlayLayer::get() != self.data()) return;
            auto pl = static_cast<StudioPlayLayer*>(self.data());
            pl->m_currentCheckpoint = nullptr;
            if (pl->m_isPracticeMode) pl->resetLevelFromStart();
            pl->resetLevel();
            pl->startMusic();
        });
    }

    void checkRoute() {
        auto f = m_fields.self();
        if (!f->routeOn || f->routeReached) return;
        if (m_player1->getPositionX() < f->routeEnd) return;
        if (m_levelLength > 0 && f->routeEnd >= m_levelLength - 5) return;  // the real end: GD finishes the level
        this->routeDone(true);
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
        f->attemptClicks++;
        f->clickTimes.push_back(f->clock);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        PlayLayer::destroyPlayer(player, object);
        if (object == m_anticheatSpike) return;
        if (this->playerIsDead()) this->finishAttempt(true, false);
    }

    void levelComplete() {
        if (m_fields->routeOn) this->routeDone(false);
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
        if (!m_player1) return;
        if (f->recording && f->attemptTime > 0.1f && this->playerIsDead()) this->finishAttempt(true, false);
        if (m_isPaused) return;
        f->clock += dt;
        this->updateHud(dt);
        if (!f->recording) {
            if (this->playerIsDead() || m_hasCompletedLevel) return;
            this->beginAttempt();
        }
        f->attemptTime += dt;
        f->sampleTimer += dt;
        if (f->sampleTimer >= 1.f / 30.f) {
            f->sampleTimer = 0;
            this->pushSample();
        }
        this->checkStarts();
        this->checkRoute();
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
            auto& pts = runData(m_level).deathPoints;
            size_t from = pts.size() > 600 ? pts.size() - 600 : 0;
            for (size_t i = from; i < pts.size(); i++) this->drawDeath(pts[i]);
            f->deathNode->setVisible(deathsVisible());
        }
    }

    void drawDeath(CCPoint p) {
        auto f = m_fields.self();
        if (!f->deathNode) return;
        ccColor4F red = {1.f, 0.2f, 0.2f, 0.55f};
        float r = 6.f;
        f->deathNode->drawSegment({p.x - r, p.y - r}, {p.x + r, p.y + r}, 1.4f, red);
        f->deathNode->drawSegment({p.x - r, p.y + r}, {p.x + r, p.y - r}, 1.4f, red);
    }

    // ------------------------------------------------------------ HUD

    void buildHud() {
        auto f = m_fields.self();
        if (f->hud) return;
        auto win = CCDirector::get()->getWinSize();
        auto hud = CCNode::create();
        hud->setID("hud"_spr);
        hud->setPosition({6.f, win.height - 6.f});
        this->addChild(hud, 500);
        f->hud = hud;
        auto line = [&](float scale) {
            auto l = CCLabelBMFont::create("", "bigFont.fnt");
            l->setAnchorPoint({0.f, 1.f});
            l->setScale(scale);
            hud->addChild(l);
            return l;
        };
        f->hudPercent = line(0.45f);
        f->hudRoute = line(0.3f);
        f->hudCps = line(0.33f);
        f->hudClicks = line(0.33f);
        f->hudInfo = line(0.3f);
        f->hudInfo->setOpacity(0);
        this->layoutHud();
    }

    // stacks the lines you turned on, one under the other
    void layoutHud() {
        auto f = m_fields.self();
        if (!f->hud) return;
        f->hudPercent->setVisible(setting("show-percent"));
        f->hudRoute->setVisible(setting("show-percent") && f->routeOn);
        f->hudCps->setVisible(setting("show-cps"));
        f->hudClicks->setVisible(setting("show-clicks"));
        float y = 0;
        for (CCLabelBMFont* l : {f->hudPercent.data(), f->hudRoute.data(), f->hudCps.data(), f->hudClicks.data(), f->hudInfo.data()}) {
            l->setPosition({0.f, y});
            if (l->isVisible()) y -= l->getScaledContentSize().height + 3.f;
        }
    }

    void updateHud(float dt) {
        auto f = m_fields.self();
        if (!f->hud) return;
        while (!f->clickTimes.empty() && f->clickTimes.front() < f->clock - 1.f) f->clickTimes.pop_front();
        f->hudTimer += dt;
        if (f->hudTimer < 0.1f) return;
        f->hudTimer = 0;
        int now = m_player1 ? this->percentOf(m_player1->getPositionX()) : 0;
        if (f->routeOn) {
            f->hudPercent->setString(fmt::format("{}%  >  {}%", now, this->percentOf(f->routeEnd)).c_str());
            auto& data = levelData(m_level);
            auto r = data.activeRoute();
            int done = r ? r->done : 0;
            std::string name = r ? r->name : std::string();
            f->hudRoute->setString(fmt::format("{}  done x{}", name, done).c_str());
            f->hudRoute->setColor({90, 255, 90});
        }
        else {
            f->hudPercent->setString(fmt::format("{}%", now).c_str());
        }
        f->hudCps->setString(fmt::format("CPS {}", f->clickTimes.size()).c_str());
        f->hudClicks->setString(fmt::format("Clicks {}", f->attemptClicks).c_str());
        this->layoutHud();
    }

    // shows which StartPos you are on for a moment (after switching or placing one)
    void refreshUI(bool flash) {
        auto f = m_fields.self();
        this->updateProgressMarks();
        if (!flash || !f->hudInfo) return;
        int n = static_cast<int>(f->starts.size());
        if (n == 0) return;
        int idx = std::clamp(f->index, 0, n);
        SPState state;
        std::string text;
        if (idx == 0) {
            if (m_levelSettings) state = stateOf(m_levelSettings);
            text = fmt::format("Start of the level (0/{})", n);
        }
        else {
            auto& s = f->starts[idx - 1];
            state = stateOf(s.obj->m_startSettings);
            text = fmt::format("{} ({}/{})  {}%  {}", startName(levelData(m_level), s.obj->getPositionX(), idx), idx, n,
                               this->percentOf(s.obj->getPositionX()), describe(state));
        }
        f->hudInfo->setString(text.c_str());
        f->hudInfo->setColor(modeColor(state.mode));
        f->hudInfo->stopAllActions();
        f->hudInfo->setOpacity(255);
        f->hudInfo->runAction(CCSequence::create(CCDelayTime::create(2.f), CCFadeOut::create(0.5f), nullptr));
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

// every press of the jump button of your icon (mouse, keyboard or controller)
class $modify(StudioPlayer, PlayerObject) {
    bool pushButton(PlayerButton button) {
        bool result = PlayerObject::pushButton(button);
        if (button == PlayerButton::Jump) {
            auto pl = PlayLayer::get();
            if (pl && pl->m_player1 == static_cast<PlayerObject*>(this)) static_cast<StudioPlayLayer*>(pl)->recordClick();
        }
        return result;
    }
};

// ---------------------------------------------------------------- helpers for the map

bool studioPlaying() {
    return PlayLayer::get() != nullptr;
}

void studioSwitchToX(float x) {
    if (auto pl = PlayLayer::get()) static_cast<StudioPlayLayer*>(pl)->switchToX(x);
}

void studioDeathsChanged() {
    auto pl = PlayLayer::get();
    if (!pl || !pl->m_player1 || !pl->m_player1->getParent()) return;
    if (auto node = pl->m_player1->getParent()->getChildByID("death-marks"_spr)) node->setVisible(deathsVisible());
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
