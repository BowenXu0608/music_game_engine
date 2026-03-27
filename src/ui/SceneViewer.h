#pragma once
#include <string>

class Engine;

class SceneViewer {
public:
    void render(Engine& engine);

    bool isPlaying() const { return m_playing; }
    void setPlaying(bool playing) { m_playing = playing; }

private:
    bool m_playing = false;
    bool m_showStats = true;
    float m_songTime = 0.0f;
};
