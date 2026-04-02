#pragma once
#include "JudgmentSystem.h"

class ScoreTracker {
public:
    void onJudgment(Judgment j) {
        switch (j) {
            case Judgment::Perfect: m_score += 1000; m_combo++; break;
            case Judgment::Good:    m_score += 500;  m_combo++; break;
            case Judgment::Bad:     m_score += 100;  resetCombo(); break;
            case Judgment::Miss:    resetCombo(); break;
        }
        if (m_combo > m_maxCombo) m_maxCombo = m_combo;
    }

    void reset() { m_score = 0; m_combo = 0; m_maxCombo = 0; }
    int getScore() const { return m_score; }
    int getCombo() const { return m_combo; }
    int getMaxCombo() const { return m_maxCombo; }

private:
    void resetCombo() { m_combo = 0; }

    int m_score = 0;
    int m_combo = 0;
    int m_maxCombo = 0;
};
