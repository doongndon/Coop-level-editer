#include "Coop.hpp"

#include <Geode/ui/Notification.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/web.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace geode::prelude;

// 모드를 게임 안에서 직접 갈아끼운다.
//
// 지금까지는 새 빌드가 나올 때마다 GitHub에서 압축 파일을 받고, 풀고, 모드
// 폴더로 옮기고, 게임을 다시 켜야 했다. 폰에서는 특히 번거롭다.
//
// 그래서 GitHub 릴리스에 늘 같은 주소로 최신 파일을 올려두고, 모드가 그것을
// 자기 자리에 직접 내려받게 했다. 게임을 다시 켜면 새 것이 올라온다.
namespace {
    // "latest" 태그는 빌드할 때마다 덮어써서 주소가 늘 같다.
    constexpr char const* RELEASE_BASE =
        "https://github.com/doongndon/Coop-level-editer/releases/download/latest/";

    // 기기마다 파일이 다르다. 남의 기기 파일을 받으면 게임이 켜지지 않는다.
    char const* assetName() {
#if defined(GEODE_IS_WINDOWS)
        return "coopedit-Win64.geode";
#elif defined(GEODE_IS_ANDROID64)
        return "coopedit-Android64.geode";
#elif defined(GEODE_IS_ANDROID32)
        return "coopedit-Android32.geode";
#else
        return nullptr;
#endif
    }

    std::atomic<bool> g_busy = false;

    void finish(bool ok, std::string message) {
        Loader::get()->queueInMainThread([ok, message = std::move(message)]() {
            g_busy = false;
            Notification::create(
                message, ok ? NotificationIcon::Success : NotificationIcon::Error, 6.f
            )->show();
        });
    }
}

namespace coop {

    bool isUpdating() {
        return g_busy;
    }

    std::string modVersion() {
        return Mod::get()->getVersion().toVString();
    }

    // 실제로 도는 코드가 어느 판인지. 빌드할 때 새겨 넣은 값이다.
    std::string builtVersion() {
#ifdef COOP_BUILT_VERSION
        return COOP_BUILT_VERSION;
#else
        return "?";
#endif
    }

    // 파일과 도는 코드가 어긋났는지.
    //
    // 이 경우 무엇을 고쳐도 반영되지 않는데 화면에는 새 버전이 떠서
    // 원인을 찾는 데 오래 걸린다. 눈에 띄게 알려야 한다.
    bool binaryIsStale() {
        auto built = builtVersion();
        return built != "?" && built != modVersion();
    }

    void updateMod() {
        // 두 번 눌러 같은 파일을 동시에 두 번 쓰면 반쯤 쓰인 파일이 남는다.
        if (g_busy.exchange(true)) return;

        auto name = assetName();
        if (!name) {
            finish(false, "No build for this device");
            return;
        }

        auto url = std::string(RELEASE_BASE) + name;
        auto target = Mod::get()->getPackagePath();

        Notification::create("Downloading the newest build...", NotificationIcon::Loading, 4.f)->show();

        // 내려받는 동안 게임이 멈추면 안 되므로 딴 실톳에서 받는다.
        // 여기서는 게임 화면을 절대 건드리지 않는다. 알림은 메인으로 넘겨서 띄운다.
        std::thread([url, target]() {
            auto response = web::WebRequest()
                .followRedirects(true)   // 릴리스 주소는 실제 파일 주소로 한 번 넘어간다
                .timeout(std::chrono::seconds(180))
                .getSync(url);

            if (!response.ok()) {
                finish(false, fmt::format("Download failed (HTTP {})", response.code()));
                return;
            }

            auto const& bytes = response.data();
            // 파일이 없을 때 GitHub은 짧은 안내 문서를 대신 준다.
            // 그걸 모드 파일로 덮어쓰면 다음에 게임이 켜지지 않는다.
            if (bytes.size() < 10000) {
                finish(false, "The build is not on the server yet");
                return;
            }

            // 곧바로 덮어쓰지 않는다. 받다가 끊기면 반쯤 쓰인 파일이 자리를
            // 차지해 모드가 아예 안 켜진다. 옆에 다 쓴 뒤 자리를 바꾼다.
            auto temporary = target;
            temporary += ".new";

            {
                std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
                if (!out) {
                    finish(false, "Could not write to the mods folder");
                    return;
                }
                out.write(reinterpret_cast<char const*>(bytes.data()), bytes.size());
            }

            // 받은 것이 정말 쓸 수 있는 모드 파일인지 먼저 확인한다.
            //
            // 크기만 보고 넘겼더니 모드가 통째로 죽은 적이 있다. 빌드가 올라가는
            // 도중에 받으면 절반만 받아지는데, 절반짜리도 크기 검사는 통과한다.
            // 그걸 자리에 앉히면 Geode가 풀다가 실패하면서 이미 풀어뒀던 것까지
            // 지워버려서, 다음에 켤 때 "이 기기용 파일이 없다"며 아예 안 켜진다.
            //
            // 이사할 때 새 집 문이 열리는지 먼저 확인하고 옛 집 열쇠를 반납하는
            // 것과 같다. 확인 전에는 옛 파일을 건드리지 않는다.
            auto binaryName = std::string(Mod::get()->getMetadata().getBinaryName());
            auto dropTemporary = [&temporary]() {
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
            };

            {
                auto opened = file::Unzip::create(temporary);
                if (!opened) {
                    dropTemporary();
                    finish(false, "The download was broken. Nothing was changed");
                    return;
                }
                if (!opened.unwrap().hasEntry(binaryName)) {
                    dropTemporary();
                    finish(false, "That build has no file for this device");
                    return;
                }
            }

            std::error_code ec;
            std::filesystem::rename(temporary, target, ec);
            if (ec) {
                dropTemporary();
                finish(false, fmt::format("Could not replace the mod ({})", ec.message()));
                return;
            }

            // .geode만 바꾸면 모자란다.
            //
            // Geode는 mod.json은 파일에서 새로 읽지만, 실행할 코드는 예전에
            // 풀어둔 것을 그대로 다시 쓴다. 그래서 버전 표시만 새것이 되고
            // 동작은 옛것으로 남는다.
            //
            // 예전에는 풀어둔 실행 파일을 직접 지웠다. 그런데 그러면 새 파일에
            // 문제가 있을 때 돌아갈 자리가 없어진다. 대신 Geode가 "다시 풀어야
            // 하나"를 판단할 때 보는 표시만 지운다. 그러면 다음에 켤 때 무조건
            // 새로 푼다.
            std::error_code markEc;
            std::filesystem::remove(
                dirs::getModRuntimeDir() / Mod::get()->getID() / "modified-at", markEc
            );

            // 옛 판이 남겨놓은 찌꺼기도 치운다.
            std::error_code oldEc;
            std::filesystem::remove(
                Mod::get()->getBinaryPath().string() + ".old", oldEc
            );

            finish(true, "Updated! Restart GD to use it");
        }).detach();
    }

}
