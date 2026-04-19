#include "ChartStyle.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <variant>

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

int clampLane(float laneX, int trackCount) {
    if (trackCount < 1) return 0;
    int idx = (int)std::round(laneX);
    if (idx < 0) idx = 0;
    if (idx >= trackCount) idx = trackCount - 1;
    return idx;
}

// Extract integer lane index from a NoteEvent's variant payload, or return -1
// if the variant has no laneX field (Arc, Ring, etc.).
int noteEventLane(const NoteEvent& n, int trackCount) {
    return std::visit([trackCount](const auto& d) -> int {
        using T = std::decay_t<decltype(d)>;
        if constexpr (std::is_same_v<T, TapData> ||
                      std::is_same_v<T, HoldData> ||
                      std::is_same_v<T, FlickData>) {
            return clampLane(d.laneX, trackCount);
        } else {
            return -1;
        }
    }, n.data);
}

bool isEligibleType(NoteType t) {
    return t == NoteType::Tap || t == NoteType::Hold || t == NoteType::Flick;
}

bool isEligibleEditorType(EditorNoteType t) {
    return t == EditorNoteType::Tap || t == EditorNoteType::Hold ||
           t == EditorNoteType::Flick;
}

// Lane inference mirror of SongEditor::inferLaneFromCentroid.
int laneFromCentroid(float centroid, int trackCount, int prevLane, bool antiJack) {
    if (trackCount <= 0) return -1;
    if (trackCount == 1) return 0;
    int lane = (int)std::floor(std::clamp(centroid, 0.f, 0.9999f) * trackCount);
    if (lane < 0) lane = 0;
    if (lane >= trackCount) lane = trackCount - 1;
    if (antiJack && lane == prevLane) {
        if (lane + 1 < trackCount)       lane += 1;
        else if (lane - 1 >= 0)          lane -= 1;
    }
    return lane;
}

// Peak NPS in a 2s sliding window over sorted times.
float peakNps2sFromTimes(const std::vector<double>& sortedTimes) {
    const double windowSec = 2.0;
    int best = 0;
    size_t head = 0;
    for (size_t tail = 0; tail < sortedTimes.size(); ++tail) {
        while (head < sortedTimes.size() &&
               sortedTimes[head] < sortedTimes[tail] - windowSec)
            ++head;
        int count = (int)(tail - head + 1);
        if (count > best) best = count;
    }
    return (float)best / (float)windowSec;
}

} // namespace

StyleFingerprint computeFingerprint(const ChartData& chart, int trackCount) {
    StyleFingerprint fp;
    fp.trackCount = trackCount;
    fp.laneHist.assign(trackCount > 0 ? trackCount : 1, 0.f);

    int tap = 0, hold = 0, flick = 0;
    int sameLanePairs = 0, transitionPairs = 0;
    int prevLane = -1;
    double sumDLane = 0.0;
    double maxTime = 0.0;
    std::vector<double> sortedTimes;
    sortedTimes.reserve(chart.notes.size());

    for (const auto& n : chart.notes) {
        if (n.time > maxTime) maxTime = n.time;
        if (!isEligibleType(n.type)) continue;
        int lane = noteEventLane(n, trackCount);
        if (lane < 0) continue;

        switch (n.type) {
            case NoteType::Tap:   ++tap;   break;
            case NoteType::Hold:  ++hold;  break;
            case NoteType::Flick: ++flick; break;
            default: continue;
        }
        ++fp.noteCount;
        if ((size_t)lane < fp.laneHist.size()) fp.laneHist[lane] += 1.f;
        sortedTimes.push_back(n.time);

        if (prevLane >= 0) {
            ++transitionPairs;
            if (lane == prevLane) ++sameLanePairs;
            sumDLane += std::abs(lane - prevLane);
        }
        prevLane = lane;
    }

    if (fp.noteCount > 0) {
        fp.tapPct   = (float)tap   / fp.noteCount;
        fp.holdPct  = (float)hold  / fp.noteCount;
        fp.flickPct = (float)flick / fp.noteCount;
        for (auto& v : fp.laneHist) v /= (float)fp.noteCount;
    }
    fp.durationSec = (float)maxTime;
    fp.avgNps = (maxTime > 0.0) ? (float)(fp.noteCount / maxTime) : 0.f;
    std::sort(sortedTimes.begin(), sortedTimes.end());
    fp.peakNps2s = peakNps2sFromTimes(sortedTimes);
    if (transitionPairs > 0) {
        fp.meanDLane = (float)(sumDLane / transitionPairs);
        fp.sameLaneRepeatRate = (float)sameLanePairs / (float)transitionPairs;
    }
    return fp;
}

StyleFingerprint computeFingerprintFromEditor(const std::vector<EditorNote>& notes,
                                               int trackCount, float durationSec) {
    StyleFingerprint fp;
    fp.trackCount = trackCount;
    fp.laneHist.assign(trackCount > 0 ? trackCount : 1, 0.f);

    int tap = 0, hold = 0, flick = 0;
    int sameLanePairs = 0, transitionPairs = 0;
    int prevLane = -1;
    double sumDLane = 0.0;
    float maxTime = 0.f;
    std::vector<double> sortedTimes;
    sortedTimes.reserve(notes.size());

    for (const auto& n : notes) {
        if (n.time > maxTime) maxTime = n.time;
        if (!isEligibleEditorType(n.type)) continue;
        int lane = n.track;
        if (lane < 0 || lane >= trackCount) continue;
        switch (n.type) {
            case EditorNoteType::Tap:   ++tap;   break;
            case EditorNoteType::Hold:  ++hold;  break;
            case EditorNoteType::Flick: ++flick; break;
            default: continue;
        }
        ++fp.noteCount;
        fp.laneHist[lane] += 1.f;
        sortedTimes.push_back(n.time);
        if (prevLane >= 0) {
            ++transitionPairs;
            if (lane == prevLane) ++sameLanePairs;
            sumDLane += std::abs(lane - prevLane);
        }
        prevLane = lane;
    }

    if (fp.noteCount > 0) {
        fp.tapPct   = (float)tap   / fp.noteCount;
        fp.holdPct  = (float)hold  / fp.noteCount;
        fp.flickPct = (float)flick / fp.noteCount;
        for (auto& v : fp.laneHist) v /= (float)fp.noteCount;
    }
    fp.durationSec = (durationSec > 0.f) ? durationSec : maxTime;
    fp.avgNps = (fp.durationSec > 0.f) ? (float)fp.noteCount / fp.durationSec : 0.f;
    std::sort(sortedTimes.begin(), sortedTimes.end());
    fp.peakNps2s = peakNps2sFromTimes(sortedTimes);
    if (transitionPairs > 0) {
        fp.meanDLane = (float)(sumDLane / transitionPairs);
        fp.sameLaneRepeatRate = (float)sameLanePairs / (float)transitionPairs;
    }
    return fp;
}

std::string describeFingerprint(const StyleFingerprint& fp) {
    std::string s;
    char buf[256];

    std::snprintf(buf, sizeof(buf),
        "notes=%d  lanes=%d  dur=%.1fs  avg_nps=%.2f  peak_nps_2s=%.2f\n",
        fp.noteCount, fp.trackCount, fp.durationSec, fp.avgNps, fp.peakNps2s);
    s += buf;

    std::snprintf(buf, sizeof(buf),
        "types:  tap=%.0f%%  hold=%.0f%%  flick=%.0f%%\n",
        fp.tapPct * 100.f, fp.holdPct * 100.f, fp.flickPct * 100.f);
    s += buf;

    std::snprintf(buf, sizeof(buf),
        "motion: mean_dLane=%.2f  same_lane_repeat=%.0f%%\n",
        fp.meanDLane, fp.sameLaneRepeatRate * 100.f);
    s += buf;

    s += "lanes:  ";
    for (size_t i = 0; i < fp.laneHist.size(); ++i) {
        std::snprintf(buf, sizeof(buf), "%zu:%.0f%%  ", i, fp.laneHist[i] * 100.f);
        s += buf;
    }
    s += "\n";
    return s;
}

std::vector<StyleCandidate> enumerateStyleCandidates(
    const std::string& projectPath,
    const std::vector<MusicSetInfo>& sets,
    const GameModeConfig& currentMode,
    const std::string& currentSongName,
    Difficulty currentDifficulty) {

    std::vector<StyleCandidate> out;

    // Mode filter: require same game-mode family (dropNotes / circle / scanLine)
    // and same dimension for dropNotes. trackCount mismatches are allowed - the
    // lane-rebalance path already falls back to uniform distribution when the
    // reference's laneHist size differs from the current trackCount. The label
    // exposes the ref's trackCount so the user can judge compatibility.
    auto matchMode = [&](const GameModeConfig& gm) -> bool {
        if (gm.type != currentMode.type) return false;
        if (gm.type == GameModeType::DropNotes &&
            gm.dimension != currentMode.dimension) return false;
        return true;
    };

    struct Slot { const std::string* rel; const char* diffLabel; Difficulty diff; };

    for (const auto& set : sets) {
        for (const auto& song : set.songs) {
            if (!matchMode(song.gameMode)) continue;
            Slot slots[] = {
                {&song.chartEasy,   "Easy",   Difficulty::Easy},
                {&song.chartMedium, "Medium", Difficulty::Medium},
                {&song.chartHard,   "Hard",   Difficulty::Hard},
            };
            for (const auto& slot : slots) {
                if (!slot.rel || slot.rel->empty()) continue;
                if (song.name == currentSongName && slot.diff == currentDifficulty)
                    continue;
                StyleCandidate cand;
                char tag[32];
                std::snprintf(tag, sizeof(tag), " (%dt)", song.gameMode.trackCount);
                cand.label      = set.name + " / " + song.name + " [" +
                                  slot.diffLabel + "]" + tag;
                cand.absPath    = (fs::path(projectPath) / *slot.rel).string();
                cand.trackCount = song.gameMode.trackCount;
                out.push_back(std::move(cand));
            }
        }
    }
    return out;
}

// -- applyStyleTransfer ------------------------------------------------------

namespace {

// Per-note working data for the rebalancer.
struct NoteAux {
    size_t       idx;
    EditorNoteType origType;
    int          origLane;
    MarkerFeature feat;     // nearest marker or defaults
    bool         eligible;  // Tap/Hold/Flick
    bool         singleLane; // eligible AND (not Hold with cross-lane geometry)
};

MarkerFeature nearestFeature(float time,
                              const std::vector<float>& markerTimes,
                              const std::vector<MarkerFeature>& features) {
    MarkerFeature f;  // defaults: strength=0.5, sustain=0, centroid=0.5
    if (markerTimes.empty() || features.empty()) return f;
    auto it = std::lower_bound(markerTimes.begin(), markerTimes.end(), time);
    size_t i;
    if (it == markerTimes.end()) {
        i = markerTimes.size() - 1;
    } else if (it == markerTimes.begin()) {
        i = 0;
    } else {
        size_t hi = (size_t)(it - markerTimes.begin());
        size_t lo = hi - 1;
        i = (time - markerTimes[lo] <= markerTimes[hi] - time) ? lo : hi;
    }
    if (i < features.size()) return features[i];
    return f;
}

bool holdIsCrossLane(const EditorNote& n) {
    if (!n.waypoints.empty()) {
        int first = n.waypoints.front().lane;
        for (const auto& w : n.waypoints)
            if (w.lane != first) return true;
        return false;
    }
    return (n.endTrack >= 0 && n.endTrack != n.track);
}

} // namespace

StyleTransferStats applyStyleTransfer(std::vector<EditorNote>& notes,
                                       const std::vector<MarkerFeature>& features,
                                       const std::vector<float>& markerTimes,
                                       int trackCount,
                                       const StyleFingerprint& target,
                                       const StyleTransferOptions& opts) {
    StyleTransferStats stats;
    if (trackCount <= 0 || notes.empty()) return stats;

    // -- Aux table ---------------------------------------------------------
    std::vector<NoteAux> aux;
    aux.reserve(notes.size());
    for (size_t i = 0; i < notes.size(); ++i) {
        const auto& n = notes[i];
        NoteAux a;
        a.idx        = i;
        a.origType   = n.type;
        a.origLane   = n.track;
        a.feat       = nearestFeature(n.time, markerTimes, features);
        a.eligible   = isEligibleEditorType(n.type);
        a.singleLane = a.eligible &&
                       !(n.type == EditorNoteType::Hold && holdIsCrossLane(n));
        aux.push_back(a);
    }

    // Eligible-index list for type rebalance.
    std::vector<size_t> eligibleIdx;
    int curTap = 0, curHold = 0, curFlick = 0;
    for (const auto& a : aux) {
        if (!a.eligible) { ++stats.skipped; continue; }
        eligibleIdx.push_back(a.idx);
        switch (notes[a.idx].type) {
            case EditorNoteType::Tap:   ++curTap;   break;
            case EditorNoteType::Hold:  ++curHold;  break;
            case EditorNoteType::Flick: ++curFlick; break;
            default: break;
        }
    }
    int eligibleTotal = (int)eligibleIdx.size();
    if (eligibleTotal == 0) return stats;

    // Round targets so they sum to eligibleTotal.
    int targetHold  = (int)std::round(target.holdPct  * eligibleTotal);
    int targetFlick = (int)std::round(target.flickPct * eligibleTotal);
    if (!opts.supportsHold) targetHold = 0;
    if (targetHold + targetFlick > eligibleTotal)
        targetFlick = eligibleTotal - targetHold;
    int targetTap = eligibleTotal - targetHold - targetFlick;
    (void)targetTap;

    // Helper: mutate a note's type, track stats.
    auto toTap = [&](EditorNote& n) {
        if (n.type == EditorNoteType::Hold) { n.endTime = 0.f; }
        n.type = EditorNoteType::Tap;
        ++stats.retyped;
    };
    auto toHold = [&](EditorNote& n, float sustain) {
        float dur = std::max(opts.holdMinSec, sustain);
        n.type    = EditorNoteType::Hold;
        n.endTime = n.time + dur;
        ++stats.retyped;
    };
    auto toFlick = [&](EditorNote& n) {
        if (n.type == EditorNoteType::Hold) n.endTime = 0.f;
        n.type = EditorNoteType::Flick;
        ++stats.retyped;
    };

    // -- Type demotion (surplus) ------------------------------------------
    // Demote surplus Holds (lowest sustain) -> Taps. Skip cross-lane holds.
    if (curHold > targetHold) {
        std::vector<size_t> holdCands;
        for (size_t i : eligibleIdx) {
            if (notes[i].type == EditorNoteType::Hold && !holdIsCrossLane(notes[i]))
                holdCands.push_back(i);
        }
        std::sort(holdCands.begin(), holdCands.end(),
            [&](size_t a, size_t b) {
                return aux[a].feat.sustain < aux[b].feat.sustain;
            });
        int need = curHold - targetHold;
        for (size_t k = 0; k < holdCands.size() && need > 0; ++k, --need) {
            toTap(notes[holdCands[k]]);
            --curHold; ++curTap;
        }
    }
    if (curFlick > targetFlick) {
        std::vector<size_t> flickCands;
        for (size_t i : eligibleIdx)
            if (notes[i].type == EditorNoteType::Flick) flickCands.push_back(i);
        std::sort(flickCands.begin(), flickCands.end(),
            [&](size_t a, size_t b) {
                return aux[a].feat.strength < aux[b].feat.strength;
            });
        int need = curFlick - targetFlick;
        for (size_t k = 0; k < flickCands.size() && need > 0; ++k, --need) {
            toTap(notes[flickCands[k]]);
            --curFlick; ++curTap;
        }
    }

    // -- Type promotion (deficit) -----------------------------------------
    // Promote Taps -> Holds (highest sustain) first, then Taps -> Flicks
    // (highest remaining strength).
    if (curHold < targetHold) {
        std::vector<size_t> tapCands;
        for (size_t i : eligibleIdx)
            if (notes[i].type == EditorNoteType::Tap) tapCands.push_back(i);
        std::sort(tapCands.begin(), tapCands.end(),
            [&](size_t a, size_t b) {
                return aux[a].feat.sustain > aux[b].feat.sustain;
            });
        int need = targetHold - curHold;
        for (size_t k = 0; k < tapCands.size() && need > 0; ++k, --need) {
            toHold(notes[tapCands[k]], aux[tapCands[k]].feat.sustain);
            --curTap; ++curHold;
        }
    }
    if (curFlick < targetFlick) {
        std::vector<size_t> tapCands;
        for (size_t i : eligibleIdx)
            if (notes[i].type == EditorNoteType::Tap) tapCands.push_back(i);
        std::sort(tapCands.begin(), tapCands.end(),
            [&](size_t a, size_t b) {
                return aux[a].feat.strength > aux[b].feat.strength;
            });
        int need = targetFlick - curFlick;
        for (size_t k = 0; k < tapCands.size() && need > 0; ++k, --need) {
            toFlick(notes[tapCands[k]]);
            --curTap; ++curFlick;
        }
    }

    // -- Lane rebalance ---------------------------------------------------
    // Operate on eligible single-lane notes only. Iterate by time so
    // anti-jack can track "previous lane".
    std::vector<size_t> laneIdx;
    for (size_t i : eligibleIdx) {
        const auto& n = notes[i];
        bool crossHold = (n.type == EditorNoteType::Hold && holdIsCrossLane(n));
        if (crossHold) { ++stats.skipped; continue; }
        laneIdx.push_back(i);
    }
    std::sort(laneIdx.begin(), laneIdx.end(),
        [&](size_t a, size_t b) { return notes[a].time < notes[b].time; });

    // Current lane histogram after type rebalance (type change didn't affect lane).
    std::vector<int> curHist(trackCount, 0);
    for (size_t i : laneIdx) {
        int lane = notes[i].track;
        if (lane >= 0 && lane < trackCount) curHist[lane] += 1;
    }
    int total = (int)laneIdx.size();

    // Integer-per-lane target counts (rounded; not forced to sum exactly).
    // When the reference's laneHist size differs from trackCount (cross-lane-
    // count transfer), resample by mapping each ref lane's mass to the target
    // lane its center falls into. Coarser than proportional overlap but fine
    // as a style heuristic.
    std::vector<int> targetPerLane(trackCount, 0);
    if ((int)target.laneHist.size() == trackCount) {
        for (int l = 0; l < trackCount; ++l)
            targetPerLane[l] = (int)std::round(target.laneHist[l] * total);
    } else if (!target.laneHist.empty()) {
        int refN = (int)target.laneHist.size();
        std::vector<float> mass(trackCount, 0.f);
        for (int r = 0; r < refN; ++r) {
            float center = (r + 0.5f) / (float)refN;
            int   tlane  = (int)std::floor(center * trackCount);
            if (tlane < 0) tlane = 0;
            if (tlane >= trackCount) tlane = trackCount - 1;
            mass[tlane] += target.laneHist[r];
        }
        for (int l = 0; l < trackCount; ++l)
            targetPerLane[l] = (int)std::round(mass[l] * total);
    } else {
        int per = total / trackCount;
        for (int l = 0; l < trackCount; ++l) targetPerLane[l] = per;
    }

    // Tolerance band: only move if over by >= 2 notes AND a strictly-below
    // lane exists. Keeps small charts from being churned pointlessly.
    const int TOL = 2;

    int prevLane = -1;
    for (size_t i : laneIdx) {
        auto& n = notes[i];
        int curLane = n.track;
        if (curHist[curLane] - targetPerLane[curLane] < TOL) {
            prevLane = curLane;
            continue;
        }

        // Preferred lane from marker centroid.
        int pref = laneFromCentroid(aux[i].feat.centroid, trackCount, prevLane,
                                     opts.antiJack);

        auto underfilled = [&](int lane) {
            return lane >= 0 && lane < trackCount &&
                   (curHist[lane] < targetPerLane[lane]);
        };

        int newLane = -1;
        if (pref != curLane && underfilled(pref) &&
            !(opts.antiJack && pref == prevLane)) {
            newLane = pref;
        } else {
            // Search outward from pref by +/-1, +/-2 up to trackCount.
            for (int delta = 1; delta <= trackCount && newLane < 0; ++delta) {
                for (int sign : {-1, +1}) {
                    int cand = pref + sign * delta;
                    if (cand == curLane) continue;
                    if (opts.antiJack && cand == prevLane) continue;
                    if (underfilled(cand)) { newLane = cand; break; }
                }
            }
        }

        if (newLane >= 0 && newLane != curLane) {
            // For cross-lane holds we already skipped via laneIdx; but guard
            // again just in case the hold is single-lane with endTrack sentinel.
            if (n.type == EditorNoteType::Hold && n.endTrack >= 0 &&
                n.endTrack == curLane) {
                n.endTrack = newLane;
            }
            curHist[curLane] -= 1;
            curHist[newLane] += 1;
            n.track = newLane;
            ++stats.relaned;
            prevLane = newLane;
        } else {
            prevLane = curLane;
        }
    }

    return stats;
}
