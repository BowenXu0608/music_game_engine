# Bandori (2D Drop Notes) Skill

## Brief

Vertical drop chart like BanG Dream / Project Sekai. Notes scroll from
the top of the screen toward a judgment line at the bottom. Lanes are
straight vertical rails; there is no depth, no sky, no rotating disk.

## Concepts

- **Lane / track** — discrete 0-based vertical rail. `trackCount` is
  typically 7, range is 3..12. Lane 0 is leftmost.
- **Judgment line** — horizontal bar at the bottom; notes are hit when
  they cross it.
- **Scroll speed** — configured by the player, not the chart.

## Supported note types

| Type  | Meaning | Notes |
|-------|---------|-------|
| tap   | single tap at (time, track) | most common |
| hold  | sustained press, duration > 0 | supports cross-lane transitions via waypoints |
| flick | swipe, scored like a tap | usually ends a phrase |

No arcs, no arctaps, no slides, no disk animation, no scan sweeps in
this mode.

## Constraints

- `track` must be in `[0, lane_count)`.
- `duration` > 0 for holds.
- Bandori charts never use `type="slide"` / `"arc"` / `"arctap"`. If
  asked, return `"ops": []` and explain.

## Mode-specific ops

### `add_hold_waypoint`
Append a lane-change waypoint to an existing Hold. Identify the hold by
its start time; `at_time` is the absolute time along the body; `lane` is
the target lane at that point; `style` is one of `"straight"`,
`"angle90"`, `"curve"`, `"rhomboid"`. No-op if no Hold matches or
`at_time` falls outside the body.
```
{"op":"add_hold_waypoint", "note_time":4.0, "at_time":4.8,
 "lane":3, "style":"curve"}
```

### `remove_hold_waypoint`
Remove the waypoint of the hold at `note_time` whose absolute time
equals `at_time` (1 ms tolerance).
```
{"op":"remove_hold_waypoint", "note_time":4.0, "at_time":4.8}
```

### `set_hold_transition`
Set the transition style on every Hold in `[from, to]` — applies to the
legacy single-transition field AND every waypoint. Same behavior as
"Apply to All Holds" in the Note tab.
```
{"op":"set_hold_transition", "from":16.0, "to":32.0, "style":"rhomboid"}
```

## Common ops

All six shared ops from `_common.md`:
`delete_range`, `insert`, `mirror_lanes`, `shift_lanes`, `shift_time`,
`convert_type`.

Other mode-specific ops (arcs, slides, disks, scan-speed) are not
relevant to Bandori and would be rejected; don't emit them here.

## Examples

### Example 1 — mirror a chorus
User: *"Mirror the notes between 16 and 32 seconds."*
```
{
  "explanation": "Mirror lanes in 16..32s.",
  "ops": [{"op":"mirror_lanes", "from":16.0, "to":32.0}]
}
```

### Example 2 — shift a phrase earlier to fix a late feel
User: *"The chorus hits 80ms late, nudge 24s onward back a bit."*
```
{
  "explanation": "Shift all notes from 24s onward 80ms earlier.",
  "ops": [{"op":"shift_time", "from":24.0, "to":9999.0, "delta":-0.08}]
}
```

### Example 3 — upgrade phrase-end taps to flicks
User: *"Turn every tap between 48 and 64s into a flick."*
```
{
  "explanation": "Convert taps in 48..64s to flicks.",
  "ops": [{"op":"convert_type", "from":48.0, "to":64.0,
           "from_type":"tap", "to_type":"flick"}]
}
```

### Example 4 — delete a failed section so the author can re-chart it
User: *"Clear everything between 12 and 20 seconds."*
```
{
  "explanation": "Delete all notes in 12..20s.",
  "ops": [{"op":"delete_range", "from":12.0, "to":20.0, "type_filter":"any"}]
}
```

### Example 5 — out-of-scope ask
User: *"Add an arc from lane 0 to lane 6."*
```
{
  "explanation": "Bandori 2D mode has no arcs; this op is unavailable.",
  "ops": []
}
```

### Example 6 — add a zigzag to a long hold
User: *"Turn the hold at 4s into a zigzag between lanes 1, 3, 1."*
```
{
  "explanation": "Add two waypoints to the hold at 4s: lane 3 at 4.8s, lane 1 at 5.6s, both curve.",
  "ops": [
    {"op":"add_hold_waypoint", "note_time":4.0, "at_time":4.8,
     "lane":3, "style":"curve"},
    {"op":"add_hold_waypoint", "note_time":4.0, "at_time":5.6,
     "lane":1, "style":"curve"}
  ]
}
```

### Example 7 — bulk-change transition style
User: *"Make every hold in the chorus (32..48s) use the rhomboid style."*
```
{
  "explanation": "Set hold transition to rhomboid across 32..48s.",
  "ops": [{"op":"set_hold_transition", "from":32.0, "to":48.0, "style":"rhomboid"}]
}
```
