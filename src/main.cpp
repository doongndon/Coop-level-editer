#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <ixwebsocket/IXWebSocket.h>

using namespace geode::prelude;

ix::WebSocket g_socket;

std::string getServerUrl() {
    return Mod::get()->getSettingValue<std::string>("server-url");
}

std::string getRoomName() {
    return Mod::get()->getSettingValue<std::string>("room-name");
}

// 네트워크로 받은 오브젝트를 다시 서버로 보내는 걸 막기 위한 플래그
bool g_applyingRemoteEvent = false;

void sendObjectAdded(int objectId, float x, float y) {
    if (g_applyingRemoteEvent) return;

    matjson::Value msg;
    msg["type"] = "add";
    msg["objectId"] = objectId;
    msg["x"] = x;
    msg["y"] = y;

    g_socket.send(msg.dump());
}

void sendJoin() {
    matjson::Value joinMsg;
    joinMsg["type"] = "join";
    joinMsg["room"] = getRoomName();
    g_socket.send(joinMsg.dump());
}

void connectToServer() {
    g_socket.stop();
    g_socket.setUrl(getServerUrl());

    g_socket.setOnMessageCallback([](const ix::WebSocketMessagePtr& msg) {
        // 연결이 (재)성사될 때마다 방에 다시 입장 — 여기서 join을 보내야
        // 연결되기 전에 join이 유실되는 걸 막을 수 있음
        if (msg->type == ix::WebSocketMessageType::Open) {
            sendJoin();
            return;
        }

        if (msg->type != ix::WebSocketMessageType::Message) return;

        auto parsed = matjson::parse(msg->str);
        if (!parsed) return;

        auto& data = parsed.unwrap();
        auto type = data["type"].asString().unwrapOr("");

        if (type == "add") {
            int objectId = data["objectId"].asInt().unwrapOr(0);
            float x = data["x"].asDouble().unwrapOr(0);
            float y = data["y"].asDouble().unwrapOr(0);

            // 메인 스레드에서 오브젝트를 실제로 생성
            Loader::get()->queueInMainThread([objectId, x, y]() {
                auto editor = LevelEditorLayer::get();
                if (!editor || !editor->m_editorUI) return;

                g_applyingRemoteEvent = true;
                editor->m_editorUI->createObject(objectId, {x, y}, false);
                g_applyingRemoteEvent = false;
            });
        }
    });

    // 연결이 끊기면 자동으로 재연결을 시도함
    g_socket.enableAutomaticReconnection();
    g_socket.start();
}

// 에디터에서 오브젝트를 놓는 함수를 후킹해서, 놓일 때마다 서버로 전송
class $modify(CoopEditorUI, EditorUI) {
    void createObject(int objectId, cocos2d::CCPoint pos, bool p2) {
        EditorUI::createObject(objectId, pos, p2);
        sendObjectAdded(objectId, pos.x, pos.y);
    }
};

$on_mod(Loaded) {
    connectToServer();

    // 설정 화면에서 서버 주소나 방 이름을 바꾸면 자동으로 다시 연결
    listenForSettingChanges("server-url", [](std::string) {
        connectToServer();
    });
    listenForSettingChanges("room-name", [](std::string) {
        connectToServer();
    });
}
