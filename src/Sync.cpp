#include "Coop.hpp"

#include <Geode/modify/LevelEditorLayer.hpp>

#include <random>
#include <string>
#include <unordered_map>

using namespace geode::prelude;

namespace {
    // 이 게임에서 만든 오브젝트에 붙일 고유 번호의 앞부분.
    // 사람마다 달라야 서로 만든 오브젝트의 uid가 겹치지 않는다.
    std::string g_clientId;
    unsigned long long g_counter = 0;

    // GD가 붙인 오브젝트 번호(내 게임 안에서만 유효) -> 우리가 붙인 uid
    std::unordered_map<int, std::string> g_uidByLocalId;
    // 우리가 붙인 uid -> 실제 오브젝트
    std::unordered_map<std::string, GameObject*> g_objectByUid;
    // 우리가 붙인 uid -> 마지막으로 서버와 맞춘 저장 문자열
    std::unordered_map<std::string, std::string> g_lastSaved;

    bool g_applyingRemote = false;

    // 서버에서 받은 내용을 적용하는 동안 참이 되는 표시.
    // 적용 중에 걸린 훅이 그 내용을 다시 서버로 보내는 걸 막는다.
    struct RemoteScope {
        RemoteScope() { g_applyingRemote = true; }
        ~RemoteScope() { g_applyingRemote = false; }
    };

    std::string makeUid() {
        if (g_clientId.empty()) {
            std::random_device rd;
            g_clientId = fmt::format("{:08x}", static_cast<unsigned int>(rd()));
        }
        return fmt::format("{}-{}", g_clientId, ++g_counter);
    }

    std::string saveStringOf(GameObject* object, LevelEditorLayer* editor) {
        return std::string(object->getSaveString(editor));
    }

    void forget(std::string const& uid) {
        if (auto it = g_objectByUid.find(uid); it != g_objectByUid.end()) {
            g_uidByLocalId.erase(it->second->m_uniqueID);
            g_objectByUid.erase(it);
        }
        g_lastSaved.erase(uid);
    }

    void remember(std::string const& uid, GameObject* object, std::string saved) {
        g_uidByLocalId[object->m_uniqueID] = uid;
        g_objectByUid[uid] = object;
        g_lastSaved[uid] = std::move(saved);
    }

    // 저장 문자열로부터 오브젝트를 다시 만든다. 실패하면 nullptr.
    GameObject* spawnFromSaveString(LevelEditorLayer* editor, std::string const& data) {
        auto created = editor->createObjectsFromString(data, true, true);
        if (!created || created->count() == 0) return nullptr;
        return static_cast<GameObject*>(created->objectAtIndex(0));
    }

    void applyAdd(LevelEditorLayer* editor, std::string const& uid, std::string const& data) {
        // 이미 갖고 있는 uid면 새로 만들지 않고 내용만 갱신한다.
        if (g_objectByUid.contains(uid)) {
            if (g_lastSaved[uid] == data) return;

            auto old = g_objectByUid[uid];
            forget(uid);
            editor->removeObject(old, true);
        }

        auto object = spawnFromSaveString(editor, data);
        if (!object) {
            log::warn("오브젝트를 만들지 못했습니다: {}", uid);
            return;
        }
        remember(uid, object, data);
    }

    void applyRemove(LevelEditorLayer* editor, std::string const& uid) {
        auto it = g_objectByUid.find(uid);
        if (it == g_objectByUid.end()) return;

        auto object = it->second;
        forget(uid);
        editor->removeObject(object, true);
    }
}

namespace coop {

    bool isApplyingRemote() {
        return g_applyingRemote;
    }

    void enterEditor() {
        // 에디터에 새로 들어오면 이전 대응표는 모두 의미가 없다.
        g_uidByLocalId.clear();
        g_objectByUid.clear();
        g_lastSaved.clear();

        matjson::Value msg;
        msg["type"] = "resync";
        send(std::move(msg));
    }

    void trackNewObject(GameObject* object) {
        if (g_applyingRemote || !object) return;

        auto editor = LevelEditorLayer::get();
        if (!editor) return;

        auto uid = makeUid();
        auto data = saveStringOf(object, editor);
        remember(uid, object, data);

        matjson::Value msg;
        msg["type"] = "add";
        msg["uid"] = uid;
        msg["data"] = data;
        send(std::move(msg));
    }

    void syncSelection(CCArray* selected) {
        if (g_applyingRemote || !selected) return;

        auto editor = LevelEditorLayer::get();
        if (!editor) return;

        for (unsigned int i = 0; i < selected->count(); ++i) {
            auto object = static_cast<GameObject*>(selected->objectAtIndex(i));
            if (!object) continue;

            auto found = g_uidByLocalId.find(object->m_uniqueID);
            if (found == g_uidByLocalId.end()) continue;

            auto const& uid = found->second;
            auto data = saveStringOf(object, editor);
            if (g_lastSaved[uid] == data) continue;

            g_lastSaved[uid] = data;

            matjson::Value msg;
            msg["type"] = "update";
            msg["uid"] = uid;
            msg["data"] = data;
            send(std::move(msg));
        }
    }

    void handleMessage(matjson::Value const& msg) {
        auto editor = LevelEditorLayer::get();
        if (!editor) return;

        auto type = msg["type"].asString().unwrapOr("");
        RemoteScope scope;

        if (type == "state") {
            // 방에 들어왔을 때 서버가 지금까지 쌓인 오브젝트를 한 번에 보내준다.
            auto objects = msg["objects"];
            if (!objects.isArray()) return;

            for (auto const& entry : objects) {
                auto uid = entry["uid"].asString().unwrapOr("");
                auto data = entry["data"].asString().unwrapOr("");
                if (uid.empty() || data.empty()) continue;
                applyAdd(editor, uid, data);
            }
            return;
        }

        auto uid = msg["uid"].asString().unwrapOr("");
        if (uid.empty()) return;

        if (type == "add" || type == "update") {
            auto data = msg["data"].asString().unwrapOr("");
            if (data.empty()) return;
            applyAdd(editor, uid, data);
        }
        else if (type == "remove") {
            applyRemove(editor, uid);
        }
    }

}

// 오브젝트 삭제는 종류를 가리지 않고 전부 이 함수를 거친다.
class $modify(CoopLevelEditorLayer, LevelEditorLayer) {
    void removeObject(GameObject* object, bool noUndo) {
        if (!coop::isApplyingRemote() && object) {
            if (auto it = g_uidByLocalId.find(object->m_uniqueID); it != g_uidByLocalId.end()) {
                auto uid = it->second;
                forget(uid);

                matjson::Value msg;
                msg["type"] = "remove";
                msg["uid"] = uid;
                coop::send(std::move(msg));
            }
        }

        LevelEditorLayer::removeObject(object, noUndo);
    }
};
