#include "MapPopup.hpp"
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/EditLevelLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>

namespace {
    CCMenuItemSpriteExtra* mapButton(GJGameLevel* level, MapPopup::From from, CCNode* owner, float scale) {
        auto top = CCSprite::createWithSpriteFrameName("checkpoint_01_001.png");
        auto spr = CircleButtonSprite::create(top, CircleBaseColor::Green, CircleBaseSize::Medium);
        spr->setScale(scale);
        Ref<GJGameLevel> lvl = level;
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [lvl, from, owner](auto) {
            if (lvl->m_levelString.empty()) {
                Notification::create("Wait until the level is downloaded", NotificationIcon::Info)->show();
                return;
            }
            if (auto popup = MapPopup::create(lvl, from, owner)) popup->show();
        });
        btn->setID("startpos-map"_spr);
        return btn;
    }

    // puts the button in the menu that node-ids names, or in a small menu of our own
    void place(CCNode* layer, CCMenuItemSpriteExtra* btn, char const* menuID, CCPoint fallback) {
        if (auto menu = typeinfo_cast<CCMenu*>(layer->getChildByID(menuID))) {
            menu->addChild(btn);
            menu->updateLayout();
            return;
        }
        auto menu = CCMenu::create();
        menu->setID("menu"_spr);
        menu->setPosition(fallback);
        menu->addChild(btn);
        layer->addChild(menu, 10);
    }
}

class $modify(StudioLevelInfo, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;
        auto win = CCDirector::get()->getWinSize();
        place(this, mapButton(level, MapPopup::From::Info, this, 0.8f), "left-side-menu", {30.f, win.height / 2 - 60.f});
        return true;
    }
};

class $modify(StudioEditLevel, EditLevelLayer) {
    bool init(GJGameLevel* level) {
        if (!EditLevelLayer::init(level)) return false;
        auto win = CCDirector::get()->getWinSize();
        place(this, mapButton(level, MapPopup::From::Editor, this, 0.8f), "level-actions-menu", {win.width - 30.f, 40.f});
        return true;
    }
};

class $modify(StudioPause, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto pl = PlayLayer::get();
        if (!pl || !pl->m_level) return;
        auto win = CCDirector::get()->getWinSize();
        place(this, mapButton(pl->m_level, MapPopup::From::Pause, this, 0.7f), "left-button-menu", {36.f, win.height / 2});
    }
};
