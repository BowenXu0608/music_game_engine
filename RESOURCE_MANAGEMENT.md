# System 2 — Resource Management

**Last updated:** 2026-04-05  
**Status:** ✅ Complete

Handles all external file I/O: textures, audio, chart data, asset browsing, and animated GIFs.  
See also: [README.md](README.md) | [RENDERING_SYSTEM.md](RENDERING_SYSTEM.md)

---

## Components Overview

```
System 2 — Resource Management
├── TextureManager       — stb_image → Vulkan GPU upload → ImTextureID
├── AudioEngine          — miniaudio wrapper (play/stop/seek/DSP clock)
├── ChartLoader          — auto-detect format → unified ChartData
├── ChartTypes           — NoteEvent / ChartData schema (unified)
├── AssetBrowser         — thumbnail grid, drag-drop, import, delete
└── GifPlayer            — animated GIF via per-frame Vulkan textures
```

---

## TextureManager — `engine/src/renderer/vulkan/TextureManager.h/.cpp`

- Loads images via `stb_image` → uploads to GPU via VMA
- `STB_IMAGE_IMPLEMENTATION` is defined **only here** (once per translation unit)
- `ImGuiLayer::addTexture(view, sampler)` calls into TextureManager to register as `ImTextureID`
- Thumbnail cache in editors: 80×80, stored in `m_thumbCache` (path → `ImTextureID`)
- Texture lifecycle: `loadFromFile()` → `addTexture()` → `ImGui::Image()` → destroy on clear/delete

---

## AudioEngine — `engine/src/engine/AudioEngine.h/.cpp`

Wraps the `miniaudio` single-header library.

- `MINIAUDIO_IMPLEMENTATION` defined **only here** (once per translation unit)
- `miniaudio.h` bundled at `engine/src/engine/miniaudio.h`

| Method | Details |
|---|---|
| `play(path)` | Load and start playback of an audio file |
| `stop()` | Stop playback |
| `seek(seconds)` | Seek to position |
| `currentTime()` | Returns DSP playback position in seconds |
| `isPlaying()` | Playback state query |

**DSP Clock:** `GameClock` (System 3) reads `AudioEngine::currentTime()` as its authoritative time source while a song is playing. This ensures chart events are synced to actual audio position, not wall clock drift.

Supported formats: `.mp3`, `.ogg`, `.wav`, `.flac`, `.aac`

### Waveform LOD Generation

`AudioEngine::decodeWaveform()` generates multi-level LOD (Level of Detail) waveform data for fast rendering at different zoom levels.

**Bug fix (2026-04-04):** The LOD generation loop used `emplace_back()` to append coarse LOD levels to the `std::vector<WaveformLOD>`. A `const WaveformLOD& prev` reference was taken to the previous level before `emplace_back()`, but `emplace_back()` can reallocate the vector, invalidating the reference. This caused a dangling reference crash when building the second or later LOD level. Fixed by building each coarse LOD in a **local variable** first, then `std::move`-ing it into the vector after computation is complete.

---

## ChartTypes — `engine/src/game/chart/ChartTypes.h`

Unified data schema for all game modes. All chart parsers produce this output.

```cpp
enum class NoteType { Tap, Hold, Flick, Drag, Arc, ArcTap, Ring, Slide };

// Per-note-type data (held in std::variant inside NoteEvent)
struct TapData   { float laneX; };
struct HoldData  { float laneX; float duration; };
struct FlickData { float laneX; int direction = 0; };  // -1=left, 0=up, 1=right
struct ArcData   { glm::vec2 startPos, endPos; float duration;
                   float curveXEase, curveYEase; int color; bool isVoid; };
struct PhigrosNoteData { float posOnLine; NoteType subType; float duration; };
struct LanotaRingData  { float angle; int ringIndex; };

struct NoteEvent {
    double   time;                 // seconds from song start
    NoteType type;
    uint32_t id;
    double   beatPosition = 0.0;  // accumulated beats from song start (set by computeBeatPositions)
    std::variant<TapData, HoldData, FlickData,
                 ArcData, PhigrosNoteData, LanotaRingData> data;
};

struct TimingPoint {
    double time;   // seconds when this BPM takes effect
    float  bpm;
    int    meter;  // beats per measure
};

struct JudgmentLineEvent {
    double    time;
    glm::vec2 position;   // normalized [-1,1]
    float     rotation;   // radians
    float     speed;
    std::vector<NoteEvent> attachedNotes;
};

struct ChartData {
    std::string title;
    std::string artist;
    float       offset = 0.f;                     // audio sync offset in seconds
    std::vector<TimingPoint>       timingPoints;   // BPM change events (all formats)
    std::vector<NoteEvent>         notes;
    std::vector<JudgmentLineEvent> judgmentLines;  // Phigros only
};
```

### `beatPosition` — what it is and why

`NoteEvent::beatPosition` is filled by `ChartLoader::computeBeatPositions()` after every parse. It counts the total beats elapsed from the song start to the note's timestamp, accumulating correctly across BPM changes.

**Why it matters:** renderers use `beatPosition` as the scroll coordinate instead of raw `time`. Because beats are time-normalized by BPM, a tempo change automatically increases the spatial density of notes — visual spacing between beats stays constant regardless of tempo.

---

## ChartLoader — `engine/src/game/chart/ChartLoader.h/.cpp`

Single entry point: `ChartLoader::load(path)` — auto-detects format, returns `ChartData`.  
After parsing, every loader calls `computeBeatPositions(chart)` to fill `NoteEvent::beatPosition`.

### Format Detection Logic

```
Has "version" field in JSON  → Unified Chart Format (UCF)
Extension == ".aff"          → Arcaea AFF
Extension == ".xml"          → Cytus XML
Extension == ".pec" / ".pgr" → Phigros PEC
Extension == ".lan"          → Lanota LAN
JSON without "version"       → Bandori legacy JSON
```

**Fix (2026-04-04):** `load()` now reads the **full JSON content** into a string before checking for `"version"`. Previously it only checked the first line, which broke if `"version"` was not on line 1.

### `findMatchingBracket()` Helper

New private static helper for safe nested JSON array parsing. Given a string and an opening bracket position, returns the position of the matching closing bracket, respecting nested brackets and string escaping. Used by `loadBandori` and `loadUnified` to extract complex nested note arrays.

### Timing Point Sources (per format)

| Format | Source field | Multi-BPM? |
|---|---|---|
| UCF | `"timing": { "bpm", "timeSignature" }` | Single point at t=0 |
| Bandori | `"system": [{ "time", "bpm" }]` — fallback to top-level `"bpm"` | ✅ Full array |
| Arcaea AFF | `timing(time_ms, bpm, meter);` lines | ✅ Full array |
| Cytus XML | `<bpm_event time="..." bpm="..."/>` tags | ✅ Full array |
| Phigros PEC | Top-level `"bpm"` field | Single point at t=0 |
| Lanota LAN | `bpm <time> <value>` directive lines | ✅ Full array |

All formats fall back to `{ t=0, bpm=120, meter=4 }` when no timing data is present.

### `computeBeatPositions` — algorithm

`ChartLoader::computeBeatPositions(ChartData&)` is a private static helper:

1. Pre-compute `accum[i]` = total beats at the start of timing segment `i`  
   `accum[i] = accum[i-1] + (tp[i].time - tp[i-1].time) × (tp[i-1].bpm / 60)`
2. For each note: find its segment via linear scan, then  
   `note.beatPosition = accum[seg] + (note.time - tp[seg].time) × (tp[seg].bpm / 60)`
3. Also applied to `judgmentLine.attachedNotes` (Phigros)

### Unified Chart Format (UCF) — `.json` with `"version"` field

The recommended format. Works for all game modes in a single schema.

```json
{
  "version": "1.0",
  "metadata": {
    "title": "Song Name",
    "artist": "Artist Name",
    "charter": "Charter Name",
    "difficulty": "Hard",
    "level": 12
  },
  "audio": {
    "file": "song.ogg",
    "offset": 0.0,
    "previewStart": 30.0
  },
  "timing": {
    "bpm": 120.0,
    "timeSignature": "4/4"
  },
  "gameMode": "bandori",
  "notes": [
    { "time": 1.0,  "type": "tap",   "lane": 3 },
    { "time": 2.0,  "type": "hold",  "lane": 2, "duration": 1.0 },
    { "time": 3.5,  "type": "flick", "lane": 5, "direction": 1 }
  ]
}
```

### UCF Note Types by Game Mode

**Bandori / Cytus / Lanota (lane-based):**
```json
{ "time": 1.0, "type": "tap",   "lane": 3 }
{ "time": 2.0, "type": "hold",  "lane": 2, "duration": 1.0 }
{ "time": 3.0, "type": "flick", "lane": 5, "direction": 1 }
```

**Arcaea:**
```json
{ "time": 1.0, "type": "tap",  "lane": 2 }
{ "time": 2.0, "type": "arc",  "startX": 0.25, "endX": 0.75,
  "startY": 0.5, "endY": 1.0, "duration": 1.0, "color": 0 }
{ "time": 3.0, "type": "hold", "lane": 3, "duration": 0.5 }
{ "time": 4.0, "type": "skyNote", "lane": 2 }
```

**Phigros:**
```json
{ "time": 1.0, "type": "tap",  "line": 0, "posOnLine": 0.5 }
{ "time": 2.0, "type": "hold", "line": 0, "posOnLine": 0.3, "duration": 1.0 }
{ "time": 3.0, "type": "flick","line": 1, "posOnLine": -0.2 }
```

With judgment line animation events:
```json
"judgmentLines": [
  {
    "id": 0,
    "events": [
      { "time": 0.0, "x": 0.5, "y": 0.5, "rotation": 0.0, "speed": 1.0 }
    ]
  }
]
```

**Lanota (radial):**
```json
{ "time": 1.0, "type": "tap",  "ring": 0, "angle": 45.0 }
{ "time": 2.0, "type": "hold", "ring": 1, "angle": 90.0, "duration": 1.0 }
```

### Legacy Format Support

| Format | Extension | Game | Notes |
|---|---|---|---|
| Bandori JSON | `.json` (no `"version"`) | BanG Dream | Original format |
| Arcaea AFF | `.aff` | Arcaea | Text-based timing format |
| Cytus XML | `.xml` | Cytus | XML note placement |
| Phigros PEC | `.pec` / `.pgr` | Phigros | Line + note events |
| Lanota LAN | `.lan` | Lanota | Custom binary/text |

### Usage

```cpp
#include "game/chart/ChartLoader.h"

// Auto-detects format (UCF or any legacy):
ChartData chart = ChartLoader::load("assets/charts/my_song.json");
// chart.gameMode == "bandori"
// chart.notes[0].type == NoteType::Tap
```

### Slide Note Support

`loadUnified` now handles `"slide"` as a note type string, mapping it to `NoteType::Slide` with `TapData` semantics. Example:

```json
{ "time": 1.5, "type": "slide", "lane": 4 }
```

This allows slide notes authored in the editor to round-trip through export → load without data loss.

### ChartLoader Completion (2026-04-04)

All six format parsers were completed with full note type coverage. Synced internal and public `ChartTypes.h` — added `direction` field to `FlickData` (default 0: -1=left, 0=up, 1=right).

#### `loadUnified` — New Note Types

| Note type string | NoteType | Data struct |
|---|---|---|
| `"drag"` | `NoteType::Drag` | `TapData` |
| `"arctap"` | `NoteType::ArcTap` | `TapData` |
| `"ring"` | `NoteType::Ring` | `LanotaRingData` |

Also added: `FlickData.direction` from JSON `"direction"` field; `ArcData.curveXEase`, `ArcData.curveYEase`, `ArcData.isVoid` fields.

#### `loadBandori` — Slide + Flick Direction

- Added `Slide` note type (previously ignored)
- `FlickData.direction` parsed from note JSON
- Guarded `endTime` field (only read when present, avoids crash on tap/flick notes)

#### `loadPhigros` — Complete Rewrite

Completely rewritten parser. Now actually parses:
- `time` (beat time), `positionX`, `type` (1=tap, 2=drag, 3=hold, 4=flick), `holdTime`
- Parses both `notesAbove` and `notesBelow` arrays from each judgment line
- Converts beat time to seconds using `bpm` field
- Maps type codes: 1→Tap, 2→Drag, 3→Hold, 4→Flick

#### `loadArcaea` — Robust Manual Parser

Replaced fragile `sscanf` parsing with manual comma-split parser for reliable field extraction. New features:
- **ArcTap sub-notes**: parses `[arctap(ms),arctap(ms),...]` fragments inside arc lines
- **Easing type mapping**: `s`, `b`, `si`, `so`, `sisi`, `siso`, `sosi`, `soso` → curveXEase/curveYEase float values
- **`isVoid` flag**: parsed from arc line
- **Windows `\r` stripping**: removes carriage returns before parsing

#### `loadCytus` — Drag + Flick

- Added Drag (type=2) and Flick (type=3) note types (previously only Tap and Hold)
- Handles both self-closing (`<note ... />`) and regular (`<note ...></note>`) XML tags
- Fixed time precision: `float` → `double` for sub-millisecond accuracy

#### `loadLanota` — Hold Duration + Flick

- Added hold duration parsing from 5th field in note line
- Added Flick note type (type=2) with `FlickData.direction`

### Chart Export from SongEditor

`SongEditor::exportAllCharts()` writes editor notes to unified JSON chart files.

- Output path: `assets/charts/<songname>_<difficulty>.json` (inside the project folder)
- One file per difficulty that contains notes
- Format:

```json
{
  "version": "1.0",
  "title": "Song Name",
  "notes": [
    { "time": 1.0, "type": "tap", "lane": 3 },
    { "time": 2.0, "type": "hold", "lane": 2, "duration": 1.0 }
  ]
}
```

`"version"` appears on the first line of the object so `ChartLoader::load()` detects it as UCF and dispatches to `loadUnified`.

### Chart Loading in SongEditor

`SongEditor::setSong()` loads chart files back into editor notes when opening a song:

1. For each difficulty, checks for `assets/charts/<songname>_<difficulty>.json`
2. Calls `ChartLoader::load(path)` to parse the file into `ChartData`
3. Converts each `NoteEvent` back to an `EditorNote` (type, time, lane, duration, etc.)
4. Populates the per-difficulty note vectors in the editor

This enables full round-trip: edit → export → reopen → continue editing.

### Test Charts — `test_charts/`

| File | Format |
|---|---|
| `unified_demo.json` | UCF |
| `bandori_demo.json` | Legacy Bandori |
| `arcaea_demo.aff` | Legacy Arcaea AFF |
| `cytus_demo.xml` | Legacy Cytus XML |
| `lanota_demo.lan` | Legacy Lanota |

---

## AssetBrowser — `engine/src/ui/AssetBrowser.h`

Header-only. Shared by all editor pages.

### Panel Layout

```
┌──────────────────────────────────────────────────────┐
│  Editor content (main area)                          │
├─── draggable splitter (m_vSplit) ────────────────────┤
│  [img 80×80] [img 80×80] [MUS] [img 80×80] ...      │
│  [Open File...]                                      │
└──────────────────────────────────────────────────────┘
```

### Scanning

`scanAssets(projectPath)` walks `{projectPath}/assets/` recursively.  
Uses `fs::absolute()` before `fs::relative()` to avoid Windows path resolution issues.

Groups:

| Group | Extensions |
|---|---|
| images | `.png` `.jpg` `.jpeg` |
| gifs | `.gif` |
| videos | `.mp4` `.webm` |
| audios | `.mp3` `.ogg` `.wav` `.flac` `.aac` |

### Thumbnails

- Size: 80×80 px
- Loaded lazily on first display via `getThumb(relPath)` → `TextureManager::loadFromFile`
- Cached in `m_thumbCache` (`std::unordered_map<std::string, ImTextureID>`)
- Audio files: 80×80 blue tile labeled "MUS"
- Cache cleared on project reload and engine shutdown

### Interactions

| Action | Effect |
|---|---|
| Hover | Blue border highlight + tooltip (full relative path) |
| Left-drag | `BeginDragSource("ASSET_PATH")` with relative path payload |
| Right-click | Context menu → Delete |

### Drag-Drop Target Pattern (all file path fields)

```cpp
if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
        std::string relPath(static_cast<const char*>(p->Data), p->DataSize);
        // assign relPath to the field
    }
    ImGui::EndDragDropTarget();
}
```

### Import Routing

| Extension | Destination |
|---|---|
| `.png` `.jpg` `.jpeg` `.gif` | `assets/textures/` |
| `.mp3` `.ogg` `.wav` `.flac` `.aac` | `assets/audio/` |
| `.mp4` `.webm` | `assets/videos/` |

### Delete

Right-click → Delete:
1. `fs::remove(absolutePath)`
2. Clear all editor references to the deleted file
3. Evict from `m_thumbCache`, destroy Vulkan texture
4. Trigger rescan next frame

**Rule: ALL editor pages must have the asset browser strip.** (User requirement)

---

## GifPlayer — `engine/src/ui/GifPlayer.h/.cpp`

Animated GIF playback inside ImGui panels.

- Decodes GIF frames using stb (each frame as a separate Vulkan texture)
- Advances frame index each tick based on per-frame delay
- Returns current frame's `ImTextureID` for `ImGui::Image()`
- Used in StartScreenEditor for animated background preview

---

## AudioAnalyzer — `engine/src/engine/AudioAnalyzer.h/.cpp`

Beat detection via **Madmom** (Python neural network library). Added 2026-04-04.

### Architecture

```
SongEditor "Analyze Beats" button
        ↓
AudioAnalyzer::analyzeAsync(audioPath, callback)
        ↓  (background thread)
CreateProcessA → python tools/analyze_audio.py <audioPath>
        ↓  (stdout pipe capture via CreatePipe)
JSON result → parse → AnalysisResult { easy[], medium[], hard[] }
        ↓  (callback on main thread)
SongEditor populates m_diffMarkers for all 3 difficulties
```

### `tools/analyze_audio.py` — Python Analysis Script

Uses Madmom neural network processors for beat/onset detection. Uses `miniaudio` for audio decoding (no ffmpeg dependency). Outputs JSON to stdout with 3 difficulty levels:

| Difficulty | Algorithm | Density |
|---|---|---|
| Easy | `DBNDownBeatTrackingProcessor` (downbeats only) | ~30 markers/min |
| Medium | `DBNBeatTrackingProcessor` (all beats) | ~120 markers/min |
| Hard | Both beat + `RNNOnsetProcessor` (beats + onsets) | ~200-500 markers/min |

Output format:
```json
{
  "easy": [0.5, 1.0, 1.5, ...],
  "medium": [0.25, 0.5, 0.75, 1.0, ...],
  "hard": [0.12, 0.25, 0.37, 0.5, ...]
}
```

### C++ AudioAnalyzer Class

- Spawns `analyze_audio.py` as a **subprocess** with stdout pipe capture
- Windows-specific: uses `CreateProcessA` + `CreatePipe` for stdout redirection
- Runs analysis on a **background thread** to avoid blocking the UI
- Parses JSON result with a **custom lightweight parser** (no nlohmann dependency — the output is simple enough to parse manually)
- Thread-safe callback delivery to the main thread

### Integration with SongEditor

- **"Analyze Beats"** button (green) in the toolbar triggers analysis
- **"Analyzing..."** text shown while the background thread is running
- On completion, results populate `m_diffMarkers` for Easy/Medium/Hard difficulties
- Error popup displayed on failure (Python not found, Madmom not installed, etc.)

---

## Project Folder Convention

```
Projects/<ProjectName>/
├── project.json           — window size, asset paths, engine version
├── start_screen.json      — start screen settings
├── music_selection.json   — music sets, songs, cover paths
└── assets/
    ├── charts/            — .json (UCF preferred) or legacy formats
    ├── audio/             — .mp3 / .ogg / .wav / .flac / .aac
    └── textures/          — .png / .jpg / .gif
```

---

## GameModeConfig Serialization in MusicSelectionEditor

`MusicSelectionEditor` save/load (`music_selection.json`) now serializes all `GameModeConfig` fields added for gameplay integration:

| Field Group | Keys |
|---|---|
| Judgment windows (ms) | `perfectMs`, `goodMs`, `badMs` |
| Score values | `perfectScore`, `goodScore`, `badScore` |
| Result images | `fcImage`, `apImage` |
| HUD text configs | `scoreHud`, `comboHud` (each: `{ "fontPath", "fontSize", "x", "y", "color" }`) |
| Camera | `cameraEye` (`[x,y,z]`), `cameraTarget` (`[x,y,z]`), `cameraFov` |

These fields are written per-song inside each song object in the JSON array, and loaded back into the corresponding `GameModeConfig` struct on `loadFromFile()`.
