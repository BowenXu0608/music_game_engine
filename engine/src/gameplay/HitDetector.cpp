#include "HitDetector.h"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace {
constexpr float kPi    = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;
}

int HitDetector::angleToLane(float angle) const {
    // Reverse of LanotaRenderer: angle = PI/2 - (lane/trackCount) * 2PI
    float raw = (kPi * 0.5f - angle) / kTwoPi * m_trackCount;
    int lane = static_cast<int>(std::round(raw));
    // Wrap into [0, trackCount)
    lane = ((lane % m_trackCount) + m_trackCount) % m_trackCount;
    return lane;
}

void HitDetector::init(const ChartData& chart) {
    m_activeNotes = chart.notes;
    std::sort(m_activeNotes.begin(), m_activeNotes.end(),
              [](const NoteEvent& a, const NoteEvent& b) { return a.time < b.time; });
    m_nextNoteIndex = 0;
    m_activeHolds.clear();
}

std::vector<MissedNote> HitDetector::update(double songTime) {
    std::vector<MissedNote> missed;
    const double adj = songTime - static_cast<double>(m_audioOffset);

    auto it = std::remove_if(m_activeNotes.begin(), m_activeNotes.end(),
        [this, adj, &missed](const NoteEvent& note) {
            if (note.time < adj - 0.1) {
                MissedNote m;
                m.noteId = note.id;
                m.noteType = note.type;
                m.lane = -1;
                if (auto* tap = std::get_if<TapData>(&note.data))          m.lane = static_cast<int>(std::lround(tap->laneX));
                else if (auto* hold = std::get_if<HoldData>(&note.data))   m.lane = static_cast<int>(std::lround(hold->laneX));
                else if (auto* flick = std::get_if<FlickData>(&note.data)) m.lane = static_cast<int>(std::lround(flick->laneX));
                else if (auto* ring = std::get_if<LanotaRingData>(&note.data)) m.lane = angleToLane(ring->angle);
                missed.push_back(m);
                return true;
            }
            return false;
        });
    m_activeNotes.erase(it, m_activeNotes.end());

    return missed;
}

std::optional<HitResult> HitDetector::checkHit(int lane, double songTime) {
    const double adj = songTime - static_cast<double>(m_audioOffset);
    for (auto it = m_activeNotes.begin(); it != m_activeNotes.end(); ++it) {
        float timingDelta = static_cast<float>(it->time - adj);
        if (std::abs(timingDelta) <= 0.1f) {
            int noteLane = -1;
            if (std::holds_alternative<TapData>(it->data)) {
                noteLane = static_cast<int>(std::lround(std::get<TapData>(it->data).laneX));
            } else if (std::holds_alternative<HoldData>(it->data)) {
                noteLane = static_cast<int>(std::lround(std::get<HoldData>(it->data).laneX));
            } else if (std::holds_alternative<FlickData>(it->data)) {
                noteLane = static_cast<int>(std::lround(std::get<FlickData>(it->data).laneX));
            } else if (std::holds_alternative<LanotaRingData>(it->data)) {
                noteLane = angleToLane(std::get<LanotaRingData>(it->data).angle);
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

std::vector<HitResult> HitDetector::consumeDrags(int lane, double songTime) {
    std::vector<HitResult> results;
    const double adj = songTime - static_cast<double>(m_audioOffset);
    // Wider window than checkHit: ±0.15s so drags feel forgiving
    for (auto it = m_activeNotes.begin(); it != m_activeNotes.end(); ) {
        if (it->type != NoteType::Drag) { ++it; continue; }
        float timingDelta = static_cast<float>(it->time - adj);
        if (std::abs(timingDelta) > 0.15f) { ++it; continue; }
        int noteLane = -1;
        if (std::holds_alternative<TapData>(it->data))
            noteLane = static_cast<int>(std::lround(std::get<TapData>(it->data).laneX));
        if (noteLane != lane) { ++it; continue; }
        results.push_back({it->id, timingDelta, it->type});
        it = m_activeNotes.erase(it);
    }
    return results;
}

std::vector<HitDetector::AutoHit> HitDetector::autoPlayTick(double songTime) {
    std::vector<AutoHit> out;

    // 1) Consume every note whose time has arrived.
    for (auto it = m_activeNotes.begin(); it != m_activeNotes.end(); ) {
        if (it->time > songTime) { ++it; continue; }

        // Lane extraction (best effort)
        int lane = -1;
        if (auto* tap  = std::get_if<TapData>(&it->data))         lane = static_cast<int>(std::lround(tap->laneX));
        else if (auto* hd = std::get_if<HoldData>(&it->data))     lane = static_cast<int>(std::lround(hd->laneX));
        else if (auto* fl = std::get_if<FlickData>(&it->data))    lane = static_cast<int>(std::lround(fl->laneX));
        else if (auto* rg = std::get_if<LanotaRingData>(&it->data)) lane = angleToLane(rg->angle);

        const bool isHold  = std::holds_alternative<HoldData>(it->data);
        const bool isSlide = std::holds_alternative<TapData>(it->data) && it->type == NoteType::Slide;
        const bool isArc   = std::holds_alternative<ArcData>(it->data);

        if (isHold || isSlide || isArc) {
            ActiveHold hold{};
            hold.noteId        = it->id;
            hold.startTime     = songTime;
            hold.noteStartTime = it->time;
            hold.noteType      = it->type;
            if (isHold) {
                const auto& hd = std::get<HoldData>(it->data);
                hold.noteDuration = hd.duration;
                hold.lane         = static_cast<int>(std::lround(hd.laneX));
                hold.currentLane  = hold.lane;
                hold.holdData     = hd;
                hold.sampleOffsets.reserve(hd.samplePoints.size());
                for (const auto& sp : hd.samplePoints) hold.sampleOffsets.push_back(sp.tOffset);
                std::sort(hold.sampleOffsets.begin(), hold.sampleOffsets.end());
            } else if (isSlide) {
                const auto& td = std::get<TapData>(it->data);
                hold.noteDuration = td.duration;
                hold.lane         = static_cast<int>(std::lround(td.laneX));
                hold.currentLane  = hold.lane;
            } else {
                hold.noteDuration = std::get<ArcData>(it->data).duration;
            }
            m_activeHolds[it->id] = std::move(hold);
            out.push_back({HitResult{it->id, 0.f, it->type}, lane, false});
        } else {
            out.push_back({HitResult{it->id, 0.f, it->type}, lane, false});
        }

        it = m_activeNotes.erase(it);
    }

    // 2) Keep active-hold currentLane synced to expected — makes
    // consumeSampleTicks award Perfect for every tick.
    for (auto& [id, hold] : m_activeHolds) {
        if (hold.broken) continue;
        float tOff = static_cast<float>(songTime - hold.noteStartTime);
        if (tOff < 0.f) tOff = 0.f;
        if (hold.noteType == NoteType::Hold) {
            hold.currentLane = static_cast<int>(std::lround(evalHoldLaneAt(hold.holdData, tOff)));
        }
    }

    // 3) Finalize holds whose duration has elapsed.
    for (auto it = m_activeHolds.begin(); it != m_activeHolds.end(); ) {
        double endT = it->second.noteStartTime + it->second.noteDuration;
        if (songTime >= endT && !it->second.broken) {
            out.push_back({HitResult{it->first, 0.f, it->second.noteType},
                           it->second.lane, true});
            it = m_activeHolds.erase(it);
        } else {
            ++it;
        }
    }

    return out;
}

std::optional<HitResult> HitDetector::consumeNoteById(uint32_t noteId, double songTime) {
    const double adj = songTime - static_cast<double>(m_audioOffset);
    for (auto it = m_activeNotes.begin(); it != m_activeNotes.end(); ++it) {
        if (it->id != noteId) continue;
        float timingDelta = static_cast<float>(it->time - adj);
        if (std::abs(timingDelta) > 0.15f) return std::nullopt; // outside window
        HitResult result{it->id, timingDelta, it->type};
        m_activeNotes.erase(it);
        return result;
    }
    return std::nullopt;
}

std::optional<HitResult> HitDetector::checkHitPosition(glm::vec2 screenPos,
                                                         glm::vec2 screenSize,
                                                         double songTime) {
    const double adj = songTime - static_cast<double>(m_audioOffset);
    for (auto it = m_activeNotes.begin(); it != m_activeNotes.end(); ++it) {
        float timingDelta = static_cast<float>(it->time - adj);
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
    const double adj = songTime - static_cast<double>(m_audioOffset);

    for (auto it = m_activeNotes.begin(); it != m_activeNotes.end(); ++it) {
        float timingDelta = static_cast<float>(it->time - adj);
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
    const double adj = songTime - static_cast<double>(m_audioOffset);
    for (auto it = m_activeNotes.begin(); it != m_activeNotes.end(); ++it) {
        float timingDelta = static_cast<float>(it->time - adj);
        if (std::abs(timingDelta) > 0.1f) continue;

        if (std::holds_alternative<HoldData>(it->data)) {
            const auto& hd = std::get<HoldData>(it->data);
            int noteLane = static_cast<int>(std::lround(hd.laneX));
            if (noteLane == lane) {
                ActiveHold hold{};
                hold.noteId        = it->id;
                hold.startTime     = songTime;
                hold.noteStartTime = it->time;
                hold.noteDuration  = hd.duration;
                hold.noteType      = it->type;
                hold.lane          = noteLane;
                hold.currentLane   = noteLane;
                hold.holdData      = hd;
                hold.sampleOffsets.reserve(hd.samplePoints.size());
                for (const auto& sp : hd.samplePoints) hold.sampleOffsets.push_back(sp.tOffset);
                std::sort(hold.sampleOffsets.begin(), hold.sampleOffsets.end());
                uint32_t id = it->id;
                m_activeHolds[id] = std::move(hold);
                m_activeNotes.erase(it);
                return id;
            }
        }
    }
    return std::nullopt;
}

std::optional<HitResult> HitDetector::beginHoldById(uint32_t noteId, double songTime) {
    const double adj = songTime - static_cast<double>(m_audioOffset);
    for (auto it = m_activeNotes.begin(); it != m_activeNotes.end(); ++it) {
        if (it->id != noteId) continue;
        float timingDelta = static_cast<float>(it->time - adj);
        if (std::abs(timingDelta) > 0.15f) return std::nullopt;

        ActiveHold hold{};
        hold.noteId        = it->id;
        hold.startTime     = songTime;
        hold.noteStartTime = it->time;
        hold.noteType      = it->type;

        if (std::holds_alternative<HoldData>(it->data)) {
            const auto& hd = std::get<HoldData>(it->data);
            hold.noteDuration  = hd.duration;
            hold.lane          = static_cast<int>(std::lround(hd.laneX));
            hold.currentLane   = hold.lane;
            hold.holdData      = hd;
            hold.sampleOffsets.reserve(hd.samplePoints.size());
            for (const auto& sp : hd.samplePoints) hold.sampleOffsets.push_back(sp.tOffset);
            std::sort(hold.sampleOffsets.begin(), hold.sampleOffsets.end());
        } else if (std::holds_alternative<TapData>(it->data) && it->type == NoteType::Slide) {
            // Slides use TapData with a duration field
            const auto& td = std::get<TapData>(it->data);
            hold.noteDuration = td.duration;
            hold.lane         = static_cast<int>(std::lround(td.laneX));
            hold.currentLane  = hold.lane;
        } else {
            return std::nullopt;
        }

        HitResult result{it->id, timingDelta, it->type};
        m_activeHolds[it->id] = std::move(hold);
        m_activeNotes.erase(it);
        return result;
    }
    return std::nullopt;
}

std::optional<uint32_t> HitDetector::beginHoldPosition(glm::vec2 screenPos,
                                                         glm::vec2 screenSize,
                                                         double songTime) {
    const double adj = songTime - static_cast<double>(m_audioOffset);
    for (auto it = m_activeNotes.begin(); it != m_activeNotes.end(); ++it) {
        float timingDelta = static_cast<float>(it->time - adj);
        if (std::abs(timingDelta) > 0.1f) continue;

        if (std::holds_alternative<ArcData>(it->data)) {
            const auto& arc = std::get<ArcData>(it->data);
            // Map arc startPos (normalized -1..1) to screen space
            float noteScreenX = (arc.startPos.x * 0.5f + 0.5f) * screenSize.x;
            float noteScreenY = (1.0f - (arc.startPos.y * 0.5f + 0.5f)) * screenSize.y;
            glm::vec2 notePos{noteScreenX, noteScreenY};
            if (glm::length(screenPos - notePos) < HIT_RADIUS_PX) {
                ActiveHold hold{};
                hold.noteId        = it->id;
                hold.startTime     = songTime;
                hold.noteStartTime = it->time;
                hold.noteDuration  = arc.duration;
                hold.noteType      = it->type;
                uint32_t id = it->id;
                m_activeHolds[id] = std::move(hold);
                m_activeNotes.erase(it);
                return id;
            }
        }
    }
    return std::nullopt;
}

std::optional<HitResult> HitDetector::endHold(uint32_t noteId, double releaseTime) {
    auto holdIt = m_activeHolds.find(noteId);
    if (holdIt == m_activeHolds.end()) return std::nullopt;

    const ActiveHold& hold = holdIt->second;
    const double adjRelease = releaseTime - static_cast<double>(m_audioOffset);
    float expectedEnd  = static_cast<float>(hold.noteStartTime + hold.noteDuration);
    float timingDelta  = static_cast<float>(adjRelease) - expectedEnd;

    HitResult result{noteId, timingDelta, hold.noteType};

    // Remove from active notes
    m_activeNotes.erase(
        std::remove_if(m_activeNotes.begin(), m_activeNotes.end(),
            [noteId](const NoteEvent& n) { return n.id == noteId; }),
        m_activeNotes.end());

    m_activeHolds.erase(holdIt);
    return result;
}

std::vector<HoldSampleTick> HitDetector::consumeSampleTicks(double songTime) {
    constexpr int BREAK_AFTER = 2; // consecutive miss ticks → hold breaks

    std::vector<HoldSampleTick> ticks;
    for (auto& [id, hold] : m_activeHolds) {
        if (hold.broken) continue;
        while (hold.nextSampleIdx < hold.sampleOffsets.size()) {
            float tOff = hold.sampleOffsets[hold.nextSampleIdx];
            double absT = hold.noteStartTime + tOff;
            if (songTime < absT) break;

            // Bandori-style gate: at the tick's time, the player must be on
            // the lane the hold is *currently expected to be at*. We round
            // the smoothstep value to the nearest lane.
            int expected = static_cast<int>(std::lround(evalHoldLaneAt(hold.holdData, tOff)));
            bool ok = (hold.currentLane == expected);

            ticks.push_back({id, expected, ok});
            if (ok) {
                hold.consecutiveMissedTicks = 0;
            } else {
                hold.consecutiveMissedTicks++;
                if (hold.consecutiveMissedTicks >= BREAK_AFTER) {
                    hold.broken = true;
                    hold.nextSampleIdx++;
                    break;
                }
            }
            hold.nextSampleIdx++;
        }
    }
    return ticks;
}

void HitDetector::updateHoldLane(uint32_t noteId, int lane) {
    auto it = m_activeHolds.find(noteId);
    if (it == m_activeHolds.end()) return;
    it->second.currentLane = lane;
}

std::vector<uint32_t> HitDetector::consumeBrokenHolds() {
    std::vector<uint32_t> out;
    for (auto it = m_activeHolds.begin(); it != m_activeHolds.end(); ) {
        if (it->second.broken) {
            out.push_back(it->first);
            // Also remove the underlying note so it isn't double-counted as
            // a Miss by the timing-window sweep in update().
            uint32_t id = it->first;
            m_activeNotes.erase(
                std::remove_if(m_activeNotes.begin(), m_activeNotes.end(),
                    [id](const NoteEvent& n) { return n.id == id; }),
                m_activeNotes.end());
            it = m_activeHolds.erase(it);
        } else {
            ++it;
        }
    }
    return out;
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

const HitDetector::ActiveHold* HitDetector::getActiveHold(uint32_t noteId) const {
    auto it = m_activeHolds.find(noteId);
    return (it != m_activeHolds.end()) ? &it->second : nullptr;
}

std::vector<uint32_t> HitDetector::activeHoldIds() const {
    std::vector<uint32_t> ids;
    ids.reserve(m_activeHolds.size());
    for (auto& [id, _] : m_activeHolds) ids.push_back(id);
    return ids;
}
