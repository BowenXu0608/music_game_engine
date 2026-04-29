# Arcaea (3D Drop Notes) Skill

## Brief

3D-perspective drop chart inspired by Arcaea. Ground notes scroll along
a highway toward the judgment line; floating sky notes (ArcTaps) sit on
curved arc trajectories overhead. The scene has both a ground plane and
a sky plane, and arcs connect them.

## Concepts

- **Ground lane / track** — discrete 0-based lane at `y = 0`. `trackCount`
  is typically 7, range 3..12.
- **Sky plane** — floating region above the ground; arcs and ArcTaps live
  here. `skyHeight` multiplies the normalized `y` coord into world space.
- **Arc** — a curved, 3D-tube-rendered path from `(startX, startY)` to
  `(endX, endY)`. X and Y are normalized `[0..1]`. `color` is 0 (cyan/
  blue) or 1 (pink/red). `isVoid=true` hides the arc visually; it only
  carries ArcTaps.
- **ArcTap** — a flat tap attached to a point along an arc. Position is
  evaluated from the parent arc's curve at the tap's time. The parent is
  resolved automatically — just emit the tap time.
- **Arc easing** — per-axis ease codes (pass as strings in the JSON):
  `"s"` linear (default), `"b"` bezier, `"si"` sine-in, `"so"` sine-out,
  `"sisi"`, `"siso"`, `"sosi"`, `"soso"`. Positive codes accelerate in;
  negative codes decelerate out.

## Supported note types

| Type   | Meaning | Notes |
|--------|---------|-------|
| tap    | ground tap at `lane` | standard |
| hold   | ground sustained press, `duration > 0` | supports multi-waypoint path |
| flick  | ground swipe | phrase ends |
| arc    | curved path, ground↔sky | mode-specific — use `add_arc` |
| arctap | tap floating on an arc | mode-specific — use `add_arctap` |

No slides in this mode.

## Constraints

- Ground `track` must be in `[0, lane_count)`.
- Arc `startX/endX/startY/endY` must be in `[0, 1]` (normalized scene).
- Arc `color` must be 0 (blue) or 1 (pink); set `"void": true` for an
  invisible carrier that only exists to hold ArcTaps.
- ArcTap `time` must fall inside the `[time, time+duration]` window of
  some existing arc. If no arc brackets the tap's time, the op no-ops
  and the user is warned — ask for or add an arc first.
- Hold `duration` > 0.

## Mode-specific ops

### `add_arc`
Create one arc segment. Use multiple `add_arc` calls to build a longer
curve — the editor reconstructs multi-waypoint arcs at import time by
chaining consecutive arcs of the same `color`.
```
{"op":"add_arc", "time":10.0, "duration":2.0,
 "startX":0.1, "startY":0.0, "endX":0.5, "endY":0.8,
 "easeX":"si", "easeY":"so", "color":0, "void":false}
```

### `delete_arc`
Remove every arc whose `time` is in `[from, to]`. Any ArcTap parented
to a deleted arc is also removed (no orphans).
```
{"op":"delete_arc", "from":8.0, "to":12.0}
```

### `shift_arc_height`
Add `delta` to every arc endpoint Y in `[from, to]`; values clamp to
`[0, 1]`. Use to raise or lower a whole phrase without touching shape.
```
{"op":"shift_arc_height", "from":16.0, "to":24.0, "delta":0.2}
```

### `add_arctap`
Place one ArcTap at `time`; the parent arc is resolved automatically
from the existing chart. No-op if no arc brackets `time`.
```
{"op":"add_arctap", "time":11.5}
```

### `delete_arctap`
Remove every ArcTap whose `time` is in `[from, to]`.
```
{"op":"delete_arctap", "from":20.0, "to":24.0}
```

### Hold waypoint ops
Arcaea ground holds support the same multi-waypoint path as other drop
modes. See `_common.md`-adjacent references below for the three ops:
`add_hold_waypoint`, `remove_hold_waypoint`, `set_hold_transition`.
Style codes: `"straight"`, `"angle90"`, `"curve"`, `"rhomboid"`.
```
{"op":"add_hold_waypoint", "note_time":4.0, "at_time":4.8,
 "lane":3, "style":"curve"}
{"op":"set_hold_transition", "from":16.0, "to":32.0, "style":"rhomboid"}
```

## Common ops

All six shared ops from `_common.md` (`delete_range`, `insert`,
`mirror_lanes`, `shift_lanes`, `shift_time`, `convert_type`) operate
on ground notes (tap / hold / flick). They do NOT touch arcs or
ArcTaps — use the mode-specific ops above for those.

## Examples

### Example 1 — delete a ground phrase without touching arcs
User: *"Remove the ground taps between 20 and 28 seconds but keep the arcs."*
```
{
  "explanation": "Delete taps only in 20..28s; arcs untouched.",
  "ops": [{"op":"delete_range", "from":20.0, "to":28.0, "type_filter":"tap"}]
}
```

### Example 2 — add a sky-rising blue arc
User: *"Add a blue arc from lane 0 ground to the middle sky over 2 seconds starting at 10s."*
```
{
  "explanation": "Add blue arc from (0.0, 0.0) to (0.5, 1.0) over 2s at 10s.",
  "ops": [{"op":"add_arc", "time":10.0, "duration":2.0,
           "startX":0.0, "startY":0.0, "endX":0.5, "endY":1.0,
           "easeX":"si", "easeY":"so", "color":0, "void":false}]
}
```

### Example 3 — raise all arcs in the chorus
User: *"Raise the arc heights by 0.2 between 32 and 48 seconds."*
```
{
  "explanation": "Shift arc heights in 32..48s by +0.2.",
  "ops": [{"op":"shift_arc_height", "from":32.0, "to":48.0, "delta":0.2}]
}
```

### Example 4 — attach three arctaps along an existing arc
User: *"Put three sky taps at 11, 11.5, and 12 seconds (there's already an arc there)."*
```
{
  "explanation": "Add three ArcTaps along the existing arc.",
  "ops": [
    {"op":"add_arctap", "time":11.0},
    {"op":"add_arctap", "time":11.5},
    {"op":"add_arctap", "time":12.0}
  ]
}
```

### Example 5 — tidy up a failed arc experiment
User: *"Delete the arcs and arctaps between 36 and 40 seconds."*
```
{
  "explanation": "Delete arcs + arctaps in 36..40s (deleting an arc cascades to its arctaps).",
  "ops": [
    {"op":"delete_arc",    "from":36.0, "to":40.0},
    {"op":"delete_arctap", "from":36.0, "to":40.0}
  ]
}
```
