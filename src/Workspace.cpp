#include "Coop.hpp"

#include <Geode/modify/EditorPauseLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/ui/Notification.hpp>

#include <string>
#include <vector>

using namespace geode::prelude;

// 손님용 임시 작업 레벨.
//
// 예전에는 남의 방에 들어가면 그때 열려 있던 레벨을 비우고 방 내용을 채웠다.
// 그러면 합작에 참여한 사람 전원이 그 맵의 사본을 갖게 된다. 원본은 방장의
// 것 하나여야 하는데, 나갈 때마다 사본이 늘어나는 셈이다.
//
// 그래서 손님은 그때그때 만든 임시 레벨에서 작업하고, 나갈 때 그 레벨을
// 지운다. 손님의 원래 레벨 목록은 손도 대지 않는다.
namespace {
    // 이 표시로 시작하는 레벨은 우리가 만든 임시 레벨이다.
    //
    // 예전에는 "[COOP] " 였는데 대괄호를 뺐다. 레벨 이름을 그대로 파일 이름이나
    // 저장 열쇠로 쓰는 모드들이 있는데, 거기에 대괄호 같은 글자가 들어가면
    // 열쇠를 못 찾고 빈 것을 돌려준다. 그걸 그대로 쓰다 죽는 모드가 있었다.
    constexpr char const* WORKSPACE_MARK = "COOP ";
    // 옛 판이 만들어놓고 간 것도 계속 치워줘야 한다.
    constexpr char const* OLD_WORKSPACE_MARK = "[COOP] ";

    // 방 이름에도 같은 위험이 있다. 글자와 숫자만 남긴다.
    std::string safeName(std::string const& room) {
        std::string out;
        for (auto ch : room) {
            auto u = static_cast<unsigned char>(ch);
            if ((u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z')
                || ch == '-' || ch == '_' || ch == ' ') {
                out.push_back(ch);
            }
        }
        if (out.empty()) out = "room";
        return out;
    }

    // 지금 쓰고 있는 임시 레벨.
    Ref<GJGameLevel> g_workspace;
    // 방을 옮길 때 버려야 할 이전 임시 레벨. 그 레벨의 에디터에서 빠져나온
    // 뒤에 지워야 해서 잠시 들고 있는다.
    Ref<GJGameLevel> g_discarded;

    // 게임이 첫 화면까지 떴는지. 여기 오기 전에는 화면을 건드리면 안 된다.
    bool g_gameReady = false;

    // 우리가 방 때문에 에디터를 여는 중이라는 표시.
    // 이게 없으면 새로 열린 에디터가 "에디터에 들어왔으니 방에서 나간다"는
    // 평소 규칙을 그대로 적용해서, 방금 들어간 방에서 곧바로 튕겨 나온다.
    bool g_opening = false;

    bool isWorkspaceName(std::string const& name) {
        return name.rfind(WORKSPACE_MARK, 0) == 0
            || name.rfind(OLD_WORKSPACE_MARK, 0) == 0;
    }

    void reallyDelete(GJGameLevel* level) {
        if (!level) return;
        if (auto manager = GameLevelManager::sharedState()) {
            manager->deleteLevel(level);
        }
    }
}

namespace coop {

    bool inWorkspace() {
        auto editor = LevelEditorLayer::get();
        return editor && editor->m_level && g_workspace.data() == editor->m_level;
    }

    bool consumeWorkspaceEntry() {
        if (!g_opening) return false;
        g_opening = false;

        // 이제 이전 레벨의 에디터에서 완전히 빠져나왔으니 지워도 안전하다.
        if (auto old = g_discarded.data()) {
            reallyDelete(old);
            g_discarded = nullptr;
        }
        return true;
    }

    void openWorkspace(std::string const& room) {
        // 게임이 아직 뜨는 중이면 아무것도 하지 않는다.
        //
        // 로딩 화면에서 에디터를 열면, 아직 준비되지 않은 다른 에디터 모드들이
        // 그대로 깨어나서 없는 것을 짚고 죽는다. 실제로 게임을 켤 때마다
        // 튕기던 원인이 이것이었다.
        if (!g_gameReady) {
            log::warn("게임이 아직 뜨는 중이라 임시 레벨을 열지 않습니다");
            coop::leaveRoom();
            return;
        }

        auto manager = GameLevelManager::sharedState();
        if (!manager) return;

        auto level = manager->createNewLevel();
        if (!level) {
            Notification::create("Could not make a workspace level", NotificationIcon::Error)->show();
            return;
        }

        level->m_levelName = WORKSPACE_MARK + safeName(room);

        // 이미 임시 레벨에 있었다면 그건 버린다. 다만 지금 그 레벨의 에디터
        // 안에 있으므로, 화면이 넘어간 뒤에 지운다.
        g_discarded = g_workspace;
        g_workspace = level;
        g_opening = true;

        // 화면 교체는 다음 차례로 미룬다.
        //
        // 여기는 서버 메시지를 처리하는 도중이다. 그 자리에서 씬을 갈아치우면
        // 다른 모드들이 화면을 훑고 있는 한복판에서 발밑이 바뀐다. 한 박자
        // 미루면 게임이 하던 일을 끝낸 뒤에 넘어간다.
        Loader::get()->queueInMainThread([level = Ref<GJGameLevel>(level)]() {
            // 이미 에디터 안에서 부른 것인지. 새 에디터를 만들기 전에 봐야 한다.
            // 만들고 나면 게임이 들고 있는 "지금 에디터"가 새것으로 바뀐다.
            auto fromEditor = LevelEditorLayer::get() != nullptr;

            // LevelEditorLayer::scene은 윈도우에서 쓸 수 없어서 씬을 직접 만든다.
            // 다만 GD가 하는 세 가지를 하나도 빠뜨리면 안 된다.
            //
            // 처음에는 "빈 씬에 에디터를 얹는 것뿐"이라고 보고 가운데 한 줄만
            // 옮겼는데, 그게 크래시의 원인이었다. 다른 에디터 모드들은 씬에
            // 찍힌 표시(objType)를 보고 "여기가 에디터구나" 하고 자기 준비를
            // 한다. 표시가 없으면 준비를 건너뛰고, 그래놓고 화면이 돌기
            // 시작하면 준비되지 않은 자리를 짚어서 게임이 죽는다.
            // 문패 없는 가게에 배달이 안 오는 것과 같다.
            auto scene = CCScene::create();
            if (auto app = AppDelegate::get()) app->m_runningScene = scene;
            scene->addChild(LevelEditorLayer::create(level.data(), false));
            scene->setObjType(CCObjectType::LevelEditorLayer);

            auto director = CCDirector::sharedDirector();
            if (fromEditor) {
                // 방을 옮기는 경우다. 어둡게 넘기면 그 0.5초 동안 옛 에디터와
                // 새 에디터가 같이 살아서 둘 다 돌아간다. 다른 모드들은 에디터가
                // 하나뿐이라고 보고 만들어져 있어서 이때 자주 엉킨다.
                // 여기서는 연출 없이 바로 바꿔 옛 에디터를 즉시 끝낸다.
                director->replaceScene(scene);
            } else {
                // GD도 에디터에 들어갈 때 어둡게 넘긴다. 그냥 바꿔치우면 다른
                // 모드가 미처 준비되지 않은 화면을 훑게 될 수 있다.
                director->replaceScene(CCTransitionFade::create(0.5f, scene));
            }
        });
    }

    void dropWorkspace() {
        if (auto level = g_workspace.data()) {
            reallyDelete(level);
        }
        g_workspace = nullptr;
    }

    // 게임이 갑자기 꺼지면 임시 레벨이 남는다. 다음에 켤 때 치운다.
    void sweepOldWorkspaces() {
        auto local = LocalLevelManager::sharedState();
        auto manager = GameLevelManager::sharedState();
        if (!local || !manager || !local->m_localLevels) return;

        // 지금 그 레벨의 에디터 안에 있을 때만 남겨둔다.
        //
        // 예전에는 "우리가 마지막으로 만든 임시 레벨"이면 무조건 건너뛰었다.
        // 그런데 나가는 길에서 정리가 한 번이라도 어긋나면 그 레벨은 영영
        // 건너뛰기 대상으로 남아, 목록에 그대로 눌러앉는다.
        //
        // 이 훑기는 첫 화면과 레벨 목록에서만 돈다. 거기서는 에디터가 열려
        // 있을 수가 없으니, 사실상 남은 것은 전부 지운다.
        auto inUse = inWorkspace() ? g_workspace.data() : nullptr;

        // 지우는 동안 목록이 줄어들므로 먼저 따로 담는다.
        std::vector<Ref<GJGameLevel>> doomed;
        for (unsigned int i = 0; i < local->m_localLevels->count(); ++i) {
            auto level = static_cast<GJGameLevel*>(local->m_localLevels->objectAtIndex(i));
            if (level && level != inUse && isWorkspaceName(std::string(level->m_levelName))) {
                doomed.push_back(level);
            }
        }

        for (auto& held : doomed) {
            reallyDelete(held.data());
        }

        if (!doomed.empty()) {
            log::info("남아 있던 임시 작업 레벨 {}개를 치웠습니다", doomed.size());
        }
    }

}

// 첫 화면이 뜰 때마다 남은 임시 레벨을 치운다.
//
// 한 번만 하지 않는 이유는, 에디터에서 나가는 길이 여러 갈래라 그중 하나라도
// 놓치면 임시 레벨이 목록에 그대로 남기 때문이다. 첫 화면은 결국 언제나
// 지나가게 되므로 여기서 한 번 더 훑는 것이 가장 확실하다.
//
// 모드가 올라오는 시점에 하면 안 된다. 그때는 레벨 목록을 들고 있는 쪽이
// 아직 준비되지 않아서, 억지로 부르면 그쪽을 너무 일찍 깨우게 된다.
class $modify(CoopMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        // 여기까지 왔으면 게임은 다 뜬 것이다.
        g_gameReady = true;
        coop::sweepOldWorkspaces();
        return true;
    }
};

// 에디터에서 나갈 때 임시 레벨을 지운다.
//
// 나가는 길이 세 갈래다. 저장하고 나가기, 그냥 나가기, 저장 없이 나가기.
// 처음에는 두 개만 걸어놔서, 사람들이 가장 많이 쓰는 "Save & Exit"으로
// 나가면 임시 레벨이 그대로 남았다.
class $modify(CoopEditorPauseLayer, EditorPauseLayer) {
    void cleanUpWorkspace() {
        coop::leaveRoom();
        coop::dropWorkspace();
    }

    void onExitEditor(CCObject* sender) {
        auto leaving = coop::inWorkspace();
        EditorPauseLayer::onExitEditor(sender);
        if (leaving) this->cleanUpWorkspace();
    }

    void onSaveAndExit(CCObject* sender) {
        auto leaving = coop::inWorkspace();
        EditorPauseLayer::onSaveAndExit(sender);
        if (leaving) this->cleanUpWorkspace();
    }

    void onExitNoSave(CCObject* sender) {
        auto leaving = coop::inWorkspace();
        EditorPauseLayer::onExitNoSave(sender);
        if (leaving) this->cleanUpWorkspace();
    }
};
