#include "Coop.hpp"
#include "CoopPopup.hpp"

#include <Geode/modify/LevelBrowserLayer.hpp>

using namespace geode::prelude;

// 내 레벨 목록에 COOP 버튼을 붙인다.
//
// 지금까지는 방에 들어가려면 먼저 아무 레벨이나 열어 에디터에 들어가야 했다.
// 그러면 에디터가 한창 돌아가는 한복판에서 화면을 통째로 갈아치우게 된다.
// 다른 모드들이 그 순간 화면을 훑고 있으면 발밑이 바뀌는 셈이라 위험하다.
//
// 레벨 목록에서 들어가면 GD가 원래 레벨을 여는 방식과 같아진다.
// 에디터를 부수는 일이 아예 없어진다.
class $modify(CoopLevelBrowser, LevelBrowserLayer) {
    bool init(GJSearchObject* object) {
        if (!LevelBrowserLayer::init(object)) return false;

        // 온라인 검색 결과 같은 곳에는 띄우지 않는다. 내 레벨 목록에서만
        // 의미가 있고, 다른 화면에서는 자리만 차지한다.
        if (!object) return true;
        auto type = object->m_searchType;
        if (type != SearchType::MyLevels && type != SearchType::SavedLevels) return true;

        auto menu = CCMenu::create();
        menu->setContentSize({ 110.f, 34.f });
        menu->addChild(CCMenuItemExt::createSpriteExtra(
            ButtonSprite::create("COOP", "bigFont.fnt", "GJ_button_01.png", 0.6f),
            [](CCMenuItemSpriteExtra*) {
                if (auto popup = CoopPopup::create()) popup->show();
            }
        ));
        menu->setLayout(RowLayout::create());
        menu->setZOrder(50);

        // 왼쪽 아래. 이 화면에서 GD가 쓰지 않는 자리다.
        menu->setPosition({ 60.f, 32.f });
        this->addChild(menu);

        return true;
    }
};
