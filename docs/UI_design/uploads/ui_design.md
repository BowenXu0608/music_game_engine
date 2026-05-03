---
name: UI Design
description: Consolidated design principles, palette, layer flow, and per-page / per-game-mode property tables for the editor + player UI
type: reference
---
# UI Design

**Source-of-truth:** `engine/src/ui/` for layout, `engine/src/ui/ProjectHub.h` for `GameModeConfig`, `engine/src/game/PlayerSettings.{h,cpp}` for player settings. Layout *what* lives in `sys7_editor.md`; this doc covers the *why* and the per-page / per-mode property inventory.

## 1. Design Principles

The rules below are extracted from feedback collected across the editor build (2026-04-03 → 2026-04-26). They apply to every editor page; the Android player UI is excluded except for the Settings page.

### 1.1 Layout
- **Reference-page replication.** When the user nominates a page as "the reference" (e.g. SongEditor for the assets strip), sibling pages copy the same ImGui call sequence, widths, and reserve logic — not a near-equivalent. Shared state lives in helpers (`copilotOverlayWidth()`, `setOverlayBottomReserve()`, `PreviewAspect.h`).
- **Preview is prominent.** SongEditor: scene preview on top, controls below. MusicSelection / StartScreen: preview-left + properties-right (70/30) with a pinned bottom Assets strip on every page.
- **Sidebars are tabbed when crowded.** SongEditor's left sidebar is a 3-tab `BeginTabBar` (Basic / Note / Material); the right sidebar is a 2-tab bar (Copilot / Audit). Tabs > stacked CollapsingHeaders once a page outgrows one screen.
- **Letterbox the preview.** Both StartScreen and MusicSelection paint the preview inside a `fitAndLetterbox` rect driven by the shared `Engine::PreviewAspect`. `PushClipRect` wraps the scene draw so logo / wheel / cover can't bleed into the bars. Landscape only — `enforceLandscape(a)` clamps after every edit.

### 1.2 Controls
- **Tooltips over inline help.** Sidebars show labels + values only; explanations attach via `SetTooltip` on hover. Short-hand button labels are fine if the tooltip expands them. (Excludes modal bodies, first-run wizards, and inline validation/status messages.)
- **Minimal surfaces.** New feature panels ship with the single input + single action that makes the feature work. No preset prompts, no Regenerate/retry shortcuts unless the user later asks. Wait for actual usage pain.
- **Use gameplay terms.** UI labels match the note-tool palette: **Click / Hold / Flick / Slide / Arc / ArcTap / Shadow**. Avoid renderer-internal jargon (Tap, Halo, Arc Tile, Ribbon) in visible labels — internals stay internal.

### 1.3 Persistence
- **User-data writes fail loud.** Disk writes (`music_selection.json`, charts, materials) never use `error_handler_t::replace` or analogous silent-substitution flags. Transcode at the boundary (CP_ACP → UTF-8 via the `toUtf8` helper), wrap the write in `try/catch`, and on exception **refuse to write** + log to stderr. A throw the user sees is annoying; corrupted data they can't see is destruction.
- **Auto-save is drag-safe.** 30 s debounced cadence in `Engine::tickAutoSave`; aborted while `IsAnyMouseDown()`; never fires during Gameplay layer. Crash hooks (`SetUnhandledExceptionFilter`, `glfwSetWindowCloseCallback`, `std::set_terminate`, `mainLoop` exit) all flush through the same `performAutoSaveNow(reason)` and are idempotent.

### 1.4 Behavior
- **Run after every code update.** Engine launches in the background after every successful build so the user can visually verify; don't wait to be asked. Skip only for pure docs/memory edits.
- **Test Game = whole game.** A single green "Test Game" button at the top-right of every editor page launches `MusicGameEngineTest.exe --test <project>` end-to-end (StartScreen → MusicSelection → Gameplay). No per-page "Game Preview" tabs.
- **Asset-system everywhere.** All editors use the unified `importAssetsToProject()` from `AssetBrowser.h`. Asset tile tooltips show **filename only** — full paths stay in drag-drop payloads + persisted fields.

## 2. Global Palette

Set in `ImGuiLayer::init` (overrides `StyleColorsDark`):

| Role | RGB / α | Notes |
|---|---|---|
| WindowBg | (0.03, 0.03, 0.03) | near-black canvas |
| FrameBg | 0.10 | input fields, sliders |
| Button (primary) | (0.00, 0.55, 0.85) | cyan |
| Button (active) | (0.95, 0.30, 0.75) | magenta |
| Header family | (0.22–0.70, α 0.55–0.75) | neutral gray — purple-free |
| Text / disabled | 0.96 / 0.54 | |
| Rounding | Frame 4, Window 6, Popup 6 | |

The Android `AndroidEngine.cpp` keeps the stock dark theme; the override only applies to editor windows.

## 3. Layer Flow

```
EditorLayer: ProjectHub → StartScreen → MusicSelection → SongEditor
                                     ↘  Settings  (4th layer)
                                       (Test Game = separate child process)
```

Each layer is a self-contained ImGui panel under `engine/src/ui/`. The Test Game spawns `MusicGameEngineTest.exe --test <project_path>` via `CreateProcessW`; the editor window is unaffected.

## 4. Per-Page Property Inventory

Sourced from the current code. Field names match the C++ structs / member variables; ranges are slider clamps where applicable.

### 4.1 ProjectHub (`ProjectHub.h` / `ProjectHub.cpp`)

`ProjectInfo` per project:

| Field | Type | Source |
|---|---|---|
| name | string | `project.json` |
| path | string | filesystem |
| version | string | `project.json` |
| defaultChart | string | `project.json` |
| shaderPath | string | `project.json` |
| lastModified | string `YYYY-MM-DD HH:MM` | `fs::file_time_type` |
| lastModifiedRaw | long long unix seconds | sort key |

**Sections:** action bar (search `InputTextWithHint` + `+ Create Game` + `+ Add File` + conditional `Build APK: <name>` magenta button) → project rows (`Selectable` with `AllowDoubleClick`, single-click selects, double-click opens) → modal dialogs (Create / Add File / APK build).

### 4.2 StartScreenEditor (`StartScreenEditor.{h,cpp}`)

Persisted to `start_screen.json`:

| Section | Field | Type / Range |
|---|---|---|
| Background | file, type | path / `none\|image\|gif\|video` |
| Logo | type, logoText, fontSize, color[4], bold, imageFile, glow, glowColor[4], glowRadius, position{x,y}, scale | `text\|image` / clamp `fontSize ≤ 96`, `scale ≤ 3.0` |
| Tap Text | text, position{x,y}, size | clamp `size ≤ 72` |
| Transition | effect, duration, customScript | `fade\|slideLeft\|zoomIn\|ripple\|custom` |
| Audio | bgMusic, bgMusicVolume, bgMusicLoop, tapSfx, tapSfxVolume | path / 0..1 / bool |

**UI:** preview (left, `fitAndLetterbox`) + properties (right, CollapsingHeaders) + bottom Assets strip. Each section's first body row is a `Default` pill (placed inside the body, not the header, so the CollapsingHeader hit-area doesn't steal the click). Italic and per-section Load/Reset removed; tap-text content + position controls removed (size only). Materials moved out — see SongEditor.

**Fit-to-width:** if `CalcTextSizeA(fontSize).x > pw * 0.96`, both `renderPreview` and `renderGamePreview` scale `fontSize` by `pw*0.96 / textSize.x` before draw.

### 4.3 MusicSelectionEditor (`MusicSelectionEditor.{h,cpp}`)

Persisted to `music_selection.json`. Top-level page fields:

| Field | Type | Notes |
|---|---|---|
| background | string (path) | painted under both preview + game-preview |
| fcImage / apImage | string (path) | 96×96 page-level achievement badges (per-game, not per-song) |
| sets[] | array | each `MusicSetInfo` |

`MusicSetInfo`: `{ name, coverImage, songs[] }`.
`SongInfo`:

| Field | Type | Notes |
|---|---|---|
| name, artist, coverImage, audioFile | string | |
| chartEasy / chartMedium / chartHard | string | UCF chart paths |
| scoreEasy/Medium/Hard | int | runtime-written, read-only in editor |
| achievementEasy/Medium/Hard | int | runtime-written, read-only in editor |
| previewStart, previewDuration | float | `previewStart=-1` → auto (25 % of duration) |
| gameMode | `GameModeConfig` | per-song, see §5 |

**Ephemeral state:** `m_autoPlay` (orange when on, grey when off — toggle button below START), `m_previewPath` (decode cache).

**Sections:**
- **Preview** (left): aspect controls row + `fitAndLetterbox` scene + 5-band frosted overlay + rhombus song cards (FC + AP diamonds via `AddImageQuad`) + AUTO PLAY toggle.
- **Hierarchy panel** (right, 70/30 split): set tree + song sub-items + add/delete; Background drop zone; FC + AP badge drop zones (96×96 aspect-fit). Per-difficulty score/achievement is **read-only** here (runtime-owned).
- **Properties panel:** name + artist + audio + 3 chart paths + cover (cover-path text hidden — thumbnail + Clear remain).
- **Bottom Assets strip:** images + audio + materials (live `drawMaterialPreviewAt` previews).
- **Audio preview:** gated on `engine->isTestMode()`; 500 ms dwell on selection-change → `AudioEngine::playFrom(previewStart)` → stop after `previewDuration` (10–45 s cap).
- **Nav:** `< Back` (left), `Next: Settings >` (right).

### 4.4 SongEditor (`SongEditor.{h,cpp}`)

#### Left sidebar — `BeginTabBar`

| Tab | Renderer | Hosts |
|---|---|---|
| **Basic** | `renderProperties()` | Game Mode (mode + dimension + tracks), Audio (file + Preview Clip group), Judgment + Scoring (perfect/good/bad ms + scores + total-score auto-calc), HUD (Pos / Size / Color), Background, Camera, AI (Copilot config gear) |
| **Note** | `renderNotePage()` | Lane Layout band (Tracks / Sky Height / Default Note Width) + per-note-type CollapsingHeader (filtered by mode) + Playfield section. Each note-type section carries its filtered material slot pickers + drag-drop **Texture** + **Music Effect** rows (`GameModeConfig::noteAssets`) |
| **Material** | `renderMaterialBuilderPage()` | Delegates to `StartScreenEditor::renderMaterials(hideSelector=true)`. Inline Selectable list suppressed; MAT-tile left-click in Assets calls `selectMaterialByName(stem)` |

#### Right sidebar — `BeginTabBar` (`m_rightSidebarTab`, enum `RightSidebarTab { Copilot, Audit }`, `SongEditor.h:230`)

| Tab | Source | Behavior |
|---|---|---|
| **Copilot** (default) | `engine/src/editor/AIEditorClient` + `ChartEditOps` | NL prompt → Apply / Undo / config gear |
| **Audit** | `ChartAudit::computeAuditMetrics` | Local metrics summary + clickable issue buttons; hover/pin paints colored bands on the waveform via `auditCategoryColor` |

Toolbar Audit button is one-shot: `m_rightSidebarTab = Audit` + `m_rightSidebarTabPending = true` + `m_copilotBarOpen = true`. The `ImGuiTabItemFlags_SetSelected` flag is consumed once per pending event (asserting it every frame fights user clicks).

#### Note toolbar — `renderNoteToolbar`

Order: `Marker · Click · Hold · Flick · Slide · Arc · ArcTap · Analyze · Clr Mrk · Thin · Undo · Place · AI · Clr Note · Audit`.
- Note-type buttons toggle the active `NoteTool`; visibility filtered per mode (see §5).
- `Thin` is disabled when `markers().empty()`; `Undo` is disabled unless a snapshot exists for the current difficulty.
- `AI` opens the tuning popup (`ai_tuning` non-ScanLine, `ai_tuning_scan` ScanLine):

| Field | Range | Default | Notes |
|---|---|---|---|
| `m_autoFlickPct` | 0.50 – 0.99 | 0.88 | strength percentile → Flick |
| `m_autoHoldMin` | 0.05 – 0.80 s | — | sustain → Hold |
| `m_autoAntiJack` | bool | true | nudge same-lane repeats |
| `m_autoLaneCooldownMs` | 0 – 400 ms | — | hidden in ScanLine |
| `m_autoScanTimeGapMs` | 0 – 300 ms | — | ScanLine only |
| `m_autoMarkerThinNps` | 1 – 10 / s | 4 | target NPS for `Thin` |

#### Scene-area extras

- **3D Drop:** Arc Height Curve editor strip (fixed 120 px) above the timeline.
- **Circle:** Disk FX keyframe strip (34 px) above the scene.
- **ScanLine:** paginated page-based authoring (one sweep = one page) — see §5.4.
- **Bottom waveform** (100 px) shared with both the Preview Clip overlay and the Audit problem-range overlay (hover/pin contract).

### 4.5 SettingsPageUI / SettingsEditor (`SettingsPageUI.{h,cpp}` + `SettingsEditor.cpp`)

`PlayerSettings` — exactly 8 fields, persisted to `<internal_storage>/player_settings.json` on Android, in-memory on the editor preview:

| # | Field | Type / Range | Default | UI | Wiring |
|---|---|---|---|---|---|
| 1 | musicVolume | float 0..1 | 0.8 | SliderFloat `%.2f` | `AudioEngine::setMusicVolume` |
| 2 | hitSoundVolume | float 0..1 | 0.8 | SliderFloat `%.2f` | `AudioEngine::setSfxVolume` |
| 3 | hitSoundEnabled | bool | true | Checkbox | `AudioEngine::setHitSoundEnabled` |
| 4 | audioOffsetMs | float -200..+200 | 0 | SliderFloat `%.0f ms` + tap-to-calibrate | `HitDetector::setAudioOffset` |
| 5 | noteSpeed | float 1..10 | 5 (= 1.0×) | SliderFloat `%.1f` | `GameModeRenderer::setNoteSpeedMultiplier` (Bandori/Arcaea/Lanota; ScanLine ignores) |
| 6 | backgroundDim | float 0..1 | 0.3 | SliderFloat `%.2f` | overlay via `GetBackgroundDrawList` |
| 7 | fpsCounter | bool | false | Checkbox | `ImGui::GetIO().Framerate` text |
| 8 | language | string | "en" | Combo `en/zh/ja/ko` | **store-only**, no localization wired |

Layout: full-screen opaque scrim window + centered 640 px card (`ChildBg`); `BringWindowToDisplayFront` each frame so the scrim stays above caller windows without touching active-item state. Section grouping: Audio (1–4) / Gameplay (5) / Visual (6–7) / Misc (8). Slider width reserves 55 % of card width.

## 5. Per-Game-Mode Property Inventory

Authoritative source: the renderer impls in `engine/src/game/modes/` (the toolbar still has known visibility bugs — see `project_note_types_per_mode.md`). **Phigros mode is out of scope.**

`GameModeConfig` fields shared by every mode (`ProjectHub.h:27`):

```
type, dimension, trackCount,
perfectMs/goodMs/badMs, perfectScore/goodScore/badScore, totalScore,
fcImage, apImage,
scoreHud / comboHud (HudTextConfig: pos, fontSize, color, scale, bold, glow, glowColor, glowRadius),
audioOffset, cameraEye[3] / cameraTarget[3] / cameraFov,
backgroundImage,
noteAssets : map<sectionName, {texturePath, sfxPath}>
```

Mode-specific subset:

### 5.1 Bandori — DropNotes 2D (`BandoriRenderer.cpp`)

| Property | Range / Default |
|---|---|
| `trackCount` | 3 – 12 |
| Note types | Click, Hold, Flick |
| Default note width | 1 lane |
| Material slots | Tap Note, Hold Note (Body / Head / Tail), Flick Note, Lane Divider, Judgment Bar |
| Slide branch | `BandoriRenderer.cpp:471` — unreachable; toolbar hides Slide for 2D |
| Note speed | scales `SCROLL_SPEED` |

### 5.2 Arcaea — DropNotes 3D (`ArcaeaRenderer.cpp`)

| Property | Range / Default |
|---|---|
| `trackCount` | 3 – 12 |
| `dimension` | `ThreeD` (combo + button toggle) |
| `skyHeight` | -1.0 – 3.0, default 1.0 — sky judgment line world-Y |
| Note types | Click, Hold, Flick, **Arc**, **ArcTap** |
| Arc data | `EditorNote::arcWaypoints` (≥ 2 `{time, x, y, easeX, easeY}`); decomposes into N-1 connected `ArcData` segments on export; auto-merges connected segments on import |
| Arc colors | Cyan (0) / Pink (1) via inline C/P toolbar buttons |
| Arc void flag | hidden, no-judgment connectors |
| ArcTap | sky-only — diamond render via `MeshRenderer` rect-prism + shadow |
| Material slots | Tap Note, Hold Note, Arc Tile (renamed visually to "Arc"), ArcTap, Sky Bar, Judgment Bar |
| Note speed | scales `SCROLL_SPEED` |
| Editor extras | Arc Height Curve editor (120 px), per-waypoint draggable handles, flatten-to-2-endpoints button |

### 5.3 Lanota — Circle (`LanotaRenderer.cpp`)

| Property | Range / Default |
|---|---|
| `trackCount` | 3 – 36 (`> 32` ungates lane mask via `0xFFFFFFFFu`) |
| `diskInnerRadius` | 0.2 – 3.0, default 0.9 — spawn-disk radius |
| `diskBaseRadius` | 1.0 – 6.0, default 2.4 — hit-ring radius (drives lane reachability) |
| `diskRingSpacing` | 0.1 – 1.5, default 0.6 |
| `diskInitialScale` | 0.3 – 5.0, default 1.0 — base multiplier in editor + runtime |
| Note types | Click, Hold, Flick (red tint via `SlotArcTile`) |
| Disk animation | per-difficulty `m_diffDiskRot/Move/Scale` keyframes (Add/Edit/Delete with Linear / SineInOut / QuadInOut / CubicInOut easing) |
| Lane mask | rebuilt when Tracks or any disk slider changes (`m_laneMaskDirty`); fixed FOV bound `kFovHalfX/Y + 0.15` |
| Material slots | Tap, Hold, Flick, Disk Surface, Ring, Lane Divider, Judgment Bar |
| Note speed | divides `APPROACH_SECS` |
| Editor extras | "Reset disk defaults" button restores 0.9 / 2.4 / 0.6 / 1.0 |

### 5.4 Cytus — Scan Line (`CytusRenderer.cpp`)

| Property | Range / Default |
|---|---|
| `trackCount` | unused (lane-less) |
| Note types | Click, Hold, Flick, **Slide** (only mode that supports it) |
| Authoring | paginated — one page = one sweep; default duration `240 / BPM` s; direction alternates per page |
| Per-page speed | `ScanPageOverride`, 0.25× – 4× (set back to 1.0 → removed) |
| Per-difficulty data | `m_diffScanPages`, `m_diffScanSpeed` |
| BPM sensitivity | mid-page BPM change truncates the page (`partialTail=true`) |
| Marker snap | `min(0.06s, 0.15 × page.duration)`; `Alt` to disable |
| Material slots | Scan Line, Tap, Hold, Flick, Slide, Track, Judgment Bar |
| Note speed | **ignored** (sweep-driven; see `project_note_speed_setting.md`) |
| AI tuning | `m_autoScanTimeGapMs` replaces `laneCooldownMs` |

### 5.5 Material slot summary

A **slot** is a named visual role per mode, with a default `MaterialKind` (Unlit / Glow / Scroll / Pulse / Gradient / Custom). Per-slot overrides live in the chart as `{slot, asset}` references; Phase 4 promotes inline entries to per-mode `default_<mode>_<slug>.mat` shared assets, with differing entries spilled to `<chartStem>__<slug>.mat`. Custom kind compiles `.frag → .spv` via `ShaderCompiler` (mtime-cached) and adds Template + Compile + AI Generate buttons in StartScreen → Properties → Materials. The SongEditor slot picker is filtered via `MaterialAssetLibrary::namesCompatibleWith(mode, slug)`.

## 6. Cross-References

- Layout deltas + milestone history → `sys7_editor.md`
- Rendering / material kinds → `sys1_rendering.md`
- Auto-save + crash hooks → `sys3_core_engine.md`
- Note-type per-mode authoritative table → `memory/project_note_types_per_mode.md`
- Player Settings scope rules → `memory/project_settings_page.md`
- Naming conventions → `memory/feedback_naming.md`
