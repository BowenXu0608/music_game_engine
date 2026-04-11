#!/usr/bin/env python3
"""Audio beat/onset analysis for MusicGameEngine editor.

Usage: python analyze_audio.py <audio_path>

Outputs JSON to stdout with:
  - bpm_changes: dynamic tempo map [{time, bpm}, ...]
  - easy/medium/hard: marker timestamps for 3 difficulty levels
  - bpm: dominant (most common) BPM

Difficulty levels reflect musical rhythm, not just pace:
  - Easy:   downbeats + strong musical accents (sparse, groove-focused)
  - Medium: all beats  + melodic onsets (rhythmic feel)
  - Hard:   all beats  + every onset (full musical detail)

Requires: pip install madmom miniaudio numpy
"""

import sys
import json
import numpy as np

# Monkey-patch numpy for madmom 0.16.1 compatibility with numpy >= 1.24
np.int = np.int64
np.float = np.float64
np.complex = np.complex128
np.bool = np.bool_
np.str = np.str_
np.object = np.object_


def load_audio_miniaudio(path, target_sr=44100):
    """Decode audio to mono float32 numpy array using miniaudio."""
    import os
    # Normalize path separators for Windows
    path = os.path.normpath(path)
    if not os.path.isfile(path):
        raise FileNotFoundError(f"Audio file not found: {path}")

    import miniaudio
    decoded = miniaudio.decode_file(path, output_format=miniaudio.SampleFormat.FLOAT32,
                                   nchannels=1, sample_rate=target_sr)
    samples = np.frombuffer(decoded.samples, dtype=np.float32).copy()
    return samples, target_sr


def compute_bpm_changes(beats, min_section_beats=8, bpm_change_threshold=5.0):
    """Compute dynamic BPM map from beat timestamps.

    Analyzes beat intervals in sliding windows to detect where tempo changes.
    Returns list of {time, bpm} dicts representing tempo sections.

    Args:
        beats: sorted array of beat timestamps in seconds
        min_section_beats: minimum beats in a section before allowing a BPM change
        bpm_change_threshold: minimum BPM difference to register a new section
    """
    if len(beats) < 4:
        if len(beats) >= 2:
            avg_interval = float(np.median(np.diff(beats)))
            return [{"time": 0.0, "bpm": round(60.0 / avg_interval, 1)}]
        return [{"time": 0.0, "bpm": 120.0}]

    intervals = np.diff(beats)

    # Use a sliding window to compute local BPM at each beat
    window_size = min(min_section_beats, len(intervals))
    half_w = window_size // 2

    local_bpms = []
    for i in range(len(intervals)):
        start = max(0, i - half_w)
        end = min(len(intervals), i + half_w + 1)
        local_interval = float(np.median(intervals[start:end]))
        if local_interval > 0:
            local_bpms.append(60.0 / local_interval)
        else:
            local_bpms.append(120.0)

    # Segment into sections where BPM is stable
    bpm_changes = []
    current_bpm = round(local_bpms[0], 1)
    bpm_changes.append({"time": 0.0, "bpm": current_bpm})

    section_start_idx = 0
    for i in range(1, len(local_bpms)):
        candidate_bpm = round(local_bpms[i], 1)

        # Check if BPM has changed significantly over a sustained region
        if abs(candidate_bpm - current_bpm) >= bpm_change_threshold:
            # Confirm: check that the next few beats also agree
            lookahead_end = min(len(local_bpms), i + max(3, min_section_beats // 2))
            if lookahead_end > i + 1:
                lookahead_bpms = local_bpms[i:lookahead_end]
                confirmed_bpm = round(float(np.median(lookahead_bpms)), 1)
                if abs(confirmed_bpm - current_bpm) >= bpm_change_threshold:
                    # BPM change confirmed
                    change_time = round(float(beats[i]), 4)
                    current_bpm = confirmed_bpm
                    bpm_changes.append({"time": change_time, "bpm": current_bpm})
                    section_start_idx = i
            else:
                # Near end of song, just accept the change
                change_time = round(float(beats[i]), 4)
                current_bpm = candidate_bpm
                bpm_changes.append({"time": change_time, "bpm": current_bpm})

    # Deduplicate very close BPM changes (within 1 second)
    if len(bpm_changes) > 1:
        filtered = [bpm_changes[0]]
        for bc in bpm_changes[1:]:
            if bc["time"] - filtered[-1]["time"] > 1.0:
                filtered.append(bc)
            else:
                # Keep the later one (more accurate)
                filtered[-1] = bc
        bpm_changes = filtered

    return bpm_changes


def merge_onsets_with_beats(beats, onsets, min_gap=0.05):
    """Merge onset times into beat times, deduplicating within min_gap seconds."""
    beat_set = set(round(float(t), 3) for t in beats)
    merged = set(round(float(t), 4) for t in beats)
    for t in onsets:
        rounded = round(float(t), 3)
        if not any(abs(rounded - b) < min_gap for b in beat_set):
            merged.add(round(float(t), 4))
    return sorted(merged)


def analyze(audio_path):
    import madmom
    from madmom.audio.signal import Signal

    result = {
        "status": "ok",
        "bpm": 0.0,
        "bpm_changes": [],
        "easy": [],
        "medium": [],
        "hard": [],
    }

    # Load audio via miniaudio (handles MP3/OGG/WAV/FLAC without ffmpeg)
    samples, sr = load_audio_miniaudio(audio_path, target_sr=44100)

    # Wrap in madmom Signal object
    signal = Signal(samples, sample_rate=sr, num_channels=1)

    # ── Beat tracking ────────────────────────────────────────────────────
    beat_act = madmom.features.beats.RNNBeatProcessor()(signal)
    beat_proc = madmom.features.beats.DBNBeatTrackingProcessor(fps=100)
    beats = beat_proc(beat_act)

    # ── Downbeat tracking ────────────────────────────────────────────────
    try:
        downbeat_act = madmom.features.beats.RNNDownBeatProcessor()(signal)
        downbeat_proc = madmom.features.beats.DBNDownBeatTrackingProcessor(
            beats_per_bar=[3, 4], fps=100
        )
        downbeats_raw = downbeat_proc(downbeat_act)
        downbeats = [float(row[0]) for row in downbeats_raw if int(row[1]) == 1]
    except Exception:
        # Fallback: every 4th beat
        downbeats = [float(beats[i]) for i in range(0, len(beats), 4)]

    # ── Onset detection with multiple thresholds ─────────────────────────
    try:
        onset_act = madmom.features.onsets.RNNOnsetProcessor()(signal)

        # Strong onsets (high threshold) — prominent musical accents
        onset_proc_strong = madmom.features.onsets.OnsetPeakPickingProcessor(
            fps=100, threshold=0.5
        )
        onsets_strong = onset_proc_strong(onset_act)

        # Medium onsets — melodic/harmonic events
        onset_proc_medium = madmom.features.onsets.OnsetPeakPickingProcessor(
            fps=100, threshold=0.35
        )
        onsets_medium = onset_proc_medium(onset_act)

        # All onsets (low threshold) — every audible event
        onset_proc_all = madmom.features.onsets.OnsetPeakPickingProcessor(
            fps=100, threshold=0.2
        )
        onsets_all = onset_proc_all(onset_act)
    except Exception:
        onsets_strong = np.array([])
        onsets_medium = np.array([])
        onsets_all = np.array([])

    # ── Dynamic BPM map ──────────────────────────────────────────────────
    bpm_changes = compute_bpm_changes(beats)
    result["bpm_changes"] = bpm_changes

    # Dominant BPM = most common section BPM (weighted by duration)
    if bpm_changes:
        if len(bpm_changes) == 1:
            bpm = bpm_changes[0]["bpm"]
        else:
            # Weight each section by its duration
            song_end = float(beats[-1]) if len(beats) > 0 else 0.0
            weighted_bpms = []
            for i, bc in enumerate(bpm_changes):
                next_time = bpm_changes[i + 1]["time"] if i + 1 < len(bpm_changes) else song_end
                duration = next_time - bc["time"]
                weighted_bpms.append((bc["bpm"], max(0, duration)))
            # Dominant = longest section's BPM
            if weighted_bpms:
                bpm = max(weighted_bpms, key=lambda x: x[1])[0]
            else:
                bpm = 120.0
    else:
        bpm = 120.0

    # ── Build difficulty markers (musically meaningful) ───────────────────

    # Easy: downbeats + strong musical accents (the main groove)
    # Merge downbeats with strong onsets, ensuring rhythmic consistency
    easy_set = set(round(t, 4) for t in downbeats)
    for t in onsets_strong:
        rounded = round(float(t), 4)
        # Only add strong onsets that aren't too close to existing markers
        if not any(abs(rounded - e) < 0.15 for e in easy_set):
            easy_set.add(rounded)
    easy = sorted(easy_set)

    # Medium: all beats + melodic/harmonic onsets (the rhythmic feel)
    medium = merge_onsets_with_beats(beats, onsets_medium, min_gap=0.06)

    # Hard: all beats + every onset (full musical detail)
    hard = merge_onsets_with_beats(beats, onsets_all, min_gap=0.04)

    result["bpm"] = bpm
    result["easy"] = easy
    result["medium"] = medium
    result["hard"] = hard

    return result


def main():
    if len(sys.argv) < 2:
        json.dump(
            {"status": "error", "message": "Usage: analyze_audio.py <audio_path>"},
            sys.stdout,
        )
        return

    audio_path = sys.argv[1]

    try:
        result = analyze(audio_path)
    except Exception as e:
        result = {"status": "error", "message": str(e)}

    json.dump(result, sys.stdout)


if __name__ == "__main__":
    main()
