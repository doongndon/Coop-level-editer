#include "CoopPopup.hpp"
#include "Coop.hpp"

#include <Geode/ui/Notification.hpp>

#include <random>

using namespace geode::prelude;

namespace {
    // 방 이름을 직접 짓지 않아도 되도록 읽기 쉬운 이름을 만들어준다.
    // 상대에게 불러주기 쉬워야 해서 헷갈리는 글자(0/O, 1/l)는 쓰지 않는다.
    std::string randomRoomName() {
        static char const* words[] = {
            "cube", "wave", "ship", "ball", "ufo", "robot", "spider", "swing",
            "dash", "portal", "spike", "orb", "pad", "coin", "star", "moon",
        };
        static char const* digits = "23456789";

        std::random_device rd;
        std::mt19937 gen(rd());

        auto word = words[gen() % (sizeof(words) / sizeof(words[0]))];
        std::string number;
        for (int i = 0; i < 3; ++i) {
            number += digits[gen() % 8];
        }
        return fmt::format("{}-{}", word, number);
    }
}

bool CoopPopup::init() {
    if (!Popup::init(320.f, 220.f)) return false;

    this->setTitle("COOP ROOM");

    // 서버 주소
    auto serverLabel = CCLabelBMFont::create("SERVER ADDRESS", "bigFont.fnt");
    serverLabel->setScale(0.35f);
    m_mainLayer->addChildAtPosition(serverLabel, Anchor::Top, ccp(0.f, -42.f));

    m_serverInput = TextInput::create(270.f, "ws://192.168.0.10:8787");
    m_serverInput->setString(Mod::get()->getSettingValue<std::string>("server-url"));
    m_mainLayer->addChildAtPosition(m_serverInput, Anchor::Top, ccp(0.f, -66.f));

    // 방 이름
    auto roomLabel = CCLabelBMFont::create("ROOM NAME", "bigFont.fnt");
    roomLabel->setScale(0.35f);
    m_mainLayer->addChildAtPosition(roomLabel, Anchor::Center, ccp(0.f, 4.f));

    m_roomInput = TextInput::create(270.f, "room name");
    m_roomInput->setString(Mod::get()->getSettingValue<std::string>("room-name"));
    m_mainLayer->addChildAtPosition(m_roomInput, Anchor::Center, ccp(0.f, -20.f));

    // 버튼 두 개
    auto menu = CCMenu::create();
    menu->setContentSize({ 280.f, 40.f });

    auto randomBtn = CCMenuItemExt::createSpriteExtra(
        ButtonSprite::create("New Room", "bigFont.fnt", "GJ_button_05.png", 0.7f),
        [this](CCMenuItemSpriteExtra*) { this->onRandomRoom(nullptr); }
    );
    randomBtn->setPosition({ 75.f, 20.f });
    menu->addChild(randomBtn);

    auto joinBtn = CCMenuItemExt::createSpriteExtra(
        ButtonSprite::create("Join", "bigFont.fnt", "GJ_button_01.png", 0.7f),
        [this](CCMenuItemSpriteExtra*) { this->onJoin(nullptr); }
    );
    joinBtn->setPosition({ 205.f, 20.f });
    menu->addChild(joinBtn);

    m_mainLayer->addChildAtPosition(menu, Anchor::Bottom, ccp(-140.f, 34.f));

    // 접속 상태
    m_statusLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_statusLabel->setScale(0.35f);
    m_mainLayer->addChildAtPosition(m_statusLabel, Anchor::Bottom, ccp(0.f, 14.f));

    this->updateStatus(0.f);
    this->schedule(schedule_selector(CoopPopup::updateStatus), 0.25f);
    return true;
}

void CoopPopup::updateStatus(float) {
    if (!m_statusLabel) return;

    switch (coop::state()) {
        case coop::State::Connected: {
            auto peers = coop::peerCount();
            m_statusLabel->setString(
                fmt::format("Connected - {} partner(s) in room", peers).c_str()
            );
            m_statusLabel->setColor(peers > 0 ? ccColor3B{ 90, 255, 90 } : ccColor3B{ 255, 220, 90 });
            break;
        }
        case coop::State::Connecting:
            m_statusLabel->setString("Connecting...");
            m_statusLabel->setColor({ 255, 220, 90 });
            break;
        case coop::State::Disconnected:
            m_statusLabel->setString("Not connected - check the address");
            m_statusLabel->setColor({ 255, 110, 110 });
            break;
    }
}

void CoopPopup::onRandomRoom(CCMenuItemSpriteExtra*) {
    m_roomInput->setString(randomRoomName());
}

void CoopPopup::onJoin(CCMenuItemSpriteExtra*) {
    auto server = std::string(m_serverInput->getString());
    auto room = std::string(m_roomInput->getString());

    if (room.empty()) {
        Notification::create("Enter a room name", NotificationIcon::Warning)->show();
        return;
    }

    // 설정에 저장해두면 다음에 게임을 켜도 그대로 이어진다.
    Mod::get()->setSettingValue<std::string>("server-url", server);
    Mod::get()->setSettingValue<std::string>("room-name", room);

    coop::connect();
    Notification::create(fmt::format("Joining \"{}\"", room), NotificationIcon::Info)->show();
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
