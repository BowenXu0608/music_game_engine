# April, Part 1: Making It Editable — and Learning Not to Trust the Agent

*Build journal #2 — April 3–17. The engine becomes an authoring tool across four
game modes, and the bugs that taught me to verify generated code instead of
trusting that it runs. (Part 2 covers April 18–29.)*

---

By the end of March the engine could draw a chart, but the chart was hardcoded.
The first half of April closed that gap and taught me the lesson that defines an
AI-assisted build at this scale: **generated code that compiles and runs is not
the same as correct code, and the operator's core job is finding the
difference.** Three bugs in two weeks made that concrete, each a category of
failure a fast, confident agent is well-positioned to produce.

For scope: by April 17 the engine carried an editor with four interactive
layers, a chart loader auto-detecting four external formats, four playable game
modes with distinct renderers, and an Android packaging pipeline — all directed
through agents against an architecture I owned. The interesting engineering
isn't the line count; it's the verification discipline that keeps a build that
size from drifting into incoherence.

---

## The editor, as bounded delegation

Turning a hardcoded chart into an authoring pipeline is the kind of large, vague
task that produces mush if handed to an agent whole. The work was decomposing it
into pieces with crisp contracts: a music-selection layer, a DAW-style editor
(scene preview, chart timeline, waveform, transport), and a per-song game-mode
config. One decision there was mine to make — `GameModeConfig` lives *inside*
`SongInfo`, persisted per song, so one project can hold a flat tap chart and a 3D
arc chart at once. Make game mode project-level and you've foreclosed a feature;
the agent can't know that, because it's a product decision, not a code one.

Gameplay also runs as a separate process — "Test the game" spawns a child via
`CreateProcessW` so the editor stays interactive. That isolation has a
verification payoff too: a crash in agent-generated gameplay code can't take the
editor down with it.

Beat detection I integrated rather than reinvented: `AudioAnalyzer` shells out to
`tools/analyze_audio.py` (the Madmom neural beat-tracker) and drops difficulty-
appropriate markers. Knowing when to wrap an existing tool instead of directing
an agent to write one is part of the same judgment. Underneath, `ChartLoader`
reached its final form — one unified `.json`, auto-detecting four external
formats — with every parser accessor bounds-checked, the kind of defensive detail
worth specifying explicitly since the happy-path version is what you get by
default.

![The DAW-style song editor with a waveform and beat markers](img/2026-04-05_song-editor-daw.png)

> 📸 *Capturable today — `img/2026-04-05_song-editor-daw.png`: the SongEditor on
> a loaded song (preview, timeline, waveform, "Analyze Beats" markers).*

---

## Trust, but verify: the Android incident

The clearest evidence of the discipline this build demanded is from the Android
work, and it matters because the failure was a *documentation/code mismatch* —
the signature failure mode of moving fast with an agent.

A VMA assertion crashed the APK: `vulkanApiVersion >= VK_API_VERSION_1_2`.
Android API 24 guarantees only Vulkan 1.0, so the allocator needed a guard:

```cpp
#if defined(VMA_VULKAN_VERSION) && VMA_VULKAN_VERSION < 4198400
    ai.vulkanApiVersion = VK_API_VERSION_1_0;   // Android: API 24 → 1.0 only
#else
    ai.vulkanApiVersion = VK_API_VERSION_1_2;   // desktop
#endif
```

(The literal `4198400` is mandatory — the `VK_API_VERSION_1_2` macro contains a
cast that's illegal in a `#if`, a platform trap an agent steps into readily.)

The instructive part came a day later: a fresh build crashed with **the exact
assertion the notes said was fixed.** I grepped the source — the guard wasn't
there. It had been documented as done but never committed. The lesson, which I
wrote into the Android notes verbatim, is the central skill of this methodology:
**always grep the actual source before trusting a "fixed" status — including in
notes the agent wrote.** A fast agent generates plausible status as readily as
code; "it says it's done" and "it's done" are different claims, and only one
survives a `grep`. (A second bug the same week — phones reporting a portrait
surface extent with a `ROTATE_90` transform, causing a double-rotation — was a
reminder that on Android, Vulkan surface dimensions are not the displayed window
dimensions.)

---

## Four games, one engine — an interface clean enough to hide a bug

The March architecture paid off here: adding Circle and Scan Line never touched
Bandori. But it surfaced a hazard specific to clean plugin systems. An audit
found the factory had `Circle` and `ScanLine` wired to each other's renderers —
`Circle → CytusRenderer`, `ScanLine → LanotaRenderer` — backwards. Circle charts
rendered as scan-line sweeps. It compiled, it ran, it rendered the wrong game.
That's the precise risk of an interface so uniform the *wrong* implementation
still satisfies it: the types all check, so an agent wiring the factory has no
signal it's wrong. The takeaway
isn't "the agent failed" — it's that a contract clean enough to make delegation
safe is clean enough to hide a mis-wiring from the compiler, so **plugin systems
need end-to-end tests, not just type-checking.**

The headline feature was cross-lane holds — a hold as a *path* of waypoints:

```cpp
struct HoldWaypoint{ float tOffset; int lane; float transitionLen; HoldStyle style; };
int evalHoldLaneAt(const HoldData& h, float tOffset);   // the canonical evaluator
```

That evaluator became the backbone of holds in every mode — and the thing the
next bug turns on. Alongside it, chart files are keyed on `(mode, difficulty)` so
experimenting with a second mode can't overwrite the first's notes.

![The four reachable game modes side by side](img/2026-04-12_four-modes.png)

> 📸 *Capturable today — `img/2026-04-12_four-modes.png`: a 2×2 montage of 2D
> drop, 3D drop, Circle, Scan Line.*

---

## Two bugs that passed every test I'd have thought to write

Here is the month's through-line: **the dangerous bug isn't the one that crashes
— it's the one that works at the value you tested and does nothing, silently, at
the value you didn't.** Agent-generated code is especially prone to it, being
optimized to satisfy the example in front of it.

**The lane mask that never masked (April 14).** In Circle mode, off-screen lanes
should grey out when a keyframe scales the disk past the camera. At 2.82× nothing
greyed. The math worked at the value it was tested with (1.28×) and failed above
it — the bound was the bug:

```cpp
// Broken: half-extents that GROW WITH the disk.  r = outerR * scale
const float halfX = std::max(kFovHalfX, r) + 0.15f;
```

`max(fov, r)` ties the visibility box to the very quantity it measures against:
scale up, and the box grows in lockstep, so a hit point at radius `r` is *always*
inside a box of size ≥ `r` — the test can't fail. The camera's visible rect is
fixed, so the bound must be too:

```cpp
const float halfX = kFovHalfX + 0.15f;   // CONSTANT — the viewport doesn't grow
```

I rewrote the comment above it to explain *why the bound must stay fixed*, so a
later refactor — mine or an agent's — doesn't "simplify" it back. An invariant
that isn't written down is one the next edit will violate.

![Lane-mask gating greying off-screen lanes at high disk scale](img/2026-04-14_lane-mask-gating.png)

> 📸 *Capturable today — `img/2026-04-14_lane-mask-gating.png`: a Circle song with
> a 3×+ disk keyframe, off-screen lanes grey-hatched after it.*

**The click that leaked (April 15).** The *music* stuttered whenever a hold body
crossed the judgment line — an audio-thread symptom. The cause was a one-line
convenience on a hot path:

```cpp
void AudioEngine::playClickSfx() {
    ma_audio_buffer* audioBuf = new ma_audio_buffer();   // new, every call
    ma_sound*        sfx      = new ma_sound();          // new, every call — never freed
    // "leak per click is negligible"  ← the comment left in the generated code
}
```

One shot per tap is fine; cross-lane holds fire sample ticks dozens per second,
and the leaked sources pile up in the mixer while registration blocks the audio
thread. Same shape as the lane mask: correct at one click, broken at a hundred a
second. I deleted the redundant call and recorded the proper fix — a pooled SFX —
as deliberate future work. Both bugs hardened a habit I now apply to anything an
agent produces: **don't ask "does it work," ask "what happens at 10× the input,
and what does this leave behind each time it runs."**

---

## The 3D rebuild: performance is a correctness property

Rebuilding `ArcaeaRenderer`, the interesting decisions were about the GPU cost of
*how* you update geometry. Arcs are static meshes whose consumed portion must
recede smoothly. The naive re-upload runs through a staging buffer — a
`vkQueueWaitIdle` per arc per frame, a stall that wrecks frame pacing. The right
tool is a host-mapped vertex buffer written by `memcpy`, with a continuous clip
parameter so the edge doesn't snap to segments:

```cpp
float tClip = (songTime - startTime) / duration;   // continuous, not segment-snapped
// memcpy clipped vertices into the mapped pointer — no staging, no vkQueueWaitIdle.
```

Choosing the cheap upload path is exactly the GPU-cost reasoning an agent doesn't
apply unprompted: `updateMesh` is *correct*, and catastrophically slow here.

The bug underneath half the others was three magic numbers for one dimension —
`w=3.f`, `LANE_RANGE=5.f`, a hardcoded `0–4` lane formula — silently disagreeing.
Collapsing them to one source of truth (`LANE_HALF_WIDTH`, `LANE_FAR_Z`,
`JUDGMENT_Z`, `GROUND_Y`) made a whole cluster of misalignment bugs vanish at
once. Most of my April bugs weren't bad logic — they were the same quantity
computed two ways and allowed to drift, something an agent generates readily
because each call site looks locally reasonable.

![Arcaea 3D drop with arcs tracing across the sky bar](img/2026-04-17_arcaea-3d.png)

> 📸 *Capturable today — `img/2026-04-17_arcaea-3d.png`: 3D drop with arcs between
> ground and sky bar, taps on lanes, judgment gate.*

---

## What the first half of April established

1. **Verification is the operator's primary job.** The Android doc/code mismatch,
   the backwards factory wiring, the two extreme-value bugs — none caught by the
   compiler, all in code that ran. The question is never "did the agent produce
   something," it's "is what it produced true."
2. **Test at the extremes, not the example.** Generated code satisfies the case
   in front of it; the habit that catches the rest is asking what happens at 10×
   and what gets left behind on the hot path.
3. **Write invariants down or watch them get refactored away** — the fixed bound,
   the single-source constants, the "both subpass dependencies" note.
4. **Performance is a correctness property in graphics** — host-mapped vs. staged
   uploads, a leaked allocation on a dense path show up as stutter, not wrong
   output, and reasoning about that cost is systems knowledge the agent doesn't
   bring on its own.

Part 2: teaching the engine to author *itself* — a material system and five AI
assistants — and the realization that prompting a 3-billion-parameter model is
the same skill as directing the agent that built the engine.
