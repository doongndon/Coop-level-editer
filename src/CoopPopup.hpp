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
    CCMenuItemSpriteExtra* m_createButton = nullptr;
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

    CCMenuItemSpriteExtra* m_soloButton = nullptr;

    void onCreate(CCMenuItemSpriteExtra*);
    void onLeave(CCMenuItemSpriteExtra*);
    void onSolo(CCMenuItemSpriteExtra*);
    void onUpdate(CCMenuItemSpriteExtra*);
    void onChat(CCMenuItemSpriteExtra*);
    void onFollow(CCMenuItemSpriteExtra*);
    // 들어가면 내 레벨이 지워지므로 먼저 물어본다.
    void askJoin(std::string const& name, bool locked);

public:
    static CoopPopup* create();
};

// 방에서 레벨을 받는 동안 띄우는 창.
//
// 오브젝트가 많은 레벨은 다 받는 데 시간이 걸린다. 그동안 아무것도
// 안 보여주면 멈춘 것처럼 보이고, 편집을 시작해버리면 받는 중인 것과
// 뒤섞인다. 다 받을 때까지 막아두는 편이 낫다.
class CoopLoadingPopup : public geode::Popup {
protected:
    cocos2d::CCLabelBMFont* m_countLabel = nullptr;
    cocos2d::CCLayerColor* m_barFill = nullptr;
    int m_mostSeen = 0;

    bool init();
    void tick(float);

public:
    static CoopLoadingPopup* create();
    // 지금 받는 중이면 띄우고, 다 받았으면 닫는다.
    static void refresh();
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
