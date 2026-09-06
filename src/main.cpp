#include "Coop.hpp"

using namespace geode::prelude;

namespace {
    // 예전 버전에서는 서버 주소를 직접 쳐 넣게 되어 있었고, 입력칸이 : 와 . 를
    // 걸러버리는 문제가 있었다. 그래서 "WSSCOOPLEVELEDITERONRENDERCOM" 같은
    // 망가진 값이 저장에 남아 있을 수 있다.
    //
    // 저장된 값은 mod.json의 기본값보다 우선하므로, 새 기본값을 넣어도
    // 그 사람에게는 적용되지 않는다. 그래서 여기서 한 번 고쳐준다.
    // 점이 하나도 없는 주소는 정상적인 주소일 수가 없다.
    void repairServerUrl() {
        auto url = Mod::get()->getSettingValue<std::string>("server-url");
        if (url.find('.') != std::string::npos) return;

        if (auto setting = Mod::get()->getSetting("server-url")) {
            setting->reset();
            log::info("망가진 서버 주소 \"{}\" 를 기본값으로 되돌렸습니다", url);
        }
    }
}

namespace {
    // 레벨 목록의 COOP 버튼을 오른쪽으로 옮긴다.
    //
    // 기본값만 바꿔서는 이미 쓰던 사람에게 적용되지 않는다. 저장된 값이
    // 기본값보다 우선하기 때문이다. 왼쪽 가운데는 GD 버튼 하나를 덮고
    // 있었으므로, 그 값을 쓰고 있던 사람만 한 번 되돌려준다.
    // 일부러 왼쪽을 고른 사람이 다시 골라도 두 번 옮기지는 않는다.
    void moveBrowserButtonOnce() {
        auto mod = Mod::get();
        // 자리를 옮길 때마다 이 번호를 올린다. 저장된 값이 기본값을 이기기
        // 때문에, 새 자리를 정해도 한 번 되돌려주지 않으면 아무도 못 본다.
        if (mod->getSavedValue<int>("browser-spot-gen", 0) >= 3) return;
        mod->setSavedValue<int>("browser-spot-gen", 3);
        if (auto setting = mod->getSetting("browser-spot")) setting->reset();
    }
}

namespace {
    // 에디터 줄을 EDIT 탭 안의 빈 자리로 옮긴다.
    //
    // 여기도 저장된 값이 기본값을 이긴다. 예전 기본값(top-left)을 그대로
    // 쓰고 있고 손으로 옮긴 적도 없는 사람만 한 번 새 자리로 데려온다.
    // 직접 골랐거나 끌어다 놓은 자리는 건드리지 않는다.
    void moveEditorBarOnce() {
        auto mod = Mod::get();
        if (mod->getSavedValue<bool>("hud-spot-moved", false)) return;
        mod->setSavedValue<bool>("hud-spot-moved", true);

        if (mod->getSavedValue<bool>("hud-moved", false)) return;
        if (mod->getSettingValue<std::string>("hud-spot") != "top-left") return;
        if (auto setting = mod->getSetting("hud-spot")) setting->reset();
    }
}

$on_mod(Loaded) {
    repairServerUrl();
    moveBrowserButtonOnce();
    moveEditorBarOnce();
    coop::connect();

    // 설정 화면에서 서버 주소를 바꾸면 곧바로 다시 연결한다.
    listenForSettingChanges<std::string>("server-url", [](std::string) {
        coop::connect();
    });

    // 구석을 다시 고르면, 손으로 옮겨둔 자리는 잊는다.
    // 안 그러면 설정을 바꿔도 아무 일이 안 일어나서 고장 난 줄 안다.
    listenForSettingChanges<std::string>("hud-spot", [](std::string) {
        Mod::get()->setSavedValue<bool>("hud-moved", false);
    });

    // room-name은 감시하지 않는다. 이 값은 모드가 방에 들어갈 때마다 스스로
    // 적어 넣기 때문에, 여기서 다시 연결하면 방을 바꿀 때마다 접속이 끊기고
    // 심하면 접속 -> 저장 -> 접속이 끝없이 반복된다.
}
