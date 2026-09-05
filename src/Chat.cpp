#include "Coop.hpp"

#include <Geode/ui/Notification.hpp>

#include <deque>
#include <string>

using namespace geode::prelude;

// 방 안에서 짧게 주고받는 말.
//
// GD 기본 글꼴에는 한글 글자가 없다. 한글을 넣으면 화면에 아무것도 안 나오고
// 무엇이 잘못됐는지도 알 수 없다. 그래서 보낼 때 아예 걸러내고, 걸러졌다는
// 것을 본인에게 알려준다.
namespace {
    std::deque<std::string> g_lines;
    constexpr std::size_t KEEP = 8;

    // 이 글꼴이 그릴 수 있는 글자만 남긴다.
    std::string keepDrawable(std::string const& text) {
        std::string out;
        for (auto ch : text) {
            auto code = static_cast<unsigned char>(ch);
            if (code >= 32 && code < 127) out += ch;
        }
        return out;
    }
}

namespace coop {

    void sendChat(std::string const& text) {
        if (!inRoom()) {
            Notification::create("Join a room first", NotificationIcon::Warning)->show();
            return;
        }

        auto clean = keepDrawable(text);
        if (clean.empty()) {
            Notification::create(
                "The game font can only draw English and numbers", NotificationIcon::Warning
            )->show();
            return;
        }

        matjson::Value msg;
        msg["type"] = "chat";
        msg["text"] = clean;
        send(std::move(msg));
    }

    void addChatLine(std::string const& who, std::string const& text) {
        g_lines.push_back(fmt::format("{}: {}", who, text));
        while (g_lines.size() > KEEP) g_lines.pop_front();

        // 창을 닫아둔 채로도 왔다는 것은 알아야 한다.
        if (Mod::get()->getSettingValue<bool>("show-notifications")) {
            Notification::create(g_lines.back(), NotificationIcon::Info, 4.f)->show();
        }
    }

    void clearChat() {
        g_lines.clear();
    }

    // 최근 몇 줄을 한 덩어리로. 창에서 그대로 띄운다.
    std::vector<std::string> chatLines() {
        return { g_lines.begin(), g_lines.end() };
    }

}
