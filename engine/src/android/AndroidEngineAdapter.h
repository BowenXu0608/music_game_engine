#pragma once
#include "engine/IPlayerEngine.h"

class AndroidEngine;

// IPlayerEngine implementation that wraps AndroidEngine. Used by shared
// game-side View classes (StartScreenView, MusicSelectionView,
// GameplayHudView, ResultsView) so they can drive the player loop on
// Android without ever touching desktop Engine / editor code.
class AndroidEngineAdapter final : public IPlayerEngine {
public:
    explicit AndroidEngineAdapter(AndroidEngine& e);
    ~AndroidEngineAdapter() override = default;

    AudioEngine&          audio()           override;
    Renderer&             renderer()        override;
    GameClock&            clock()           override;
    PlayerSettings&       playerSettings()  override;
    MaterialAssetLibrary& materialLibrary() override;
    InputManager&         inputManager()    override;
    ImGuiLayer*           imguiLayer()      override { return nullptr; }

    ScoreTracker&         score()           override;
    JudgmentSystem&       judgment()        override;
    HitDetector&          hitDetector()     override;
    GameModeRenderer*     activeMode()      override;
    const GameModeConfig& gameplayConfig()  const override;

    bool  isTestMode()         const override { return false; }
    bool  isTestTransitioning() const override { return m_transitioning; }
    float testTransProgress()   const override { return m_transProgress; }

    void launchGameplay(const SongInfo& song, Difficulty difficulty,
                        const std::string& projectPath, bool autoPlay) override;
    void exitGameplay() override;
    void requestStop() override;

private:
    AndroidEngine& m_engine;
    bool   m_transitioning = false;
    float  m_transProgress = 0.f;
};
