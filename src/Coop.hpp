#pragma once

#include <Geode/Geode.hpp>

#include <string>

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

    // 에디터에 들어올 때부터 레벨에 있던 오브젝트들을 방에 올린다.
    // 기본적으로는 올리지 않는다. 양쪽이 같은 레벨을 열었을 때 두 배가 되기 때문.
    void shareExistingLevel();
    // 아직 방에 올리지 않은 기존 오브젝트가 몇 개인지.
    int unsharedCount();

    // 서버에서 받은 내용을 적용하는 중인지. 적용 중에는 되돌려 보내지 않는다.
    bool isApplyingRemote();

    // --- 인증서 (빌드할 때 생성되는 CaBundle.cpp) ---

    // wss:// 접속 때 신뢰할 인증서 묶음 전문.
    // 안드로이드에서는 시스템 인증서를 읽을 수 없어 모드가 직접 들고 있어야 한다.
    char const* caBundlePem();

}
