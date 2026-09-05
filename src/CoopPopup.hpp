#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

// 방 접속 창.
// 서버 주소와 방 이름을 여기서 바로 고치고 접속할 수 있다.
// (설정 화면까지 들어가지 않아도 되도록)
class CoopPopup : public geode::Popup {
protected:
    geode::TextInput* m_serverInput = nullptr;
    geode::TextInput* m_roomInput = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;

    bool init();
    void updateStatus(float);
    void onJoin(CCMenuItemSpriteExtra*);
    void onRandomRoom(CCMenuItemSpriteExtra*);
    void onShareLevel(CCMenuItemSpriteExtra*);

public:
    static CoopPopup* create();
};
