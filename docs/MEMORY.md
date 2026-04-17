# Music Game Engine — Memory Index

C++20 / Vulkan music game engine at `C:/Users/wense/Music_game/`.
Supports BanG Dream, Phigros, Arcaea, Cytus, Lanota as plugin game modes.

## 8 Systems

| # | System | Doc | Status |
|---|---|---|---|
| 1 | Rendering (Vulkan backend + batchers + shaders) | [sys1_rendering.md](sys1_rendering.md) | Done |
| 2 | Resource Management (chart/audio/textures/BPM) | [sys2_resources.md](sys2_resources.md) | Done |
| 3 | Core Engine (ECS + SceneGraph + main loop) | [sys3_core_engine.md](sys3_core_engine.md) | Done |
| 4 | Input & Gesture (touch/keyboard/DPI) | [sys4_input.md](sys4_input.md) | Done |
| 5 | Gameplay (HitDetector/Judgment/Score) | [sys5_gameplay.md](sys5_gameplay.md) | Done |
| 6 | Game Mode Plugins (5 renderers) | [sys6_game_modes.md](sys6_game_modes.md) | Done |
| 7 | Editor UI (ProjectHub -> SongEditor) | [sys7_editor.md](sys7_editor.md) | Done |
| 8 | Android Packaging (APK pipeline) | [sys8_android.md](sys8_android.md) | Done |

## Other
- [Dev Log](devlog.md) — chronological daily log (2026-03-19 → today), includes UI/UX design decisions

## Recent Milestones
- Cross-lane holds + Bandori-style sample-tick gating (2026-04-10)
- Scan Line mode end-to-end rebuild (2026-04-11)
- Circle mode disk animation (keyframed rotate/scale/move) (2026-04-12)
- Cross-mode integration audit + 10 bug fixes (2026-04-12)
- Scan Line: variable-speed + straight-line slides + multi-sweep holds (2026-04-12)
- Arc/ArcTap editor: 3-panel editing (timeline ribbons, height curve, cross-section preview) (2026-04-12)
- Arc pipeline end-to-end: ChartLoader field-name compat, SongEditor reimport with parent fixup, ArcaeaRenderer ArcTap diamond rendering (2026-04-12)
- Bandori Slide color: now purple (was identical to Tap yellow) (2026-04-12)
- All 4 reachable game modes verified end-to-end: 2D/3D DropNotes, Circle, ScanLine (2026-04-12)
- Arcaea sky-region authoring: Arc/ArcTap now render + place in the purple sky region (was ground); non-inverted arcX→row mapping; void auto-parent arcs hidden from editor/scene preview; Arc click commits default 0.5s arc; ArcTap auto-spawns hidden parent arc when none exists (2026-04-12)
- ArcaeaRenderer dynamic track count: reads GameModeConfig::trackCount in onInit and maps laneX across ground width; fixes notes falling outside highway on non-5-lane charts (2026-04-12)
- Editor scene preview: skips Arc/ArcTap in generic lane-rect loop (no ghost tap under ArcTap diamond); draws arc ribbons + ArcTap diamonds in 3D scene branch using sky-space world coords (2026-04-12)
- Achievement FC/AP image pickers are now asset-drag-only 96×96 drop slots (text input + Browse removed); background picker unchanged (2026-04-12)
- Per-(mode, difficulty) chart files: `assets/charts/<song>_<modeKey>_<diff>.json` with `modeKey ∈ {drop2d, drop3d, circle, scan}`; mode/dimension button clicks auto-save old charts then reload via `reloadChartsForCurrentMode()`; `loadChartFile()` extracted from `setSong()` (2026-04-12)
- Auto Play mode: Music Selection toggle → `Engine::launchGameplay(..., autoPlay)` → `HitDetector::autoPlayTick` emits Perfect hits for every note incl. holds/arcs; keeps `currentLane` synced so sample ticks score Perfect (2026-04-12)
- Held-hold bloom: `HitDetector::activeHoldIds()` + `GameModeRenderer::m_activeHoldIds`; Engine pushes the set each frame; Bandori + Lanota brighten hold body/head RGB above 1.0 so the brightness-threshold bloom post-process glows them while held (2026-04-12)
- Particle bursts in all modes: Arcaea + Cytus `showJudgment` now `emitBurst` (previously only Bandori + Lanota); renderer pointer cached in `onInit` (2026-04-12)
- Lanota hold body rewritten as true arc slices: two points per sample on the ring at `angle ± hA` (was linear-tangent trapezoids that collapsed into radial rectangles); `HoldBody` gains `noteId` for hold-active lookup (2026-04-12)
- Circle disk defaults configurable: `GameModeConfig::diskInnerRadius / diskBaseRadius / diskRingSpacing / diskInitialScale`; UI in SongEditor "Disk Layout" panel; persisted; `LanotaRenderer` constexpr constants replaced with per-instance members seeded in `onInit` (2026-04-12)
- Lane-mask reachability fix: `laneMaskForTransform` now takes `outerR` (from `gm.diskBaseRadius`) and uses `max(fov_half, outerR)+0.15` bounds, so the ring's top/bottom lanes aren't always masked out; trackCount + disk sliders now set `m_laneMaskDirty=true` so the cached mask rebuilds when lane count changes (2026-04-12)
- Lane-mask gating actually gates: replaced `max(fov, r)+0.15` with a fixed FOV bound (`±3.0 × ±2.31` + 0.15 margin) so enlarging the disk finally produces unreachable lanes. `diskInitialScale` now behaves as a real base multiplier in both `SongEditor::rebuildLaneMaskTimeline` and `LanotaRenderer::onUpdate` (`m_diskInitialScale` member added; previously overwritten by `getDiskScale()` every frame). Scale slider caps raised: keyframe 3.0→5.0, initial 2.0→5.0 (2026-04-14)
- Circle hold body redesigned to match real Lanota: narrow rim-following ribbon with constant pixel width via screen-space tangent offset, Catmull-Rom Hermite smoothing for Bezier-style waypoint corners (renderer-local `evalHoldLaneSmoothLanota`, hit detection unaffected), head-anchor arc tile at the rim end, real-time radius mapping so the body spawns from the inner disk like a tap. Particle burst position fix: hold-tick fallback now spawns at outer hit ring, not inner disk (2026-04-15)
- Bandori hold body fixes: visible-window tessellation replaces uniform [0,dur] sampling (segments no longer pop in at the far plane); fixed chart-time grid + transition-anchored corner samples (corner geometry pinned, no morph as the hold approaches); explicit `tOffLo`/`tOffHi` boundary samples (judgement-line flicker gone during active holds); active-hold `zNear=0` clipping (past portion hidden while pressing); missed/bad head culling at +0.15s if `!holdActive` (whole hold disappears) (2026-04-15)
- Gameplay restart fully rebuilt: `Engine::restartGameplay()` caches chart + project + audio paths at launch, re-creates renderer via `createRenderer(m_gameplayConfig)`, re-runs `setMode()` (clears HitDetector + judgment + score + touches + keyboard holds), resets clock to lead-in, restores `m_pendingAudioPath`, calls `m_clock.resume()` (the pause menu had paused it). Three sequential bugs fixed: state reset, paused clock, missing audio path → instant results screen (2026-04-15)
- Music playback lag during dense hold sample ticks fixed: `Engine.cpp` no longer calls `m_audio.playClickSfx()` on hold sample ticks. `playClickSfx()` allocates and leaks a fresh `ma_audio_buffer` + `ma_sound` per call; dense Bandori sample ticks were stalling miniaudio's mixer and stuttering music as holds crossed the judgement line. Pooled SFX is the right long-term fix (2026-04-15)
- Editor flow polish: removed Test Game button from StartScreenEditor and MusicSelectionEditor; selection page START button now gated on `Engine::isTestMode()` so it's inert in the editor but launches gameplay in the test-game player flow; in test mode, Pause→Exit returns to MusicSelection instead of closing the standalone window (2026-04-15)
