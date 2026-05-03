#include "AndroidEngineAdapter.h"
#include "AndroidEngine.h"
#include "game/screens/MusicSelectionView.h"  // SongInfo, Difficulty
#include "game/modes/GameModeRenderer.h"
#include <android/log.h>
#include <algorithm>

AndroidEngineAdapter::AndroidEngineAdapter(AndroidEngine& e) : m_engine(e) {}

AudioEngine&          AndroidEngineAdapter::audio()           { return m_engine.m_audio; }
Renderer&             AndroidEngineAdapter::renderer()        { return m_engine.m_renderer; }
GameClock&            AndroidEngineAdapter::clock()           { return m_engine.m_clock; }
PlayerSettings&       AndroidEngineAdapter::playerSettings()  { return m_engine.m_playerSettings; }
MaterialAssetLibrary& AndroidEngineAdapter::materialLibrary() { return m_engine.m_materialLibrary; }
InputManager&         AndroidEngineAdapter::inputManager()    { return m_engine.m_input; }

ScoreTracker&         AndroidEngineAdapter::score()           { return m_engine.m_score; }
JudgmentSystem&       AndroidEngineAdapter::judgment()        { return m_engine.m_judgment; }
HitDetector&          AndroidEngineAdapter::hitDetector()     { return m_engine.m_hitDetector; }
GameModeRenderer*     AndroidEngineAdapter::activeMode()      { return m_engine.m_activeMode.get(); }
const GameModeConfig& AndroidEngineAdapter::gameplayConfig()  const { return m_engine.m_gameplayConfig; }

void AndroidEngineAdapter::launchGameplay(const SongInfo& song, Difficulty difficulty,
                                          const std::string& /*projectPath*/, bool autoPlay) {
    // Find the matching SongEntry in AndroidEngine::m_songs by name + audio
    // and dispatch through the existing index-based startGameplay path.
    // Phase G replaces m_songs with MusicSelectionView::sets() and lets the
    // launcher take SongInfo directly.
    auto& songs = m_engine.m_songs;
    auto it = std::find_if(songs.begin(), songs.end(),
        [&](const AndroidEngine::SongEntry& e) {
            return e.name == song.name && e.audioFile == song.audioFile;
        });
    if (it == songs.end()) {
        __android_log_print(ANDROID_LOG_WARN, "AndroidEngineAdapter",
            "launchGameplay: no matching song for '%s'", song.name.c_str());
        return;
    }
    int idx = static_cast<int>(it - songs.begin());
    // Difficulty is currently stored only in MusicSelectionView; AndroidEngine
    // assumes Hard. Phase G honours difficulty by routing through the View.
    (void)difficulty;
    m_engine.startGameplay(idx, autoPlay);
}

void AndroidEngineAdapter::exitGameplay() {
    m_engine.exitGameplay();
}

void AndroidEngineAdapter::requestStop() {
    m_engine.requestStop();
}
