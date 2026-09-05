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

    // --- 내 레벨 공유 ---
    // 자동으로 올리지 않는 이유는 둘 다 같은 레벨을 열었을 때 오브젝트가
    // 두 배로 늘어나기 때문이다. 그래서 올릴지 말지는 사용자가 정한다.
    auto shareMenu = CCMenu::create();
    shareMenu->setContentSize({ 300.f, 30.f });
    shareMenu->addChild(CCMenuItemExt::createSpriteExtra(
        ButtonSprite::create("Share My Level", "bigFont.fnt", "GJ_button_04.png", 0.55f),
        [this](CCMenuItemSpriteExtra*) { this->onShareLevel(nullptr); }
    ));
    shareMenu->setLayout(RowLayout::create());
    m_mainLayer->addChildAtPosition(shareMenu, Anchor::Bottom, ccp(0.f, 40.f));

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
                m_roomInput->setString(name);
                coop::joinRoom(name);
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

void CoopPopup::onShareLevel(CCMenuItemSpriteExtra*) {
    if (!coop::inRoom()) {
        Notification::create("Join a room first", NotificationIcon::Warning)->show();
        return;
    }

    auto count = coop::unsharedCount();
    if (count == 0) {
        Notification::create("Nothing new to share", NotificationIcon::Info)->show();
        return;
    }

    coop::shareExistingLevel();
    Notification::create(
        fmt::format("Sharing {} object(s) with the room", count), NotificationIcon::Success
    )->show();
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
