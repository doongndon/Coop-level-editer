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
    // 연결돼 있을 때만 실제로 보낸다.
    void send(matjson::Value msg);

    // --- 동기화 (Sync.cpp) ---

    // 서버에서 온 메시지 처리. 반드시 메인 스레드에서 호출한다.
    void handleMessage(matjson::Value const& msg);

    // 에디터에 새로 들어왔을 때 대응표를 비우고 서버에 현재 상태를 요청한다.
    void enterEditor();

    // 내가 방금 만든 오브젝트를 등록하고 서버에 알린다.
    void trackNewObject(GameObject* object);

    // 선택 중인 오브젝트가 바뀌었는지 검사해서 바뀐 것만 서버로 보낸다.
    void syncSelection(cocos2d::CCArray* selected);

    // 서버에서 받은 내용을 적용하는 중인지. 적용 중에는 되돌려 보내지 않는다.
    bool isApplyingRemote();

}
