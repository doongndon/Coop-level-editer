#include "Coop.hpp"

#include <cstdlib>
#include <string>
#include <unordered_map>

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
    void applyColors(LevelEditorLayer* editor, std::string const& blob) {
        auto manager = editor->m_effectManager;
        if (!manager || blob.empty()) return;

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

                // 이미 있는 색 통로를 그 자리에서 고친다. 새로 만들어 갈아끼우면
                // 누가 그 통로를 들고 있는지 알 수 없어 위험하다.
                if (channel != 0) {
                    if (auto action = manager->getColorAction(channel)) {
                        action->setupFromString(entry);
                    }
                }
            }

            if (bar == std::string::npos) break;
            start = bar + 1;
        }

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
        if (auto found = values.find(COLOR_KEY); found != values.end() && !found->second.empty()) {
            if (!out.empty()) out += ',';
            out += COLOR_KEY;
            out += ',';
            out += found->second;
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

    void forgetAppliedSettings() {
        g_appliedRest.clear();
    }

    void applyLevelSettings(std::string const& data, int songID, int audioTrack) {
        auto editor = LevelEditorLayer::get();
        if (!editor) return;

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

        applyColors(editor, stringOf(values, COLOR_KEY));

        // 노래는 설정 문자열이 아니라 레벨에 붙어 있어서 따로 받는다.
        // 상대가 그 노래를 내려받아 두지 않았으면 번호만 맞고 소리는 나지 않는다.
        if (auto level = editor->m_level) {
            if (songID > 0 || audioTrack > 0) {
                level->m_songID = songID;
                level->m_audioTrack = audioTrack;
            }
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
