#include "Coop.hpp"

using namespace geode::prelude;

$on_mod(Loaded) {
    coop::connect();

    // 설정 화면에서 서버 주소나 방 이름을 바꾸면 곧바로 다시 연결한다.
    listenForSettingChanges<std::string>("server-url", [](std::string) {
        coop::connect();
    });
    listenForSettingChanges<std::string>("room-name", [](std::string) {
        coop::connect();
    });
}
