# AI Editor Copilot — Design & Rollout

**Status:** Shipped 2026-04-24 on branch `song-editor-layout`. All 8 phases complete. Uncommitted.
**Authors:** Plan approved by user; implemented in auto mode.
**Scope:** Expansion of the Editor Copilot from a 6-op vocabulary over the flat notes vector into a mode-aware, 23-op editor that can reason about arcs, slides, hold waypoints, disk animation, and scan-speed events — with per-mode skill.md documents driving the LLM's knowledge of each mode.

---

## 1. Problem

The Editor Copilot shipped 2026-04-18 with 6 ops (`delete_range`, `insert`, `mirror_lanes`, `shift_lanes`, `shift_time`, `convert_type`), all operating on a flat `std::vector<EditorNote>`. Every request used the same hardcoded system prompt at `SongEditor::buildCopilotSystemPrompt` (SongEditor.cpp around line 7757). The prompt injected `mode name`, `lane_count`, `difficulty`, and note counts, but otherwise knew nothing mode-specific.

Consequences:
- The LLM couldn't reason about Arcaea 3D arcs/arctaps, Cytus slide paths, Lanota disk animation, or Cytus per-page speed.
- When users asked about any of those, the model either returned `ops=[]` (best case) or silently emitted a semantically empty `shift_lanes` (worst case — the memory on `project_copilot_scope.md` explicitly calls this out).
- Adding new ops meant editing code AND the inline prompt — two places to keep in sync.

## 2. Goal

1. Teach the LLM each mode's vocabulary via **per-mode skill.md documents** that ship with the engine.
2. Expand the op vocabulary so the copilot can actually perform arc/slide/waypoint/disk/scan-speed edits (not just refuse them nicely).
3. Keep the existing async HTTP plumbing, single-level undo, OpenAI-compatible chat endpoint (Ollama default), and review-before-Apply UI intact.

## 3. Architecture

### 3.1 Data flow

```
User prompt
   │
   ▼
SongEditor::buildCopilotSystemPrompt
   │ ─ builds dynamic context header (mode, lane_count, difficulty, counts)
   │ ─ loads engine/assets/copilot_skills/_common.md + <mode>.md
   │ ─ falls back to inline schema if a file is missing
   ▼
AIEditorClient::startRequest  ─  worker thread  ─  runChatRequest (httplib POST /v1/chat/completions)
   │
   ▼
OpenAI-compatible JSON reply  (Ollama / OpenAI-compatible endpoint)
   │
   ▼
SongEditor::pollCopilot (UI thread, every frame)
   │
   ▼
parseChartEditOps   →   std::vector<ChartEditOp> (std::variant<…>)
   │
   ▼
isOpAllowedForMode filter  →  rejects surface in lastError
   │
   ▼
Preview UI (describeChartEditOp bullet list)
   │
   ▼
User hits Apply
   │
   ▼
ChartSnapshot captures notes+markers+features+disk+scan state
   │
   ▼
For each op:
   isExtendedOp(op)? applyChartEditOpExtended(*this, …)   ← disk / scan-speed
                   : applyChartEditOp(notes(), …)         ← everything else
   │
   ▼
Undo button restores snapshot
```

### 3.2 Files

**New files**
- `engine/assets/copilot_skills/_common.md` (82 LOC) — shared response envelope + coordinate conventions + the 6 common ops.
- `engine/assets/copilot_skills/bandori.md` (~130 LOC) — 2D drop mode.
- `engine/assets/copilot_skills/arcaea.md` (~130 LOC) — 3D drop + arcs + arctaps.
- `engine/assets/copilot_skills/lanota.md` (~140 LOC) — circle + disk animation + hold waypoints.
- `engine/assets/copilot_skills/cytus.md` (~140 LOC) — scan sweep + slides + page speed.
- `engine/src/editor/CopilotSkill.{h,cpp}` — `std::string loadCopilotSkill(modeName)` reads the per-mode file + `_common.md` and concatenates. No caching; reads every request (files are small, HTTP call dominates latency). Probes four candidate paths (`./assets/copilot_skills`, `../assets/copilot_skills`, `engine/assets/copilot_skills`, `../../engine/assets/copilot_skills`) so running from a worktree without a rebuild still works. Warns once per mode to stderr on miss.
- `engine/tests/chart_roundtrip_test.cpp` — standalone regression gate. Verifies both `ChartLoader` roundtrip for hand-crafted JSON fixtures AND the Phase 4-7 ops (parse → apply → assert).

**Modified files**
- `engine/src/editor/ChartEditOps.h` — migrated from single bloated struct to `std::variant` of per-op payloads. 23 variants today.
- `engine/src/editor/ChartEditOps.cpp` — `parseChartEditOps`, `applyChartEditOp`, `applyChartEditOpExtended`, `describeChartEditOp`, `isExtendedOp`, `isOpAllowedForMode` all use `std::visit` with `if constexpr (std::is_same_v<T, …>)` dispatch. `describe` switched from 256 B `char` buffer to `std::string` (new ops can't overflow).
- `engine/src/editor/ChartSnapshot.h` — grew 5 new fields (`diskRot`, `diskMove`, `diskScale`, `scanSpeed`, `scanPages`) so disk/scan ops can be undone.
- `engine/src/ui/SongEditor.h` — `diskRot()` / `diskMove()` / `diskScale()` / `scanSpeed()` / `scanPages()` accessors promoted from private → public via narrow `public:` / `private:` bookends (no friend needed; simpler than fighting the ChartEditOps.h ↔ SongEditor.h include cycle).
- `engine/src/ui/SongEditor.cpp` — `buildCopilotSystemPrompt` loads the skill doc; Send-button callback mode-gates the parsed ops; Apply branch captures the enlarged snapshot and routes via `isExtendedOp`; Undo restores all 10 snapshot fields.
- `CMakeLists.txt` — new `POST_BUILD` `copy_directory` step places skill assets at `$<TARGET_FILE_DIR:MusicGameEngineTest>/assets/copilot_skills/`; new `ChartRoundtripTest` executable target.

## 4. Op vocabulary (23 ops)

### 4.1 Common ops (all modes)

| Op | Purpose |
|---|---|
| `delete_range` | Remove notes in `[from, to]` optionally filtered by type |
| `insert` | Create one note at `(time, track, type)`; `duration` for holds |
| `mirror_lanes` | Reflect track across `lane_count - 1 - track` in range |
| `shift_lanes` | Add integer delta to tracks in range |
| `shift_time` | Add seconds delta to times in range |
| `convert_type` | Convert notes matching `from_type` to `to_type` in range |

### 4.2 Arc / ArcTap (arcaea only)

| Op | Purpose |
|---|---|
| `add_arc` | Create one arc segment. Coords normalized `[0,1]`; easing as string codes (`"s"/"si"/"so"/"sisi"/...`); color 0=blue, 1=pink; `void` for invisible carrier |
| `delete_arc` | Remove arcs in range; **cascades** to ArcTap children + rewrites parent indices for survivors (two-pass scheme) |
| `shift_arc_height` | Bulk Y offset for arcs in range, clamped `[0,1]` |
| `add_arctap` | Place one ArcTap at `time`; parent arc auto-resolved by bracketing time; no-op if no arc exists |
| `delete_arctap` | Remove ArcTaps in range |

### 4.3 Slide path (cytus only)

| Op | Purpose |
|---|---|
| `add_slide` | Create slide with `scanPath [[x,y]...]` + `samples [t...]`; path should follow sweep direction at `time`; auto-synthesizes starting node from `(scanX, scanY)` if the LLM omits it |
| `delete_slide` | Remove slides in range |

### 4.4 Hold waypoints (bandori, arcaea, lanota — not cytus)

| Op | Purpose |
|---|---|
| `add_hold_waypoint` | Append lane-change waypoint to hold matched by `note_time` (1 ms tolerance); `style` ∈ `straight/angle90/curve/rhomboid`; inserts sorted by tOffset |
| `remove_hold_waypoint` | Remove hold's waypoint matching `at_time` (1 ms tolerance) |
| `set_hold_transition` | Bulk set transition style for holds in `[from, to]` — rewrites legacy `transition` field AND every waypoint's `style` (mirrors the "Apply to All Holds" button) |

### 4.5 Disk animation (lanota only)

| Op | Purpose |
|---|---|
| `add_disk_rotation` | Keyframe the disk to an **absolute** target angle (radians) over duration with easing ∈ `linear/sineInOut/quadInOut/cubicInOut` |
| `add_disk_move` | Keyframe the disk center to world `[x, y]` |
| `add_disk_scale` | Keyframe the disk scale multiplier |
| `delete_disk_event` | Remove one disk event by `(kind, start_time)` where `kind` ∈ `rotation/move/scale` (1 ms tolerance) |

### 4.6 Scan page speed (cytus only)

| Op | Purpose |
|---|---|
| `set_page_speed` | Upsert the speed multiplier for a specific `page_index`; replaces in place if an entry for that page already exists |
| `add_scan_speed_event` | Low-level scan-speed keyframe — prefer `set_page_speed` for page-boundary step changes |
| `delete_scan_speed_event` | Remove events whose `start_time` is in `[from, to]` |

### 4.7 Mode gating

`isOpAllowedForMode(op, modeName)` returns `true` when the op's variant type matches the mode's policy:

| Family | `bandori` | `arcaea` | `lanota` | `cytus` |
|---|:-:|:-:|:-:|:-:|
| Common (6 ops) | ✓ | ✓ | ✓ | ✓ |
| Arc / ArcTap (5) | — | ✓ | — | — |
| Slide (2) | — | — | — | ✓ |
| Hold waypoints (3) | ✓ | ✓ | ✓ | — |
| Disk animation (4) | — | — | ✓ | — |
| Scan page speed (3) | — | — | — | ✓ |

Gating is enforced in the Copilot response callback in SongEditor.cpp, not inside the parser — the parser has no song context. Rejected ops surface in `lastError` as `Rejected (mode=<x>): <describe>` lines so the user sees why an op didn't land in the preview; valid ops in the same batch still apply.

## 5. Skill document format

Every per-mode file follows the same structure so the LLM can generalize across modes:

```
# <Mode> Skill

## Brief
1-3 sentences: visual metaphor, what the mode looks like.

## Concepts
Mode-specific vocabulary: lane, ring, page, sweep, sky, arc color,
easing codes, etc. Coordinate system (world vs normalized vs angle).

## Supported note types
Table: Click/Hold/Flick/Slide/Arc/ArcTap with ✓/— per type.

## Mode-specific ops
Each op: name, JSON schema example, 1-line semantics.

## Common ops
Reference the 6 shared ops. Note any mode-specific caveats
(e.g. mirror_lanes has limited meaning in ScanLine).

## Examples
5-7 few-shot prompt → ops pairs. Per Plan-agent + ShaderGen
precedent, 3B models need few-shot to reliably produce mode-
specific JSON. Budget ~120-150 LOC per mode total.
```

### 5.1 Why few-shot matters

`qwen2.5:3b` (the default Ollama model) is the cheapest reliable target. The ShaderGen memory (`project_copilot_skill_docs.md` §3) noted rule-only prompts failed on this model; few-shot examples are what pulled it back into line. The skill docs commit to 5-7 examples per mode with at least one explicit out-of-scope refusal so the LLM learns to say "not in my vocabulary; use the timeline panel" instead of silently no-oping.

### 5.2 Loading + fallback

`loadCopilotSkill(modeName)`:
1. Probe 4 candidate paths for `_common.md` and `<modeName>.md`.
2. If `<modeName>.md` is missing, log once to stderr and return empty string.
3. `buildCopilotSystemPrompt` checks — if skill doc is empty, falls back to the original inline schema so the Copilot keeps working even when assets aren't shipped.

No mtime caching. Files are small (avg 100 LOC each), the HTTP call dominates latency, and live-editing skill docs during dev should "just work" — save the file, retry the prompt, the new content is there.

## 6. Variant migration (Phase 3)

`ChartEditOp` went from a field-bloated struct (every op had every possible field, most unused per op) to:

```cpp
using ChartEditOp = std::variant<
    DeleteRangeOp, InsertOp, MirrorLanesOp, ShiftLanesOp, ShiftTimeOp, ConvertTypeOp,
    AddArcOp, DeleteArcOp, ShiftArcHeightOp, AddArcTapOp, DeleteArcTapOp,
    AddSlideOp, DeleteSlideOp,
    AddHoldWaypointOp, RemoveHoldWaypointOp, SetHoldTransitionOp,
    AddDiskRotationOp, AddDiskMoveOp, AddDiskScaleOp, DeleteDiskEventOp,
    SetPageSpeedOp, AddScanSpeedEventOp, DeleteScanSpeedEventOp
>;
```

Each op struct carries only its own fields. Dispatch via `std::visit` + `if constexpr (std::is_same_v<T, …>)` gives compile-time exhaustiveness. **No call-site changes needed** in `SongEditor.cpp` — `std::vector<ChartEditOp>`, `describeChartEditOp(op)`, `applyChartEditOp(notes, laneCount, op)`, `parseChartEditOps(msg)` signatures were preserved.

Side win: `describeChartEditOp` swapped the 256 B `char` buffer for `std::string` formatting. Long new-op descriptions can't overflow.

## 7. Snapshot expansion (Phase 7)

```cpp
struct ChartSnapshot {
    std::vector<EditorNote>        notes;      // existing
    std::vector<float>             markers;    // existing
    std::vector<MarkerFeature>     features;   // existing
    // Phase 7 additions — disk + scan-speed ops live outside the notes vector
    std::vector<DiskRotationEvent> diskRot;
    std::vector<DiskMoveEvent>     diskMove;
    std::vector<DiskScaleEvent>    diskScale;
    std::vector<ScanSpeedEvent>    scanSpeed;
    std::vector<ScanPageOverride>  scanPages;
};
```

Arc / slide / hold-waypoint data are all inside `EditorNote` fields (`arcStartX`, `arcEndY`, `waypoints`, `scanPath`, `samplePoints`, `arcTapParent`, etc.), so the existing `snap.notes = notes()` capture already covered them. Only disk + scan-speed needed new fields.

Capturing all 5 extended vectors even in Bandori/Arcaea mode (where they're always empty) is harmless and simpler than per-mode snapshot gating.

## 8. Threading invariants

- `AIEditorClient` spawns a worker thread that calls `runChatRequest` (blocking HTTP + JSON parse). No UI state touched.
- Worker stores result; UI thread drains via `pollCopilot()` in the per-frame render loop.
- Response callback runs on UI thread. It mutates only `m_copilot` fields (`proposed`, `explanation`, `lastError`, `inFlight`, `hasResult`).
- Apply button (UI thread) is the **only** mutation site for chart data. Don't let the callback touch `m_diffDiskRot` / `m_diffScanSpeed` / notes directly.
- No new races introduced by the Phase 7 extended apply path — it's still UI-thread only.

## 9. Phase plan (shipped order)

1. **1a — Skill loader plumbing.** CopilotSkill.{h,cpp}, bandori.md + _common.md, CMake POST_BUILD copy. No new ops. Regression: existing 6-op vocabulary still works.
2. **1b — Roundtrip test harness.** Hand-crafted JSON fixture hitting arc segments, multi-waypoint holds, slides with scanPath + samples, disk animation (all 4 easings), per-page scan speed. `ChartLoader::load` recovers every field. De-risk gate for Phases 4-7.
3. **2 — arcaea/lanota/cytus skill docs.** Pure content; no code changes.
4. **3 — Variant migration of `ChartEditOp`.** Structural refactor, 6 existing ops still pass.
5. **4 — Arc + ArcTap ops.** 5 variants. Cascading delete + parent-index rewrite.
6. **5 — Slide ops.** 2 variants. Path clamping + sample clamping.
7. **6 — Hold waypoint ops.** 3 variants. Shared across Bandori/Arcaea/Lanota.
8. **7 — Snapshot expansion + disk + scan-speed ops.** 7 variants. Must land together because disk/scan data lives outside `EditorNote`.

Each phase landed independently revertable, with the test extended at each step.

## 10. Tests

`engine/tests/chart_roundtrip_test.cpp` — standalone executable; exits 0 on success, 1 on any failed assertion. Covers:

1. **Chart loader roundtrip** (Phase 1b gate)
   - 7-note hand-crafted fixture: tap, hold-with-4-waypoint-styles, flick, slide-with-scanPath, 2-segment arc, arctap.
   - Disk animation with all 4 easing values.
   - Per-page scan speed overrides.
   - Assertion that `ChartLoader::load` recovers every field.

2. **Arc / ArcTap ops** (Phase 4)
   - Parse → apply → assert 2 arcs + 2 arctaps.
   - Easing code translation (`si`/`so`/`sisi`/`siso`).
   - `shift_arc_height` with clamp.
   - `delete_arc` cascading delete + parent-index rewrite.
   - Mode-gate: allowed in arcaea, blocked in bandori/lanota/cytus.

3. **Slide ops** (Phase 5)
   - Parse → apply → assert 2 slides, 3-node and 2-node paths.
   - Path + sample clamping.
   - `delete_slide` in range.
   - Mode-gate: allowed in cytus only.

4. **Hold waypoint ops** (Phase 6)
   - Parse → apply 2 waypoints → assert tOffset, lane, style.
   - `remove_hold_waypoint` by time match.
   - `set_hold_transition` bulk rewrites both legacy field + all waypoint styles.
   - Mode-gate: blocked in cytus only.

5. **Extended ops routing** (Phase 7)
   - `isExtendedOp(op)` returns true for disk/scan variants, false for common/note variants.
   - Parse all 7 extended ops and confirm each routes as extended.
   - Mode-gate: disk ops allowed in lanota only; scan-speed ops allowed in cytus only.

## 11. Non-goals / deferred

These were deliberately **not** included in the rollout. Log here so future maintenance doesn't have to rediscover them:

- **HTTPS / cloud providers.** Current client rejects `https://` endpoints. Requires OpenSSL linked into cpp-httplib.
- **Multi-level undo.** Single snapshot only. A general engine-wide undo/redo is its own initiative.
- **Material / shader ops.** Materials have their own asset system + editor (see `sys1_rendering.md` Phase 4).
- **Per-project AI config.** Config stays user-global at `%APPDATA%/MusicGameEngine/ai_editor_config.json`.
- **Streaming / partial responses.** Single-shot request/response.
- **Cancel button for in-flight requests.** `cpp-httplib` doesn't expose socket-kill cleanly.
- **Difficulty-level edits.** Copilot acts on the currently selected difficulty only.
- **AI-side difficulty differentiation by type, AI arc generation, AI sky-note inference.** Still manual per `project_autocharter_scope.md` — authoring expression, not detection.
- **Phigros mode.** Ignored per user direction.

## 12. Known gotchas recorded in memory

- `ChartLoader` does **not** sort notes by time — they stay in JSON source order. `insert` ops must re-sort (they do).
- Multi-waypoint arcs decompose into N-1 `Arc` events on export; the editor reconstructs waypoints on import by matching successive arcs with the same color.
- `ArcTap` serializes as `TapData` with `laneX=arcX`, `scanY=arcY`. No separate struct.
- `scanPageOverrides` is the source of truth for Cytus page speed; `scanSpeedEvents` is only emitted when no page overrides exist (avoids round-trip drift).
- Arc parent links survive a `sortByTime` inside `AddArcTap` apply via a post-sort parent-time match pass — preserve that logic in any refactor.
- Cascading deletes need a two-pass scheme: mark doomed arcs, mark dependent arctaps, then remap survivor arctap parent indices. Single-pass `remove_if` would be wrong.
- `AddArcTap` silently no-ops when no parent arc exists in the vector. The skill doc primes the LLM to emit `add_arc` first when constructing arcs from scratch.
- `SetPageSpeedOp` is an upsert, not an insert — replaces in place if an entry for that `pageIndex` already exists.
- `applyChartEditOpExtended` takes `SongEditor&` (not const). The dispatch mutates per-difficulty maps. Pass the live editor, not a snapshot.

## 13. Verification recipe

```
cmake -S . -B build
cmake --build build --config Debug
./build/Debug/ChartRoundtripTest.exe          # expects exit 0, PASS line
./build/Debug/MusicGameEngineTest.exe         # editor launches, no crash
```

For each mode, exercise a mode-specific prompt end-to-end (requires Ollama running with `qwen2.5:3b`):

- **Bandori:** *"Add a zigzag hold from 2s to 4s switching lanes 1-3-1-3"*
- **Arcaea:** *"Place a blue arc from lane 0 ground to lane 6 sky over 4s starting at 10s"*
- **Lanota:** *"Rotate the disk 180 degrees over 2s starting at 16s with sine ease"*
- **Cytus:** *"Set page 4 speed to 1.5x"*

Each should appear in the preview list with the matching op name. Apply, verify the editor reflects the change, Undo, verify the state reverts.

## 14. Future work candidates (in priority order, pending user direction)

1. **Prompt-length measurement.** Instrument `runChatRequest` to log the system-prompt byte count so we can tell if any mode's skill doc is pushing the 3B context budget.
2. **HTTPS support.** Link OpenSSL into cpp-httplib so the config endpoint can reach Groq / Gemini / Cerebras / Anthropic.
3. **Export/import roundtrip extraction.** Refactor `SongEditor::exportAllCharts`'s ~300 LOC inline JSON writer into `engine/src/editor/ChartJsonWriter.{h,cpp}`. Enables a true end-to-end (EditorNote → JSON → ChartData → EditorNote) roundtrip test for the ops the copilot emits.
4. **Per-project skill overrides.** Let a project ship its own `Projects/<name>/ai/copilot_skills/*.md` that shadow the engine defaults. Useful for chart packs with bespoke authoring conventions.
5. **Multi-level undo.** Already mentioned as a broader engine initiative; when it lands, the copilot's single-slot snapshot should yield to it.
6. **Material ops.** Only if the user asks. Materials have their own well-established editing surface already.

---

## 15. Capability surface report — what the Copilot can edit, by chart region

This is the reader-oriented map of *where in a chart* the Copilot is now allowed to write. Each region documents the underlying data, the ops that reach it, what kinds of changes are in scope, what is deliberately out of scope, the modes that expose it, and the edge cases worth knowing before issuing a prompt.

The Copilot edits the **currently-selected difficulty only**. Cross-difficulty edits, project-level changes (audio, materials, settings), and any data outside `EditorNote` / disk events / scan events / scan-page overrides are out of scope.

### 15.1 Region A — Note timeline (the `notes` vector)

**Data:** `std::vector<EditorNote>` for the current difficulty. Every Click/Hold/Flick/Slide/Arc/ArcTap lives here as one entry. Order is JSON source order on load; ops re-sort by time when needed (`InsertOp`, `ShiftTimeOp`, `AddArc`, `AddArcTap`, `AddSlide`).

**Ops:** `delete_range`, `insert`, `mirror_lanes`, `shift_lanes`, `shift_time`, `convert_type` (the original 6 from the 2026-04-18 ship).

**What the Copilot can do here:**
- Bulk-delete in any time range, optionally filtered to one type (`tap` / `hold` / `flick` / `any`).
  - *"Delete every flick between 30 and 45 seconds."*
  - *"Clear all notes from the start to 4 seconds so I can re-author the intro."*
- Add a single Click/Hold/Flick at `(time, track)`. Hold defaults to 0.3 s body if `duration` is omitted; body is clamped to `>=0.05 s`.
  - *"Add a tap at 12.5s on lane 3."*
  - *"Place a 1.5-second hold on lane 0 starting at 20s."*
- Reflect tracks across the lane axis in a range (`mirror_lanes` uses `laneCount - 1 - track`; mirrors `endTrack` too when present).
  - *"Mirror the chorus from 60s to 80s."*
- Slide tracks left/right by an integer delta (`shift_lanes`); tracks clamp at lane bounds (no wraparound).
  - *"Move every note between 10s and 20s one lane to the right."*
- Slide times forward/back by a float seconds delta (`shift_time`); preserves Hold body length, re-sorts.
  - *"Push everything from 40s to 60s 0.25 seconds later — the audio drifted."*
- Convert one type to another in a range (`convert_type`); the `from_type` filter must match. `tap → hold` populates `endTime` to `time + duration`; converting away from Hold zeros `endTime`.
  - *"Turn every tap between 50s and 55s into a 0.5s hold."*
  - *"Convert flicks to taps from 1:00 to 1:30."*

**Out of scope here:**
- No multi-track insertion in one op (issue several `insert` ops in the same response).
  - ✗ *"Add a chord at 12s spanning lanes 0, 2, and 4 in one op."* → the LLM has to emit three `insert` ops.
- No "create a chord" / "create a stream" macros — the LLM expresses these as sequences of `insert` ops.
  - ✗ *"Generate a 16th-note stream from 30s to 32s alternating lanes 1 and 5."* (it can be done, but only as ~16 explicit `insert` ops; no macro op.)
- No quantize-to-grid or snap-to-marker. `shift_time` takes raw seconds.
  - ✗ *"Snap every note to the nearest beat."*
  - ✗ *"Align all taps in the chorus to the AI markers."*
- No copy/paste region.
  - ✗ *"Copy the pattern from 30s–34s and paste it at 60s."*

**Modes:** All four (`bandori`, `arcaea`, `lanota`, `cytus`).

**Caveats:**
- `convert_type` will not convert into Slide / Arc / ArcTap from the common vocabulary — those types take parent-link / coordinate / sample data the common op can't supply. Use `add_arc` / `add_slide` / `add_arctap` instead.
- Tracks are clamped silently. A `shift_lanes delta=+5` on a 7-lane chart pushes notes that started at lane 5 or 6 into lane 6, not lane 7+.
- Mirroring on Cytus has limited semantic meaning (the playfield is sweep-driven, not lane-driven) — the skill doc warns the LLM about this.

---

### 15.2 Region B — Arc geometry (Arcaea 3D)

**Data:** `EditorNote` entries with `type == Arc`. Stored fields: `time`, `endTime`, `arcStartX`, `arcEndX`, `arcStartY`, `arcEndY` (all in `[0,1]`), `arcEaseX`, `arcEaseY` (float-encoded codes — see `parseArcEase`), `arcColor` (0=blue, 1=pink), `arcIsVoid`. Multi-waypoint arcs decompose into N-1 single-segment arcs on export, reconstructed on import by matching successive arcs of the same color.

**Ops:** `add_arc`, `delete_arc`, `shift_arc_height`, `add_arctap`, `delete_arctap`.

**What the Copilot can do here:**
- Place one arc segment per `add_arc`. Multi-segment arcs require multiple ops (the skill doc tells the LLM to chain them).
  - *"Place a blue arc from lane 0 ground to lane 6 sky over 4s starting at 10s."*
  - *"Add a pink arc from (0.2, 0.0) to (0.8, 1.0) at 25s, 2 seconds long."*
- Specify both X and Y easing as string codes — `s`, `b`, `si`, `so`, `sisi`, `siso`, `sosi`, `soso`. Unknown codes silently fall to linear (`s`).
  - *"Add an arc at 18s with sine-in easing on X and sine-out easing on Y."*
- Mark an arc as `void` so it's invisible — useful as an ArcTap carrier.
  - *"Add a void carrier arc at 40s for 1 second at sky height to hold sky-tap diamonds."*
- Bulk-shift the height of every arc whose start time is in a range, with `[0,1]` clamping (`shift_arc_height`).
  - *"Raise every arc between 60s and 75s by 0.2."*
  - *"Drop the arcs in the bridge by 0.3 — they're floating too high."*
- Place an ArcTap at a time; the parent arc is auto-resolved by finding the arc whose `[time, endTime]` brackets the tap, falling back to the nearest-edge arc when none strictly brackets. If no Arc exists in the chart at all, the op is a no-op.
  - *"Place an arctap at 18.5s"* (assumes a void/visible arc already covers that time).
- Delete arcs in a range. **Cascading delete** also removes ArcTaps whose `arcTapParent` points at a doomed arc, and rewrites parent indices for surviving ArcTaps via a remap table (two-pass scheme — single-pass `remove_if` would be wrong).
  - *"Delete every arc from 90s to 100s — including any arctaps that were riding them."*

**Out of scope here:**
- No coordinate-space math beyond clamp. The Copilot can't "rotate this arc 90° in playfield space" or "mirror in X" as a single op (express as repeated `add_arc` / `delete_arc`).
  - ✗ *"Rotate the arc at 30s 90 degrees clockwise around the playfield center."*
  - ✗ *"Mirror every arc between 50s and 60s along the X axis."* (no single mirror-arc op; the LLM would have to delete and re-add each one with `1 - x` coords).
- No arc-merge or split ops. The waypoint reconstruction happens at load time inside `ChartLoader`, not via Copilot ops.
  - ✗ *"Merge the two adjacent arcs at 14s and 15s into one continuous arc."*
  - ✗ *"Split the arc at 20s into two segments at 21.5s."*
- No bulk recolor (no op flips `arcColor` for a range — express as delete + re-add).
  - ✗ *"Recolor every blue arc in the chorus to pink."* (each arc must be deleted and re-added with `color=1`.)

**Modes:** `arcaea` only. Gating rejects in `bandori`/`lanota`/`cytus` and surfaces a `Rejected (mode=…)` line in `lastError`.

**Caveats:**
- ArcTap parent re-fixup after `sortByTime` works by matching the original parent's time; if two arcs share the same time, the first one wins (same policy the editor uses).
- Coordinates that arrive `<0` or `>1` are clamped, not rejected — the skill doc primes the LLM to stay in range.
- Constructing a chart from scratch: the LLM must `add_arc` before `add_arctap`, otherwise the tap silently no-ops.

---

### 15.3 Region C — Slide paths (Cytus / ScanLine)

**Data:** `EditorNote` entries with `type == Slide`. Stored fields: `time`, `endTime`, `scanX`, `scanY` (start point), `scanPath` (`std::vector<std::pair<float,float>>`, normalized `[0,1]`), `samplePoints` (slide-tick times in seconds-from-start, used for combo scoring).

**Ops:** `add_slide`, `delete_slide`.

**What the Copilot can do here:**
- Author a slide with a polyline path and a sample-tick schedule. Each path point clamps to `[0,1]`. Each sample clamps to `[0, duration]`.
  - *"Add a slide at 22s lasting 1.5s that traces a left-to-right zigzag across the sweep."*
  - *"Place a slide at 40s starting at (0.2, 0.5), curving through (0.5, 0.8) and ending at (0.8, 0.5), with 4 sample ticks evenly spaced."*
- If the LLM omits the starting node, the apply path synthesizes one from `(scanX, scanY)` so a single-node `path` is still valid.
  - *"Add a tiny slide at 30s at scan position (0.5, 0.5) with no path — just a one-point ribbon."*
- Delete every Slide in a time range.
  - *"Delete every slide in the chorus from 60s to 80s."*

**Out of scope here:**
- No partial-path edit (insert / remove a single waypoint mid-path). Re-author the slide with a new path.
  - ✗ *"Insert a waypoint at (0.6, 0.4) into the slide at 22s."*
  - ✗ *"Remove the third node of the slide at 22s."* (delete + re-add with the new path is the only way.)
- No path-direction validation. The skill doc tells the LLM that the path should follow the sweep direction at the slide's `time`, but the apply path doesn't enforce it.
  - ✗ *"Verify all my slides go in the right sweep direction."* (no validator op; you'd have to inspect manually.)
- No sample-density inference. The LLM has to provide `samples` itself or accept the LLM-emitted defaults.
  - ✗ *"Auto-fill 16 evenly-spaced sample ticks across this slide."* (the LLM may emit them in the prompt, but there is no dedicated densify op.)

**Modes:** `cytus` only.

**Caveats:**
- Slide paths in normalized space — the LLM must understand the sweep convention. The skill doc describes the coordinate system; ScanLine reverses Y between sweeps, but this is handled at render time, not inside the op.

---

### 15.4 Region D — Hold body waypoints (Bandori / Arcaea / Lanota)

**Data:** Each `EditorNote` of type `Hold` carries `waypoints: std::vector<EditorHoldWaypoint>` where `tOffset` is seconds from the hold start, `lane` is the integer target lane, `transitionLen` is fixed at 0.3 s on insert (matches the UI drag-record default), and `style` is one of `Straight` / `Angle90` / `Curve` / `Rhomboid`. There is also a **legacy** `transition` field on the Hold itself; `set_hold_transition` updates both for parity with the editor's "Apply to All Holds" button.

**Ops:** `add_hold_waypoint`, `remove_hold_waypoint`, `set_hold_transition`.

**What the Copilot can do here:**
- Append a waypoint to one specific Hold, identified by its start time (`note_time`) within a 1 ms tolerance.
  - *"Add a waypoint to the hold at 12s that switches to lane 4 at 12.5s."*
- Pin the lane change at an absolute `at_time` along the hold body. The op clamps to "must be inside `(noteTime, noteTime + bodyLength]`" — out-of-body waypoints are silently no-op.
  - *"On the hold at 8s (which lasts until 11s), add a waypoint at 10s on lane 2."*
- Choose the visual transition style per waypoint. Inserts are sorted by `tOffset` so the gameplay evaluator can walk the list linearly.
  - *"Add a 90-degree-angle waypoint to the hold at 14s, switching to lane 5 at 14.4s."*
  - *"Add a rhomboid-style waypoint on the hold at 20s at 20.6s targeting lane 0."*
- Remove a single waypoint by time match (1 ms tolerance).
  - *"Remove the waypoint at 12.5s from the hold at 12s."*
- Bulk-rewrite the transition style for every Hold in a time range (touches both legacy `transition` and every waypoint's `style`).
  - *"Make every hold in the bridge from 60s to 80s use straight transitions."*
  - *"Apply curve style to all holds in the verse."*
- Author a multi-step zigzag inside one hold (express as several `add_hold_waypoint` ops in the same response).
  - *"Add a zigzag hold from 2s to 4s switching lanes 1-3-1-3."* (the LLM emits an `insert` for the hold + 3 `add_hold_waypoint` ops.)

**Out of scope here:**
- No single-op zigzag macro. The LLM expresses this as a sequence of `add_hold_waypoint` ops; there is no `add_hold_zigzag` op that takes a list of lanes and times.
  - ✗ *"Make the hold at 30s zigzag through lanes [1, 4, 2, 6] in one shot."* (must be 4 separate `add_hold_waypoint` ops.)
- No body-length edit (would require touching the parent Hold's `endTime`, which goes through `convert_type` or `insert` instead).
  - ✗ *"Extend the hold at 30s by 0.5 seconds."*
  - ✗ *"Shorten every hold in the chorus by 20%."*
- Cytus has no waypoint model — gated off.
  - ✗ *"Add a waypoint to the cytus hold at 30s."* → rejected with `Rejected (mode=cytus): add_hold_wp …` in `lastError`.

**Modes:** `bandori`, `arcaea`, `lanota`. Rejected in `cytus`.

**Caveats:**
- `noteTime` matching uses **nearest hold within 1 ms**. If two holds share the same start time, the first one in the vector wins — the LLM should disambiguate by including the lane in the prompt context, but the op itself can't.
- `transitionLen` is hardcoded to 0.3 s on insert. This matches the UI drag-record default; the Copilot has no op to change it.

---

### 15.5 Region E — Disk animation (Lanota)

**Data:** Three independent keyframe vectors held by the `SongEditor`, *not* by `EditorNote`:
- `diskRot()` → `std::vector<DiskRotationEvent>` (`startTime`, `duration`, `targetAngle` in radians, `easing`)
- `diskMove()` → `std::vector<DiskMoveEvent>` (`startTime`, `duration`, `target = {x, y}`, `easing`)
- `diskScale()` → `std::vector<DiskScaleEvent>` (`startTime`, `duration`, `targetScale`, `easing`)

Easings: `linear`, `sineInOut`, `quadInOut`, `cubicInOut`. All inserts are kept sorted by `startTime` via `std::upper_bound`.

**Ops:** `add_disk_rotation`, `add_disk_move`, `add_disk_scale`, `delete_disk_event`.

**What the Copilot can do here:**
- Append a single keyframe to any of the three channels. Targets are **absolute** (rotation in radians from chart origin, not deltas; move in world coords; scale as a multiplier).
  - *"Rotate the disk to π radians (180°) over 2 seconds starting at 16s."*
  - *"Move the disk to (0.3, -0.2) over 1 second starting at 24s."*
  - *"Scale the disk down to 0.7 over 1.5 seconds starting at 40s."*
- Choose an easing per keyframe.
  - *"Rotate the disk 360° over 4s at 60s with cubic-in-out easing."*
  - *"Move the disk to (0, 0) starting at 80s with linear easing — I want a hard slide."*
- Delete one event from one channel by `(kind, start_time)` with 1 ms tolerance. `kind` is `"rotation"` / `"move"` / `"scale"`.
  - *"Delete the disk rotation event that starts at 16s."*
  - *"Remove the scale keyframe at 40s."*

**Out of scope here:**
- No bulk delete in a range (single-event delete only).
  - ✗ *"Delete every disk rotation between 60s and 90s."* (each one must be deleted individually by start time.)
  - ✗ *"Clear all disk animation in the chart."*
- No relative-target ops ("rotate by +90°"). The LLM has to compute the absolute target itself; the skill doc explains the radian convention with examples.
  - ✗ *"Rotate the disk an additional 90° on top of the current angle."* (the LLM must inspect the current state and compute the absolute target.)
- No support for the renderer's other animation hooks (per-ring offsets, etc.).
  - ✗ *"Animate the inner ring's color over the chorus."*
  - ✗ *"Tilt the disk plane 15° forward."*

**Modes:** `lanota` only.

**Caveats:**
- Routing: these ops live outside the `notes` vector, so the Copilot apply loop calls `applyChartEditOpExtended(SongEditor&, …)` for them. `isExtendedOp(op)` is the gate.
- Snapshot must capture all three vectors so Undo restores them — `ChartSnapshot` was extended in Phase 7 for exactly this.
- `duration` is clamped `>= 0` (a 0-second event = instant snap).

---

### 15.6 Region F — Scan-page speed map + scan-speed events (Cytus / ScanLine)

**Data:** Two structures held by the `SongEditor`:
- `scanPages()` → `std::vector<ScanPageOverride>` (`pageIndex`, `speed`). Source of truth for per-page speed; pages without an override use `1.0`.
- `scanSpeed()` → `std::vector<ScanSpeedEvent>` (`startTime`, `duration`, `targetSpeed`, `easing`). Low-level continuous speed keyframes; only emitted on export when no page overrides exist (avoids round-trip drift).

**Ops:** `set_page_speed`, `add_scan_speed_event`, `delete_scan_speed_event`.

**What the Copilot can do here:**
- **Upsert** the speed multiplier for one specific `page_index` (`set_page_speed`). If an override for that page already exists, it's replaced in place; otherwise inserted in `pageIndex` order. This is the recommended op for page-boundary step changes.
  - *"Set page 4 speed to 1.5x."*
  - *"Slow page 12 down to 0.5x — it's the breakdown."*
  - *"Reset page 7 to normal speed."* (LLM emits `set_page_speed page_index=7 speed=1.0`.)
- Add a low-level scan-speed keyframe (`add_scan_speed_event`) with easing — for continuous speed ramps that don't align to page boundaries.
  - *"Ramp the scan speed from 1.0x up to 2.0x over 2 seconds starting at 60s, with sine-in-out easing."*
- Delete scan-speed events whose `startTime` falls in a range.
  - *"Remove every scan-speed event between 60s and 70s — I want to redo that ramp."*

**Out of scope here:**
- No bulk page-speed reset ("set every page back to 1.0" is a sequence of `set_page_speed` ops).
  - ✗ *"Reset every page in the chart to 1.0x."* (each page needs its own `set_page_speed`; no wildcard.)
- No mass-reflow of pages (page count comes from the page-authoring UI, not from Copilot ops).
  - ✗ *"Add 4 more pages to this chart."*
  - ✗ *"Delete page 8 entirely."*
- No "speed up everything by 1.2x" multiplier.
  - ✗ *"Multiply every existing page speed by 1.2."* (the LLM would have to enumerate each existing page and emit one `set_page_speed` per page.)
- No clearing the page-override list via `delete_scan_speed_event` (that op only touches the keyframe vector — to clear a page override, issue `set_page_speed speed=1.0`).
  - ✗ *"Clear the override on page 4."* (the workaround is `set_page_speed page_index=4 speed=1.0`.)

**Modes:** `cytus` only.

**Caveats:**
- `set_page_speed` is an upsert, not an insert — issuing it twice for the same page produces one entry, not two.
- Prefer `set_page_speed` over `add_scan_speed_event` for normal page-step changes; the skill doc tells the LLM this explicitly.
- `delete_scan_speed_event` only touches the keyframe vector, not the page-override vector. To clear a page override, issue `set_page_speed speed=1.0`.

---

### 15.7 Mode availability summary

| Region | Ops | bandori | arcaea | lanota | cytus |
|---|---|:-:|:-:|:-:|:-:|
| A. Note timeline | 6 common ops | ✓ | ✓ | ✓ | ✓ |
| B. Arc geometry | 5 arc/arctap ops | — | ✓ | — | — |
| C. Slide paths | 2 slide ops | — | — | — | ✓ |
| D. Hold waypoints | 3 waypoint ops | ✓ | ✓ | ✓ | — |
| E. Disk animation | 4 disk ops | — | — | ✓ | — |
| F. Scan-page speed | 3 scan ops | — | — | — | ✓ |
| **Total ops per mode** |  | **9** | **14** | **13** | **11** |

Phigros mode is intentionally excluded (see §11).

### 15.8 Cross-region invariants the Copilot honors

- **Single-difficulty scope.** Every op acts on the currently-selected difficulty's vectors. There is no cross-difficulty op.
- **Single-level undo.** One snapshot is captured at Apply time covering all 8 fields (`notes`, `markers`, `features`, `diskRot`, `diskMove`, `diskScale`, `scanSpeed`, `scanPages`). A second Apply overwrites it. Multi-level undo is deferred (§11).
- **Mode-gating before apply.** `isOpAllowedForMode` filters the parsed ops at the SongEditor send callback; rejected ops surface in `lastError` so the user sees why they were dropped, but the remaining valid ops in the same batch still apply.
- **Routing by extended-op flag.** `isExtendedOp(op)` decides whether the Copilot apply loop calls `applyChartEditOp(notes, …)` (note-vector ops) or `applyChartEditOpExtended(*this, …)` (disk + scan-speed ops).
- **No silent acceptance of unknown ops.** `parseChartEditOps` skips kinds it doesn't recognize. The per-mode skill doc constrains the LLM's vocabulary so this should not happen in practice — but if a future model emits a vocab the engine doesn't know yet, those ops are dropped rather than misapplied.
- **All clamping is silent.** Out-of-range tracks, out-of-`[0,1]` arc/slide coordinates, negative durations, etc. are clamped without warning. The skill doc trains the LLM to stay in range; the apply path is the safety net.

### 15.9 What the Copilot still cannot edit

Recorded here so the next time someone asks "can it do X" the answer is fast. Each item lists a representative prompt the Copilot will *refuse* (returning explanation + empty `ops`).

- **Audio.** Source file, gain, offset, EQ, BPM map. Authored in the Audio tab.
  - ✗ *"Replace the song audio with my new mixdown.wav."*
  - ✗ *"Lower the music volume by 3 dB."*
  - ✗ *"Shift the audio offset by +20 ms."*
  - ✗ *"Set the BPM to 174."*
- **Markers + audio features.** AI marker placement is a separate analysis pipeline (`tools/analyze_audio.py` + `SongEditor::Place All`). The Copilot reads notes only.
  - ✗ *"Re-analyze the audio and place new markers."*
  - ✗ *"Drop a marker at every snare hit."*
  - ✗ *"Thin the markers in the chorus."*
- **Materials and shaders.** Owned by the Material tab and the asset library. AI shader generation is a separate flow (`ShaderGenClient`). See `sys1_rendering.md` Phase 4 + the Shader Generator memory.
  - ✗ *"Make the tap notes glow neon pink."*
  - ✗ *"Generate a custom shader that pulses with the bass."*
  - ✗ *"Switch the hold material to the new MAT asset."*
- **HUD layout.** `Pos / Size / Color` from the Basic tab.
  - ✗ *"Move the score readout to the top-left."*
  - ✗ *"Make the combo counter twice as big."*
  - ✗ *"Color the HUD text cyan."*
- **Background, judgment-window, scoring fields.** Basic tab; persisted in `music_selection.json`.
  - ✗ *"Set the background to bg.png."*
  - ✗ *"Tighten the perfect window to 30 ms."*
  - ✗ *"Set total score to 1,200,000."*
- **Project-level config.** Hub, Start Screen, settings page — all out of scope.
  - ✗ *"Rename this project to 'NewSong'."*
  - ✗ *"Change the player note-speed setting to 8."*
- **Difficulty management.** Add/remove a difficulty, retitle, duplicate. SongEditor does that, not the Copilot.
  - ✗ *"Add a 'Master' difficulty to this song."*
  - ✗ *"Duplicate the Hard chart and call it Expert."*
  - ✗ *"Delete the Easy difficulty."*
- **Cross-difficulty operations.** "Copy the easy chart's tap pattern into hard." Not in scope.
  - ✗ *"Copy every tap from Easy into Hard at half density."*
  - ✗ *"Make Medium identical to Easy but with all flicks added on the offbeats."*
- **AI marker thinning, audit metrics, style transfer, autocharter.** Each of these has its own dedicated entry point in the editor; the Copilot does not invoke them.
  - ✗ *"Run the chart audit and fix the jacks."*
  - ✗ *"Auto-generate the whole chart from the markers."*
  - ✗ *"Style-transfer this chart to look like the Aa_drop3d_hard chart."*

If a user prompt asks for any of the above, the per-mode skill docs prime the LLM to respond with an explanation + empty `ops` array — the preview shows the explanation, no edits land.
