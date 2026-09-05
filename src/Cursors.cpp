#include "Coop.hpp"

#include <chrono>
#include <string>
#include <unordered_map>

using namespace geode::prelude;

// 상대가 지금 어디를 만지고 있는지 화면에 보여준다.
//
// 커서는 레벨 좌표로 주고받는다. 사람마다 화면 크기와 확대 배율이 다르기 때문에
// 화면 좌표를 그대로 보내면 엉뚱한 곳에 찍힌다. 레벨 안의 자리로 말해야
// 서로 같은 곳을 가리킬 수 있다.
namespace {
    struct PeerCursor {
        Ref<CCNode> node;
        std::chrono::steady_clock::time_point lastSeen;
    };

    std::unordered_map<std::string, PeerCursor> g_cursors;
    std::chrono::steady_clock::time_point g_lastSent;

    // 보낸 개수와 받은 개수. 어느 쪽이 끊겼는지 창에서 바로 보려고 센다.
    int g_sentCount = 0;
    int g_gotCount = 0;

    // 손가락은 계속 움직이므로 그대로 다 보내면 회선이 낭비된다.
    constexpr auto SEND_INTERVAL = std::chrono::milliseconds(100);
    // 한동안 소식이 없는 커서는 치운다. 상대가 손을 뗐거나 나간 것.
    constexpr auto CURSOR_LIFETIME = std::chrono::seconds(3);

    // 사람마다 다른 색을 준다. 누가 누군지 구분되도록.
    ccColor3B colorFor(std::string const& id) {
        static ccColor3B const palette[] = {
            { 110, 200, 255 }, { 255, 170, 90 }, { 160, 255, 130 },
            { 255, 130, 200 }, { 220, 160, 255 }, { 255, 230, 110 },
        };
        std::size_t sum = 0;
        for (auto ch : id) sum += static_cast<unsigned char>(ch);
        return palette[sum % (sizeof(palette) / sizeof(palette[0]))];
    }

    CCNode* buildCursor(std::string const& name, ccColor3B color) {
        auto holder = CCNode::create();
        holder->setZOrder(30000);

        // 게임 그림을 빌려 쓰지 않고 직접 그린다.
        //
        // 처음에는 게임에 있는 동그라미 그림을 갖다 썼는데, 그 그림은 특정
        // 그림 묶음이 메모리에 올라와 있을 때만 찾을 수 있다. 에디터에서
        // 그 묶음이 없으면 아무것도 안 나온다. 직접 그리면 그럴 일이 없다.
        auto dot = CCDrawNode::create();
        auto fill = ccColor4F{ color.r / 255.f, color.g / 255.f, color.b / 255.f, 0.85f };
        dot->drawDot({ 0.f, 0.f }, 7.f, fill);
        dot->drawDot({ 0.f, 0.f }, 3.f, ccColor4F{ 1.f, 1.f, 1.f, 0.9f });
        holder->addChild(dot);

        auto label = CCLabelBMFont::create(name.c_str(), "chatFont.fnt");
        label->setScale(0.5f);
        label->setColor(color);
        label->setAnchorPoint({ 0.f, 0.5f });
        label->setPosition({ 9.f, 9.f });
        holder->addChild(label);

        return holder;
    }
}

namespace coop {

    void sendCursor(CCPoint position) {
        if (!inRoom()) return;
        if (!Mod::get()->getSettingValue<bool>("show-cursors")) return;

        auto now = std::chrono::steady_clock::now();
        if (now - g_lastSent < SEND_INTERVAL) return;
        g_lastSent = now;

        matjson::Value msg;
        msg["type"] = "cursor";
        msg["x"] = position.x;
        msg["y"] = position.y;
        send(std::move(msg));
        ++g_sentCount;
    }

    int cursorsSent() { return g_sentCount; }
    int cursorsReceived() { return g_gotCount; }

    void applyCursor(std::string const& id, std::string const& name, CCPoint position) {
        // 세는 것을 맨 먼저 한다. 아래에서 걸러져도 "서버에서 오긴 왔다"는
        // 사실은 남아야, 안 보이는 원인이 전달 문제인지 그리기 문제인지 갈린다.
        ++g_gotCount;

        if (!Mod::get()->getSettingValue<bool>("show-cursors")) return;

        auto editor = LevelEditorLayer::get();
        if (!editor || !editor->m_objectLayer) return;

        auto found = g_cursors.find(id);
        if (found == g_cursors.end()) {
            auto node = buildCursor(name.empty() ? "?" : name, colorFor(id));
            // 오브젝트 레이어에 붙여야 레벨을 움직이거나 확대해도 같이 따라간다.
            editor->m_objectLayer->addChild(node);
            found = g_cursors.emplace(id, PeerCursor{ node, {} }).first;
        }

        if (auto node = found->second.node.data()) {
            node->setPosition(position);
            node->setVisible(true);
        }
        found->second.lastSeen = std::chrono::steady_clock::now();
    }

    void removeCursor(std::string const& id) {
        auto found = g_cursors.find(id);
        if (found == g_cursors.end()) return;

        if (auto node = found->second.node.data()) {
            node->removeFromParent();
        }
        g_cursors.erase(found);
    }

    void clearCursors() {
        for (auto& [id, cursor] : g_cursors) {
            if (auto node = cursor.node.data()) {
                node->removeFromParent();
            }
        }
        g_cursors.clear();
    }

    void fadeOldCursors() {
        auto now = std::chrono::steady_clock::now();
        for (auto it = g_cursors.begin(); it != g_cursors.end();) {
            if (now - it->second.lastSeen > CURSOR_LIFETIME) {
                if (auto node = it->second.node.data()) {
                    node->removeFromParent();
                }
                it = g_cursors.erase(it);
            } else {
                ++it;
            }
        }
    }

}
