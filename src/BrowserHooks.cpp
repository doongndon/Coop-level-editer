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

        // 남은 임시 레벨을 여기서도 치운다.
        //
        // 첫 화면에서만 치우면, 에디터에서 곧장 레벨 목록으로 돌아온 경우에는
        // 그 레벨이 목록에 보인 채로 남는다. 사람들이 실제로 보는 자리가
        // 여기라서, 여기서도 한 번 훑어야 "남았다"는 말이 안 나온다.
        coop::sweepOldWorkspaces();

        // 온라인 검색 결과 같은 곳에는 띄우지 않는다. 내 레벨 목록에서만
        // 의미가 있고, 다른 화면에서는 자리만 차지한다.
        if (!object) return true;
        auto type = object->m_searchType;
        if (type != SearchType::MyLevels && type != SearchType::SavedLevels) return true;

        auto menu = CCMenu::create();
        menu->setContentSize({ 96.f, 30.f });
        menu->addChild(CCMenuItemExt::createSpriteExtra(
            ButtonSprite::create("COOP", "bigFont.fnt", "GJ_button_01.png", 0.5f),
            [](CCMenuItemSpriteExtra*) {
                if (auto popup = CoopPopup::create()) popup->show();
            }
        ));
        menu->setLayout(RowLayout::create());
        menu->setZOrder(50);
        menu->setPosition(this->coopSpot());
        this->addChild(menu);

        return true;
    }

    // 이 화면도 사방이 버튼이라 어디가 비었는지는 화면 크기마다 다르다.
    // 처음에 왼쪽 아래에 뒀더니 GD 자기 버튼 위에 겹쳤다. 고르게 한다.
    cocos2d::CCPoint coopSpot() {
        auto size = CCDirector::get()->getWinSize();
        auto spot = Mod::get()->getSettingValue<std::string>("browser-spot");

        // 화면 가장자리에 최대한 붙인다. 이 화면 왼쪽 줄에는 GD 버튼들이
        // 안쪽으로 한 칸 들어와 있어서, 바깥쪽 끝이 제일 안전하다.
        auto left = 38.f;
        auto right = size.width - 38.f;

        if (spot == "top-left") return { left, size.height - 26.f };
        if (spot == "top-right") return { right, size.height - 26.f };
        if (spot == "bottom-left") return { left, 26.f };
        if (spot == "bottom-right") return { right, 26.f };
        if (spot == "left-middle") return { left, size.height * 0.45f };
        if (spot == "right-middle") return { right, size.height * 0.45f };

        // 기본값. 왼쪽 위 돋보기와 왼쪽 아래 파일 버튼 사이가 통째로 비어 있다.
        // 목록 창보다 왼쪽이라 목록을 가리지도 않는다.
        return { size.width * 0.148f, size.height * 0.636f };
    }
};
