#!/usr/bin/env python3
"""Audio beat/onset analysis for MusicGameEngine editor.

Usage: python analyze_audio.py <audio_path>

Outputs JSON to stdout with marker timestamps for 3 difficulty levels.
Uses miniaudio for decoding (no ffmpeg needed) and madmom for beat detection.

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
    import miniaudio

    # Decode entire file to PCM
    decoded = miniaudio.decode_file(path, output_format=miniaudio.SampleFormat.FLOAT32,
                                   nchannels=1, sample_rate=target_sr)
    samples = np.frombuffer(decoded.samples, dtype=np.float32).copy()
    return samples, target_sr


def analyze(audio_path):
    import madmom
    from madmom.audio.signal import Signal

    result = {"status": "ok", "bpm": 0.0, "easy": [], "medium": [], "hard": []}

    # Load audio via miniaudio (handles MP3/OGG/WAV/FLAC without ffmpeg)
    samples, sr = load_audio_miniaudio(audio_path, target_sr=44100)

    # Wrap in madmom Signal object
    signal = Signal(samples, sample_rate=sr, num_channels=1)

    # ── Beat tracking (used by Medium + Hard) ────────────────────────────
    beat_act = madmom.features.beats.RNNBeatProcessor()(signal)
    beat_proc = madmom.features.beats.DBNBeatTrackingProcessor(fps=100)
    beats = beat_proc(beat_act)

    # ── Downbeat tracking (used by Easy) ─────────────────────────────────
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

    # ── Onset detection (used by Hard) ───────────────────────────────────
    try:
        onset_act = madmom.features.onsets.RNNOnsetProcessor()(signal)
        onset_proc = madmom.features.onsets.OnsetPeakPickingProcessor(
            fps=100, threshold=0.3
        )
        onsets = onset_proc(onset_act)
    except Exception:
        onsets = np.array([])

    # ── BPM estimation ───────────────────────────────────────────────────
    if len(beats) >= 2:
        intervals = np.diff(beats)
        median_interval = float(np.median(intervals))
        bpm = round(60.0 / median_interval, 1) if median_interval > 0 else 120.0
    else:
        bpm = 120.0

    # ── Build difficulty marker lists ────────────────────────────────────

    # Easy: downbeats only
    easy = sorted(set(round(t, 4) for t in downbeats))

    # Medium: all beats
    medium = sorted(set(round(float(t), 4) for t in beats))

    # Hard: beats + onsets (deduplicate within 50ms)
    beat_times = set(round(float(t), 3) for t in beats)
    hard_set = set(round(float(t), 4) for t in beats)
    for t in onsets:
        rounded = round(float(t), 3)
        if not any(abs(rounded - b) < 0.05 for b in beat_times):
            hard_set.add(round(float(t), 4))
    hard = sorted(hard_set)

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
