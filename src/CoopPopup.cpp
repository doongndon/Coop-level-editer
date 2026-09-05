#include "CoopPopup.hpp"
#include "Coop.hpp"

#include <Geode/ui/Notification.hpp>

using namespace geode::prelude;

namespace {
    // 목록에 너무 많이 띄우면 창을 넘친다. 붐빌 일도 없으니 위에서 몇 개만.
    constexpr std::size_t MAX_LISTED_ROOMS = 4;
}

bool CoopPopup::init() {
    if (!Popup::init(360.f, 290.f)) return false;

    this->setTitle("COOP ROOM");

    // --- 방 이름 ---
    auto roomLabel = CCLabelBMFont::create("ROOM NAME", "bigFont.fnt");
    roomLabel->setScale(0.35f);
    m_mainLayer->addChildAtPosition(roomLabel, Anchor::Top, ccp(0.f, -40.f));

    m_roomInput = TextInput::create(300.f, "room name", "chatFont.fnt");
    m_roomInput->setCommonFilter(CommonFilter::Any);
    m_roomInput->setString(coop::currentRoom());
    m_mainLayer->addChildAtPosition(m_roomInput, Anchor::Top, ccp(0.f, -62.f));

    // --- 내 방 / 참여 ---
    auto actions = CCMenu::create();
    actions->setContentSize({ 300.f, 34.f });

    actions->addChild(CCMenuItemExt::createSpriteExtra(
        ButtonSprite::create("My Room", "bigFont.fnt", "GJ_button_05.png", 0.6f),
        [this](CCMenuItemSpriteExtra*) { this->onMyRoom(nullptr); }
    ));
    actions->addChild(CCMenuItemExt::createSpriteExtra(
        ButtonSprite::create("Join", "bigFont.fnt", "GJ_button_01.png", 0.6f),
        [this](CCMenuItemSpriteExtra*) { this->onJoin(nullptr); }
    ));

    actions->setLayout(RowLayout::create()->setGap(20.f));
    m_mainLayer->addChildAtPosition(actions, Anchor::Top, ccp(0.f, -92.f));

    // --- 열려 있는 방 목록 ---
    // 이름을 직접 치지 않고 눌러서 바로 들어갈 수 있도록.
    auto listLabel = CCLabelBMFont::create("OPEN ROOMS", "bigFont.fnt");
    listLabel->setScale(0.35f);
    m_mainLayer->addChildAtPosition(listLabel, Anchor::Top, ccp(0.f, -118.f));

    m_roomListMenu = CCMenu::create();
    m_roomListMenu->setContentSize({ 320.f, 92.f });
    m_roomListMenu->setLayout(ColumnLayout::create()->setGap(4.f));
    m_mainLayer->addChildAtPosition(m_roomListMenu, Anchor::Top, ccp(0.f, -168.f));

    m_emptyLabel = CCLabelBMFont::create("Looking for rooms...", "chatFont.fnt");
    m_emptyLabel->setScale(0.5f);
    m_emptyLabel->setOpacity(150);
    m_mainLayer->addChildAtPosition(m_emptyLabel, Anchor::Top, ccp(0.f, -168.f));

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
    m_statusLabel->setScale(0.5f);
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
    std::string signature;
    for (auto const& room : rooms) {
        signature += fmt::format("{}:{};", room.name, room.count);
    }
    if (signature == m_shownRooms) return;
    m_shownRooms = signature;

    m_roomListMenu->removeAllChildren();

    std::size_t shown = 0;
    for (auto const& room : rooms) {
        if (shown >= MAX_LISTED_ROOMS) break;
        if (room.name == here) continue;  // 이미 있는 방은 띄울 필요가 없다

        auto caption = fmt::format("{} ({})", room.name, room.count);
        auto sprite = ButtonSprite::create(caption.c_str(), "bigFont.fnt", "GJ_button_02.png", 0.5f);

        auto name = room.name;
        m_roomListMenu->addChild(CCMenuItemExt::createSpriteExtra(
            sprite,
            [this, name](CCMenuItemSpriteExtra*) {
                m_roomInput->setString(name);
                coop::joinRoom(name);
                Notification::create(
                    fmt::format("Joining \"{}\"", name), NotificationIcon::Info
                )->show();
            }
        ));
        ++shown;
    }

    m_roomListMenu->updateLayout();

    if (m_emptyLabel) {
        m_emptyLabel->setVisible(shown == 0);
        m_emptyLabel->setString(
            coop::state() == coop::State::Connected ? "No other rooms open" : "Connecting..."
        );
    }
}

void CoopPopup::tick(float) {
    if (m_statusLabel) {
        switch (coop::state()) {
            case coop::State::Connected: {
                auto peers = coop::peerCount();
                m_statusLabel->setString(
                    fmt::format("In \"{}\" - {} partner(s)", coop::currentRoom(), peers).c_str()
                );
                m_statusLabel->setColor(peers > 0 ? ccColor3B{ 90, 255, 90 } : ccColor3B{ 255, 220, 90 });
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

void CoopPopup::onMyRoom(CCMenuItemSpriteExtra*) {
    auto mine = coop::defaultRoomName();
    m_roomInput->setString(mine);
    coop::joinRoom(mine);
    Notification::create("Back to your own room", NotificationIcon::Info)->show();
}

void CoopPopup::onJoin(CCMenuItemSpriteExtra*) {
    auto room = std::string(m_roomInput->getString());
    if (room.empty()) {
        room = coop::defaultRoomName();
        m_roomInput->setString(room);
    }

    coop::joinRoom(room);
    Notification::create(fmt::format("Joining \"{}\"", room), NotificationIcon::Info)->show();
}

void CoopPopup::onShareLevel(CCMenuItemSpriteExtra*) {
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
