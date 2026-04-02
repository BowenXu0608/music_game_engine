#pragma once
#include <chrono>

// DSP-clock-aware game clock.
// Wall clock drives dt/animation; audio position drives note timing.
class GameClock {
public:
    void start() {
        m_startTime = std::chrono::high_resolution_clock::now();
        m_lastTime  = m_startTime;
        m_songTime  = 0.0;
        m_running   = true;
    }

    void pause()  { m_running = false; }
    void resume() { m_running = true; m_lastTime = now(); }

    // Call once per frame — returns wall-clock dt
    float tick() {
        auto cur = now();
        float dt = std::chrono::duration<float>(cur - m_lastTime).count();
        m_lastTime = cur;
        if (m_running) m_wallTime += dt;
        return m_running ? dt : 0.f;
    }

    // Override song time from DSP clock (call after audio position query)
    void setSongTime(double t) { m_songTime = t; }

    double songTime()  const { return m_songTime; }
    float  wallTime()  const { return m_wallTime; }
    bool   isRunning() const { return m_running; }

private:
    using Clock = std::chrono::high_resolution_clock;
    Clock::time_point now() const { return Clock::now(); }

    Clock::time_point m_startTime;
    Clock::time_point m_lastTime;
    double m_songTime = 0.0;
    float  m_wallTime = 0.f;
    bool   m_running  = false;
};
