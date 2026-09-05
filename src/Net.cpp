#include "Coop.hpp"

#include <ixwebsocket/IXWebSocket.h>

#include <filesystem>
#include <fstream>
#include <Geode/ui/Notification.hpp>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

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

    // 서버가 알려준 방 목록. 메인 스레드에서만 쓴다.
    std::vector<coop::RoomEntry> g_roomList;

    // 서버가 "너는 이 방에 있다"고 확인해준 이름. 내가 원하는 방이 아니라
    // 실제로 들어간 방만 여기 들어간다. 이 둘을 섞어 쓴 것이 지난번 혼선의 원인이었다.
    std::string g_currentRoom;

    // 서버가 알려준 자기 버전. 배포가 갱신됐는지 창에서 바로 보려고 받아둔다.
    std::string g_serverVersion;

    // 상대가 알려준 자기 상태. 두 기기를 오가지 않고 한 화면에서 보려는 것.
    std::string g_peerStats;

    // 아직 접속 전에 "방 만들기"를 누른 경우. 연결되면 들어가기 대신 만들기를 보낸다.
    std::atomic<bool> g_createOnConnect = false;

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

    // 마지막으로 있었던 방. 게임을 껐다 켜도 그 방으로 돌아가려고 기억해둔다.
    std::string savedRoom() {
        return settingString("room-name");
    }

    void rememberRoom(std::string const& room) {
        if (savedRoom() != room) {
            Mod::get()->setSettingValue<std::string>("room-name", room);
        }
    }

    void sendRaw(matjson::Value const& msg) {
        g_socket.send(msg.dump(matjson::NO_INDENTATION));
    }

    void sendHello() {
        matjson::Value msg;
        // 상대 화면에 누구 커서인지 띄우려면 이름이 필요하다.
        msg["type"] = "hello";
        msg["name"] = coop::defaultPlayerName();
        sendRaw(msg);
    }

    // 방을 만들거나 들어갈 때 쓴 열쇠. 연결이 끊겨 다시 들어갈 때도 필요하다.
    std::string g_roomPassword;

    void sendRoomAction(char const* type, std::string const& room) {
        matjson::Value msg;
        msg["type"] = type;
        msg["room"] = room;
        msg["name"] = coop::defaultPlayerName();
        msg["password"] = g_roomPassword;
        sendRaw(msg);
    }

    void onSocketMessage(ix::WebSocketMessagePtr const& msg) {
        switch (msg->type) {
            case ix::WebSocketMessageType::Open: {
                g_state = coop::State::Connected;
                setLastError("");
                sendHello();
                // 연결이 (재)성사되면 마지막에 있던 방으로 돌아가 본다.
                // 그 방이 이미 사라졌으면 서버가 거절하고, 우리는 방 밖에 남는다.
                Loader::get()->queueInMainThread([]() {
                    if (auto room = savedRoom(); !room.empty()) {
                        sendRoomAction(g_createOnConnect.exchange(false) ? "create" : "join", room);
                    }
                });
                return;
            }

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
            auto type = value["type"].asString().unwrapOr("");

            if (type == "server") {
                g_serverVersion = value["version"].asString().unwrapOr("");
                return;
            }

            if (type == "stats") {
                g_peerStats = fmt::format(
                    "{}: {}",
                    value["name"].asString().unwrapOr("?"),
                    value["text"].asString().unwrapOr("")
                );
                return;
            }

            if (type == "peers") {
                g_peerCount = static_cast<int>(value["count"].asInt().unwrapOr(0));
                return;
            }

            if (type == "rooms") {
                g_roomList.clear();
                auto list = value["list"];
                if (list.isArray()) {
                    for (auto const& entry : list) {
                        coop::RoomEntry room;
                        room.name = entry["name"].asString().unwrapOr("");
                        room.count = static_cast<int>(entry["count"].asInt().unwrapOr(0));
                        room.owner = entry["owner"].asString().unwrapOr("");
                        room.objects = static_cast<int>(entry["objects"].asInt().unwrapOr(0));
                        room.locked = entry["locked"].asInt().unwrapOr(0) != 0;
                        if (!room.name.empty()) g_roomList.push_back(std::move(room));
                    }
                }
                return;
            }

            // 어느 방에 있는지는 서버 말만 믿는다.
            if (type == "room") {
                auto name = value["name"].asString().unwrapOr("");
                // 없어진 방으로 계속 되돌아가려 하지 않도록, 결과는 늘 기억해둔다.
                rememberRoom(name);
                if (name == g_currentRoom) return;

                g_currentRoom = name;
                g_peerCount = 0;
                coop::clearCursors();
                coop::clearChat();

                // 방이 바뀌면 이전 방에서 쓰던 대응표는 전부 의미가 없다.
                // 여기서 비우지 않으면 옛 uid가 새 방에 섞여 들어가 꼬인다.
                coop::enterEditor();

                // 한 레벨에서 같이 작업하므로 어느 레벨이 원본인지 정해야 한다.
                // 방이 비어 있으면 내 레벨이 원본이 되고(host),
                // 이미 레벨이 있으면 내 것을 비우고 그것을 받는다(guest).
                auto mode = value["mode"].asString().unwrapOr("");
                std::string note;
                if (mode == "host") {
                    coop::uploadWholeLevel();
                    note = fmt::format("Sharing your level in \"{}\"", name);
                } else if (mode == "guest") {
                    // 손님은 자기 레벨을 건드리지 않는다. 임시 레벨을 새로
                    // 만들어 그리로 옮겨가고, 방 내용은 거기에 받는다.
                    coop::openWorkspace(name);
                    note = fmt::format("Loading the level from \"{}\"", name);
                } else {
                    note = "Left the room";
                }

                if (Mod::get()->getSettingValue<bool>("show-notifications")) {
                    Notification::create(note, NotificationIcon::Success)->show();
                }
                return;
            }

            if (type == "error") {
                auto reason = value["reason"].asString().unwrapOr("Something went wrong");
                setLastError(reason);
                Notification::create(reason, NotificationIcon::Error)->show();
                return;
            }

            if (type == "cursor") {
                coop::applyCursor(
                    value["from"].asString().unwrapOr(""),
                    value["name"].asString().unwrapOr(""),
                    cocos2d::CCPoint(
                        static_cast<float>(value["x"].asDouble().unwrapOr(0)),
                        static_cast<float>(value["y"].asDouble().unwrapOr(0))
                    )
                );
                return;
            }

            if (type == "sel") {
                coop::applyPeerSelection(value["uids"].asString().unwrapOr(""));
                return;
            }

            if (type == "view") {
                coop::applyPeerView(
                    static_cast<float>(value["x"].asDouble().unwrapOr(0)),
                    static_cast<float>(value["y"].asDouble().unwrapOr(0)),
                    static_cast<float>(value["z"].asDouble().unwrapOr(0))
                );
                return;
            }

            if (type == "chat") {
                coop::addChatLine(
                    value["name"].asString().unwrapOr("?"),
                    value["text"].asString().unwrapOr("")
                );
                return;
            }

            if (type == "left") {
                coop::removeCursor(value["from"].asString().unwrapOr(""));
                return;
            }

            if (type == "joined") {
                if (Mod::get()->getSettingValue<bool>("show-notifications")) {
                    auto who = value["name"].asString().unwrapOr("Someone");
                    Notification::create(
                        fmt::format("{} joined the room", who), NotificationIcon::Info
                    )->show();
                }
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
        // 서버가 방을 확인해주기 전까지는 방 밖이다.
        g_currentRoom.clear();
        g_serverVersion.clear();
        g_peerStats.clear();

        if (url.empty()) {
            g_state = State::Disconnected;
            return;
        }

        // 앞부분을 빼먹고 주소만 적는 경우가 흔해서 그대로 살려준다.
        // 호스팅은 대부분 암호화 연결이므로 wss:// 로 본다.
        if (url.find("://") == std::string::npos) {
            url = "wss://" + url;
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

    std::string serverVersion() {
        return g_serverVersion;
    }

    std::string peerStats() {
        return g_peerStats;
    }

    void send(matjson::Value msg) {
        if (g_state != State::Connected) return;
        g_socket.send(msg.dump(matjson::NO_INDENTATION));
    }

    std::string defaultPlayerName() {
        if (auto manager = GameManager::sharedState()) {
            auto name = std::string(manager->m_playerName);
            if (!name.empty()) return name;
        }
        return "Player";
    }

    std::string suggestedRoomName() {
        return defaultPlayerName() + "'s room";
    }

    std::string currentRoom() {
        return g_currentRoom;
    }

    bool inRoom() {
        return g_state == State::Connected && !g_currentRoom.empty();
    }

    // 방 만들기와 들어가기는 접속을 다시 하지 않는다. 예전에는 방을 바꿀 때마다
    // 소켓을 끊고 새로 이었는데, 그 사이에 오간 메시지가 사라져서 꼬였다.
    void setRoomPassword(std::string password) {
        g_roomPassword = std::move(password);
    }

    void createRoom(std::string room) {
        if (g_state != State::Connected) {
            rememberRoom(room);
            g_createOnConnect = true;
            connect();
            return;
        }
        sendRoomAction("create", room);
    }

    void joinRoom(std::string room) {
        if (g_state != State::Connected) {
            rememberRoom(room);
            g_createOnConnect = false;
            connect();
            return;
        }
        sendRoomAction("join", room);
    }

    void leaveRoom() {
        rememberRoom("");
        if (g_state != State::Connected) {
            g_currentRoom.clear();
            return;
        }
        matjson::Value msg;
        msg["type"] = "leave";
        sendRaw(msg);
    }

    void requestRoomList() {
        matjson::Value msg;
        msg["type"] = "rooms";
        send(std::move(msg));
    }

    std::vector<RoomEntry> roomList() {
        return g_roomList;
    }

}
