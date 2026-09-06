#pragma once

#include <Geode/Geode.hpp>

#include <string>
#include <vector>

// 협동 편집 기능의 공용 인터페이스.
//
// 설계 요약
// - 오브젝트를 좌표만이 아니라 GD 자체 저장 문자열(getSaveString)로 통째로 주고받는다.
//   그래서 회전/크기/색상/그룹/트리거 설정까지 별도 처리 없이 함께 전달된다.
// - 양쪽이 같은 오브젝트를 가리킬 수 있도록 우리가 직접 uid를 붙이고 대응표를 관리한다.
//   GD의 m_uniqueID는 각자의 게임 안에서만 유효해서 그대로 쓸 수 없다.
// - 편집 경로를 하나하나 훅으로 잡는 대신, 주기적으로 레벨을 훑어 달라진 것을 찾는다.
//   되돌리기처럼 어떤 경로로 바뀌든 결과만 보면 되므로 빠뜨릴 구멍이 적다.
namespace coop {

    enum class State {
        Disconnected,
        Connecting,
        Connected,
    };

    // --- 네트워크 (Net.cpp) ---

    // 설정에 적힌 주소로 (재)접속한다.
    void connect();
    State state();
    // 같은 방에 있는 다른 사람 수(나 제외). 서버가 알려준 값.
    int peerCount();
    // 마지막 접속 실패 이유. 성공했거나 아직 시도 전이면 빈 문자열.
    std::string lastError();
    // 서버가 알려준 자기 버전. 배포가 갱신됐는지 창에서 바로 보려고.
    std::string serverVersion();

    // 이 사람의 GD 계정 이름.
    std::string defaultPlayerName();

    // 방은 "만들기"로만 생긴다. 들어가기는 이미 있는 방에만 된다.
    // 예전처럼 들어가는 것만으로 방이 생기면 목록과 실제 방이 어긋나서
    // 서로 다른 방에 들어가 놓고 같은 방인 줄 아는 일이 생긴다.

    // 지금 들어가 있는 방. 서버가 확인해준 이름만 들어간다. 방 밖이면 빈 문자열.
    std::string currentRoom();
    bool inRoom();
    // 방 이름 칸에 미리 채워줄 이름. 계정 이름을 따서 만든다.
    std::string suggestedRoomName();

    // 혼자 시험하기.
    //
    // 켜면 서버가 내가 보낸 커서, 선택, 화면, 상태를 "Echo"라는 가짜 상대가
    // 보낸 것처럼 돌려준다. 기기 두 대를 붙이지 않고도 그것들이 제대로
    // 그려지는지 확인할 수 있다. 오브젝트는 돌려주지 않는다. 돌려주면
    // 물건이 두 배가 된다.
    void setSoloTest(bool on);
    bool soloTest();
    // 시험용 기능을 볼 사람인지. 만드는 사람 계정에서만 참.
    bool isTester();

    // 방을 만들거나 들어갈 때 쓸 열쇠. 비워두면 잠그지 않는다.
    // 만들기/들어가기를 부르기 직전에 정해준다.
    void setRoomPassword(std::string password);

    // 새 방을 만들고 바로 들어간다. 만들어진 방은 목록에 올라간다.
    void createRoom(std::string room);
    // 이미 있는 방에 들어간다. 없는 방이면 서버가 거절한다.
    void joinRoom(std::string room);
    // 방에서 나온다. 접속은 유지해서 목록은 계속 볼 수 있다.
    void leaveRoom();

    // 방 목록을 서버에 요청한다. 답이 오면 목록이 채워진다.
    void requestRoomList();
    struct RoomEntry {
        std::string name;
        int count = 0;
        std::string owner;
        int objects = 0;
        bool locked = false;
    };
    std::vector<RoomEntry> roomList();
    // 연결돼 있을 때만 실제로 보낸다.
    void send(matjson::Value msg);

    // --- 동기화 (Sync.cpp) ---

    // 서버에서 온 메시지 처리. 반드시 메인 스레드에서 호출한다.
    void handleMessage(matjson::Value const& msg);

    // 에디터에 들어왔다. 대응표를 비우고 서버에 현재 상태를 요청한다.
    void enterEditor();

    // 레벨을 훑어 새로 생긴 것과 달라진 것을 서버로 보낸다. 주기적으로 부른다.
    void reconcile();

    // 선택 중인 오브젝트만 빠르게 검사한다. 편집 반응을 즉각적으로 만들기 위한 것.
    void syncSelection(cocos2d::CCArray* selected);

    // 방금 만들어진 오브젝트를 즉시 알린다. 큰 레벨에서 주기 검사가 여기까지
    // 돌아오기를 기다리지 않도록 하기 위한 지름길.
    void noticeObject(GameObject* object);

    // 우리가 uid를 붙여 아는 오브젝트 수. 레벨에 있는 수와 다르면 짝을 못 지은
    // 오브젝트가 있다는 뜻이고, 그것이 양쪽 개수가 벌어지는 원인이 된다.
    int trackedCount();

    // 방을 만들려면 올릴 레벨이 열려 있어야 한다.
    // 레벨 목록에서 창을 열었을 때는 만들기를 막아야 한다.
    bool canHost();

    // 방장이 될 때: 지금 레벨에 있는 것을 전부 방에 올린다.
    void uploadWholeLevel();
    // 손님이 될 때: 내 레벨을 비운다. 방의 레벨을 그대로 받기 위해서다.
    void clearLevel();

    // --- 손님용 임시 레벨 (Workspace.cpp) ---
    //
    // 원본은 방장의 레벨 하나뿐이어야 한다. 손님이 자기 레벨에 방 내용을
    // 받으면 참여한 사람 수만큼 사본이 생긴다. 그래서 손님은 그때그때 만든
    // 임시 레벨에서 작업하고, 나갈 때 그 레벨을 지운다.
    void openWorkspace(std::string const& room);
    void dropWorkspace();
    void sweepOldWorkspaces();
    // 에디터 안에서 방에 들어가면, 에디터 위에 에디터를 얹지 않으려고 먼저
    // 레벨 목록으로 나간다. 목록이 올라온 뒤에 이 둘로 마저 연다.
    bool hasPendingWorkspace();
    void openPendingWorkspace();
    bool inWorkspace();
    // 방금 열린 에디터가 우리가 방 때문에 연 것인지. 한 번만 참을 돌려준다.
    bool consumeWorkspaceEntry();

    // --- 레벨 설정 (LevelState.cpp) ---
    //
    // 배경, 바닥, 색깔, 노래, 게임 모드 같은 것. 오브젝트와 달리 레벨 전체에
    // 하나씩만 있는 값이라 문자열 하나로 통째로 주고받는다.
    std::string levelSettingsString();
    int levelSongID();
    int levelAudioTrack();
    // 트리거로 바꿔 트는 곡들. "123,456" 처럼 번호가 이어져 있다.
    std::string levelSongList();
    void applyLevelSettings(
        std::string const& data, int songID, int audioTrack, std::string const& songList
    );
    // 에디터가 아직 없을 때 받아둔 설정을 반영한다. 주기적으로 부른다.
    void flushPendingSettings();
    // 없는 곡을 한 곡씩 받아온다. 주기적으로 부른다.
    void tickSongDownload();
    // 설정이 달라졌으면 방에 알린다. 주기적으로 부른다.
    void syncLevelSettings();
    // 방금 상대 설정을 반영했다. 잠깐 내 설정을 올리지 않는다.
    // 반영 직후에는 게임이 값을 다시 계산해서 내용이 미세하게 달라지는데,
    // 그걸 "내가 바꿨다"로 보내면 색이 서로 덮이며 끝나지 않는다.
    void hushSettings();
    // 방이나 레벨이 바뀌었다. 다음 설정은 같은 내용이라도 화면에 다시 반영한다.
    void forgetAppliedSettings();
    // 방의 설정을 한 번이라도 받아 반영했는지. 손님이 자기 빈 레벨의 설정으로
    // 방장의 색깔을 덮어쓰지 않도록 하는 데 쓴다.
    bool roomSettingsApplied();

    // 서버에서 받은 내용을 적용하는 중인지. 적용 중에는 되돌려 보내지 않는다.
    bool isApplyingRemote();

    // --- 상대 커서 (Cursors.cpp) ---

    // 내 손가락이 있는 자리를 상대에게 알린다. 레벨 기준 좌표.
    void sendCursor(cocos2d::CCPoint position);
    // 서버에서 온 커서 메시지를 화면에 반영한다.
    void applyCursor(std::string const& id, std::string const& name, cocos2d::CCPoint position);
    // 나간 사람의 커서를 치운다.
    void removeCursor(std::string const& id);
    // 에디터를 새로 열었으니 커서를 모두 치운다.
    void clearCursors();
    // 한동안 소식 없는 커서를 치운다. 주기적으로 부른다.
    void fadeOldCursors();
    // 커서를 몇 개 보내고 몇 개 받았는지.
    int cursorsSent();
    int cursorsReceived();

    // --- 상대가 잡고 있는 물체 / 보고 있는 화면 ---

    // 방에서 받았지만 아직 만들지 못한 오브젝트 수. 진행 상황 표시용.
    int pendingCount();

    // uid로 오브젝트를 찾는다. 없거나 이미 레벨에서 빠졌으면 nullptr.
    GameObject* objectForUid(std::string const& uid);
    // 서버에서 온 "상대가 잡고 있는 것" 목록을 받아둔다.
    void applyPeerSelection(std::string const& uids);
    // 그 물체들에 테두리를 그린다. 주기적으로 부른다.
    void drawPeerSelection();

    // 내가 보고 있는 화면 위치를 알린다. 주기적으로 부른다.
    void sendView();
    void applyPeerView(float x, float y, float zoom);
    bool hasPeerView();
    // 상대가 보고 있는 자리로 화면을 옮긴다.
    void goToPeerView();

    // --- 채팅 (Chat.cpp) ---
    //
    // GD 기본 글꼴에는 한글 글자가 없다. 그래서 그릴 수 있는 글자만 보낸다.
    void sendChat(std::string const& text);
    void addChatLine(std::string const& who, std::string const& text);
    void clearChat();
    std::vector<std::string> chatLines();
    // 창을 닫아둔 사이에 온 줄 수. 에디터 버튼에 숫자로 띄운다.
    int unreadChat();
    void markChatRead();

    // 어디가 끊겼는지 한 줄로 보여준다. 화면에서 바로 보려고.
    std::string diagnostics();
    // 상대가 알려준 상태. 두 기기를 오가지 않고 한 화면에서 보기 위한 것.
    std::string peerStats();
    // 내 상태를 상대에게 알린다. 주기적으로 부른다.
    void sendStats();

    // --- 모드 갱신 (Updater.cpp) ---
    //
    // 새 빌드를 게임 안에서 바로 받아 자기 자리에 덮어쓴다.
    // 파일을 손으로 옮기고 게임을 껐다 켤 일을 없애기 위한 것.
    void updateMod();
    bool isUpdating();
    std::string modVersion();
    // 빌드할 때 바이너리에 새겨 넣은 버전.
    std::string builtVersion();
    // 파일(mod.json)과 실제로 도는 코드가 어긋났는지.
    bool binaryIsStale();

    // --- 인증서 (빌드할 때 생성되는 CaBundle.cpp) ---

    // wss:// 접속 때 신뢰할 인증서 묶음 전문.
    // 안드로이드에서는 시스템 인증서를 읽을 수 없어 모드가 직접 들고 있어야 한다.
    char const* caBundlePem();

}
