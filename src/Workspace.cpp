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
    constexpr char const* WORKSPACE_MARK = "[COOP] ";

    // 지금 쓰고 있는 임시 레벨.
    Ref<GJGameLevel> g_workspace;
    // 방을 옮길 때 버려야 할 이전 임시 레벨. 그 레벨의 에디터에서 빠져나온
    // 뒤에 지워야 해서 잠시 들고 있는다.
    Ref<GJGameLevel> g_discarded;

    // 우리가 방 때문에 에디터를 여는 중이라는 표시.
    // 이게 없으면 새로 열린 에디터가 "에디터에 들어왔으니 방에서 나간다"는
    // 평소 규칙을 그대로 적용해서, 방금 들어간 방에서 곧바로 튕겨 나온다.
    bool g_opening = false;

    bool isWorkspaceName(std::string const& name) {
        return name.rfind(WORKSPACE_MARK, 0) == 0;
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
        auto manager = GameLevelManager::sharedState();
        if (!manager) return;

        auto level = manager->createNewLevel();
        if (!level) {
            Notification::create("Could not make a workspace level", NotificationIcon::Error)->show();
            return;
        }

        level->m_levelName = WORKSPACE_MARK + room;

        // 이미 임시 레벨에 있었다면 그건 버린다. 다만 지금 그 레벨의 에디터
        // 안에 있으므로, 화면이 넘어간 뒤에 지운다.
        g_discarded = g_workspace;
        g_workspace = level;
        g_opening = true;

        // LevelEditorLayer::scene은 윈도우에서 쓸 수 없어서 씬을 직접 만든다.
        // 하는 일은 같다. 빈 씬에 에디터를 얹는 것뿐이다.
        auto scene = CCScene::create();
        scene->addChild(LevelEditorLayer::create(level, false));
        CCDirector::sharedDirector()->replaceScene(scene);
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

        // 지우는 동안 목록이 줄어들므로 먼저 따로 담는다.
        std::vector<Ref<GJGameLevel>> doomed;
        for (unsigned int i = 0; i < local->m_localLevels->count(); ++i) {
            auto level = static_cast<GJGameLevel*>(local->m_localLevels->objectAtIndex(i));
            if (level && isWorkspaceName(std::string(level->m_levelName))) {
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

// 남은 임시 레벨은 첫 화면이 뜰 때 한 번 치운다.
//
// 모드가 올라오는 시점에 하면 안 된다. 그때는 레벨 목록을 들고 있는 쪽이
// 아직 준비되지 않아서, 억지로 부르면 그쪽을 너무 일찍 깨우게 된다.
class $modify(CoopMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        static bool swept = false;
        if (!swept) {
            swept = true;
            coop::sweepOldWorkspaces();
        }
        return true;
    }
};

// 에디터에서 나갈 때 임시 레벨을 지운다.
// 저장하고 나가든 저장하지 않고 나가든 똑같이 지워야 한다.
class $modify(CoopEditorPauseLayer, EditorPauseLayer) {
    void onExitEditor(CCObject* sender) {
        auto leaving = coop::inWorkspace();
        EditorPauseLayer::onExitEditor(sender);
        if (leaving) {
            coop::leaveRoom();
            coop::dropWorkspace();
        }
    }

    void onExitNoSave(CCObject* sender) {
        auto leaving = coop::inWorkspace();
        EditorPauseLayer::onExitNoSave(sender);
        if (leaving) {
            coop::leaveRoom();
            coop::dropWorkspace();
        }
    }
};
