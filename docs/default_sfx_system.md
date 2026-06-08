# Default Sound-Effect Library System

> Status as of 2026-06-09. This is the complete handoff for the bundled default
> SFX + library + editor-picker feature. Read the **"How to resume tomorrow"**
> section at the bottom to continue. All code is implemented and the desktop
> build is green; the only remaining work is choosing/obtaining the real CC0
> sounds (the placeholder WAVs are in place so the pipeline runs end-to-end).

---

## 1. Goal

Ship the engine with a **browsable library of built-in sound effects** so a game
developer (the engine's user) always has good sounds out of the box, and can:

- Pick any bundled sound for a note type or UI control from an in-editor picker.
- Import their own `.mp3/.wav` into the project and apply it.
- Leave a slot empty and get a sensible **engine default** (never silent).

Two sound classes:
- **Short** one-shots — note hits (Click/Flick/ArcTap…), UI buttons (START,
  scroll, back, toggle, tap-to-start).
- **Long** loops — the sustained tone while a Hold note is pressed, plus a short
  tick at each hold sample point.

Sources: free sounds from freesound.org + Kenney (prefer **CC0** for safe
redistribution inside shipped games). Licensing policy: prefer CC0; final policy
TBD when sounds are chosen.

---

## 2. Current status

| Area | State |
|------|-------|
| Engine audio (cache + looping) | ✅ implemented, builds |
| DefaultSfx library façade (manifest-driven) | ✅ implemented, builds |
| Gameplay wiring (note hits, hold loop, ticks) | ✅ implemented, builds |
| UI wiring (music-select, start, settings) | ✅ implemented, builds |
| Editor picker (note Hit + Hold Sustain) | ✅ implemented, builds |
| Editor picker (UI buttons: wheel + tap) | ✅ implemented, builds |
| Android extraction + APK asset copy | ✅ code wired (not APK-tested) |
| Placeholder WAVs (8 short + 2 long) | ✅ generated, pipeline runs |
| **Real CC0 sounds chosen/obtained** | ⏳ TODO (candidate list in §7) |
| Manual audio/GUI verification | ⏳ TODO (can't be done headless) |
| APK build + on-device check | ⏳ TODO (needs real sounds) |

Desktop `MusicGameEngineTest` builds green (only pre-existing C4819 codepage
warnings) and runs `--test Projects/test` without crashing. Actual sound output
and picker clicks were **not** verified (headless environment — no audio, no GUI
interaction). That manual pass is listed in §8.

---

## 3. The original approved plan (verbatim)

This is the plan that was approved before the scope grew into a full library
(§5 explains how it evolved). Kept here in full for the record.

> # Default (Fallback) Sound-Effect System
>
> ## Context
> Today the engine has no fallback SFX. `GameModeConfig::noteAssets[...].sfxPath`
> is loaded and saved but never played during gameplay; the UI has only three
> project-supplied SFX fields on the Music Selection page (`m_wheelScrollSfx` /
> `m_difficultySfx` / `m_wheelClickSfx`) and an unwired `m_tapSfx` on the Start
> Screen. So if a game dev does not add their own SFX assets, the game is silent
> on note hits and most buttons.
>
> Goal: ship a small set of built-in default sound effects with the engine so
> every note hit and key button makes a sound out-of-the-box, while any per-note
> / per-button override the dev supplies still wins. Most roles are very short
> one-shots (clicks, buttons); the hold role needs a sustained looping sound
> while held plus short tick sounds at sample points.
>
> Two real constraints discovered during exploration:
> 1. `AudioEngine::playSfxFile()` decodes from disk on every call and
>    `playClickSfx()` leaks a `ma_audio_buffer`+`ma_sound` per call — the code at
>    `Engine.cpp:459-466` deliberately plays no sound on hold ticks for exactly
>    this reason (audio-thread stutter on dense holds). The default system must
>    therefore add a preloaded/cached playback path and looping support — neither
>    exists yet.
> 2. Player-facing audio must be wired in the shared game-side View classes
>    (`engine/src/game/screens/*`), not editor code, so it works on Android. The
>    existing `MusicSelectionView::playWheelSfx` is the template to generalize.
>
> Sources are free sounds from freesound.org. Licensing policy is TBD (decided
> later); when we search we will prefer CC0 to be safe for redistribution.
>
> ## Phase 0 — Curated sound shopping list (deliver FIRST, for user review)
> Produce a list of candidate sounds before obtaining anything. Deliver a role
> spec + search guidance list; the user reviews; for approved roles they ask to
> obtain the files (a small Python downloader using the freesound API + the
> user's token, normalizing each to 16-bit PCM WAV, trimmed, peak-normalized,
> mono where appropriate; the hold-loop rendered seamless/looping).
>
> Default SFX roles (keep the bundled set lean — note types map onto these):
> click (Click/Tap/ArcTap, 30–80 ms), flick (60–150 ms), hold_start (40–100 ms),
> hold_loop (0.3–1 s seamless loop), hold_tick (15–40 ms), hold_end (40–120 ms),
> ui_tap, ui_confirm, ui_scroll, ui_back, ui_toggle. Filter to CC0 on freesound.
>
> ## Phase 1 — AudioEngine: cached + looping playback
> `engine/src/engine/AudioEngine.{h,cpp}` (miniaudio, dedicated sfxGroup exists).
> Add: `preloadSfx(key, path)` (decode once into a cached ma_audio_buffer);
> `playCachedSfx(key, minIntervalMs=0)` (cheap instance from cache; min-interval
> rate-limits dense ticks); `startLoopingSfx(path)`/`stopLoopingSfx(handle)`/
> `stopAllLoopingSfx()`. Keep `playSfxFile()` for arbitrary overrides. All new
> playback respects hitSoundEnabled / sfxVolume.
>
> ## Phase 2 — Default SFX resolution + bundling
> New repo-root `sfx/` folder (mirrors `shaders/`). CMake copy step next to the
> exe. New `engine/src/engine/DefaultSfx.{h,cpp}`: SfxRole enum;
> defaultFileForRole; sfxBundleDir (desktop next to exe, Android extracted path);
> preloadDefaults; resolveNoteSfx. Register DefaultSfx.cpp in BOTH desktop and
> `engine/src/android/CMakeLists.txt` (Material files were once missed there).
>
> ## Phase 3 — Gameplay wiring (`Engine.cpp`/`.h`)
> Preload at launchGameplay; note-hit role mapping in dispatchHitResult
> (isHoldEnd→HoldEnd, Flick→Flick, Hold→HoldStart, ArcTap→Click, else Click);
> hold sustained loop keyed by noteId on begin, stopped on end/break/teardown;
> hold ticks play throttled cached tick SFX replacing the no-SFX block.
>
> ## Phase 4 — UI wiring (game-side Views)
> MusicSelectionView scroll/difficulty/START fall back to defaults when empty +
> AUTO PLAY toggle; StartScreenView wire tap-to-start; SettingsPageUI Back/Accept
> /Retry/Cancel. Preload UI defaults at app init.
>
> ## Phase 5 — Android packaging
> AndroidEngine onWindowInit: extraction whitelist + m_sfxDir + preloadDefaults;
> AndroidEngineAdapter audio already routes to the same AudioEngine; copy sfx/
> into APK assets.
>
> ## Verification
> Build, run, play a chart with no overrides → hear defaults; hold → start/loop/
> ticks/end; UI sounds; override replaces default; hit-sound-off silences;
> Android APK extract + on-device.

---

## 4. Architecture as built

### Data model

- **`sfx/` bundle** (ships with engine, copied next to the exe and into the APK):
  - `sfx/short/` — short one-shot `.wav` files
  - `sfx/long/`  — long sustained-loop `.wav` files (must loop seamlessly)
  - `sfx/manifest.json` — the single source of truth (library lists + per-role
    defaults), read by desktop, Android extraction, and the editor picker.

- **`manifest.json` schema**
  ```json
  {
    "version": 1,
    "short": [ { "file": "short/click.wav", "name": "Click" }, ... ],
    "long":  [ { "file": "long/drone.wav",  "name": "Warm Drone" }, ... ],
    "defaults": { "click": "short/click.wav", "hold_loop": "long/drone.wav", ... }
  }
  ```
  `defaults` keys are the 11 role strings: `click, flick, hold_start, hold_loop,
  hold_tick, hold_end, ui_tap, ui_confirm, ui_scroll, ui_back, ui_toggle`.

- **Chart references** (stored in `GameModeConfig::noteAssets[section]`):
  - `sfxPath` — the **hit** sound (short). `loopSfxPath` — the **sustain** loop
    (long), Hold only.
  - A value is one of: `""` (use the role default), `builtin:short/click.wav`
    (a bundled library pick), or a project-relative path like
    `assets/audio/my_hit.mp3` (the dev's own imported file).
  - `DefaultSfx::resolveRef(ref, projectDir)` turns any of these into a playable
    path (`builtin:` → bundle dir; relative → project dir; absolute → unchanged).

### Runtime (engine)

- **AudioEngine** gained a preloaded cache + looping (see §5 file list). Dense
  SFX (note clicks, hold ticks) play from a decoded-once `ma_audio_buffer` with
  an optional per-key throttle, fixing the original audio-thread-stutter reason
  that holds ticks were silent. Holds get a real looping `ma_sound`.
- **DefaultSfx** is a namespace façade backed by `manifest.json`. It exposes the
  library lists for the picker, the per-role defaults, ref resolution, and
  `preloadDefaults(audio)`. A missing/partial manifest degrades to silence (no
  crash) — which is why everything runs even before real sounds exist.
- **Engine** preloads UI defaults at init and note defaults + per-song overrides
  at gameplay launch. `dispatchHitResult` plays the override-or-default hit
  sound (gated by the player's hit-sound toggle). The **hold loop** is driven by
  diffing the active-hold set each frame (`updateHoldLoopSfx`) — newly-held ids
  start a loop, departed ids stop it — which covers begin/end/break/pause across
  **all modes** uniformly without touching each `beginHold` site. Hold ticks
  play a throttled (≈60 ms) cached tick.

### Editor picker

A "Lib" button next to each sound field opens a popup listing the
category-filtered library (short or long); each entry has a **Play** preview and
selecting writes `builtin:<file>`. Sits alongside the existing drag-drop /
Browse / Clear. Covered surfaces:
- **SongEditor** Note tab: every note type's **Hit Sound** (short) for **all
  modes** (sections are mode-driven: Drop2D/Circle = Click/Hold/Flick; Drop3D =
  Click/Flick/ArcTap/Arc; ScanLine = Click/Hold/Flick/Slide). Hold also gets a
  **Sustain Sound** row (long).
- **MusicSelectionEditor** Wheel Sounds: Scroll / Difficulty / Click (short).
- **StartScreenEditor** Audio: Tap Sound Effect (short).

Decision (2026-06-09): **Slide does not get its own sustain slot** — all held
notes (incl. ScanLine slides) share the single "Hold Note" sustain pick/default.

---

## 5. File-by-file change log (this session)

**New files**
- `engine/src/engine/DefaultSfx.h` / `.cpp` — library façade: `Role`, `Category`,
  `LibEntry`; `cacheKeyForRole`, `setBundleDir`/`bundleDir` (reloads manifest),
  `library(cat)`, `allLibraryFiles`, `resolveRef`, `defaultFileForRole`,
  `pathForRole`, `preloadDefaults`, `roleForNoteHit`, `sectionKeyForNoteType`;
  lazy `manifest.json` loader (nlohmann).
- `sfx/manifest.json` — library index + defaults (currently PLACEHOLDER entries).
- `sfx/MANIFEST.txt` — human notes on the library + license tracking.
- `tools/gen_placeholder_sfx.py` — pure-stdlib WAV generator for the placeholders.
- `sfx/short/*.wav` (8) + `sfx/long/*.wav` (2) — placeholder sounds.

**AudioEngine** (`engine/src/engine/AudioEngine.{h,cpp}`)
- Added `preloadSfx`, `playCachedSfx(key, minIntervalMs)`, `startLoopingSfx`,
  `stopLoopingSfx`, `stopAllLoopingSfx`, private `reapFinishedSfx`.
- `Impl`: `sfxSounds` changed to `ActiveSfx{ma_sound*, ma_audio_buffer*}`; added
  `sfxCache` (key→decoded `CachedSfx{frames,channels,rate,frameCount}`),
  `lastPlayMs` (throttle), `loopSounds` (handle→ma_sound), `nextLoopHandle`.
- `shutdown` cleans loops + cache + buffers.

**Engine** (`engine/src/engine/Engine.{h,cpp}`)
- `#include "engine/DefaultSfx.h"`. New members/methods: `m_holdLoopHandles`,
  `preloadGameplaySfx`, `updateHoldLoopSfx`, `stopAllHoldLoopSfx`.
- `init()`: `DefaultSfx::preloadDefaults(m_audio)` after `m_audio.init()`.
- `dispatchHitResult`: plays override-or-default hit sound (gated by
  `hitSoundEnabled`), override key `ov_<section>` else `def_<role>`.
- hold-tick block (was the "No SFX" comment): now throttled cached `def_hold_tick`.
- `update()`: computes active holds once → `updateHoldLoopSfx`; `else`
  (paused/stopped) → `stopAllHoldLoopSfx`.
- `launchGameplay` + `launchGameplayDirect`: call `preloadGameplaySfx()`.
- `restartGameplay`: `stopAllHoldLoopSfx()`.
- `updateHoldLoopSfx`: resolves Hold Note `loopSfxPath` override (via
  `resolveRef`) else default long loop; starts/stops loops by active-set diff.
- `preloadGameplaySfx`: `preloadDefaults` + preload each non-empty `sfxPath`
  override under `ov_<section>` via `resolveRef`.

**Data model**
- `engine/src/ui/ProjectHub.h` — `NoteTypeAssets` gained `loopSfxPath`.
- `engine/src/game/screens/MusicSelectionView.{h,cpp}` — load/save `loopSfxPath`;
  `playWheelSfx(engine, rel, DefaultSfx::Role fallback)` (override-or-default);
  scroll→UiScroll, difficulty→UiTap, START→UiConfirm, AUTO PLAY→UiToggle.
- `engine/src/game/screens/StartScreenView.h` — `tapSfx()` accessor.

**Editor UI**
- `engine/src/ui/SongEditor.cpp` — `#include DefaultSfx`; `assetRow` gained
  `libCat` (−1 none / 0 short / 1 long) + Lib popup w/ Play preview; "Hit Sound"
  row uses short; Hold adds "Sustain Sound" row using long.
- `engine/src/ui/MusicSelectionEditor.cpp` — `#include DefaultSfx`; `sfxDropZone`
  gained Lib picker; hint text "Empty = default".
- `engine/src/ui/StartScreenEditor.cpp` — `#include DefaultSfx`; tap-to-start
  default sound; `audioZone` gained `showLib`; Tap row gets Lib picker.
- `engine/src/ui/SettingsPageUI.cpp` — `#include DefaultSfx`; Back→UiBack,
  Accept→UiConfirm, Retry→UiTap, Cancel→UiBack.

**Android**
- `engine/src/android/AndroidEngine.cpp` — `#include DefaultSfx`; manifest-driven
  extraction (extract `sfx/manifest.json`, `setBundleDir`, extract every
  `allLibraryFiles()`), `preloadDefaults`; tap-to-start default sound.
- `engine/src/android/CMakeLists.txt` — `DefaultSfx.cpp` added to
  `SHARED_ENGINE_SOURCES`.

**Build / bundle**
- `CMakeLists.txt` — POST_BUILD copy of `sfx/` tree next to the test exe.
- `tools/build_apk.bat` — copy whole `sfx/` tree into APK `assets/sfx/`.

---

## 6. Cache keys & roles reference

- One-shot cache keys: `def_click, def_flick, def_hold_start, def_hold_tick,
  def_hold_end, def_ui_tap, def_ui_confirm, def_ui_scroll, def_ui_back,
  def_ui_toggle`. Per-song overrides: `ov_<section>` (e.g. `ov_Click Note`).
- `hold_loop` is NOT cached — it is played by path via `startLoopingSfx`.
- Note-type → section key: Tap/Drag/Ring → "Click Note", Hold → "Hold Note",
  Flick → "Flick Note", Slide → "Slide Note", Arc → "Arc Note", ArcTap →
  "ArcTap Note".

---

## 7. Real sound candidate list (the deliverable)

Target library: **~20 short + ~5 long**, all **CC0** preferred. The bulk of the
short set comes from two Kenney CC0 packs (one download each); the swipe and all
long loops come from freesound CC0. Final filenames below are the **target**
`manifest.json` layout — obtain the sounds, drop them into `sfx/short` + `sfx/long`
with these names (or your own names + update the manifest), and set `defaults`.

### Sources
- Kenney · Interface Sounds (100 sounds, CC0): https://kenney.nl/assets/interface-sounds
- Kenney · UI Audio (50 sounds, CC0): https://kenney.nl/assets/ui-audio
- qubodup · Swipe Whoosh (CC0): https://freesound.org/people/qubodup/sounds/60007/
- qubodup · Guitar Drone F2 Loop (CC0, seamless): https://freesound.org/people/qubodup/sounds/854238/
- LookIMadeAThing · Sci-fi Ambient Drone (CC0): https://freesound.org/people/LookIMadeAThing/sounds/534018/
- Erokia · Electronic Samples Misc (CC0) pack: https://freesound.org/people/Erokia/packs/26717/
- Breviceps · Clicks, Buttons & UI (CC0) pack: https://freesound.org/people/Breviceps/packs/25371/
- RokZRooM · CC0 pack: https://freesound.org/people/RokZRooM/packs/12161/

### SHORT library (~20) → `sfx/short/`
| file | character / use | source |
|------|-----------------|--------|
| click_soft.wav | soft note click (default `click`) | Kenney UI Audio |
| click_sharp.wav | crisp click alt | Kenney Interface |
| tap_soft.wav | soft tap (default `hold_start`, `ui_tap`) | Kenney UI Audio |
| tap_wood.wav | woody tap alt | Kenney Interface |
| blip.wav | tiny blip (default `hold_tick`) | Kenney Interface / Breviceps |
| pop.wav | short pop alt | Kenney Interface |
| swipe.wav | swipe/whoosh (default `flick`) | qubodup #60007 |
| whoosh_soft.wav | softer swipe alt | Kenney Interface |
| confirm.wav | positive confirm (default `ui_confirm`/START) | Kenney Interface |
| select.wav | select alt | Kenney UI Audio |
| success.wav | success chime | Kenney Interface |
| back.wav | back/dismiss (default `ui_back`) | Kenney Interface |
| cancel.wav | cancel alt | Kenney Interface |
| toggle_on.wav | switch on (default `ui_toggle`) | Kenney Interface |
| toggle_off.wav | switch off | Kenney Interface |
| switch.wav | neutral switch | Kenney UI Audio |
| scroll_tick.wav | light detent (default `ui_scroll`) | Kenney Interface |
| detent.wav | scroll detent alt | Kenney UI Audio |
| ping.wav | ping/question | Kenney Interface |
| error.wav | negative/error | Kenney Interface |

### LONG library (~5) → `sfx/long/` (must loop seamlessly)
| file | character / use | source |
|------|-----------------|--------|
| drone_warm.wav | warm sustained (default `hold_loop`) | qubodup #854238 |
| drone_deep.wav | deeper drone alt | LookIMadeAThing #534018 |
| pad_soft.wav | soft pad | Erokia pack #26717 |
| hum_synth.wav | synth hum | Erokia / RokZRooM #12161 |
| shimmer_loop.wav | bright shimmer | Erokia pack #26717 |

### Target `defaults` map for manifest.json
```
click → short/click_soft.wav      ui_tap     → short/tap_soft.wav
flick → short/swipe.wav           ui_confirm → short/confirm.wav
hold_start → short/tap_soft.wav   ui_scroll  → short/scroll_tick.wav
hold_loop  → long/drone_warm.wav  ui_back    → short/back.wav
hold_tick  → short/blip.wav       ui_toggle  → short/toggle_on.wav
hold_end   → short/tap_soft.wav
```

### Obtaining / normalizing
Kenney packs are direct downloads (no account). freesound needs a login/API
token (Claude cannot download them). Normalize each to 16-bit PCM WAV, trimmed,
peak-normalized; render `long/` files seamless (no click at the wrap point). A
freesound API downloader can be written if a token is provided.

---

## 8. Remaining work

1. **Choose & obtain the real sounds** (§7). Replace `sfx/short/*` + `sfx/long/*`
   placeholders; update `sfx/manifest.json` (entries + `defaults`); fill the
   license table in `sfx/MANIFEST.txt`.
2. **Manual verification** (needs a machine with audio + GUI):
   - SongEditor → a note → Hit Sound **Lib** → **Play** previews; select assigns;
     Hold shows **Sustain Sound** (long library).
   - MusicSelectionEditor Wheel Sounds + StartScreenEditor Tap → Lib pickers.
   - Play a chart with no overrides → default click/flick; Hold → start + sustain
     loop + throttled ticks + end; confirm no audio-thread stutter on a dense hold.
   - Set an override → it replaces the default; Settings "hit sound off" / SFX
     volume 0 → all default SFX silent.
3. **APK build + on-device check**: `tools/build_apk.bat` now copies `sfx/`;
   confirm extraction + on-device note/UI sounds. Re-run the editor/HTTP symbol
   audit if the APK is rebuilt.
4. **Licensing policy** finalize (prefer CC0); record each source in MANIFEST.txt.

---

## 9. How to resume tomorrow

1. Read §2 (status) + §8 (remaining work). The code is done and builds.
2. Decide sounds from §7. Download the two Kenney CC0 packs + the freesound CC0
   clips. Normalize, rename to the §7 target filenames, drop into
   `sfx/short/` + `sfx/long/`.
3. Update `sfx/manifest.json` to list all entries and set `defaults` (target map
   in §7). Update license table in `sfx/MANIFEST.txt`.
4. Rebuild: `cmake --build build --config Release --target MusicGameEngineTest`.
   (No reconfigure needed unless source files were added.)
5. Run `build/Release/MusicGameEngineTest.exe --test Projects/test` and do the
   manual verification in §8.2.
6. When happy, build the APK and verify on device (§8.3).

### Notes / gotchas
- New `.cpp` files need a CMake **reconfigure** (`cmake ..`) because desktop uses
  `GLOB_RECURSE`; Android needs the file added to its source list explicitly.
- Bundle dir: desktop = relative `sfx` (CWD is the exe dir, anchored in
  `engine/src/main.cpp`); Android = extracted internal path via `setBundleDir`.
- Placeholders can be regenerated any time: `python tools/gen_placeholder_sfx.py`.
- Approved plan file: `~/.claude/plans/asset-freesound-org-click-hold-dreamy-feigenbaum.md`.
