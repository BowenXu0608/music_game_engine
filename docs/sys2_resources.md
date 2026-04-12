---
name: Resource Management System
description: Assets, audio, chart loader/export, textures, beat analysis, dynamic BPM, HoldData model
type: project
originSessionId: d4e6dddd-1cc1-4f7b-8da6-079be9eb81c0
---
# System 2 — Resource Management ✅ COMPLETE

**Files:** scattered across `engine/src/`

## Components

| Resource Type | Component | Location |
|---|---|---|
| Textures | TextureManager | `renderer/vulkan/TextureManager` |
| Audio | AudioEngine (miniaudio) | `engine/AudioEngine` |
| Charts | ChartLoader + ChartTypes | `game/chart/` |
| Chart export | SongEditor::exportAllCharts() | `ui/SongEditor.cpp` |
| Assets | AssetBrowser | `ui/AssetBrowser.h` |
| GIFs | GifPlayer | `ui/GifPlayer` |

## Unified Chart Format (UCF)

`.json` with `"version"` field — auto-detects unified vs legacy formats.

```json
{
  "version": "1.0",
  "title": "...", "artist": "...", "offset": 0.0,
  "timing": { "bpm": 120.0, "timeSignature": "4/4", "bpm_changes": [...] },
  "notes": [
    {"time": 1.0, "type": "tap", "lane": 3, "scan": {"x":0.5, "y":0.3}},
    {"time": 2.0, "type": "hold", "lane": 2, "duration": 1.0,
     "scan": {"x":0.5, "y":0.3, "endY":0.7, "sweeps": 2}},
    {"time": 3.0, "type": "slide", "lane": 0, "duration": 2.0,
     "samples": [0.5, 1.0, 1.5],
     "scan": {"x":0.2, "y":0.3, "path": [[0.2,0.3],[0.5,0.5],[0.8,0.7]]}}
  ],
  "diskAnimation": {"rotations": [...], "moves": [...], "scales": [...]},
  "scanSpeedEvents": [{"startTime":5, "duration":2, "targetSpeed":2, "easing":"sineInOut"}]
}
```

**Legacy format support:** Bandori JSON (no version), Phigros (.pec/.pgr), Arcaea (.aff), Cytus (.xml), Lanota (.lan).

## ChartTypes.h — Key Data Structures

**Note types:** `Tap, Hold, Flick, Drag, Arc, ArcTap, Ring, Slide`

**Note data variants:** `TapData` (+ scanX/Y/duration/scanPath/samplePoints for slides), `HoldData` (+ waypoints + scanX/Y/EndY/sweeps), `FlickData` (+ scanX/Y), `ArcData`, `PhigrosNoteData`, `LanotaRingData`.

**Hold model:** Primary = `vector<HoldWaypoint>` with multi-lane path. Legacy = single endLaneX + transition. `evalHoldLaneAt` inline helper walks waypoints or falls back. Transition styles: Straight, Angle90, Curve, Rhomboid.

**Disk animation:** `DiskRotationEvent`, `DiskMoveEvent`, `DiskScaleEvent` with `DiskEasing` enum.

**Scan speed:** `ScanSpeedEvent { startTime, duration, targetSpeed, easing }`.

**Shared utility:** `catmullRomPathEval(path, u)` inline Catmull-Rom spline interpolation.

## Dynamic BPM Detection (2026-04-08)

`AudioAnalyzer.h/.cpp` — C++ subprocess manager calling `tools/analyze_audio.py` (Madmom neural network). Produces `BpmChange` list stored in `SongEditor::m_bpmChanges`. Exported as `timing.bpm_changes` array in UCF. ChartLoader reads back into multiple `TimingPoint` entries.

3 difficulty auto-markers: Easy (downbeats), Medium (all beats), Hard (beats + onsets).

## AudioEngine

miniaudio-based. `load(path) -> bool`, `play()`, `pause()`, `resume()`, `stop()`, `positionSeconds()`, `isPlaying()`. `playClickSfx()` generates a 30ms 1200Hz sine click for hold sample ticks.

## ChartLoader JSON Parser Safety

All `findValue`/`getVal` lambdas have bounds checks: `pos >= size()` guard after whitespace skip, `find()` return check for closing quote. Prevents out-of-bounds reads on malformed JSON.
