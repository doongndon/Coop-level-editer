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
        CCMenu* m_menu = nullptr;
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
        menu->setContentSize({ 260.f, 22.f });
        menu->addChild(button);
        // setLayout을 부르면 Geode가 CCMenu의 기준점 무시 설정을 꺼서
        // 자리 계산이 상식대로 동작한다. 직접 좌표를 잡으면 어긋난다.
        menu->setLayout(RowLayout::create());
        menu->setZOrder(1000);

        auto winSize = CCDirector::get()->getWinSize();
        menu->setPosition({ winSize.width / 2.f, winSize.height - 14.f });

        this->addChild(menu);
        m_fields->m_menu = menu;

        coop::clearCursors();

        if (coop::consumeWorkspaceEntry()) {
            // 방에 들어가느라 우리가 직접 연 에디터다. 아래 규칙을 적용하면
            // 방금 들어간 방에서 곧바로 튕겨 나온다.
            coop::enterEditor();
            // 빈 레벨이지만 여기서 동기화를 켜고 방 내용을 받기 시작한다.
            coop::clearLevel();
        } else {
            // 평소에 에디터에 들어올 때는 항상 방 밖에서 시작한다.
            //
            // 방에 들어가는 순간 화면이 그 방의 레벨로 넘어가기 때문에,
            // 레벨을 열자마자 저절로 그런 일이 일어나면 방금 연 레벨을
            // 눈앞에서 놓치게 된다. 방은 사용자가 직접 고르게 한다.
            if (coop::inRoom()) coop::leaveRoom();
            coop::enterEditor();
        }

        // 설정에서 꺼두지 않았으면 서버에는 알아서 접속한다.
        // 접속만 할 뿐 방에는 들어가지 않는다.
        if (Mod::get()->getSettingValue<bool>("auto-join")
            && coop::state() == coop::State::Disconnected) {
            coop::connect();
        }

        this->updateCoopStatus();

        this->schedule(schedule_selector(CoopEditorUI::coopTick), 0.25f);
        return true;
    }

    void updateCoopStatus() {
        auto label = m_fields->m_status;
        if (!label) return;

        switch (coop::state()) {
            case coop::State::Connected: {
                // 접속만 됐다고 같이 편집되는 게 아니다. 방에 들어가야 한다.
                // 그 차이를 여기서 분명히 보여줘야 헷갈리지 않는다.
                if (!coop::inRoom()) {
                    label->setString("COOP - TAP TO PICK A ROOM");
                    label->setColor({ 255, 180, 90 });
                    break;
                }

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
            auto size = label->getScaledContentSize();
            button->setContentSize(size);
            label->setPosition(cocos2d::CCPoint(size.width / 2.f, size.height / 2.f));

            if (auto menu = m_fields->m_menu) {
                menu->updateLayout();
            }
        }
    }

    void coopTick(float) {
        // 선택 중인 것부터 본다. 편집 중인 오브젝트라 가장 먼저 전해져야 한다.
        coop::syncSelection(this->getSelectedObjects());
        // 그다음 레벨 전체를 조금씩 나눠 훑는다. 되돌리기처럼 어떤 경로로 바뀌었든
        // 결과적으로 달라진 것이 있으면 여기서 잡힌다.
        coop::reconcile();
        coop::tickSongDownload();
        coop::fadeOldCursors();
        this->updateCoopStatus();
    }

    // 손가락이 움직일 때마다 상대에게 위치를 알린다.
    // 화면 좌표가 아니라 레벨 좌표로 바꿔서 보내야 서로 같은 곳을 가리킨다.
    // 사람마다 화면 크기와 확대 배율이 다르기 때문이다.
    // 손가락이 닿고, 움직이고, 떨어질 때 모두 자리를 알린다.
    // 움직일 때만 보내면 한 번 콕 찍는 동작은 상대에게 보이지 않는다.
    void tellWhereIAm(CCTouch* touch) {
        if (auto editor = LevelEditorLayer::get(); editor && editor->m_objectLayer && touch) {
            coop::sendCursor(editor->m_objectLayer->convertToNodeSpace(touch->getLocation()));
        }
    }

    bool ccTouchBegan(CCTouch* touch, CCEvent* event) {
        auto handled = EditorUI::ccTouchBegan(touch, event);
        this->tellWhereIAm(touch);
        return handled;
    }

    void ccTouchMoved(CCTouch* touch, CCEvent* event) {
        EditorUI::ccTouchMoved(touch, event);
        this->tellWhereIAm(touch);
    }

    void ccTouchEnded(CCTouch* touch, CCEvent* event) {
        EditorUI::ccTouchEnded(touch, event);
        this->tellWhereIAm(touch);
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
