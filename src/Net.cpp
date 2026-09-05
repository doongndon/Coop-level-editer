#include "Coop.hpp"

#include <ixwebsocket/IXWebSocket.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

using namespace geode::prelude;

namespace {
    ix::WebSocket g_socket;
    std::atomic<coop::State> g_state = coop::State::Disconnected;
    std::atomic<int> g_peerCount = 0;

    // 접속이 왜 실패했는지. 소켓 스레드가 쓰고 화면 쪽에서 읽는다.
    // 이게 없으면 서버가 꺼졌든 주소가 틀렸든 전부 "연결 중"으로만 보여서
    // 무엇을 고쳐야 할지 알 수가 없다.
    std::mutex g_errorMutex;
    std::string g_lastError;

    void setLastError(std::string reason) {
        std::lock_guard lock(g_errorMutex);
        g_lastError = std::move(reason);
    }

    std::string settingString(char const* key) {
        return Mod::get()->getSettingValue<std::string>(key);
    }

    // 인증서 묶음을 파일로 한 번 꺼내놓고 그 경로를 돌려준다.
    // 내용을 그대로 넘기는 방법도 있지만 IXWebSocket v11.4.6은 그 경우
    // 검사 단계에서 "파일이 없다"며 막아버려서 파일로 쓰는 편이 확실하다.
    std::string caBundlePath() {
        auto path = Mod::get()->getSaveDir() / "cacert.pem";

        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            std::ofstream out(path, std::ios::binary);
            if (!out) {
                log::warn("인증서 묶음을 저장하지 못했습니다. wss:// 접속이 실패할 수 있습니다");
                return "";
            }
            out << coop::caBundlePem();
        }
        return path.string();
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
                setLastError("");
                // 연결이 (재)성사될 때마다 방에 다시 들어간다. 여기서 보내야
                // 연결되기 전에 join이 유실되는 걸 막을 수 있다.
                sendJoin();
                return;

            case ix::WebSocketMessageType::Error:
                g_state = coop::State::Connecting;
                g_peerCount = 0;
                setLastError(msg->errorInfo.reason);
                log::warn("접속 실패: {}", msg->errorInfo.reason);
                return;

            case ix::WebSocketMessageType::Close:
                g_state = coop::State::Connecting;
                g_peerCount = 0;
                setLastError(
                    msg->closeInfo.reason.empty()
                        ? fmt::format("연결이 끊어졌습니다 ({})", msg->closeInfo.code)
                        : msg->closeInfo.reason
                );
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

        // 주소가 wss:// 면 IXWebSocket이 알아서 암호화 연결을 쓴다.
        // 다만 신뢰할 인증서는 우리가 알려줘야 한다.
        if (auto caPath = caBundlePath(); !caPath.empty()) {
            ix::SocketTLSOptions tlsOptions;
            tlsOptions.caFile = caPath;
            g_socket.setTLSOptions(tlsOptions);
        }

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

    std::string lastError() {
        std::lock_guard lock(g_errorMutex);
        return g_lastError;
    }

    void send(matjson::Value msg) {
        if (g_state != State::Connected) return;
        g_socket.send(msg.dump(matjson::NO_INDENTATION));
    }

}
