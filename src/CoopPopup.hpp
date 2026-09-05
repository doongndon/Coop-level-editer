#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

#include <string>

// 방 접속 창.
//
// 서버 주소는 더 이상 여기서 다루지 않는다. 정해진 서버로 자동 접속하므로
// 사용자가 주소를 칠 일도, 잘못 고칠 일도 없다.
class CoopPopup : public geode::Popup {
protected:
    geode::TextInput* m_roomInput = nullptr;
    cocos2d::CCMenu* m_roomListMenu = nullptr;
    cocos2d::CCLabelBMFont* m_emptyLabel = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    std::string m_shownRooms;

    bool init();
    void tick(float);
    void rebuildRoomList();

    void onJoin(CCMenuItemSpriteExtra*);
    void onMyRoom(CCMenuItemSpriteExtra*);
    void onShareLevel(CCMenuItemSpriteExtra*);

public:
    static CoopPopup* create();
};
