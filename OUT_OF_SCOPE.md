# Deferred Work / Out of Scope

**Last updated:** 2026-04-10

This file collects items that have been **explicitly deferred** during recent fixes — known limitations that work today but should eventually be addressed. Each entry describes the gap, why it wasn't fixed at the time, and a starting point for the eventual fix.

This is a *known-debt* ledger, not a bug list. The current code works for the cases that have been tested; these are the cases that **don't** work or that rely on a workaround.

---

## 1. Phigros (judgment-line) mode is unreachable

**Where:** `engine/src/engine/Engine.cpp::createRenderer` (~line 360), `engine/src/ui/SongEditor.cpp::renderModeStyleSelector` (~line 515).

**Status:** Dead code path.

After the 2026-04-10 factory mismatch fix (`Circle` was wired to `CytusRenderer`, `ScanLine` was wired to `PhigrosRenderer`), the factory now correctly maps:

| `GameModeType` | Renderer |
|---|---|
| `DropNotes` (2D) | `BandoriRenderer` |
| `DropNotes` (3D) | `ArcaeaRenderer` |
| `Circle` | `LanotaRenderer` |
| `ScanLine` | `CytusRenderer` |

There is no enum value or UI button for **Phigros** (moving judgment lines). Two `dynamic_cast<PhigrosRenderer*>` branches still exist in `Engine.cpp` (the gesture dispatch at ~line 88 and `handleGesturePhigros` at ~line 822) — they are harmless dead code; the casts always return null.

**To restore Phigros:**
1. Add a fourth `GameModeType::JudgmentLine` value to `engine/src/ui/ProjectHub.h:10`.
2. Add a fourth `ModeOption` button in `SongEditor.cpp:515-519` (e.g. `{"Judgment Line", "Moving lines + attached notes", GameModeType::JudgmentLine}`).
3. Add `case GameModeType::JudgmentLine: return std::make_unique<PhigrosRenderer>();` to the factory in `Engine.cpp`.
4. Verify `PhigrosRenderer::onInit` still accepts the current `GameModeConfig` shape — it was last updated when other plugins got the config parameter.

---

## 2. Circle mode hold-drift cancellation

**Where:** `engine/src/engine/Engine.cpp::handleGestureCircle`.

**Status:** Constant reserved (`CIRCLE_HOLD_DRIFT_DP = 64`); no `HoldMove`/`SlideMove` case implemented.

When a player holds a finger on a Lanota note and the disk rotates, the note drifts away from the finger. There is currently no check that cancels the hold once the finger is "too far" from the note's current screen position. This is a non-issue today because the test chart's ring rotates slowly and most holds are short, but a fast-rotating ring with a long hold note would silently keep the hold active even though the player is no longer touching the note.

**Fix sketch:**
1. Track the finger's current screen position per `m_activeTouches[touchId]` entry — extend the value type from `noteId` to `{noteId, lastScreenPos}`.
2. Add a `case GestureType::SlideMove:` (or `HoldMove` if a separate event exists — check `engine/src/input/TouchTypes.h`) to `handleGestureCircle`.
3. On each move, project the held note's current screen position via `LanotaRenderer::projectNoteScreen(noteId, ...)` and compare to the finger position. If the pixel distance exceeds `ScreenMetrics::dp(CIRCLE_HOLD_DRIFT_DP)`, cancel the hold via `m_hitDetector.endHold(noteId, songTime)` and treat as a Miss.
4. Update `m_activeTouches[touchId].lastScreenPos = evt.pos`.

---

## 3. Multi-touch on the same note id

**Where:** `engine/src/engine/Engine.cpp::handleGestureCircle`, `handleGestureLaneBased`, `handleGestureArcaea`.

**Status:** Inherited limitation, not Circle-mode-specific.

`m_activeTouches` is a `std::unordered_map<int32_t, uint32_t>` keyed by `touchId`. Two simultaneous holds on **different** notes work (different `touchId`s, different note ids). Two fingers on the **same** note id would clobber each other's hold state because the second `beginHoldById` call replaces the first entry in `m_activeHolds`. This matches the existing Bandori/Arcaea behavior — it's not a regression.

**Why deferred:** No real chart triggers it. A proper fix would require either rejecting the second `beginHoldById` if the note is already held, or refcounting holds per note.

---

## 4. C4819 codepage warnings during build

**Where:** Most `.h`/`.cpp` files in `engine/src/`.

**Status:** Pre-existing build noise; harmless.

MSVC's default locale codepage on the dev machine is 936 (GBK). Many engine source files contain UTF-8 characters in comments (the project mixes English and Chinese commentary), which the GBK codepage can't represent, so MSVC emits a `C4819` warning per file per translation unit. The actual code compiles correctly because MSVC reads files as UTF-8 in practice; the warning is purely about the source-file encoding metadata.

**Fix options:**
- Add a UTF-8 BOM (`EF BB BF`) to the header of each affected file. Tedious but eliminates the warning permanently.
- Add `/utf-8` to the project's compiler flags in `CMakeLists.txt` — tells MSVC to assume both source and execution charsets are UTF-8 and silences C4819 globally. Lower-effort, but verify it doesn't break any string-literal output that currently relies on the GBK execution charset.

**Why deferred:** The warning floods the build log but doesn't affect correctness. Cleaning it is a one-time chore that should be batched with another tree-wide change.

---

## 5. Post-build `pwsh.exe` shader-copy step fails

**Where:** Generated CMake post-build step for the `Shaders` target (visible in `MusicGameEngineTest.vcxproj` build output).

**Status:** Build "fails" the post-build step but the executable still produces; harmless on this machine.

The build runs:
```
'pwsh.exe' 不是内部或外部命令，也不是可运行的程序
```
i.e. PowerShell 7 (`pwsh.exe`) is not installed; only Windows PowerShell (`powershell.exe`) is available. The shader files end up in `build/shaders/` correctly anyway because they were generated by the `Shaders` target itself before this copy step runs.

**Fix options:**
- Change the CMake post-build command to use `powershell.exe` (works everywhere on Windows) or `cmake -E copy_directory` (cross-platform, no shell needed).
- Check `CMakeLists.txt` for `add_custom_command` blocks that invoke `pwsh.exe`.

**Why deferred:** Doesn't block builds — `MusicGameEngineTest.exe` runs and the shaders load correctly. Cosmetic build-log cleanup.

---

## 6. Lanota DPI tolerance not yet validated on real hardware

**Where:** `engine/src/engine/Engine.cpp::handleGestureCircle::CIRCLE_PICK_DP`.

**Status:** Implemented per spec; never tested on a high-DPI device.

`CIRCLE_PICK_DP = 48` (≈ 7.6 mm at the 160-DPI Android reference) was chosen by analogy with the rest of the input system's `dp(...)` thresholds (see `engine/src/input/ScreenMetrics.h:5-15`). On the desktop dev machine the value passes through `ScreenMetrics::scale()` which estimates DPI from the resolution — the estimate is rough and the *physical* tolerance hasn't been measured on a real phone yet.

**Validation needed:** Build the APK (the pipeline is wired up — see `ANDROID_PACKAGING.md`), install on at least one ≥ 400 DPI device, and confirm:
- Tapping a visible note registers a hit comfortably (not too small).
- Tapping near but not on a note **doesn't** register (not too large).
- Adjust `CIRCLE_PICK_DP` if either case feels wrong; physical fingertip is ~7-10 mm so the tolerance has room to grow to `dp(64)` ≈ 10 mm if needed.

---

## How to use this file

When fixing one of these items, **delete its section** from this file and add a corresponding entry to the relevant system doc (e.g. moving the Phigros work to `GAME_MODES.md` once Phigros is reachable again). This file should shrink over time, not grow — new deferred items should only land here when they're truly deferred, not as a parking lot for ideas.
