#include "HitDetector.h"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

void HitDetector::init(const ChartData& chart) {
    m_activeNotes = chart.notes;
    std::sort(m_activeNotes.begin(), m_activeNotes.end(),
              [](const NoteEvent& a, const NoteEvent& b) { return a.time < b.time; });
    m_nextNoteIndex = 0;
    m_activeHolds.clear();
}

std::vector<MissedNote> HitDetector::update(double songTime) {
    std::vector<MissedNote> missed;

    auto it = std::remove_if(m_activeNotes.begin(), m_activeNotes.end(),
        [songTime, &missed](const NoteEvent& note) {
            if (note.time < songTime - 0.1) {
                MissedNote m;
                m.noteId = note.id;
                m.noteType = note.type;
                m.lane = -1;
                if (auto* tap = std::get_if<TapData>(&note.data))        m.lane = (int)tap->laneX;
                else if (auto* hold = std::get_if<HoldData>(&note.data)) m.lane = (int)hold->laneX;
                else if (auto* flick = std::get_if<FlickData>(&note.data)) m.lane = (int)flick->laneX;
                missed.push_back(m);
                return true;
            }
            return false;
        });
    m_activeNotes.erase(it, m_activeNotes.end());

    return missed;
}

std::optional<HitResult> HitDetector::checkHit(int lane, double songTime) {
    for (auto it = m_activeNotes.begin(); it != m_activeNotes.end(); ++it) {
        float timingDelta = static_cast<float>(it->time - songTime);
        if (std::abs(timingDelta) <= 0.1f) {
            int noteLane = -1;
            if (std::holds_alternative<TapData>(it->data)) {
                noteLane = static_cast<int>(std::get<TapData>(it->data).laneX);
            } else if (std::holds_alternative<HoldData>(it->data)) {
                noteLane = static_cast<int>(std::get<HoldData>(it->data).laneX);
            } else if (std::holds_alternative<FlickData>(it->data)) {
                noteLane = static_cast<int>(std::get<FlickData>(it->data).laneX);
            }

            if (noteLane == lane) {
                HitResult result{it->id, timingDelta, it->type};
                m_activeNotes.erase(it);
                return result;
            }
        }
    }
    return std::nullopt;
}

std::optional<HitResult> HitDetector::checkHitPosition(glm::vec2 screenPos,
                                                         glm::vec2 screenSize,
                                                         double songTime) {
    for (auto it = m_activeNotes.begin(); it != m_activeNotes.end(); ++it) {
        float timingDelta = static_cast<float>(it->time - songTime);
        if (std::abs(timingDelta) > 0.1f) continue;

        if (std::holds_alternative<TapData>(it->data)) {
            float laneX = std::get<TapData>(it->data).laneX;
            // Arcaea has 5 ground lanes (0-4); map to screen X
            float noteScreenX = (laneX / 4.0f) * screenSize.x;
            float noteScreenY = screenSize.y * 0.85f; // near bottom hit zone
            glm::vec2 notePos{noteScreenX, noteScreenY};
            if (glm::length(screenPos - notePos) < HIT_RADIUS_PX) {
                HitResult result{it->id, timingDelta, it->type};
                m_activeNotes.erase(it);
                return result;
            }
        }
    }
    return std::nullopt;
}

std::optional<HitResult> HitDetector::checkHitPhigros(glm::vec2 screenPos,
                                                        glm::vec2 lineOrigin,
                                                        float lineRotation,
                                                        double songTime) {
    glm::vec2 lineDir {std::cos(lineRotation), std::sin(lineRotation)};
    glm::vec2 lineNorm{-lineDir.y, lineDir.x};
    glm::vec2 delta = screenPos - lineOrigin;

    float perpDist = std::abs(glm::dot(delta, lineNorm));
    if (perpDist > HIT_RADIUS_PX) return std::nullopt;

    float alongDist = glm::dot(delta, lineDir);

    for (auto it = m_activeNotes.begin(); it != m_activeNotes.end(); ++it) {
        float timingDelta = static_cast<float>(it->time - songTime);
        if (std::abs(timingDelta) > 0.1f) continue;

        if (std::holds_alternative<PhigrosNoteData>(it->data)) {
            float posOnLine = std::get<PhigrosNoteData>(it->data).posOnLine;
            if (std::abs(alongDist - posOnLine) < HIT_RADIUS_PX) {
                HitResult result{it->id, timingDelta, it->type};
                m_activeNotes.erase(it);
                return result;
            }
        }
    }
    return std::nullopt;
}

std::optional<uint32_t> HitDetector::beginHold(int lane, double songTime) {
    for (auto it = m_activeNotes.begin(); it != m_activeNotes.end(); ++it) {
        float timingDelta = static_cast<float>(it->time - songTime);
        if (std::abs(timingDelta) > 0.1f) continue;

        if (std::holds_alternative<HoldData>(it->data)) {
            int noteLane = static_cast<int>(std::get<HoldData>(it->data).laneX);
            if (noteLane == lane) {
                float duration = std::get<HoldData>(it->data).duration;
                ActiveHold hold{it->id, songTime, it->time, duration, it->type, {}};
                m_activeHolds[it->id] = std::move(hold);
                return it->id;
            }
        }
    }
    return std::nullopt;
}

std::optional<uint32_t> HitDetector::beginHoldPosition(glm::vec2 screenPos,
                                                         glm::vec2 screenSize,
                                                         double songTime) {
    for (auto it = m_activeNotes.begin(); it != m_activeNotes.end(); ++it) {
        float timingDelta = static_cast<float>(it->time - songTime);
        if (std::abs(timingDelta) > 0.1f) continue;

        if (std::holds_alternative<ArcData>(it->data)) {
            const auto& arc = std::get<ArcData>(it->data);
            // Map arc startPos (normalized -1..1) to screen space
            float noteScreenX = (arc.startPos.x * 0.5f + 0.5f) * screenSize.x;
            float noteScreenY = (1.0f - (arc.startPos.y * 0.5f + 0.5f)) * screenSize.y;
            glm::vec2 notePos{noteScreenX, noteScreenY};
            if (glm::length(screenPos - notePos) < HIT_RADIUS_PX) {
                ActiveHold hold{it->id, songTime, it->time, arc.duration, it->type, {}};
                m_activeHolds[it->id] = std::move(hold);
                return it->id;
            }
        }
    }
    return std::nullopt;
}

std::optional<HitResult> HitDetector::endHold(uint32_t noteId, double releaseTime) {
    auto holdIt = m_activeHolds.find(noteId);
    if (holdIt == m_activeHolds.end()) return std::nullopt;

    const ActiveHold& hold = holdIt->second;
    float expectedEnd  = static_cast<float>(hold.noteStartTime + hold.noteDuration);
    float timingDelta  = static_cast<float>(releaseTime) - expectedEnd;

    HitResult result{noteId, timingDelta, hold.noteType};

    // Remove from active notes
    m_activeNotes.erase(
        std::remove_if(m_activeNotes.begin(), m_activeNotes.end(),
            [noteId](const NoteEvent& n) { return n.id == noteId; }),
        m_activeNotes.end());

    m_activeHolds.erase(holdIt);
    return result;
}

void HitDetector::updateSlide(uint32_t noteId, glm::vec2 currentPos, double /*songTime*/) {
    auto it = m_activeHolds.find(noteId);
    if (it != m_activeHolds.end())
        it->second.positionSamples.push_back(currentPos);
}

float HitDetector::getSlideAccuracy(uint32_t noteId) const {
    auto it = m_activeHolds.find(noteId);
    if (it == m_activeHolds.end() || it->second.positionSamples.empty())
        return 0.05f; // default: near-perfect

    // Placeholder: return a fixed good accuracy until arc evaluation is wired in
    // TODO: compare samples against evaluated arc positions
    return 0.05f;
}
