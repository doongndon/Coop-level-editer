#include "Coop.hpp"

#include <ixwebsocket/IXWebSocket.h>

using namespace geode::prelude;

namespace {
    ix::WebSocket g_socket;
    std::atomic<coop::State> g_state = coop::State::Disconnected;
    std::atomic<int> g_peerCount = 0;

    std::string settingString(char const* key) {
        return Mod::get()->getSettingValue<std::string>(key);
    }

    void sendJoin() {
        matjson::Value msg;
        msg["type"] = "join";
        msg["room"] = settingString("room-name");
        g_socket.send(msg.dump(matjson::NO_INDENTATION));
    }

    void onSocketMessage(ix::WebSocketMessagePtr const& msg) {
        switch (msg->type) {
            case ix::WebSocketMessageType::Open:
                g_state = coop::State::Connected;
                // 연결이 (재)성사될 때마다 방에 다시 들어간다. 여기서 보내야
                // 연결되기 전에 join이 유실되는 걸 막을 수 있다.
                sendJoin();
                return;

            case ix::WebSocketMessageType::Close:
            case ix::WebSocketMessageType::Error:
                g_state = coop::State::Connecting;
                g_peerCount = 0;
                return;

            case ix::WebSocketMessageType::Message:
                break;

            default:
                return;
        }

        auto parsed = matjson::parse(msg->str);
        if (!parsed) {
            log::warn("서버 메시지를 해석하지 못했습니다");
            return;
        }

        // 소켓 콜백은 별도 스레드에서 불린다. 게임 오브젝트를 만지는 일은
        // 반드시 메인 스레드로 넘겨야 한다.
        Loader::get()->queueInMainThread([value = std::move(parsed).unwrap()]() {
            if (value["type"].asString().unwrapOr("") == "peers") {
                g_peerCount = static_cast<int>(value["count"].asInt().unwrapOr(0));
                return;
            }
            coop::handleMessage(value);
        });
    }
}

namespace coop {

    void connect() {
        auto url = settingString("server-url");

        g_socket.stop();
        g_peerCount = 0;

        if (url.empty()) {
            g_state = State::Disconnected;
            return;
        }

        g_state = State::Connecting;
        g_socket.setUrl(url);
        g_socket.setOnMessageCallback(&onSocketMessage);
        g_socket.enableAutomaticReconnection();
        g_socket.start();
    }

    State state() {
        return g_state;
    }

    int peerCount() {
        return g_peerCount;
    }

    void send(matjson::Value msg) {
        if (g_state != State::Connected) return;
        g_socket.send(msg.dump(matjson::NO_INDENTATION));
    }

}
