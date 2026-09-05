#include "CoopPopup.hpp"
#include "Coop.hpp"

#include <Geode/ui/Notification.hpp>

using namespace geode::prelude;

namespace {
    // 목록에 너무 많이 띄우면 창을 넘친다. 붐빌 일도 없으니 위에서 몇 개만.
    constexpr std::size_t MAX_LISTED_ROOMS = 3;
}

bool CoopPopup::init() {
    if (!Popup::init(380.f, 320.f)) return false;

    this->setTitle("COOP ROOM");

    // 지금 어디에 있는지부터 보여준다. 이게 위에 있어야 방을 만들었는지
    // 들어갔는지 헷갈리지 않는다.
    m_whereLabel = CCLabelBMFont::create("...", "bigFont.fnt");
    m_whereLabel->setScale(0.38f);
    m_mainLayer->addChildAtPosition(m_whereLabel, Anchor::Top, ccp(0.f, -34.f));

    // 모드와 서버가 각각 몇 번째 판인지. 둘 다 갱신됐는지 여기서 바로 보인다.
    m_versionLabel = CCLabelBMFont::create("...", "chatFont.fnt");
    m_versionLabel->setScale(0.4f);
    m_versionLabel->setAnchorPoint({ 0.f, 0.5f });
    m_versionLabel->setColor({ 170, 170, 170 });
    m_mainLayer->addChildAtPosition(m_versionLabel, Anchor::TopLeft, ccp(12.f, -22.f));

    auto updateMenu = CCMenu::create();
    updateMenu->setContentSize({ 90.f, 26.f });
    m_updateButton = CCMenuItemExt::createSpriteExtra(
        ButtonSprite::create("Update", "bigFont.fnt", "GJ_button_02.png", 0.42f),
        [this](CCMenuItemSpriteExtra*) { this->onUpdate(nullptr); }
    );
    updateMenu->addChild(m_updateButton);
    updateMenu->setLayout(RowLayout::create());
    m_mainLayer->addChildAtPosition(updateMenu, Anchor::TopRight, ccp(-44.f, -22.f));

    // --- 방 이름과 열쇠 ---
    m_roomInput = TextInput::create(300.f, "room name", "chatFont.fnt");
    m_roomInput->setCommonFilter(CommonFilter::Any);
    m_roomInput->setMaxCharCount(32);
    m_roomInput->setString(coop::suggestedRoomName());
    m_mainLayer->addChildAtPosition(m_roomInput, Anchor::Top, ccp(0.f, -62.f));

    // 비워두면 잠그지 않는다. 잠긴 방에 들어갈 때도 이 칸을 쓴다.
    m_passwordInput = TextInput::create(300.f, "password (optional)", "chatFont.fnt");
    m_passwordInput->setCommonFilter(CommonFilter::Any);
    m_passwordInput->setMaxCharCount(32);
    m_mainLayer->addChildAtPosition(m_passwordInput, Anchor::Top, ccp(0.f, -88.f));

    // --- 만들기 / 나가기 ---
    // 들어가기 버튼은 없다. 방은 아래 목록에서 눌러 들어간다.
    // 이름을 직접 쳐서 들어가게 두면 오타 하나로 없는 방에 혼자 들어가 있게 된다.
    auto actions = CCMenu::create();
    actions->setContentSize({ 320.f, 32.f });

    actions->addChild(CCMenuItemExt::createSpriteExtra(
        ButtonSprite::create("Create Room", "bigFont.fnt", "GJ_button_01.png", 0.55f),
        [this](CCMenuItemSpriteExtra*) { this->onCreate(nullptr); }
    ));

    m_leaveButton = CCMenuItemExt::createSpriteExtra(
        ButtonSprite::create("Leave", "bigFont.fnt", "GJ_button_05.png", 0.55f),
        [this](CCMenuItemSpriteExtra*) { this->onLeave(nullptr); }
    );
    actions->addChild(m_leaveButton);

    actions->setLayout(RowLayout::create()->setGap(16.f));
    m_mainLayer->addChildAtPosition(actions, Anchor::Top, ccp(0.f, -118.f));

    // --- 방 목록 ---
    // 만들어진 방은 비어 있어도 여기 뜬다. 상대는 이름을 칠 필요 없이 누르면 된다.
    auto listLabel = CCLabelBMFont::create("ROOMS", "bigFont.fnt");
    listLabel->setScale(0.3f);
    m_mainLayer->addChildAtPosition(listLabel, Anchor::Top, ccp(0.f, -142.f));

    m_roomListMenu = CCMenu::create();
    m_roomListMenu->setContentSize({ 340.f, 72.f });
    m_roomListMenu->setLayout(ColumnLayout::create()->setGap(3.f));
    m_mainLayer->addChildAtPosition(m_roomListMenu, Anchor::Top, ccp(0.f, -186.f));

    m_emptyLabel = CCLabelBMFont::create("Looking for rooms...", "chatFont.fnt");
    m_emptyLabel->setScale(0.5f);
    m_emptyLabel->setOpacity(150);
    m_mainLayer->addChildAtPosition(m_emptyLabel, Anchor::Top, ccp(0.f, -186.f));

    // --- 상태 두 줄 ---
    // 내 쪽과 상대 쪽이 각각 얼마나 주고받고 있는지. 두 기기를 오갈 일이 없도록.
    m_myStatsLabel = CCLabelBMFont::create("...", "chatFont.fnt");
    m_myStatsLabel->setScale(0.42f);
    m_myStatsLabel->setColor({ 255, 220, 120 });
    m_mainLayer->addChildAtPosition(m_myStatsLabel, Anchor::Bottom, ccp(0.f, 88.f));

    m_peerStatsLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_peerStatsLabel->setScale(0.42f);
    m_peerStatsLabel->setColor({ 150, 220, 255 });
    m_mainLayer->addChildAtPosition(m_peerStatsLabel, Anchor::Bottom, ccp(0.f, 74.f));

    // --- 채팅 / 화면 따라가기 ---
    auto tools = CCMenu::create();
    tools->setContentSize({ 320.f, 30.f });

    tools->addChild(CCMenuItemExt::createSpriteExtra(
        ButtonSprite::create("Chat", "bigFont.fnt", "GJ_button_04.png", 0.5f),
        [this](CCMenuItemSpriteExtra*) { this->onChat(nullptr); }
    ));

    m_followButton = CCMenuItemExt::createSpriteExtra(
        ButtonSprite::create("Go To Partner", "bigFont.fnt", "GJ_button_02.png", 0.5f),
        [this](CCMenuItemSpriteExtra*) { this->onFollow(nullptr); }
    );
    tools->addChild(m_followButton);

    tools->setLayout(RowLayout::create()->setGap(14.f));
    m_mainLayer->addChildAtPosition(tools, Anchor::Bottom, ccp(0.f, 48.f));

    // --- 접속 상태 ---
    m_statusLabel = CCLabelBMFont::create("...", "chatFont.fnt");
    m_statusLabel->setScale(0.45f);
    m_mainLayer->addChildAtPosition(m_statusLabel, Anchor::Bottom, ccp(0.f, 18.f));

    coop::requestRoomList();
    this->tick(0.f);
    this->schedule(schedule_selector(CoopPopup::tick), 1.f);
    return true;
}

void CoopPopup::useTypedPassword() {
    coop::setRoomPassword(std::string(m_passwordInput->getString()));
}

void CoopPopup::rebuildRoomList() {
    if (!m_roomListMenu) return;

    auto rooms = coop::roomList();
    auto here = coop::currentRoom();

    // 내용이 그대로면 다시 만들지 않는다. 누르는 도중에 버튼이 사라지면 곤란하다.
    std::string signature = here + "|";
    for (auto const& room : rooms) {
        signature += fmt::format("{}:{}:{};", room.name, room.count, room.locked ? 1 : 0);
    }
    if (signature == m_shownRooms) return;
    m_shownRooms = signature;

    m_roomListMenu->removeAllChildren();

    std::size_t shown = 0;
    for (auto const& room : rooms) {
        if (shown >= MAX_LISTED_ROOMS) break;

        auto mine = room.name == here;
        // 잠긴 방은 별표로 표시한다. 글꼴에 자물쇠 글자가 없어서 별을 쓴다.
        auto caption = fmt::format(
            "{}{}{} ({})", mine ? "> " : "", room.locked ? "* " : "", room.name, room.count
        );

        auto sprite = ButtonSprite::create(
            caption.c_str(), "bigFont.fnt",
            mine ? "GJ_button_04.png" : "GJ_button_02.png", 0.42f
        );

        auto name = room.name;
        auto locked = room.locked;
        m_roomListMenu->addChild(CCMenuItemExt::createSpriteExtra(
            sprite,
            [this, name, mine, locked](CCMenuItemSpriteExtra*) {
                if (mine) return;  // 이미 있는 방을 다시 누르면 아무 일도 없어야 한다
                this->askJoin(name, locked);
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

    if (m_versionLabel) {
        auto server = coop::serverVersion();
        // modVersion()이 이미 앞에 v를 붙여 준다. 여기서 또 붙이면 "vv0.9.0".
        m_versionLabel->setString(fmt::format(
            "mod {} / server {}", coop::modVersion(), server.empty() ? "?" : server
        ).c_str());
    }

    if (m_myStatsLabel) {
        m_myStatsLabel->setString(fmt::format("me  {}", coop::diagnostics()).c_str());
    }

    if (m_peerStatsLabel) {
        auto theirs = coop::peerStats();
        m_peerStatsLabel->setString(theirs.empty() ? "" : theirs.c_str());
    }

    if (m_updateButton) {
        m_updateButton->setEnabled(!coop::isUpdating());
    }

    if (m_followButton) {
        m_followButton->setEnabled(coop::hasPeerView());
    }

    // 방에 있을 때만 나가기를 누를 수 있다.
    if (m_leaveButton) {
        m_leaveButton->setEnabled(coop::inRoom());
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

    this->useTypedPassword();
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
        "You are on <cy>" + coop::modVersion() + "</c>.\n"
        "It takes effect after you <cg>restart GD</c>.",
        "Cancel", "Update",
        [](FLAlertLayer*, bool go) {
            if (go) coop::updateMod();
        }
    );
}

void CoopPopup::onChat(CCMenuItemSpriteExtra*) {
    if (auto popup = CoopChatPopup::create()) {
        popup->show();
    }
}

void CoopPopup::onFollow(CCMenuItemSpriteExtra*) {
    if (!coop::hasPeerView()) {
        Notification::create("No partner view yet", NotificationIcon::Info)->show();
        return;
    }

    coop::goToPeerView();
    this->onClose(nullptr);
}

// 남의 방에 들어가면 화면이 그 방의 레벨로 넘어간다.
// 내 레벨을 건드리지는 않지만, 하던 일이 끊기므로 먼저 물어본다.
void CoopPopup::askJoin(std::string const& name, bool locked) {
    auto note = locked
        ? fmt::format(
            "<cy>{}</c> is locked.\n"
            "Type its password in the password box first,\nthen press Join.\n\nContinue?", name
        )
        : fmt::format(
            "Join <cy>{}</c>?\n\n"
            "You will move to a temporary level for that room.\n"
            "Your own levels are not touched.", name
        );

    geode::createQuickPopup(
        "Join Room", note, "Cancel", "Join",
        [this, name](FLAlertLayer*, bool joined) {
            if (!joined) return;
            this->useTypedPassword();
            coop::joinRoom(name);
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

// --- 채팅 창 ---

bool CoopChatPopup::init() {
    if (!Popup::init(340.f, 260.f)) return false;

    this->setTitle("COOP CHAT");

    // 줄마다 라벨을 따로 둔다. 한 라벨에 줄바꿈을 넣으면 폭 계산 때문에
    // 아무것도 안 그려지는 일이 있었다.
    for (int i = 0; i < 8; ++i) {
        auto line = CCLabelBMFont::create("", "chatFont.fnt");
        line->setScale(0.45f);
        line->setAnchorPoint({ 0.f, 0.5f });
        m_mainLayer->addChildAtPosition(line, Anchor::TopLeft, ccp(20.f, -46.f - i * 15.f));
        m_lines[i] = line;
    }

    m_input = TextInput::create(230.f, "message", "chatFont.fnt");
    m_input->setCommonFilter(CommonFilter::Any);
    m_input->setMaxCharCount(100);
    m_mainLayer->addChildAtPosition(m_input, Anchor::Bottom, ccp(-40.f, 34.f));

    auto sendMenu = CCMenu::create();
    sendMenu->setContentSize({ 70.f, 30.f });
    sendMenu->addChild(CCMenuItemExt::createSpriteExtra(
        ButtonSprite::create("Send", "bigFont.fnt", "GJ_button_01.png", 0.5f),
        [this](CCMenuItemSpriteExtra*) { this->onSend(nullptr); }
    ));
    sendMenu->setLayout(RowLayout::create());
    m_mainLayer->addChildAtPosition(sendMenu, Anchor::Bottom, ccp(120.f, 34.f));

    auto note = CCLabelBMFont::create("English and numbers only", "chatFont.fnt");
    note->setScale(0.35f);
    note->setOpacity(120);
    m_mainLayer->addChildAtPosition(note, Anchor::Bottom, ccp(0.f, 14.f));

    this->tick(0.f);
    this->schedule(schedule_selector(CoopChatPopup::tick), 0.5f);
    return true;
}

void CoopChatPopup::tick(float) {
    auto lines = coop::chatLines();

    std::string signature;
    for (auto const& line : lines) signature += line + "\n";
    if (signature == m_shown) return;
    m_shown = signature;

    for (int i = 0; i < 8; ++i) {
        if (!m_lines[i]) continue;
        auto index = static_cast<std::size_t>(i);
        m_lines[i]->setString(index < lines.size() ? lines[index].c_str() : "");
    }
}

void CoopChatPopup::onSend(CCMenuItemSpriteExtra*) {
    auto text = std::string(m_input->getString());
    if (text.empty()) return;

    coop::sendChat(text);
    m_input->setString("");
}

CoopChatPopup* CoopChatPopup::create() {
    auto ret = new CoopChatPopup();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}
