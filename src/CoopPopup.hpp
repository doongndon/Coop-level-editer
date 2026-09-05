#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

#include <string>

// 방 창.
//
// 규칙은 하나다. 방은 "Create Room"으로만 생기고, 들어가기는 목록에서만 한다.
// 이름을 쳐서 들어가는 길을 없앤 이유는, 오타 하나로 아무도 없는 방에 혼자
// 들어가 놓고 "왜 상대가 안 보이지" 하게 되기 때문이다.
//
// 서버 주소는 여기서 다루지 않는다. 정해진 서버로 자동 접속한다.
class CoopPopup : public geode::Popup {
protected:
    geode::TextInput* m_roomInput = nullptr;
    cocos2d::CCMenu* m_roomListMenu = nullptr;
    CCMenuItemSpriteExtra* m_leaveButton = nullptr;
    CCMenuItemSpriteExtra* m_updateButton = nullptr;
    cocos2d::CCLabelBMFont* m_whereLabel = nullptr;
    cocos2d::CCLabelBMFont* m_versionLabel = nullptr;
    cocos2d::CCLabelBMFont* m_debugLabel = nullptr;
    cocos2d::CCLabelBMFont* m_emptyLabel = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    std::string m_shownRooms;

    bool init();
    void tick(float);
    void rebuildRoomList();

    void onCreate(CCMenuItemSpriteExtra*);
    void onLeave(CCMenuItemSpriteExtra*);
    void onUpdate(CCMenuItemSpriteExtra*);
    // 들어가면 내 레벨이 지워지므로 먼저 물어본다.
    void askJoin(std::string const& name);

public:
    static CoopPopup* create();
};
