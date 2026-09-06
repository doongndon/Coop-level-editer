#include "Coop.hpp"

#include <Geode/modify/LevelEditorLayer.hpp>

#include <algorithm>
#include <cstdlib>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace geode::prelude;

namespace {
    // 이 게임에서 만든 오브젝트에 붙일 고유 번호의 앞부분.
    // 사람마다 달라야 서로 만든 오브젝트의 uid가 겹치지 않는다.
    std::string g_clientId;
    unsigned long long g_counter = 0;

    // GD가 붙인 오브젝트 번호(내 게임 안에서만 유효) -> 우리가 붙인 uid
    std::unordered_map<int, std::string> g_uidByLocalId;
    // 우리가 붙인 uid -> 실제 오브젝트
    //
    // 날 포인터로 들고 있으면 게임이 오브젝트를 먼저 없앤 뒤 우리가 그걸 건드릴 때
    // 크래시가 난다. GD의 오브젝트 번호는 삭제 후 재사용될 수도 있어서 대응표가
    // 어긋날 여지도 있다. Ref로 들고 있으면 우리가 놓을 때까지 살아 있어 그런 일이 없다.
    std::unordered_map<std::string, Ref<GameObject>> g_objectByUid;
    // 우리가 붙인 uid -> 마지막으로 서버와 맞춘 저장 문자열
    std::unordered_map<std::string, std::string> g_lastSaved;

    // 방에 들어가 있는 동안만 참. 방 밖에서는 아무것도 보내지 않는다.
    //
    // 예전에는 "원래 있던 것"과 "새로 놓은 것"을 갈라서, 원래 있던 것은 사용자가
    // 따로 올리게 했다. 양쪽이 각자 자기 레벨을 열고 있어서 그냥 올리면 물건이
    // 두 배가 됐기 때문이다. 이제는 한 레벨에서 같이 작업하므로 그 구분이 없다.
    // 방장은 자기 레벨을 통째로 올리고, 들어온 사람은 자기 레벨을 비우고 받는다.
    bool g_active = false;

    // 마지막으로 방과 맞춘 레벨 설정 문자열. 달라졌을 때만 보내기 위한 것.
    std::string g_lastSettings;
    // 마지막으로 상대에게 알린 내 상태. 바뀌었을 때만 보낸다.
    std::string g_lastStats;
    // 마지막으로 알린 내 선택. 바뀌었을 때만 보낸다.
    std::string g_lastSelection;

    // 아직 만들지 않은, 방에서 받은 오브젝트들.
    //
    // 3만 개짜리 레벨을 받을 때 한 번에 다 만들면 그 프레임에서 게임이 멈춘다.
    // 받아만 두고 매 검사마다 조금씩 만든다.
    std::vector<std::pair<std::string, std::string>> g_incoming;

    // 이 방의 원본을 내가 들고 있는지. 방장이면 참.
    bool g_hosting = false;

    // 방금 상대 것을 반영한 오브젝트들. 잠깐 조용히 둔다.
    //
    // 양쪽이 같은 오브젝트를 두고 서로 다른 답을 우기면 끝나지 않는다.
    // A가 보낸 것을 B가 반영하고, B가 자기 것을 도로 보내고, A가 다시
    // 되돌리고... 화면에서는 물체가 계속 튀고 색깔이 뒤집힌다.
    //
    // 왜 어긋나는지는 경우마다 다르다(게임이 저장 글자를 다듬거나, 두 사람이
    // 거의 같은 순간에 만졌거나). 그래서 원인마다 막는 대신, 방금 상대 것을
    // 받은 오브젝트는 잠깐 내 쪽에서 아무 말도 하지 않게 한다.
    //
    // 말다툼에서 한쪽이 잠깐 입을 다물면 그걸로 끝난다. 둘 다 계속 말하면
    // 영원히 안 끝난다.
    std::unordered_map<std::string, unsigned int> g_quiet;
    // 0.25초짜리 검사 8번 = 2초.
    constexpr unsigned int QUIET_TICKS = 8;

    void hushObject(std::string const& uid) {
        g_quiet[uid] = QUIET_TICKS;
    }

    bool isHushed(std::string const& uid) {
        auto found = g_quiet.find(uid);
        return found != g_quiet.end() && found->second > 0;
    }

    // 설정(배경, 바닥, 색깔)도 같은 말다툼에 빠진다.
    //
    // 색을 반영하면 게임이 값을 자기 방식대로 다시 계산한다. 그러면 방금 받은
    // 내용과 내 레벨의 실제 내용이 미세하게 달라지고, 우리는 그걸 "내가 바꿨다"로
    // 읽어 도로 보낸다. 상대도 똑같이 한다. 색이 계속 서로 덮인다.
    unsigned int g_settingsQuiet = 0;

    // 사용자가 실제로 설정을 바꿨는지.
    //
    // 예전에는 "설정 문자열이 달라졌으면 보낸다"였다. 그런데 상대 것을 반영하면
    // 게임이 값을 자기 방식대로 다시 계산해서 내용이 미세하게 달라진다. 그것도
    // "달라졌다"로 읽혀서 도로 보내고, 상대도 똑같이 하고, 끝나지 않았다.
    //
    // 이제는 게임이 "설정 창이 닫혔다"고 알려줄 때만 참이 된다. 그 신호는
    // 사람이 실제로 무언가를 바꿨을 때만 온다. 값을 견주어 짐작하는 대신
    // 게임에게 직접 물어보는 셈이다.
    bool g_settingsDirty = false;

    void tickQuiet() {
        if (g_settingsQuiet > 0) --g_settingsQuiet;

        for (auto it = g_quiet.begin(); it != g_quiet.end();) {
            if (it->second <= 1) it = g_quiet.erase(it);
            else { --it->second; ++it; }
        }
    }
    // 서버가 알려준 "아직 더 올 개수". 진행 상황 표시에만 쓴다.
    int g_stillComing = 0;
    constexpr std::size_t APPLY_BATCH = 400;

    // 올릴 것을 모아 한 번에 보낸다. 하나씩 보내면 오브젝트 수만큼 메시지가 생긴다.
    matjson::Value g_outgoing = matjson::Value::array();
    constexpr std::size_t SEND_BATCH = 200;
    unsigned int g_statsTick = 0;
    constexpr unsigned int STATS_EVERY = 8;   // 0.25초 x 8 = 2초

    // 방금 만들어진 오브젝트들.
    //
    // 게임이 오브젝트를 만드는 도중에 그 내용을 읽으면 아직 준비가 덜 된 상태를
    // 건드리게 된다. 하나씩 놓을 때는 티가 안 나지만 빠르게 연속으로 놓으면
    // 크래시가 난다. 그래서 훅에서는 목록에만 담아두고, 게임 일이 끝난 뒤
    // 다음 검사 때 실제로 읽는다.
    std::vector<Ref<GameObject>> g_pending;

    // 레벨이 클 수 있으니 한 번에 전부 훑지 않고 조금씩 나눠서 본다.
    // 한 프레임에 몰리면 게임이 끊기기 때문.
    unsigned int g_scanIndex = 0;
    // 평소에는 조금씩만 훑는다. 한 프레임에 몰리면 게임이 끊긴다.
    constexpr unsigned int SCAN_SLICE = 60;
    // 레벨을 통째로 올리는 중에는 크게 훑는다. 3만 개짜리를 60개씩 올리면
    // 2분이 넘게 걸린다. 이건 처음 한 번뿐이라 잠깐 무거운 편이 낫다.
    constexpr unsigned int UPLOAD_SLICE = 500;

    // 테스트 플레이 중인지. 그동안은 게임이 오브젝트를 마음대로 옮기고 떼어내서
    // 무엇이 지워졌는지 판단할 수 없다.
    bool isPlaytesting(LevelEditorLayer* editor) {
        return editor && editor->m_playbackMode != PlaybackMode::Not;
    }

    // 삭제 확인은 매 틱 할 필요가 없어서 몇 번에 한 번만 한다.
    unsigned int g_sweepTick = 0;
    constexpr unsigned int SWEEP_EVERY = 4;

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

    // 레벨에 실제로 들어 있는 오브젝트들.
    //
    // 예전에는 "부모가 있으면 레벨에 있는 것"으로 봤는데, 그게 큰 사고를 냈다.
    // 게임은 화면 밖으로 나간 오브젝트를 화면 구성에서 잠시 떼어낸다. 부모가
    // 없어지지만 레벨에서 지워진 것은 아니다. 그걸 삭제로 착각해서, 상대
    // 화면에서 안 보이게 된 물건을 진짜로 지워버렸다.
    //
    // 레벨의 진짜 목록은 m_objects다. 화면에 보이든 말든 여기에는 다 들어 있다.
    // 매번 훑기엔 무거워서 목록을 따로 들고 있다가 주기적으로 다시 맞춘다.
    std::unordered_set<GameObject*> g_present;

    void refreshPresent(LevelEditorLayer* editor) {
        g_present.clear();
        if (!editor || !editor->m_objects) return;

        auto total = editor->m_objects->count();
        g_present.reserve(total);
        for (unsigned int i = 0; i < total; ++i) {
            g_present.insert(static_cast<GameObject*>(editor->m_objects->objectAtIndex(i)));
        }
    }

    // 목록이 레벨과 어긋났는지 값싸게 확인한다.
    //
    // 우리가 만들거나 지울 때마다 목록을 같이 고치고 있어서 보통은 맞다.
    // 우리가 못 본 경로로 바뀌었을 때만 개수가 어긋나는데, 개수 비교는
    // 공짜라서 매번 해도 부담이 없다.
    void ensurePresent(LevelEditorLayer* editor) {
        if (!editor || !editor->m_objects) return;
        if (g_present.size() != editor->m_objects->count()) {
            refreshPresent(editor);
        }
    }

    bool isInLevel(GameObject* object) {
        return object && g_present.contains(object);
    }

    // 텔레포트 포탈은 짝을 생 포인터(m_orangePortal) 하나로 들고 있다.
    //
    // 한쪽을 지우면 남은 쪽은 사라진 자리를 계속 가리킨다. 게임은 매 프레임
    // 포탈 위치를 맞추면서 그 포인터를 따라가므로, 다음 프레임에 죽는다.
    // 지우기 전에 서로 손을 놓게 한다.
    //
    // 손잡고 걷다가 한 사람만 사라지면, 남은 사람은 빈 손을 계속 잡고 있는
    // 셈이다. 사라지기 전에 손을 놓아야 한다.
    void unlinkPortal(GameObject* object) {
        auto portal = typeinfo_cast<TeleportPortalObject*>(object);
        if (!portal) return;

        if (auto other = portal->m_orangePortal) {
            // 이미 못 쓰는 포인터일 수 있다. 레벨에 살아 있는 것만 건드린다.
            // 목록 확인은 주소만 견주므로 죽은 포인터라도 안전하다.
            if (g_present.contains(static_cast<GameObject*>(other))
                && other->m_orangePortal == portal) {
                other->m_orangePortal = nullptr;
            }
        }
        portal->m_orangePortal = nullptr;
    }

    // 지우기 직전에 해둘 일.
    //
    // 게임 여기저기가 오브젝트를 생 포인터로 붙들고 있다. 우리가 지우면 그
    // 손들이 전부 사라진 자리를 가리키게 된다. 지우기 전에 하나씩 놓게 한다.
    void letGoOf(LevelEditorLayer* editor, GameObject* object) {
        if (!object) return;

        // 내가 잡고 있는 것을 상대가 지울 수 있다. EditorUI도 선택한 것을
        // 생 포인터로 들고 있어서, 그대로 지우면 다음에 만질 때 죽는다.
        if (editor && editor->m_editorUI) {
            editor->m_editorUI->deselectObject(object);
        }

        unlinkPortal(object);
    }

    // 방에서 받은 것을 다 만든 뒤 한 번 훑는다.
    //
    // 큰 레벨은 나눠서 만들기 때문에 짝이 다른 묶음에 들어갈 수 있다. 만드는
    // 시점에 상대가 아직 없으면, 남는 포인터가 어디를 가리킬지 알 수 없다.
    // 레벨에 없는 것을 가리키고 있으면 그냥 끊는다. 포탈이 안 이어지는 것은
    // 고칠 수 있지만, 죽은 자리를 짚는 것은 못 고친다.
    void unlinkStrayPortals(LevelEditorLayer* editor) {
        if (!editor || !editor->m_objects) return;
        refreshPresent(editor);

        auto total = editor->m_objects->count();
        for (unsigned int i = 0; i < total; ++i) {
            auto portal = typeinfo_cast<TeleportPortalObject*>(
                static_cast<GameObject*>(editor->m_objects->objectAtIndex(i))
            );
            if (!portal) continue;

            if (auto other = portal->m_orangePortal;
                other && !g_present.contains(static_cast<GameObject*>(other))) {
                portal->m_orangePortal = nullptr;
            }
        }
    }

    void forget(std::string const& uid) {
        if (auto it = g_objectByUid.find(uid); it != g_objectByUid.end()) {
            if (auto object = it->second.data()) {
                g_uidByLocalId.erase(object->m_uniqueID);
            }
            g_objectByUid.erase(it);
        }
        g_lastSaved.erase(uid);
    }

    // 이 오브젝트를 uid와 짝지어 기억한다.
    //
    // "마지막으로 아는 모습"은 상대가 보낸 글자가 아니라 게임이 실제로 갖게 된
    // 글자를 적어야 한다. 둘이 조금이라도 다르면, 다음 검사에서 "누가 고쳤네"로
    // 보고 그 차이를 상대에게 도로 보낸다. 상대도 똑같이 하면 둘이 서로에게
    // 계속 되돌려 보내며 물체가 왔다 갔다 한다.
    void remember(
        std::string const& uid, GameObject* object, LevelEditorLayer* editor
    ) {
        g_uidByLocalId[object->m_uniqueID] = uid;
        g_objectByUid[uid] = object;
        g_lastSaved[uid] = saveStringOf(object, editor);
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

        auto first = static_cast<GameObject*>(created->objectAtIndex(0));
        g_present.insert(first);

        // 하나를 만들라고 했는데 여럿이 나오면, 나머지는 짝을 지어줄 uid가 없다.
        // 그대로 두면 레벨에만 남아 있다가 다음 검사에서 "처음 보는 것"으로
        // 보여 방에 새로 올라가고, 결국 물건이 불어난다. 첫 개만 남긴다.
        for (unsigned int i = 1; i < created->count(); ++i) {
            auto extra = static_cast<GameObject*>(created->objectAtIndex(i));
            if (!extra) continue;
            g_present.insert(extra);
            letGoOf(editor, extra);
            editor->removeObject(extra, true);
        }
        return first;
    }

    // 서버가 알려준 오브젝트 상태를 내 레벨에 반영한다.
    void applyState(LevelEditorLayer* editor, std::string const& uid, std::string const& data) {
        if (auto it = g_objectByUid.find(uid); it != g_objectByUid.end()) {
            // forget()이 대응표에서 지우면 우리가 들고 있던 참조도 사라진다.
            // 그 전에 여기서 따로 붙잡아두어야 아래에서 안전하게 지울 수 있다.
            Ref<GameObject> object = it->second;
            auto previous = g_lastSaved[uid];
            if (previous == data) return;

            cocos2d::CCPoint moved;
            if (isInLevel(object) && onlyPositionChanged(previous, data, moved)) {
                // 옮기기만 한 경우. 통째로 다시 만들면 상대가 끄는 동안 깜빡이고
                // 내가 그 오브젝트를 선택 중이었으면 선택이 풀린다.
                // 겉보기 자리만 옮기면 모자란다.
                //
                // 오브젝트는 자기 자리를 m_startPosition에도 따로 들고 있고,
                // 저장할 때 쓰는 값은 그쪽이다. setPosition만 부르면 화면에서는
                // 옮겨졌는데 "기록상"으로는 제자리다. 그러면 다음 검사에서
                // 옛 자리를 다시 상대에게 보내고, 상대는 그걸 받아 되돌리고,
                // 서로 되돌려 보내며 물체가 그 거리만큼 계속 튄다.
                //
                // 이삿짐을 옮겨놓고 주소 변경 신고를 안 한 것과 같다.
                // 우편은 계속 옛 집으로 간다.
                object->setPosition(moved);
                object->setStartPos(moved);
                editor->updateObjectSection(object);

                // 넣으려던 값이 아니라 게임이 실제로 갖게 된 값을 적어둔다.
                g_lastSaved[uid] = saveStringOf(object, editor);
                hushObject(uid);
                return;
            }

            forget(uid);

            // 옛것을 못 지운 채로 새것을 만들면 물건이 두 배가 된다.
            //
            // "레벨에 있는지"는 따로 들고 있는 목록으로 판단하는데, 그 목록이
            // 잠깐 어긋날 수 있다. 어긋난 김에 "이미 없네" 하고 넘어가면
            // 옛것은 그대로 남고 새것이 하나 더 생긴다. 그게 쌓이면 양쪽
            // 오브젝트 수가 벌어진다.
            //
            // 그래서 없다고 나오면 진짜 목록을 한 번 더 확인한다. 어긋나는
            // 경우에만 다시 읽으므로 평소에는 값이 들지 않는다.
            if (!isInLevel(object)) refreshPresent(editor);

            if (isInLevel(object)) {
                letGoOf(editor, object);
                editor->removeObject(object, true);
            }
        }

        auto object = spawnFromSaveString(editor, data);
        if (!object) {
            log::warn("오브젝트를 만들지 못했습니다: {}", uid);
            return;
        }
        remember(uid, object, editor);
        hushObject(uid);
    }

    void applyRemove(LevelEditorLayer* editor, std::string const& uid) {
        g_quiet.erase(uid);

        auto it = g_objectByUid.find(uid);
        if (it == g_objectByUid.end()) return;

        // 지우는 동안 살아 있도록 먼저 붙잡아둔다.
        Ref<GameObject> object = it->second;
        forget(uid);
        if (isInLevel(object)) {
            letGoOf(editor, object);
            editor->removeObject(object, true);
        }
    }

    // 모아둔 것을 실제로 내보낸다.
    void flushOutgoing() {
        if (g_outgoing.size() == 0) return;

        matjson::Value msg;
        msg["type"] = "addMany";
        msg["items"] = std::move(g_outgoing);
        g_outgoing = matjson::Value::array();
        coop::send(std::move(msg));
    }

    // 아직 모르는 오브젝트다. uid를 붙이고 보낼 목록에 담는다.
    void trackAndSend(LevelEditorLayer* editor, GameObject* object) {
        auto uid = makeUid();
        auto data = saveStringOf(object, editor);
        remember(uid, object, editor);

        matjson::Value item;
        item["uid"] = uid;
        item["data"] = data;
        g_outgoing.push(std::move(item));

        if (g_outgoing.size() >= SEND_BATCH) flushOutgoing();
    }

    // 이미 아는 오브젝트다. 내용이 달라졌으면 알린다.
    void sendIfChanged(LevelEditorLayer* editor, std::string const& uid, GameObject* object) {
        auto data = saveStringOf(object, editor);
        if (g_lastSaved[uid] == data) return;

        // 방금 상대 것을 받은 오브젝트는 잠깐 내버려둔다. 지금 보이는 차이는
        // 내가 고쳐서 생긴 것이 아니라, 받은 내용과 게임이 실제로 만든 모습이
        // 조금 다른 것일 뿐일 때가 많다. 그걸 "내가 고쳤다"고 보내면 상대가
        // 되돌리고, 나도 되돌리고, 끝나지 않는다.
        if (isHushed(uid)) {
            g_lastSaved[uid] = data;
            return;
        }

        g_lastSaved[uid] = data;

        matjson::Value msg;
        msg["type"] = "update";
        msg["uid"] = uid;
        msg["data"] = data;
        coop::send(std::move(msg));
    }

    // 이 오브젝트를 한 번 살펴본다.
    void inspect(LevelEditorLayer* editor, GameObject* object) {
        // 이미 레벨에서 떨어져 나간 오브젝트는 읽지 않는다.
        if (!isInLevel(object)) return;

        auto known = g_uidByLocalId.find(object->m_uniqueID);
        if (known == g_uidByLocalId.end()) {
            trackAndSend(editor, object);
        } else {
            sendIfChanged(editor, known->second, object);
        }
    }

    // 방에서 받아둔 것을 조금씩 만든다.
    //
    // 여러 개를 ";"로 이어 한 번에 만든다. GD의 저장 형식이 원래 그렇게
    // 여러 개를 담는 형식이라, 한 개씩 부르는 것보다 훨씬 빠르다.
    // 만들어진 순서는 넣은 순서와 같아서 uid를 차례로 붙일 수 있다.
    void applyIncoming(LevelEditorLayer* editor) {
        if (g_incoming.empty()) return;

        RemoteScope scope;

        auto count = std::min(APPLY_BATCH, g_incoming.size());
        std::string joined;
        std::vector<std::size_t> fresh;

        for (std::size_t i = 0; i < count; ++i) {
            auto const& [uid, data] = g_incoming[i];
            if (data.empty()) continue;

            // 이미 아는 것은 고쳐야 할 수도 있으니 따로 처리한다.
            if (g_objectByUid.contains(uid)) {
                applyState(editor, uid, data);
                continue;
            }

            if (!joined.empty()) joined += ';';
            joined += data;
            fresh.push_back(i);
        }

        if (!fresh.empty()) {
            auto created = editor->createObjectsFromString(joined, true, true);
            auto made = created ? created->count() : 0u;

            if (made == fresh.size()) {
                for (std::size_t k = 0; k < fresh.size(); ++k) {
                    auto object = static_cast<GameObject*>(created->objectAtIndex(k));
                    if (!object) continue;
                    g_present.insert(object);
                    auto const& [uid, data] = g_incoming[fresh[k]];
                    remember(uid, object, editor);
                    hushObject(uid);
                }
            } else {
                // 개수가 어긋나면 어느 것이 어느 uid인지 알 수 없다.
                // 그럴 때는 하나씩 다시 만든다. 느리지만 어긋나는 것보다 낫다.
                log::warn("묶음 생성이 어긋났습니다 ({} 요청, {} 생성). 하나씩 다시 만듭니다",
                          fresh.size(), made);

                // 다시 만들기 전에, 묶음으로 만들어진 것들을 먼저 치운다.
                //
                // 이걸 안 해서 물건이 몇 배로 불어나고 있었다. 묶음이 어긋나면
                // 만들어진 것들은 uid를 못 받은 채 레벨에 그대로 남는데, 곧바로
                // 아래에서 같은 내용을 또 만든다. 한 번 어긋날 때마다 그만큼씩
                // 쌓인다. 실제로 한쪽은 110개, 다른 쪽은 330개가 됐다.
                //
                // 짝을 못 지은 물건은 다음 검사에서 "처음 보는 것"으로 보여
                // 방에 새로 올라가기까지 한다. 반드시 치우고 시작해야 한다.
                if (created) {
                    for (unsigned int k = 0; k < created->count(); ++k) {
                        auto object = static_cast<GameObject*>(created->objectAtIndex(k));
                        if (!object) continue;
                        g_present.insert(object);
                        letGoOf(editor, object);
                        editor->removeObject(object, true);
                    }
                }

                for (auto index : fresh) {
                    auto const& [uid, data] = g_incoming[index];
                    applyState(editor, uid, data);
                }
            }
        }

        g_incoming.erase(g_incoming.begin(), g_incoming.begin() + count);

        // 다 받아 만들었으면 짝 잃은 포탈을 정리한다. 나눠 만드는 동안에는
        // 아직 안 온 짝을 끊어버릴 수 있으므로 끝난 뒤에만 한다.
        if (g_incoming.empty()) unlinkStrayPortals(editor);
    }

    // 사라진 오브젝트를 찾아 상대에게 알린다.
    //
    // 삭제를 removeObject 훅 하나로만 잡으면, 게임이 다른 경로로 지울 때
    // (한꺼번에 지우기, 되돌리기, 다른 모드가 지우는 경우) 놓친다.
    // 그래서 우리가 아는 오브젝트가 아직 레벨에 붙어 있는지 직접 확인한다.
    // 결과만 보는 방식이라 어떤 경로로 지워졌든 빠짐없이 잡힌다.
    void sweepRemoved() {
        // 판단하기 직전에 레벨의 진짜 목록을 다시 읽는다.
        // 오래된 목록으로 지우면 멀쩡한 물건을 지우게 된다.
        refreshPresent(LevelEditorLayer::get());

        std::vector<std::string> gone;
        for (auto const& [uid, held] : g_objectByUid) {
            if (!isInLevel(held.data())) gone.push_back(uid);
        }
        if (gone.empty()) return;

        // 에디터를 닫는 중이면 오브젝트가 한꺼번에 떨어져 나간다.
        // 그걸 삭제로 착각해 방의 레벨을 통째로 지워버리면 안 된다.
        if (gone.size() > 5 && gone.size() == g_objectByUid.size()) {
            for (auto const& uid : gone) forget(uid);
            return;
        }

        for (auto const& uid : gone) {
            forget(uid);

            matjson::Value msg;
            msg["type"] = "remove";
            msg["uid"] = uid;
            coop::send(std::move(msg));
        }
    }
}

namespace coop {

    bool isApplyingRemote() {
        return g_applyingRemote;
    }

    void enterEditor() {
        // 방이 바뀌었거나 에디터를 새로 열었다. 이전 대응표는 모두 의미가 없다.
        g_uidByLocalId.clear();
        g_objectByUid.clear();
        g_lastSaved.clear();
        g_quiet.clear();
        g_pending.clear();
        g_scanIndex = 0;
        g_sweepTick = 0;
        g_active = false;
        g_lastSettings.clear();
        g_lastStats.clear();
        g_lastSelection.clear();
        g_incoming.clear();
        g_stillComing = 0;
        g_outgoing = matjson::Value::array();
        forgetAppliedSettings();

        matjson::Value msg;
        msg["type"] = "resync";
        send(std::move(msg));
    }

    // 방을 만들려면 올릴 레벨이 열려 있어야 한다.
    // 우리가 uid를 붙여 아는 오브젝트 수.
    //
    // 레벨에 있는 수와 이 수가 다르면 "짝을 못 지은 오브젝트"가 있다는 뜻이다.
    // 그런 것은 다음 검사에서 "처음 보는 물건"으로 보여 방에 새로 올라가고,
    // 그러면 같은 물건이 두 개가 된다. 양쪽 오브젝트 수가 벌어지는 원인을
    // 가리려면 이 수가 필요하다.
    int trackedCount() {
        return static_cast<int>(g_objectByUid.size());
    }

    bool canHost() {
        return LevelEditorLayer::get() != nullptr;
    }

    // 이 방에서 내가 원본을 들고 있는 쪽인지.
    void uploadWholeLevel() {
        g_hosting = true;
        // 방을 열 때는 내 레벨의 설정을 한 번 올려야 한다.
        g_settingsDirty = true;
        // 대응표가 비어 있으면 다음 검사에서 레벨의 모든 오브젝트가
        // "처음 보는 것"이 되어 차례로 방에 올라간다.
        g_uidByLocalId.clear();
        g_objectByUid.clear();
        g_lastSaved.clear();
        g_scanIndex = 0;
        g_active = true;

        // 설정도 함께 올린다. 배경과 색깔은 여기에 들어 있다.
        g_lastSettings.clear();
        syncLevelSettings();
    }

    void clearLevel() {
        // 내 레벨을 비우고 방 것을 받는다. 나는 손님이다.
        g_hosting = false;

        auto editor = LevelEditorLayer::get();
        if (!editor || !editor->m_objects) return;

        // 우리가 지우는 것이므로 이 삭제를 다시 방에 알리면 안 된다.
        RemoteScope scope;

        // 아래에서 "아직 레벨에 있는지"로 판단하므로 목록을 먼저 맞춘다.
        refreshPresent(editor);

        // 지우는 동안 목록 자체가 줄어들기 때문에 먼저 따로 옮겨 담는다.
        std::vector<Ref<GameObject>> all;
        all.reserve(editor->m_objects->count());
        for (unsigned int i = 0; i < editor->m_objects->count(); ++i) {
            all.push_back(static_cast<GameObject*>(editor->m_objects->objectAtIndex(i)));
        }

        // 포탈끼리 서로를 가리키고 있다. 하나씩 지우다 보면 남은 쪽이 이미
        // 사라진 자리를 가리키게 되므로, 지우기 전에 전부 손을 놓게 한다.
        for (auto& held : all) {
            letGoOf(editor, held.data());
        }

        for (auto& held : all) {
            // 화면 밖으로 나간 오브젝트는 게임이 화면에서 떼어놓는다.
            // 그래서 "붙어 있는지"로 판단하면 안 보이는 것들만 안 지워지고
            // 남아서, 방의 레벨에 내 옛 레벨 조각이 섞인다.
            if (auto object = held.data(); object && isInLevel(object)) {
                editor->removeObject(object, true);
            }
        }

        g_uidByLocalId.clear();
        g_objectByUid.clear();
        g_lastSaved.clear();
        g_scanIndex = 0;
        g_active = true;
    }

    // 내 상태를 상대에게 알린다.
    //
    // 두 기기를 오가며 확인하는 것이 번거로워서 만들었다. 한쪽 화면만 봐도
    // 상대가 몇 개를 갖고 있는지, 커서가 오가는지 알 수 있다.
    void sendStats() {
        if (!inRoom()) return;

        auto text = diagnostics();
        if (text == g_lastStats) return;
        g_lastStats = text;

        matjson::Value msg;
        msg["type"] = "stats";
        msg["text"] = text;
        send(std::move(msg));
    }

    void hushSettings() {
        g_settingsQuiet = QUIET_TICKS;
    }

    // 상대 것을 다 반영한 뒤에 부른다.
    //
    // 반영하는 동안 우리도 levelSettingsUpdated()를 부르게 되는데, 그것까지
    // "사람이 바꿨다"로 남으면 방금 받은 것을 도로 보내게 된다. 반영이 끝난
    // 다음에 지워야 해서 hushSettings()와 따로 둔다.
    void clearSettingsDirty() {
        g_settingsDirty = false;
    }

    void syncLevelSettings() {
        if (!inRoom() || g_applyingRemote) return;

        // 방금 상대 설정을 반영했으면 잠깐 아무 말도 하지 않는다.
        // 지금 보이는 차이는 내가 바꾼 것이 아니라, 게임이 값을 다시 계산해서
        // 생긴 것일 뿐이다. 그걸 보내면 색이 서로 덮이며 끝나지 않는다.
        if (g_settingsQuiet > 0) {
            g_lastSettings = levelSettingsString();
            return;
        }

        // 사람이 실제로 바꿨을 때만 보낸다. 방장이든 손님이든 마찬가지다.
        //
        // 손님은 방의 설정을 받기 전까지는 아직 자기 레벨이 빈 상태라, 그것을
        // 올리면 방장의 색이 흰 도화지로 덮인다. 그 전까지만 막는다.
        if (!g_settingsDirty) return;
        if (!g_hosting && !roomSettingsApplied()) return;

        auto text = levelSettingsString();
        if (text.empty() || text == g_lastSettings) return;
        g_lastSettings = text;

        g_settingsDirty = false;

        matjson::Value msg;
        msg["type"] = "settings";
        msg["data"] = text;
        msg["song"] = levelSongID();
        msg["track"] = levelAudioTrack();
        msg["songs"] = levelSongList();
        send(std::move(msg));
    }

    void reconcile() {
        if (g_applyingRemote) return;

        // 방 밖에서는 아무것도 보내지 않는다. 보내지도 않으면서 "보냈다"고
        // 기록해두면, 나중에 방에 들어갔을 때 그 오브젝트들이 영영 전달되지 않는다.
        if (!inRoom() || !g_active) {
            g_pending.clear();
            return;
        }

        auto editor = LevelEditorLayer::get();
        if (!editor || !editor->m_objects) return;

        ensurePresent(editor);

        // 방에서 받아둔 것을 먼저 조금 만든다. 다 만들기 전에는 내 것을
        // 올리지 않는다. 아직 안 만든 것을 "새 오브젝트"로 착각해 방에
        // 도로 올려버리면 오브젝트가 두 배가 된다.
        if (!g_incoming.empty()) {
            applyIncoming(editor);
            return;
        }

        // 훅에서 담아둔 새 오브젝트를 게임 일이 끝난 지금 처리한다.
        if (!g_pending.empty()) {
            auto pending = std::move(g_pending);
            g_pending.clear();
            for (auto& held : pending) {
                inspect(editor, held.data());
            }
        }

        // 테스트 플레이 중에는 아무것도 보내지 않는다. 그동안 게임이
        // 오브젝트를 옮기고 떼어내기 때문에, 그걸 편집으로 오해하면
        // 상대 레벨이 엉망이 된다.
        if (isPlaytesting(editor)) return;

        tickQuiet();

        // 사라진 것을 먼저 확인한다. 삭제는 상대 화면에 남아 있으면
        // 그 위에 계속 덧그리게 되어 가장 헷갈리는 어긋남이 된다.
        if (++g_sweepTick >= SWEEP_EVERY) {
            g_sweepTick = 0;
            sweepRemoved();
        }

        // 배경, 바닥, 색깔, 노래가 달라졌으면 알린다.
        syncLevelSettings();

        // 내 상태를 가끔 알린다. 상대 화면에서 내 쪽이 잘 도는지 보이도록.
        if (++g_statsTick >= STATS_EVERY) {
            g_statsTick = 0;
            sendStats();
        }

        auto objects = editor->m_objects;
        auto total = objects->count();

        if (total == 0) {
            g_scanIndex = 0;
            return;
        }

        if (g_scanIndex >= total) g_scanIndex = 0;

        // 아직 방에 올리지 않은 것이 많이 남았으면 크게 훑는다.
        auto known = static_cast<unsigned int>(g_uidByLocalId.size());
        auto slice = (known + SCAN_SLICE < total) ? UPLOAD_SLICE : SCAN_SLICE;
        auto end = std::min(g_scanIndex + slice, total);

        for (unsigned int i = g_scanIndex; i < end; ++i) {
            inspect(editor, static_cast<GameObject*>(objects->objectAtIndex(i)));
        }

        g_scanIndex = (end >= total) ? 0 : end;

        // 이번 검사에서 모은 것을 내보낸다.
        flushOutgoing();
    }

    // 아직 만들지 못하고 쌓여 있는 개수. 창에 진행 상황으로 띄운다.
    int pendingCount() {
        return static_cast<int>(g_incoming.size()) + g_stillComing;
    }

    GameObject* objectForUid(std::string const& uid) {
        auto found = g_objectByUid.find(uid);
        if (found == g_objectByUid.end()) return nullptr;
        auto object = found->second.data();
        return isInLevel(object) ? object : nullptr;
    }

    // 내가 지금 잡고 있는 것들을 상대에게 알린다.
    // 같은 물건을 동시에 건드려 서로 덮어쓰는 사고를 눈으로 막기 위한 것.
    void sendSelection(CCArray* selected) {
        if (!inRoom() || g_applyingRemote) return;

        std::string list;
        if (selected) {
            for (unsigned int i = 0; i < selected->count(); ++i) {
                auto object = static_cast<GameObject*>(selected->objectAtIndex(i));
                if (!object) continue;
                if (auto it = g_uidByLocalId.find(object->m_uniqueID); it != g_uidByLocalId.end()) {
                    if (!list.empty()) list += ',';
                    list += it->second;
                }
            }
        }

        if (list == g_lastSelection) return;
        g_lastSelection = list;

        matjson::Value msg;
        msg["type"] = "sel";
        msg["uids"] = list;
        send(std::move(msg));
    }

    void syncSelection(CCArray* selected) {
        sendSelection(selected);

        if (g_applyingRemote || !selected || !g_active || !inRoom()) return;

        auto editor = LevelEditorLayer::get();
        if (!editor) return;

        for (unsigned int i = 0; i < selected->count(); ++i) {
            inspect(editor, static_cast<GameObject*>(selected->objectAtIndex(i)));
        }
    }

    // 사람이 이 오브젝트를 직접 건드렸다.
    //
    // 조용히 하기(hush)는 되먹임을 막으려고 둔 것이지, 사람의 편집까지 막으려는
    // 것이 아니다. 상대 것을 받은 직후 2초 안에 내가 그것을 움직이면, 그 편집은
    // 반드시 전해져야 한다. 게임이 "옮겼다"고 알려줄 때 조용히 하기를 걷는다.
    //
    // 값을 견주어 짐작하는 대신 게임에게 직접 듣는 것이라, 사람이 한 일과
    // 게임이 다시 계산한 것이 섞이지 않는다.
    void noticeLocalEdit(GameObject* object) {
        if (g_applyingRemote || !g_active || !object) return;

        auto known = g_uidByLocalId.find(object->m_uniqueID);
        if (known == g_uidByLocalId.end()) return;
        g_quiet.erase(known->second);
    }

    void noticeObject(GameObject* object) {
        if (g_applyingRemote || !g_active || !object) return;
        // 여기서 바로 읽지 않는다. 위 g_pending 설명 참고.
        g_pending.push_back(object);
    }

    void handleMessage(matjson::Value const& msg) {
        auto type = msg["type"].asString().unwrapOr("");
        RemoteScope scope;

        // 여기서 에디터가 있는지 먼저 묻고 없으면 돌아가면 안 된다.
        //
        // 방에 들어간 직후가 바로 에디터가 아직 없는 순간인데, 방의 레벨과
        // 설정은 하필 그때 온다. 화면은 다음 차례에나 에디터로 바뀐다.
        // 먼저 묻고 돌아가면 방 내용을 통째로 문 앞에서 버리는 셈이다.
        //
        // 아래 두 갈래(설정, 레벨 내용)는 에디터 없이도 받아둘 수 있다.
        // 받아만 두고, 에디터가 생기면 그때 꺼내 쓴다.

        if (type == "settings") {
            applyLevelSettings(
                msg["data"].asString().unwrapOr(""),
                static_cast<int>(msg["song"].asInt().unwrapOr(0)),
                static_cast<int>(msg["track"].asInt().unwrapOr(0)),
                msg["songs"].asString().unwrapOr("")
            );
            // 방금 받은 것을 내가 바꾼 것으로 착각해 되돌려 보내지 않도록 기록해둔다.
            g_lastSettings = levelSettingsString();
            return;
        }

        if (type == "state" || type == "addMany") {
            // 여기서 만들지 않고 담아만 둔다.
            //
            // 3만 개짜리 레벨이면 이 자리에서 다 만들다가 게임이 그대로 멈춘다.
            // 매 검사마다 조금씩 만들도록 넘긴다.
            auto objects = msg[type == "state" ? "objects" : "items"];
            if (!objects.isArray()) return;

            for (auto const& entry : objects) {
                auto uid = entry["uid"].asString().unwrapOr("");
                auto data = entry["data"].asString().unwrapOr("");
                if (uid.empty() || data.empty()) continue;
                g_incoming.emplace_back(std::move(uid), std::move(data));
            }

            if (type == "state") {
                g_stillComing = static_cast<int>(msg["left"].asInt().unwrapOr(0));
            }
            return;
        }

        auto uid = msg["uid"].asString().unwrapOr("");
        if (uid.empty()) return;

        // 여기부터는 레벨을 직접 건드리므로 에디터가 있어야 한다.
        auto editor = LevelEditorLayer::get();
        if (!editor) {
            // 아직 없으면 이것들도 줄에 세워둔다. 버리면 그 편집만 영영 빠진다.
            if (type == "add" || type == "update") {
                if (auto data = msg["data"].asString().unwrapOr(""); !data.empty()) {
                    g_incoming.emplace_back(uid, std::move(data));
                }
            } else if (type == "remove") {
                // 아직 만들지도 않은 것을 지우라는 것이니 줄에서 빼면 된다.
                std::erase_if(g_incoming, [&uid](auto const& entry) {
                    return entry.first == uid;
                });
            }
            return;
        }

        // 레벨을 건드리기 전에 우리 목록이 레벨과 맞는지 확인한다.
        ensurePresent(editor);

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
    // 게임이 설정 창을 닫을 때 부르는 함수다.
    //
    // 사람이 배경이나 색을 바꾸면 반드시 여기를 지나간다. 값을 견주어
    // "바뀐 것 같다"고 짐작하는 대신, 게임이 알려주는 이 순간만 잡는다.
    void levelSettingsUpdated() {
        LevelEditorLayer::levelSettingsUpdated();
        // 우리가 상대 것을 반영하느라 부른 것은 내 편집이 아니다.
        if (!g_applyingRemote) g_settingsDirty = true;
    }

    void removeObject(GameObject* object, bool noUndo) {
        if (!coop::isApplyingRemote() && object) {
            g_present.erase(object);

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
