#include "Coop.hpp"

#include <Geode/modify/LevelEditorLayer.hpp>

#include <algorithm>
#include <cstdlib>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>

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

    // 에디터에 들어왔을 때 이미 레벨에 있던 오브젝트들.
    // 자동으로 올리지 않는다. 둘 다 같은 레벨을 열었을 때 오브젝트가 두 배가 되기 때문.
    std::unordered_set<int> g_notShared;
    bool g_needBaseline = true;

    // 레벨이 클 수 있으니 한 번에 전부 훑지 않고 조금씩 나눠서 본다.
    // 한 프레임에 몰리면 게임이 끊기기 때문.
    unsigned int g_scanIndex = 0;
    constexpr unsigned int SCAN_SLICE = 250;

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

    // GD의 오브젝트 저장 문자열은 "키,값,키,값,..." 형태다.
    // 키 1은 오브젝트 종류, 키 2는 x좌표, 키 3은 y좌표.
    std::unordered_map<std::string, std::string> parseSaveString(std::string const& text) {
        std::unordered_map<std::string, std::string> values;

        size_t start = 0;
        std::string key;
        bool expectingKey = true;

        while (start <= text.size()) {
            auto comma = text.find(',', start);
            auto piece = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);

            if (expectingKey) {
                key = std::move(piece);
            } else {
                values[key] = std::move(piece);
            }
            expectingKey = !expectingKey;

            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        return values;
    }

    // 위치(키 2, 3)만 달라졌으면 참. 그 경우 지웠다 다시 만들 필요가 없다.
    bool onlyPositionChanged(
        std::string const& before, std::string const& after, cocos2d::CCPoint& outPosition
    ) {
        auto oldValues = parseSaveString(before);
        auto newValues = parseSaveString(after);

        if (oldValues.size() != newValues.size()) return false;

        bool positionDiffers = false;
        for (auto const& [key, value] : newValues) {
            auto found = oldValues.find(key);
            if (found == oldValues.end()) return false;
            if (found->second == value) continue;

            if (key == "2" || key == "3") {
                positionDiffers = true;
            } else {
                return false;
            }
        }

        if (!positionDiffers) return false;
        if (!newValues.contains("2") || !newValues.contains("3")) return false;

        // CCPoint는 CCSize로부터의 대입 연산자도 갖고 있어서 중괄호로 넘기면
        // 어느 쪽인지 모호해진다. 형을 분명히 적어준다.
        outPosition = cocos2d::CCPoint(
            std::strtof(newValues["2"].c_str(), nullptr),
            std::strtof(newValues["3"].c_str(), nullptr)
        );
        return true;
    }

    GameObject* spawnFromSaveString(LevelEditorLayer* editor, std::string const& data) {
        auto created = editor->createObjectsFromString(data, true, true);
        if (!created || created->count() == 0) return nullptr;
        return static_cast<GameObject*>(created->objectAtIndex(0));
    }

    // 서버가 알려준 오브젝트 상태를 내 레벨에 반영한다.
    void applyState(LevelEditorLayer* editor, std::string const& uid, std::string const& data) {
        if (auto it = g_objectByUid.find(uid); it != g_objectByUid.end()) {
            auto object = it->second;
            auto previous = g_lastSaved[uid];
            if (previous == data) return;

            cocos2d::CCPoint moved;
            if (onlyPositionChanged(previous, data, moved)) {
                // 옮기기만 한 경우. 통째로 다시 만들면 상대가 끄는 동안 깜빡이고
                // 내가 그 오브젝트를 선택 중이었으면 선택이 풀린다.
                object->setPosition(moved);
                editor->updateObjectSection(object);
                g_lastSaved[uid] = data;
                return;
            }

            forget(uid);
            editor->removeObject(object, true);
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

    // 아직 모르는 오브젝트다. uid를 붙이고 서버에 알린다.
    void trackAndSend(LevelEditorLayer* editor, GameObject* object) {
        auto uid = makeUid();
        auto data = saveStringOf(object, editor);
        remember(uid, object, data);

        matjson::Value msg;
        msg["type"] = "add";
        msg["uid"] = uid;
        msg["data"] = data;
        coop::send(std::move(msg));
    }

    // 이미 아는 오브젝트다. 내용이 달라졌으면 알린다.
    void sendIfChanged(LevelEditorLayer* editor, std::string const& uid, GameObject* object) {
        auto data = saveStringOf(object, editor);
        if (g_lastSaved[uid] == data) return;

        g_lastSaved[uid] = data;

        matjson::Value msg;
        msg["type"] = "update";
        msg["uid"] = uid;
        msg["data"] = data;
        coop::send(std::move(msg));
    }

    // 이 오브젝트를 한 번 살펴본다.
    void inspect(LevelEditorLayer* editor, GameObject* object) {
        if (!object) return;

        auto known = g_uidByLocalId.find(object->m_uniqueID);
        if (known == g_uidByLocalId.end()) {
            // 에디터에 들어올 때부터 있던 것은 사용자가 직접 공유하기 전엔 올리지 않는다.
            if (g_notShared.contains(object->m_uniqueID)) return;
            trackAndSend(editor, object);
        } else {
            sendIfChanged(editor, known->second, object);
        }
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
        g_notShared.clear();
        g_scanIndex = 0;

        // 레벨 오브젝트는 아직 다 불러오지 않았을 수 있다.
        // 그래서 기준선은 여기서 잡지 않고 첫 검사 때 잡는다.
        g_needBaseline = true;

        matjson::Value msg;
        msg["type"] = "resync";
        send(std::move(msg));
    }

    void reconcile() {
        if (g_applyingRemote) return;

        auto editor = LevelEditorLayer::get();
        if (!editor || !editor->m_objects) return;

        auto objects = editor->m_objects;
        auto total = objects->count();

        // 처음 한 번은 지금 레벨에 있는 것을 "원래 있던 것"으로 기록만 하고 넘어간다.
        if (g_needBaseline) {
            for (unsigned int i = 0; i < total; ++i) {
                if (auto object = static_cast<GameObject*>(objects->objectAtIndex(i))) {
                    g_notShared.insert(object->m_uniqueID);
                }
            }
            g_needBaseline = false;
            return;
        }

        if (total == 0) {
            g_scanIndex = 0;
            return;
        }

        if (g_scanIndex >= total) g_scanIndex = 0;
        auto end = std::min(g_scanIndex + SCAN_SLICE, total);

        for (unsigned int i = g_scanIndex; i < end; ++i) {
            inspect(editor, static_cast<GameObject*>(objects->objectAtIndex(i)));
        }

        g_scanIndex = (end >= total) ? 0 : end;
    }

    void syncSelection(CCArray* selected) {
        if (g_applyingRemote || !selected || g_needBaseline) return;

        auto editor = LevelEditorLayer::get();
        if (!editor) return;

        for (unsigned int i = 0; i < selected->count(); ++i) {
            inspect(editor, static_cast<GameObject*>(selected->objectAtIndex(i)));
        }
    }

    void noticeObject(GameObject* object) {
        if (g_applyingRemote || g_needBaseline || !object) return;

        auto editor = LevelEditorLayer::get();
        if (!editor) return;

        inspect(editor, object);
    }

    void shareExistingLevel() {
        // 목록에서 빼면 다음 검사 때 새 오브젝트로 보고 서버에 올린다.
        g_notShared.clear();
    }

    int unsharedCount() {
        return static_cast<int>(g_notShared.size());
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
                applyState(editor, uid, data);
            }
            return;
        }

        auto uid = msg["uid"].asString().unwrapOr("");
        if (uid.empty()) return;

        if (type == "add" || type == "update") {
            auto data = msg["data"].asString().unwrapOr("");
            if (data.empty()) return;
            applyState(editor, uid, data);
        }
        else if (type == "remove") {
            applyRemove(editor, uid);
        }
    }

}

// 오브젝트 삭제는 종류를 가리지 않고 전부 이 함수를 거친다.
// 삭제는 주기적 검사로는 알아채기 어려워서(사라진 걸 확인하려면 매번 전체를 훑어야 한다)
// 여기서 바로 잡는다.
class $modify(CoopLevelEditorLayer, LevelEditorLayer) {
    void removeObject(GameObject* object, bool noUndo) {
        if (!coop::isApplyingRemote() && object) {
            g_notShared.erase(object->m_uniqueID);

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
