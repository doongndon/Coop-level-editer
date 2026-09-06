#include "Coop.hpp"

#include <Geode/ui/Notification.hpp>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

using namespace geode::prelude;

// 레벨 자체의 설정을 주고받는다.
//
// 오브젝트만 주고받으면 배경, 바닥, 색깔, 노래, 게임 모드 같은 것이 각자 것으로
// 남는다. 같은 물건을 놓아도 화면이 전혀 다르게 보이는 이유가 이것이다.
//
// 다행히 GD는 이 설정들을 한 줄짜리 문자열로 저장한다(레벨 파일 맨 앞부분).
// 그래서 오브젝트와 똑같이 문자열 하나를 통째로 주고받으면 된다.
namespace {
    // "키,값,키,값,..." 을 나눈다. 오브젝트 저장 문자열과 같은 형식이다.
    std::unordered_map<std::string, std::string> splitPairs(std::string const& text) {
        std::unordered_map<std::string, std::string> values;

        size_t start = 0;
        std::string key;
        bool expectingKey = true;

        while (start <= text.size()) {
            auto comma = text.find(',', start);
            auto piece = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);

            if (expectingKey) key = std::move(piece);
            else values[key] = std::move(piece);
            expectingKey = !expectingKey;

            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        return values;
    }

    using Values = std::unordered_map<std::string, std::string>;

    int intOf(Values const& values, char const* key, int fallback) {
        auto found = values.find(key);
        if (found == values.end() || found->second.empty()) return fallback;
        return std::atoi(found->second.c_str());
    }

    bool boolOf(Values const& values, char const* key, bool fallback) {
        auto found = values.find(key);
        if (found == values.end() || found->second.empty()) return fallback;
        return found->second != "0";
    }

    float floatOf(Values const& values, char const* key, float fallback) {
        auto found = values.find(key);
        if (found == values.end() || found->second.empty()) return fallback;
        return std::strtof(found->second.c_str(), nullptr);
    }

    std::string stringOf(Values const& values, char const* key) {
        auto found = values.find(key);
        return found == values.end() ? std::string() : found->second;
    }

    // 색깔은 kS38 하나에 다 들어 있다.
    // 통로 하나가 "1_255_2_0_..._6_1001_..." 이고, 통로끼리는 | 로 이어져 있다.
    // 키 6이 몇 번 색 통로인지를 뜻한다.
    // 실제로 몇 개의 색 통로를 반영했는지. 창에 띄워서 색이 안 맞을 때
    // 내용이 안 온 것인지 반영에 실패한 것인지 구분하려고 센다.
    int g_colorsApplied = 0;
    // 내가 보내는 색 내용의 길이. 0이면 GD에게서 색을 아예 못 받아온 것이고,
    // 그건 반영 쪽이 아니라 읽어오는 쪽이 문제라는 뜻이다.
    int g_colorCharsOut = 0;
    int g_colorCharsIn = 0;

    void applyColors(LevelEditorLayer* editor, std::string const& blob) {
        auto manager = editor->m_effectManager;
        if (!manager || blob.empty()) return;

        int applied = 0;

        size_t start = 0;
        while (start <= blob.size()) {
            auto bar = blob.find('|', start);
            auto entry = blob.substr(start, bar == std::string::npos ? std::string::npos : bar - start);

            if (!entry.empty()) {
                // 통로 번호를 찾으려면 _ 로 나눠서 키 6의 값을 봐야 한다.
                int channel = 0;
                size_t at = 0;
                std::string key;
                bool expectingKey = true;
                while (at <= entry.size()) {
                    auto under = entry.find('_', at);
                    auto piece = entry.substr(at, under == std::string::npos ? std::string::npos : under - at);
                    if (expectingKey) {
                        key = std::move(piece);
                    } else if (key == "6") {
                        channel = std::atoi(piece.c_str());
                        break;
                    }
                    expectingKey = !expectingKey;
                    if (under == std::string::npos) break;
                    at = under + 1;
                }

                if (channel != 0) {
                    // 이미 있는 통로는 그 자리에서 고친다. 새로 만들어 갈아끼우면
                    // 누가 그 통로를 들고 있는지 알 수 없어 위험하다.
                    if (auto action = manager->getColorAction(channel)) {
                        action->setupFromString(entry);
                        ++applied;
                    }
                    // 상대가 쓰는 통로를 내 레벨이 아직 안 갖고 있을 수 있다.
                    // 그 경우 없다고 넘기면 그 색만 영영 안 맞는다. 만들어 넣는다.
                    else if (auto fresh = ColorAction::create()) {
                        fresh->setupFromString(entry);
                        manager->setColorAction(fresh, channel);
                        ++applied;
                    }
                }
            }

            if (bar == std::string::npos) break;
            start = bar + 1;
        }

        g_colorsApplied = applied;

        manager->calculateBaseActiveColors();
        if (editor->m_objects) {
            editor->updateObjectColors(editor->m_objects);
        }
    }
}

namespace {
    // 주고받을 항목만 추려낸다.
    //
    // getSaveString()을 통째로 보내면 안 되는 이유가 있다. 그 안에는 오브젝트를
    // 하나 놓을 때마다 바뀌는 값(kA47 등)이 섞여 있어서, 물건 하나 놓을 때마다
    // "설정이 바뀌었다"고 판단해 상대 화면의 배경을 다시 그리게 된다.
    //
    // kA14(안내선)도 뺐다. 선을 그을 때마다 바뀌는데 각자 쓰는 작업 보조선이라
    // 맞출 필요가 없다.
    constexpr char const* SHARED_KEYS[] = {
        "kA6",  // 배경
        "kA7",  // 바닥
        "kA17", // 바닥 선
        "kA25", // 중간 배경
        "kA18", // 글꼴
        "kS39", // 색 페이지
        "kA2",  // 시작 모드
        "kA4",  // 시작 속도
        "kA3",  // 미니
        "kA8",  // 듀얼
        "kA10", // 2인용
        "kA22", // 플랫포머
        "kA28", // 좌우 반전
        "kA29", // 화면 회전
        "kA11", // 뒤집힘
        "kA20", // 역주행
        "kA13", // 노래 시작 위치
        "kA15", // 페이드 인
        "kA16", // 페이드 아웃
    };

    constexpr char const* COLOR_KEY = "kS38";

    // 마지막으로 화면에 반영한 설정(색 제외). 같은 내용이면 다시 그리지 않는다.
    std::string g_appliedRest;

    // 방의 설정을 한 번이라도 받아서 반영했는지.
    //
    // 손님은 텅 빈 임시 레벨에서 시작한다. 그 상태로 자기 설정을 방에 올리면
    // 방장의 색깔과 배경이 손님의 기본값으로 덮인다. 실제로 그렇게 되고 있었다.
    // 그래서 손님은 방에서 받기 전까지 아무것도 올리지 않는다.
    bool g_haveRoomSettings = false;

    // --- 곡 내려받기 ---
    //
    // 방장이 쓰는 곡을 내가 안 갖고 있으면 번호만 맞고 소리는 나지 않는다.
    // 그래서 없는 곡은 알아서 받아온다.
    //
    // MusicDownloadManager에는 "다 받았다"고 알려주는 델리게이트가 있지만
    // 쓰지 않았다. 그걸 등록하려면 GD가 우리 객체의 메모리 배치를 자기 것과
    // 똑같이 가정하게 되는데, 게임이 조금만 바뀌어도 엉뚱한 곳을 읽는다.
    // 대신 이미 0.25초마다 돌고 있는 에디터 검사에서 상태만 물어본다.
    std::vector<int> g_wantedSongs;
    int g_songAttempts = 0;
    int g_songWait = 0;
    constexpr int MAX_SONG_ATTEMPTS = 3;
    // 0.25초짜리 검사 12번 = 3초. 곡 하나 받을 시간을 준다.
    constexpr int SONG_WAIT_TICKS = 12;

    void queueSong(int id) {
        if (id <= 0) return;
        if (std::find(g_wantedSongs.begin(), g_wantedSongs.end(), id) != g_wantedSongs.end()) return;
        g_wantedSongs.push_back(id);
    }

    // "123,456,789" 처럼 붙어 있는 곡 번호를 하나씩 꺼낸다.
    // 아직 에디터가 없을 때 받아둔 설정.
    //
    // 손님으로 방에 들어가면 서버가 곧바로 방의 설정과 레벨을 보내준다.
    // 그런데 그때 우리는 임시 레벨의 에디터를 "열라고 시켜둔" 상태일 뿐,
    // 화면은 다음 차례에나 바뀐다. 그 사이에 온 설정은 반영할 곳이 없다.
    //
    // 오브젝트는 줄 세워뒀다가 나중에 만들기 때문에 무사했는데, 설정만
    // 그 자리에서 버려지고 있었다. 배경도 색깔도 안 맞던 이유가 이것이다.
    // 짐은 다 들여놨는데 벽지 상자만 문 앞에 두고 온 셈이다.
    struct HeldSettings {
        bool waiting = false;
        std::string data;
        int songID = 0;
        int audioTrack = 0;
        std::string songList;
    };
    HeldSettings g_held;

    void queueSongList(std::string const& text) {
        size_t start = 0;
        while (start < text.size()) {
            auto comma = text.find(',', start);
            auto piece = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            queueSong(std::atoi(piece.c_str()));
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
    }
}

namespace coop {

    std::string levelSettingsString() {
        auto editor = LevelEditorLayer::get();
        if (!editor || !editor->m_levelSettings) return "";

        auto values = splitPairs(std::string(editor->m_levelSettings->getSaveString()));

        std::string out;
        for (auto key : SHARED_KEYS) {
            auto found = values.find(key);
            if (found == values.end()) continue;
            if (!out.empty()) out += ',';
            out += key;
            out += ',';
            out += found->second;
        }

        // 색깔은 양이 많아서 맨 뒤에 붙인다.
        g_colorCharsOut = 0;
        if (auto found = values.find(COLOR_KEY); found != values.end() && !found->second.empty()) {
            if (!out.empty()) out += ',';
            out += COLOR_KEY;
            out += ',';
            out += found->second;
            g_colorCharsOut = static_cast<int>(found->second.size());
        }
        return out;
    }

    int levelSongID() {
        auto editor = LevelEditorLayer::get();
        return (editor && editor->m_level) ? editor->m_level->m_songID : 0;
    }

    int levelAudioTrack() {
        auto editor = LevelEditorLayer::get();
        return (editor && editor->m_level) ? editor->m_level->m_audioTrack : 0;
    }

    // 어디가 끊겼는지 창에서 바로 보려고 모아 보여준다.
    // obj = 내 레벨의 오브젝트 수, cur = 커서 보낸/받은,
    // col = 색 글자수 보낸/받은, ok = 반영한 색 통로 수
    std::string diagnostics() {
        auto editor = LevelEditorLayer::get();
        auto objects = (editor && editor->m_objects) ? editor->m_objects->count() : 0u;

        auto waiting = pendingCount();
        if (waiting > 0) {
            return fmt::format("obj {}  loading {} more...", objects, waiting);
        }

        return fmt::format(
            "obj {}  cur {}/{}  col {}/{}  ok {}",
            objects,
            cursorsSent(), cursorsReceived(),
            g_colorCharsOut, g_colorCharsIn, g_colorsApplied
        );
    }

    bool roomSettingsApplied() {
        return g_haveRoomSettings;
    }

    void forgetAppliedSettings() {
        g_appliedRest.clear();
        g_haveRoomSettings = false;

        // 받아만 두고 아직 못 쓴 설정(g_held)은 여기서 버리지 않는다.
        //
        // 이 함수는 에디터를 새로 열 때도 불린다. 손님은 방에 들어간 직후
        // 설정을 받아 들고 있다가, 임시 에디터가 열리면 그때 쓴다. 그런데
        // 그 에디터가 열리는 바로 그 순간에 여기서 버려버리고 있었다.
        // 색깔이 안 오던 원인의 절반이 이것이다.
        //
        // 여기서 잊는 것은 "이미 화면에 반영한 것"까지다. 손에 들고 있는
        // 편지를 뜯기도 전에 버리면 안 된다.
        g_wantedSongs.clear();
        g_songAttempts = 0;
        g_songWait = 0;
    }

    std::string levelSongList() {
        auto editor = LevelEditorLayer::get();
        return (editor && editor->m_level) ? std::string(editor->m_level->m_songIDs) : "";
    }

    // 받아야 할 곡이 있으면 한 곡씩 처리한다. 에디터 검사에서 주기적으로 부른다.
    void tickSongDownload() {
        if (g_wantedSongs.empty()) return;

        // 검사는 0.25초마다 오지만 곡 하나 받는 데는 그보다 오래 걸린다.
        // 매번 다시 시도하면 채 시작하기도 전에 실패로 몰아붙이게 된다.
        if (g_songWait > 0) { --g_songWait; return; }

        auto manager = MusicDownloadManager::sharedState();
        if (!manager) return;

        auto id = g_wantedSongs.front();

        if (manager->isSongDownloaded(id)) {
            // 원래 갖고 있던 곡이면 아무 말 없이 넘어간다.
            // 우리가 받아온 곡일 때만 알린다.
            if (g_songAttempts > 0) {
                Notification::create(
                    fmt::format("Song {} is ready", id), NotificationIcon::Success
                )->show();
            }
            g_wantedSongs.erase(g_wantedSongs.begin());
            g_songAttempts = 0;
            return;
        }

        // 받는 중이면 기다린다.
        if (manager->isRunningActionForSongID(id)) {
            g_songWait = SONG_WAIT_TICKS;
            return;
        }

        if (g_songAttempts >= MAX_SONG_ATTEMPTS) {
            Notification::create(
                fmt::format("Could not get song {} - add it yourself", id),
                NotificationIcon::Warning
            )->show();
            g_wantedSongs.erase(g_wantedSongs.begin());
            g_songAttempts = 0;
            return;
        }

        if (g_songAttempts == 0) {
            Notification::create(
                fmt::format("Getting song {}...", id), NotificationIcon::Loading
            )->show();
        }
        ++g_songAttempts;
        g_songWait = SONG_WAIT_TICKS;

        // 곡 정보를 아직 모르면 정보부터 받아야 내려받을 주소를 알 수 있다.
        if (manager->getSongInfoObject(id)) {
            manager->downloadSong(id);
        } else {
            manager->getSongInfo(id, true);
        }
    }

    // 에디터가 생겼으면 들고 있던 설정을 반영한다. 주기적으로 부른다.
    void flushPendingSettings() {
        if (!g_held.waiting) return;
        if (!LevelEditorLayer::get()) return;

        auto held = g_held;
        g_held = {};
        applyLevelSettings(held.data, held.songID, held.audioTrack, held.songList);
    }

    void applyLevelSettings(
        std::string const& data, int songID, int audioTrack, std::string const& songList
    ) {
        auto editor = LevelEditorLayer::get();
        if (!editor) {
            // 아직 에디터가 없다. 버리지 말고 들고 있다가 생기면 반영한다.
            g_held = { true, data, songID, audioTrack, songList };
            return;
        }

        auto settings = editor->m_levelSettings;
        if (!settings || data.empty()) return;

        auto values = splitPairs(data);

        // 색을 뺀 나머지. 아래에서 화면을 다시 그릴지 정하는 데 쓴다.
        std::string rest;
        for (auto key : SHARED_KEYS) {
            if (auto found = values.find(key); found != values.end()) {
                rest += key;
                rest += ',';
                rest += found->second;
                rest += ',';
            }
        }

        // 눈에 보이는 것들
        settings->m_backgroundIndex   = intOf(values, "kA6", settings->m_backgroundIndex);
        settings->m_groundIndex       = intOf(values, "kA7", settings->m_groundIndex);
        settings->m_groundLineIndex   = intOf(values, "kA17", settings->m_groundLineIndex);
        settings->m_middleGroundIndex = intOf(values, "kA25", settings->m_middleGroundIndex);
        settings->m_fontIndex         = intOf(values, "kA18", settings->m_fontIndex);
        settings->m_colorPage         = intOf(values, "kS39", settings->m_colorPage);

        // 시작 상태와 게임 모드
        settings->m_startMode      = intOf(values, "kA2", settings->m_startMode);
        settings->m_startSpeed     = static_cast<Speed>(intOf(values, "kA4", static_cast<int>(settings->m_startSpeed)));
        settings->m_startMini      = boolOf(values, "kA3", settings->m_startMini);
        settings->m_startDual      = boolOf(values, "kA8", settings->m_startDual);
        settings->m_twoPlayerMode  = boolOf(values, "kA10", settings->m_twoPlayerMode);
        settings->m_platformerMode = boolOf(values, "kA22", settings->m_platformerMode);
        settings->m_mirrorMode     = boolOf(values, "kA28", settings->m_mirrorMode);
        settings->m_rotateGameplay = boolOf(values, "kA29", settings->m_rotateGameplay);
        settings->m_isFlipped      = boolOf(values, "kA11", settings->m_isFlipped);
        settings->m_reverseGameplay = boolOf(values, "kA20", settings->m_reverseGameplay);

        // 노래
        settings->m_songOffset = floatOf(values, "kA13", settings->m_songOffset);
        settings->m_fadeIn     = boolOf(values, "kA15", settings->m_fadeIn);
        settings->m_fadeOut    = boolOf(values, "kA16", settings->m_fadeOut);

        // 여기까지 왔으면 방의 설정을 실제로 반영한 것이다.
        g_haveRoomSettings = true;
        hushSettings();

        auto incomingColors = stringOf(values, COLOR_KEY);
        g_colorCharsIn = static_cast<int>(incomingColors.size());
        applyColors(editor, incomingColors);

        // 노래는 설정 문자열이 아니라 레벨에 붙어 있어서 따로 받는다.
        // 상대가 그 노래를 내려받아 두지 않았으면 번호만 맞고 소리는 나지 않는다.
        if (auto level = editor->m_level) {
            if (songID > 0 || audioTrack > 0) {
                level->m_songID = songID;
                level->m_audioTrack = audioTrack;
            }
            if (!songList.empty()) {
                level->m_songIDs = songList;
            }

            // 없는 곡은 받아둔다. 번호만 맞춰놓으면 소리가 나지 않는다.
            queueSong(songID);
            queueSongList(songList);
        }

        // levelSettingsUpdated()는 배경과 바닥을 통째로 다시 그린다.
        // 색만 바뀌었을 때까지 부르면 색을 고르는 동안 화면이 계속 깜빡인다.
        // 그래서 색이 아닌 항목이 실제로 달라졌을 때만 부른다.
        if (rest != g_appliedRest) {
            g_appliedRest = rest;

            // GD가 설정 창을 닫을 때 부르는 것과 같은 함수다.
            editor->levelSettingsUpdated();
        }
    }

}
