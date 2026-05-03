#pragma once
#include <string>

class AudioEngine;
class Renderer;
class GameClock;
class PlayerSettings;
class MaterialAssetLibrary;
class InputManager;
class ImGuiLayer;
class ScoreTracker;
class JudgmentSystem;
class HitDetector;
class GameModeRenderer;
struct GameModeConfig;
struct SongInfo;
enum class Difficulty;

class IPlayerEngine {
public:
    virtual ~IPlayerEngine() = default;

    virtual AudioEngine&          audio()           = 0;
    virtual Renderer&             renderer()        = 0;
    virtual GameClock&            clock()           = 0;
    virtual PlayerSettings&       playerSettings()  = 0;
    virtual MaterialAssetLibrary& materialLibrary() = 0;
    virtual InputManager&         inputManager()    = 0;
    virtual ImGuiLayer*           imguiLayer()      = 0;

    virtual ScoreTracker&         score()           = 0;
    virtual JudgmentSystem&       judgment()        = 0;
    virtual HitDetector&          hitDetector()     = 0;
    virtual GameModeRenderer*     activeMode()      = 0;
    virtual const GameModeConfig& gameplayConfig() const = 0;

    virtual bool  isTestMode()         const = 0;
    virtual bool  isTestTransitioning() const = 0;
    virtual float testTransProgress()   const = 0;

    // Player flow — launching gameplay from music selection.
    virtual void launchGameplay(const SongInfo& song, Difficulty difficulty,
                                const std::string& projectPath, bool autoPlay) = 0;
    // Player flow — exit current gameplay session, return to selection.
    virtual void exitGameplay() = 0;

    // Player flow — equivalent of pressing Esc during gameplay. Toggles
    // pause while playing; exits if the results overlay is up.
    virtual void requestStop() = 0;
};
