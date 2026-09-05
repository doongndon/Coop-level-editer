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
    geode::TextInput* m_passwordInput = nullptr;
    cocos2d::CCMenu* m_roomListMenu = nullptr;
    CCMenuItemSpriteExtra* m_leaveButton = nullptr;
    CCMenuItemSpriteExtra* m_updateButton = nullptr;
    CCMenuItemSpriteExtra* m_followButton = nullptr;
    cocos2d::CCLabelBMFont* m_whereLabel = nullptr;
    cocos2d::CCLabelBMFont* m_versionLabel = nullptr;
    // 진단은 두 줄이다. 한 덩어리로 만들면 줄바꿈 처리 때문에 아무것도
    // 안 그려지는 일이 있어서, 아예 라벨을 둘로 나눴다.
    cocos2d::CCLabelBMFont* m_myStatsLabel = nullptr;
    cocos2d::CCLabelBMFont* m_peerStatsLabel = nullptr;
    cocos2d::CCLabelBMFont* m_emptyLabel = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    std::string m_shownRooms;

    bool init();
    void tick(float);
    void rebuildRoomList();
    // 방을 만들거나 들어가기 직전에 열쇠를 정해준다.
    void useTypedPassword();

    void onCreate(CCMenuItemSpriteExtra*);
    void onLeave(CCMenuItemSpriteExtra*);
    void onUpdate(CCMenuItemSpriteExtra*);
    void onChat(CCMenuItemSpriteExtra*);
    void onFollow(CCMenuItemSpriteExtra*);
    // 들어가면 내 레벨이 지워지므로 먼저 물어본다.
    void askJoin(std::string const& name, bool locked);

public:
    static CoopPopup* create();
};

// 방 안에서 짧게 주고받는 말.
class CoopChatPopup : public geode::Popup {
protected:
    geode::TextInput* m_input = nullptr;
    cocos2d::CCLabelBMFont* m_lines[8] = {};
    std::string m_shown;

    bool init();
    void tick(float);
    void onSend(CCMenuItemSpriteExtra*);

public:
    static CoopChatPopup* create();
};
