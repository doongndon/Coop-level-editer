#include "Coop.hpp"
#include "CoopPopup.hpp"

#include <Geode/modify/EditorUI.hpp>

using namespace geode::prelude;

// 에디터 화면 위쪽에 접속 상태를 보여준다.
// 이 표시를 누르면 방 접속 창이 열린다.
// GD 기본 폰트는 한글 글리프가 없어서 표시 문구는 영문으로만 쓴다.
class $modify(CoopEditorUI, EditorUI) {
    struct Fields {
        CCLabelBMFont* m_status = nullptr;
        CCMenuItemSpriteExtra* m_button = nullptr;
    };

    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;

        auto label = CCLabelBMFont::create("COOP", "bigFont.fnt");
        label->setScale(0.3f);
        label->setOpacity(200);
        m_fields->m_status = label;

        // 상태 표시 자체를 버튼으로 쓴다. 따로 아이콘을 두지 않아도
        // 눌러서 방을 바꿀 수 있고, 화면도 덜 가린다.
        auto button = CCMenuItemExt::createSpriteExtra(label, [](CCMenuItemSpriteExtra*) {
            if (auto popup = CoopPopup::create()) {
                popup->show();
            }
        });

        m_fields->m_button = button;

        auto menu = CCMenu::create();
        menu->addChild(button);
        menu->setZOrder(1000);

        auto winSize = CCDirector::get()->getWinSize();
        menu->setPosition({ winSize.width / 2.f, winSize.height - 10.f });

        this->addChild(menu);

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

        // 글자가 바뀌면 버튼 크기도 같이 맞춰야 누를 수 있는 범위가 어긋나지 않는다.
        if (auto button = m_fields->m_button) {
            button->setContentSize(label->getScaledContentSize());
            label->setPosition(button->getContentSize() / 2.f);
        }
    }

    void coopTick(float) {
        // 선택 중인 것부터 본다. 편집 중인 오브젝트라 가장 먼저 전해져야 한다.
        coop::syncSelection(this->getSelectedObjects());
        // 그다음 레벨 전체를 조금씩 나눠 훑는다. 되돌리기처럼 어떤 경로로 바뀌었든
        // 결과적으로 달라진 것이 있으면 여기서 잡힌다.
        coop::reconcile();
        this->updateCoopStatus();
    }

    GameObject* createObject(int objectID, CCPoint position) {
        auto object = EditorUI::createObject(objectID, position);
        coop::noticeObject(object);
        return object;
    }

    CCArray* pasteObjects(gd::string str, bool withColor, bool noUndo) {
        auto created = EditorUI::pasteObjects(str, withColor, noUndo);

        if (created) {
            for (unsigned int i = 0; i < created->count(); ++i) {
                coop::noticeObject(static_cast<GameObject*>(created->objectAtIndex(i)));
            }
        }
        return created;
    }
};
