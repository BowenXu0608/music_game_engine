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
     "scan": {"x":0.2, "y":0.3, "path": [[0.2,0.3],[0.5,0.5],[0.8,0.7]]}},
    {"time": 4.0, "type": "arc", "lane": 0,
     "startX": 0.2, "startY": 0.0, "endX": 0.8, "endY": 0.5,
     "duration": 2.0, "easeX": 0, "easeY": 2, "color": 0, "void": false},
    {"time": 5.0, "type": "arctap", "lane": 0, "arcX": 0.5, "arcY": 0.25}
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

**Arc model:** `ArcData { vec2 startPos, endPos; float duration; float curveXEase, curveYEase; int color; bool isVoid; }`. Positions are normalized [0..1] (x=horizontal, y=height). Easing values: 0=linear, 1=bezier, ±2=sine-in/out, ±3=sisi/siso, ±4=sosi/soso (matching Arcaea `.aff` format). Color: 0=cyan, 1=pink. Void arcs are visual-only (no input).

**ArcTap model:** Stored as `TapData` with `laneX` = arc X position at that time, `scanY` = arc height. Parent arc relationship maintained in editor via `arcTapParent` index.

**Disk animation:** `DiskRotationEvent`, `DiskMoveEvent`, `DiskScaleEvent` with `DiskEasing` enum.

**Scan speed:** `ScanSpeedEvent { startTime, duration, targetSpeed, easing }`.

**Shared utility:** `catmullRomPathEval(path, u)` inline Catmull-Rom spline interpolation.

## Dynamic BPM Detection (2026-04-08)

`AudioAnalyzer.h/.cpp` — C++ subprocess manager calling `tools/analyze_audio.py` (Madmom neural network). Produces `BpmChange` list stored in `SongEditor::m_bpmChanges`. Exported as `timing.bpm_changes` array in UCF. ChartLoader reads back into multiple `TimingPoint` entries.

3 difficulty auto-markers: Easy (downbeats), Medium (all beats), Hard (beats + onsets).

## AudioEngine

miniaudio-based. `load(path) -> bool`, `play()`, `pause()`, `resume()`, `stop()`, `positionSeconds()`, `isPlaying()`. `playClickSfx()` generates a 30ms 1200Hz sine click for hold sample ticks.

## ChartLoader Arc/ArcTap Field Compatibility (2026-04-12)

The JSON unified parser accepts both legacy and new arc field names so charts round-trip through editor export/import:
- Easing: reads `easeX`/`easeY` (new, written by SongEditor) or falls back to `curveXEase`/`curveYEase`
- Void flag: reads `void` (new) or falls back to `isVoid`
- ArcTap position: reads `arcX`/`arcY` into `TapData.laneX`/`scanY` so ArcaeaRenderer can resolve the parent arc

## ChartLoader JSON Parser Safety

All `findValue`/`getVal` lambdas have bounds checks: `pos >= size()` guard after whitespace skip, `find()` return check for closing quote. Prevents out-of-bounds reads on malformed JSON.

## AudioEngine seek + duration (2026-04-23)

`AudioEngine::playFrom(startSec)` and `AudioEngine::durationSeconds()` added. `playFrom` reads the loaded sound's sample rate via `ma_sound_get_data_format(&m_impl->sound, nullptr, nullptr, &sampleRate, nullptr, 0)` (falls back to 44100 if the format call fails), clamps negative start to 0, computes `frame = startSec * sampleRate`, calls `ma_sound_seek_to_pcm_frame` followed by `ma_sound_start`, sets `m_playing = true`. `durationSeconds()` uses `ma_sound_get_length_in_seconds` on a `const_cast`'d `&m_impl->sound` (miniaudio's signature is non-const). Used by `MusicSelectionEditor::updateAudioPreview` to play a 30-second preview clip starting at `SongInfo::previewStart` when the player dwells on a song — the preview start is auto-detected by summing `MarkerFeature.strength` over a sliding window in SongEditor's new "Preview Clip" section.
