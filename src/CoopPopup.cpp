#include "CoopPopup.hpp"
#include "Coop.hpp"

#include <Geode/ui/Notification.hpp>

using namespace geode::prelude;

namespace {
    // 목록에 너무 많이 띄우면 창을 넘친다. 붐빌 일도 없으니 위에서 몇 개만.
    constexpr std::size_t MAX_LISTED_ROOMS = 4;
}

bool CoopPopup::init() {
    if (!Popup::init(380.f, 300.f)) return false;

    this->setTitle("COOP ROOM");

    // 지금 어디에 있는지부터 보여준다. 이게 위에 있어야 방을 만들었는지
    // 들어갔는지 헷갈리지 않는다.
    m_whereLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_whereLabel->setScale(0.4f);
    m_mainLayer->addChildAtPosition(m_whereLabel, Anchor::Top, ccp(0.f, -38.f));

    // --- 방 이름 ---
    m_roomInput = TextInput::create(300.f, "room name", "chatFont.fnt");
    m_roomInput->setCommonFilter(CommonFilter::Any);
    m_roomInput->setMaxCharCount(32);
    m_roomInput->setString(coop::suggestedRoomName());
    m_mainLayer->addChildAtPosition(m_roomInput, Anchor::Top, ccp(0.f, -66.f));

    // --- 만들기 / 나가기 ---
    // 들어가기 버튼은 없다. 방은 아래 목록에서 눌러 들어간다.
    // 이름을 직접 쳐서 들어가게 두면 오타 하나로 없는 방에 혼자 들어가 있게 된다.
    auto actions = CCMenu::create();
    actions->setContentSize({ 320.f, 34.f });

    actions->addChild(CCMenuItemExt::createSpriteExtra(
        ButtonSprite::create("Create Room", "bigFont.fnt", "GJ_button_01.png", 0.6f),
        [this](CCMenuItemSpriteExtra*) { this->onCreate(nullptr); }
    ));

    m_leaveButton = CCMenuItemExt::createSpriteExtra(
        ButtonSprite::create("Leave", "bigFont.fnt", "GJ_button_05.png", 0.6f),
        [this](CCMenuItemSpriteExtra*) { this->onLeave(nullptr); }
    );
    actions->addChild(m_leaveButton);

    actions->setLayout(RowLayout::create()->setGap(16.f));
    m_mainLayer->addChildAtPosition(actions, Anchor::Top, ccp(0.f, -96.f));

    // --- 방 목록 ---
    // 만들어진 방은 비어 있어도 여기 뜬다. 상대는 이름을 칠 필요 없이 누르면 된다.
    auto listLabel = CCLabelBMFont::create("ROOMS", "bigFont.fnt");
    listLabel->setScale(0.35f);
    m_mainLayer->addChildAtPosition(listLabel, Anchor::Top, ccp(0.f, -122.f));

    m_roomListMenu = CCMenu::create();
    m_roomListMenu->setContentSize({ 340.f, 92.f });
    m_roomListMenu->setLayout(ColumnLayout::create()->setGap(4.f));
    m_mainLayer->addChildAtPosition(m_roomListMenu, Anchor::Top, ccp(0.f, -172.f));

    m_emptyLabel = CCLabelBMFont::create("Looking for rooms...", "chatFont.fnt");
    m_emptyLabel->setScale(0.5f);
    m_emptyLabel->setOpacity(150);
    m_mainLayer->addChildAtPosition(m_emptyLabel, Anchor::Top, ccp(0.f, -172.f));

    // 방을 만들면 내 레벨이 원본이 되고, 남의 방에 들어가면 그 레벨을 받는다.
    // 따로 "공유하기"를 누를 일이 없어졌다.
    auto hint = CCLabelBMFont::create(
        "Create = share your level  |  Join = load theirs", "chatFont.fnt"
    );
    hint->setScale(0.42f);
    hint->setOpacity(140);
    m_mainLayer->addChildAtPosition(hint, Anchor::Bottom, ccp(0.f, 38.f));

    // --- 모드 갱신 ---
    // 새 빌드가 나왔을 때 파일을 손으로 옮기지 않아도 되도록.
    auto updateMenu = CCMenu::create();
    updateMenu->setContentSize({ 90.f, 26.f });
    m_updateButton = CCMenuItemExt::createSpriteExtra(
        ButtonSprite::create("Update", "bigFont.fnt", "GJ_button_02.png", 0.45f),
        [this](CCMenuItemSpriteExtra*) { this->onUpdate(nullptr); }
    );
    updateMenu->addChild(m_updateButton);
    updateMenu->setLayout(RowLayout::create());
    m_mainLayer->addChildAtPosition(updateMenu, Anchor::TopRight, ccp(-46.f, -24.f));

    // 모드와 서버가 각각 몇 번째 판인지. 둘 다 갱신됐는지 여기서 바로 보인다.
    m_versionLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_versionLabel->setScale(0.36f);
    m_versionLabel->setOpacity(120);
    m_versionLabel->setAnchorPoint({ 0.f, 0.5f });
    m_mainLayer->addChildAtPosition(m_versionLabel, Anchor::TopLeft, ccp(14.f, -24.f));

    // 커서와 색이 실제로 오가고 있는지. 안 될 때 어느 쪽이 끊겼는지 바로 보인다.
    m_debugLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_debugLabel->setScale(0.45f);
    m_debugLabel->setColor({ 255, 220, 120 });
    m_debugLabel->setAlignment(kCCTextAlignmentCenter);
    m_mainLayer->addChildAtPosition(m_debugLabel, Anchor::Bottom, ccp(0.f, 64.f));

    // --- 접속 상태 ---
    m_statusLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_statusLabel->setScale(0.45f);
    m_mainLayer->addChildAtPosition(m_statusLabel, Anchor::Bottom, ccp(0.f, 16.f));

    coop::requestRoomList();
    this->tick(0.f);
    this->schedule(schedule_selector(CoopPopup::tick), 1.f);
    return true;
}

void CoopPopup::rebuildRoomList() {
    if (!m_roomListMenu) return;

    auto rooms = coop::roomList();
    auto here = coop::currentRoom();

    // 내용이 그대로면 다시 만들지 않는다. 누르는 도중에 버튼이 사라지면 곤란하다.
    std::string signature = here + "|";
    for (auto const& room : rooms) {
        signature += fmt::format("{}:{};", room.name, room.count);
    }
    if (signature == m_shownRooms) return;
    m_shownRooms = signature;

    m_roomListMenu->removeAllChildren();

    std::size_t shown = 0;
    for (auto const& room : rooms) {
        if (shown >= MAX_LISTED_ROOMS) break;

        auto mine = room.name == here;
        auto caption = mine
            ? fmt::format("> {} ({})", room.name, room.count)
            : fmt::format("{} ({})", room.name, room.count);

        auto sprite = ButtonSprite::create(
            caption.c_str(), "bigFont.fnt",
            mine ? "GJ_button_04.png" : "GJ_button_02.png", 0.5f
        );

        auto name = room.name;
        m_roomListMenu->addChild(CCMenuItemExt::createSpriteExtra(
            sprite,
            [this, name, mine](CCMenuItemSpriteExtra*) {
                if (mine) return;  // 이미 있는 방을 다시 누르면 아무 일도 없어야 한다
                this->askJoin(name);
            }
        ));
        ++shown;
    }

    m_roomListMenu->updateLayout();

    if (m_emptyLabel) {
        m_emptyLabel->setVisible(shown == 0);
        m_emptyLabel->setString(
            coop::state() == coop::State::Connected ? "No rooms yet - make one" : "Connecting..."
        );
    }
}

void CoopPopup::tick(float) {
    auto here = coop::currentRoom();

    if (m_whereLabel) {
        if (coop::inRoom()) {
            m_whereLabel->setString(fmt::format("IN \"{}\"", here).c_str());
            m_whereLabel->setColor({ 90, 255, 90 });
        } else {
            m_whereLabel->setString("NOT IN A ROOM");
            m_whereLabel->setColor({ 255, 160, 160 });
        }
    }

    if (m_updateButton) {
        m_updateButton->setEnabled(!coop::isUpdating());
    }

    if (m_debugLabel) {
        // 내 상태와 상대 상태를 같이 보여준다. 두 기기를 오갈 일이 없도록.
        auto theirs = coop::peerStats();
        m_debugLabel->setString(
            theirs.empty()
                ? fmt::format("me: {}", coop::diagnostics()).c_str()
                : fmt::format("me: {}\n{}", coop::diagnostics(), theirs).c_str()
        );
    }

    if (m_versionLabel) {
        auto server = coop::serverVersion();
        // modVersion()이 이미 앞에 v를 붙여 준다. 여기서 또 붙이면 "vv0.8.0".
        m_versionLabel->setString(fmt::format(
            "mod {}   server {}", coop::modVersion(), server.empty() ? "?" : server
        ).c_str());
    }

    // 방에 있을 때만 나가기를 누를 수 있다.
    if (m_leaveButton) {
        m_leaveButton->setEnabled(coop::inRoom());
        m_leaveButton->setOpacity(coop::inRoom() ? 255 : 100);
    }

    if (m_statusLabel) {
        switch (coop::state()) {
            case coop::State::Connected: {
                auto peers = coop::peerCount();
                m_statusLabel->setString(
                    coop::inRoom()
                        ? fmt::format("{} partner(s) here", peers).c_str()
                        : "Connected - pick a room below"
                );
                m_statusLabel->setColor(peers > 0 ? ccColor3B{ 90, 255, 90 } : ccColor3B{ 200, 200, 200 });
                break;
            }
            case coop::State::Connecting: {
                auto error = coop::lastError();
                m_statusLabel->setString(error.empty() ? "Connecting..." : error.c_str());
                m_statusLabel->setColor({ 255, 220, 90 });
                break;
            }
            case coop::State::Disconnected:
                m_statusLabel->setString("Not connected");
                m_statusLabel->setColor({ 255, 110, 110 });
                break;
        }
    }

    coop::requestRoomList();
    this->rebuildRoomList();
}

void CoopPopup::onCreate(CCMenuItemSpriteExtra*) {
    auto room = std::string(m_roomInput->getString());
    if (room.empty()) {
        room = coop::suggestedRoomName();
        m_roomInput->setString(room);
    }

    coop::createRoom(room);
}

void CoopPopup::onLeave(CCMenuItemSpriteExtra*) {
    coop::leaveRoom();
}

// 새 빌드를 받아 이 모드 자리에 덮어쓴다. 게임을 다시 켜면 새 것이 올라온다.
void CoopPopup::onUpdate(CCMenuItemSpriteExtra*) {
    if (coop::isUpdating()) return;

    geode::createQuickPopup(
        "Update Mod",
        "Download the newest build and replace this mod?\n\n"
        "You are on <cy>v" + coop::modVersion() + "</c>.\n"
        "It takes effect after you <cg>restart GD</c>.",
        "Cancel", "Update",
        [](FLAlertLayer*, bool go) {
            if (go) coop::updateMod();
        }
    );
}

// 남의 방에 들어가면 지금 편집 중인 레벨은 그 방의 레벨로 바뀐다.
// 되돌릴 수 없는 일이라 반드시 먼저 물어본다.
void CoopPopup::askJoin(std::string const& name) {
    geode::createQuickPopup(
        "Join Room",
        fmt::format(
            "Joining <cy>{}</c> replaces the level you have open with that room's level.\n"
            "Your current level will be <cr>cleared</c>.\n\nContinue?",
            name
        ),
        "Cancel", "Join",
        [name](FLAlertLayer*, bool joined) {
            if (joined) coop::joinRoom(name);
        }
    );
}

CoopPopup* CoopPopup::create() {
    auto ret = new CoopPopup();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}
