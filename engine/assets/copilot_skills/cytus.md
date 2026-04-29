# Cytus (Scan Line) Skill

## Brief

Horizontal scan-sweep chart inspired by Cytus. A scan line moves up and
down the playfield; notes are hit when the line passes over them.
Authoring uses **pages** (one full sweep = one page) with alternating
direction and optional per-page speed multipliers. Slides follow a drag
path across the sweep.

## Concepts

- **Page** — one complete sweep of the scan line. Page 0 goes bottom→top,
  page 1 top→bottom, page 2 bottom→top, etc. Default duration is
  `240/bpm` seconds.
- **Sweep direction** — alternates per page; slide paths should move in
  the direction of the sweep at the note's time.
- **Normalized coords `scanX`, `scanY`** — `[0..1]` inside the sweep frame.
  `scanY=0` is the start edge, `scanY=1` is the end edge of that page's
  sweep. `scanX` is left-to-right.
- **Page speed override** — sparse array of `{pageIndex, speed}`; pages
  without an entry use `1.0`. The editor writes this as `scanPages` and
  **treats it as the source of truth**. (Legacy `scanSpeedEvents` are only
  emitted when no page overrides exist.)
- **Slide** — a path-based note with `samplePoints` (slide-tick times) and
  `scanPath` (a list of `(x,y)` control points).
- **Lane concept is soft.** ScanLine charts do not use a fixed lane grid;
  notes are positioned by `scanX`/`scanY`. The `lane_count` field in the
  chart context is a carry-over for compatibility; prefer positional ops.

## Supported note types

| Type  | Meaning | Notes |
|-------|---------|-------|
| tap   | single tap at `(scanX, scanY)` on current page | |
| hold  | sustained press; can cross pages via `scanHoldSweeps` | |
| flick | swipe at `(scanX, scanY)` | rare in Cytus; direction implicit from sweep |
| slide | drag along a path with sample-tick scoring | mode-specific |

No arcs, no arctaps, no disk animation in this mode.

## Constraints

- `scanX`, `scanY` must be in `[0, 1]`.
- Slide `scanPath` must have `>= 2` points; samples (tick times) should
  fall inside `[0, duration]`.
- Hold `scanHoldSweeps` counts *extra* sweeps the hold crosses
  (0 = single-page).
- `page_index` for speed overrides is 0-based and must be >= 0.
- Slide paths should move in the direction of the sweep at `note.time`;
  reverse-direction slides look broken even if they parse.

## Mode-specific ops

### `add_slide`
Create one slide at `time` with a path across the sweep. The path
should move in the direction of the sweep at `time`; reverse-direction
slides look broken even though they parse. Samples are slide-tick
times relative to the note start (`0` to `duration`).
```
{"op":"add_slide", "time":12.0, "duration":0.8,
 "scanX":0.1, "scanY":0.2,
 "path": [[0.1, 0.2], [0.35, 0.45], [0.7, 0.65], [0.9, 0.85]],
 "samples": [0.2, 0.4, 0.6]}
```

### `delete_slide`
Remove every slide whose `time` is in `[from, to]`.
```
{"op":"delete_slide", "from":24.0, "to":32.0}
```

## Common ops

All six shared ops from `_common.md`: `delete_range`, `insert`,
`mirror_lanes`, `shift_lanes`, `shift_time`, `convert_type`.

**Caveat:** `mirror_lanes` and `shift_lanes` are lane-index based and
have limited meaning in ScanLine, where positioning is via `scanX/scanY`.
Prefer `delete_range`, `shift_time`, `convert_type`, and the
mode-specific `add_slide` / `delete_slide` for scan charts.

### `set_page_speed`
Set (or upsert) the speed multiplier for one scan page. Pages without
an override use 1.0. `page_index` is 0-based.
```
{"op":"set_page_speed", "page_index":4, "speed":1.5}
```

### `add_scan_speed_event`
Low-level scan-speed keyframe. Prefer `set_page_speed` for page-boundary
step changes; this op is for mid-page transitions. `easing` is one of
`"linear"`, `"sineInOut"`, `"quadInOut"`, `"cubicInOut"`.
```
{"op":"add_scan_speed_event", "start_time":40.0, "duration":2.0,
 "target":1.5, "easing":"sineInOut"}
```

### `delete_scan_speed_event`
Remove scan-speed events whose `start_time` is in `[from, to]`.
```
{"op":"delete_scan_speed_event", "from":40.0, "to":44.0}
```

## Examples

### Example 1 — delete a failed section
User: *"Clear everything between 12 and 20 seconds."*
```
{
  "explanation": "Delete all notes in 12..20s.",
  "ops": [{"op":"delete_range", "from":12.0, "to":20.0, "type_filter":"any"}]
}
```

### Example 2 — pull a phrase earlier to fix sync
User: *"Move the section from 40s to 44s forward 80ms."*
```
{
  "explanation": "Shift all notes in 40..44s by -0.08s.",
  "ops": [{"op":"shift_time", "from":40.0, "to":44.0, "delta":-0.08}]
}
```

### Example 3 — page speed change
User: *"Make page 4 run at 1.5x speed."*
```
{
  "explanation": "Set page 4 speed multiplier to 1.5x.",
  "ops": [{"op":"set_page_speed", "page_index":4, "speed":1.5}]
}
```

### Example 4 — add a left-to-right slide on a rising sweep
User: *"Add a slide from the left edge to the right edge at 32s, 600ms long, on the rising sweep."*
```
{
  "explanation": "Add a slide at 32s spanning x=0.05..0.95 with rising y.",
  "ops": [{"op":"add_slide", "time":32.0, "duration":0.6,
           "scanX":0.05, "scanY":0.1,
           "path":[[0.05, 0.1], [0.35, 0.35], [0.65, 0.65], [0.95, 0.9]],
           "samples":[0.15, 0.3, 0.45]}]
}
```

### Example 5 — delete only taps, leave slides intact
User: *"Remove the taps in 24..32s but keep the slides."*
```
{
  "explanation": "Delete taps only in 24..32s; slides, holds, flicks untouched.",
  "ops": [{"op":"delete_range", "from":24.0, "to":32.0, "type_filter":"tap"}]
}
```

### Example 6 — tidy up a failed slide experiment
User: *"Delete every slide between 40 and 48 seconds."*
```
{
  "explanation": "Delete slides in 40..48s.",
  "ops": [{"op":"delete_slide", "from":40.0, "to":48.0}]
}
```
