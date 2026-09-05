#include "Coop.hpp"

#include <Geode/modify/EditorUI.hpp>

using namespace geode::prelude;

// 에디터 화면 위쪽에 접속 상태를 보여준다.
// GD 기본 폰트는 한글 글리프가 없어서 표시 문구는 영문으로만 쓴다.
class $modify(CoopEditorUI, EditorUI) {
    struct Fields {
        CCLabelBMFont* m_status = nullptr;
    };

    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;

        auto label = CCLabelBMFont::create("COOP", "bigFont.fnt");
        label->setScale(0.3f);
        label->setAnchorPoint({ 0.5f, 1.f });
        label->setZOrder(1000);
        label->setOpacity(200);

        auto winSize = CCDirector::get()->getWinSize();
        label->setPosition({ winSize.width / 2.f, winSize.height - 3.f });

        this->addChild(label);
        m_fields->m_status = label;

        coop::enterEditor();
        this->updateCoopStatus();

        this->schedule(schedule_selector(CoopEditorUI::coopTick), 0.25f);
        return true;
    }

    void updateCoopStatus() {
        auto label = m_fields->m_status;
        if (!label) return;

        switch (coop::state()) {
            case coop::State::Connected: {
                auto peers = coop::peerCount();
                label->setString(fmt::format("COOP ON - {} PARTNER(S)", peers).c_str());
                // 혼자면 노란색, 상대가 있으면 초록색
                label->setColor(peers > 0 ? ccColor3B{ 90, 255, 90 } : ccColor3B{ 255, 220, 90 });
                break;
            }
            case coop::State::Connecting:
                label->setString("COOP CONNECTING...");
                label->setColor({ 255, 220, 90 });
                break;
            case coop::State::Disconnected:
                label->setString("COOP OFF");
                label->setColor({ 255, 110, 110 });
                break;
        }
    }

    void coopTick(float) {
        // 선택 중인 오브젝트가 바뀌었는지 살펴서 바뀐 것만 보낸다.
        // 옮기기, 회전, 크기 조절, 색상 변경이 모두 여기서 잡힌다.
        coop::syncSelection(this->getSelectedObjects());
        this->updateCoopStatus();
    }

    GameObject* createObject(int objectID, CCPoint position) {
        auto object = EditorUI::createObject(objectID, position);
        coop::trackNewObject(object);
        return object;
    }

    CCArray* pasteObjects(gd::string str, bool withColor, bool noUndo) {
        auto created = EditorUI::pasteObjects(str, withColor, noUndo);

        if (created && !coop::isApplyingRemote()) {
            for (unsigned int i = 0; i < created->count(); ++i) {
                coop::trackNewObject(static_cast<GameObject*>(created->objectAtIndex(i)));
            }
        }
        return created;
    }
};
