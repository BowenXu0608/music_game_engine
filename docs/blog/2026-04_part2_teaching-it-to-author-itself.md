# April, Part 2: Teaching It to Author Itself

*Build journal #3 — April 18–29. A material system that moved visual authoring
out of C++, and five AI assistants built on one principle. The month closes with
a realization: prompting a 3-billion-parameter model to write a shader is the
same skill as directing the agent that wrote the engine. (Part 1 covered April
3–17.)*

---

By April 17 the engine was a tool. The back half of the month went two
directions at once — a **material system** that let authors decide how notes look
without me hardcoding it, and an **AI suite** of five assistants that let the
engine help author itself. Both arcs taught the same thing: build a clean
deterministic foundation, and the layer on top — custom shader or language model
— becomes almost free, and stays controllable.

---

## The material system: a render-architecture problem

Until April 18, a note's appearance was baked into its renderer — a Bandori tap
was gold because the C++ said so. Making that authorable is a graphics-
architecture problem with a real performance trap: a naive material system binds
a pipeline per draw and destroys your frame rate. The design that avoids it is a
small palette of fragment-shader *kinds* plus batching that groups draws so kind
changes cost one pipeline bind per frame, not per note.

```cpp
// renderer/Material.h
enum class MaterialKind : uint32_t {
    Unlit = 0,   // textureColor * vertexColor * tint
    Glow,        // Unlit + additive bloom boost
    Scroll,      // UV scrolls over time
    Pulse,       // rgb reacts to a hit-trigger time
    Gradient,    // two-color gradient across the quad
    Custom,      // author-supplied .frag, compiled to SPIR-V at author time
};
```

`QuadBatch` groups draws by `(MaterialKind, texture)`; per-draw data — tint, four
kind-specific params, current time, a Pulse trigger time — rides in a 128-byte
push-constant block, the cheapest way to vary state without re-binding
descriptors. The core decision: keep per-frame *kind diversity* small and the
whole thing stays within one extra pipeline bind.

I shipped it in four phases in a day, each verifiable before the next built on
it: the five kinds in `QuadBatch` plus a per-mode *slot table* the editor reads
to build its Materials panel automatically (1–2); the same pipeline-per-kind
plumbing in `MeshRenderer` so Arcaea's 3D arcs join the palette, deleting the old
monolithic `mesh.frag` rather than keep a dead shim (3); and promotion of
materials to project-level `.mat` assets plus a `Custom` kind backed by a runtime
shader compiler (4).

### Custom shaders, and a platform bug that wasn't mine

Phase 4's `Custom` kind lets an author drop a `.frag` into the project; a runtime
`ShaderCompiler` invokes `glslc` to produce SPIR-V, cached by source mtime, with
each batcher holding a pipeline cache keyed by shader path. It worked
immediately — except glslc kept failing with a Chinese-locale "directory not
found," while the identical command run by hand succeeded. The bug was in how
Windows runs the command, not in the command:

```cpp
// renderer/ShaderCompiler.cpp
// _popen runs through cmd.exe /c. When the command STARTS with a quote and
// contains more than one quoted token, cmd.exe strips the outer quotes,
// mangling the glslc path into `glslc.exe"` → ERROR_PATH_NOT_FOUND. Wrapping
// the whole line in ONE more quote pair gives cmd.exe quotes to strip and
// leaves the real quoting intact.
#ifdef _WIN32
    cmdStr = "\"" + cmdStr + "\"";
#endif
```

This is the direct descendant of March's `near`/`far` macro lesson: the platform
itself is sometimes the bug. Fixing it here also fixed the hand-written Compile
button, which had the same latent trap.

![The Materials tab editing a custom shader](img/2026-04-18_materials-tab.png)

> 📸 **SCREENSHOT (capturable today) — `docs/blog/img/2026-04-18_materials-tab.png`.**
> The Materials tab with an asset selected (kind / tint / params / target slot),
> or a note in Test Game wearing a non-default kind. Authored looks, not
> hardcoded ones.

---

## The AI suite: five assistants, one principle, and the limits of the model

Phase 4 left me with a runtime shader compiler, a custom-pipeline cache, and a
local-first config story — the substrate for the next two weeks. I built five AI
assistants on it, and every one obeys a single rule that is really a statement
about *knowing the model's limits*:

> **Don't make the small model compute. Let deterministic C++ produce the facts;
> let the LLM only prioritize and narrate.**

These run against a local Ollama with `qwen2.5:3b` — small enough to share the
laptop with the engine. Ask it to reason over a raw note list and it
hallucinates timestamps; ask it to write GLSL from a rule list and it invents
undeclared identifiers. But hand it pre-digested facts and a small, well-bounded
job and it's genuinely useful. The five assistants are five shapes of that
bargain:

**Autocharter — knowing when *not* to use the model.** The old "Place All"
dropped a tap on every beat. I extended the Madmom analysis to emit three
features per marker — strength, sustain, centroid — and used *those*, in plain
C++, to pick note type and lane. No model in the loop, because the decision is
deterministic and the model would only add hallucination:

```cpp
NoteType inferNoteType(const MarkerFeature& f, const Knobs& k) {
    if (f.sustain  >= k.holdMin)        return NoteType::Hold;   // long tail
    if (f.strength >= k.flickThreshold) return NoteType::Flick;  // sharp onset
    return NoteType::Click;
}                                       // lane from centroid + anti-jack + cooldown
```

**Editor Copilot — a deliberately tiny, safe interface.** "Delete all flicks
between 30 and 45 seconds." The model's only job is English → a six-op
vocabulary (`delete_range`, `insert`, `mirror_lanes`, `shift_lanes`,
`shift_time`, `convert_type`); C++ applies the ops, and the vocabulary is small
on purpose so every result round-trips safely through the serializer. Making it
stable on a 3B model took four robustness fixes that are pure systems work, not
AI: wrap the worker thread in try/catch (a parse failure is an error string, not
a `std::terminate`); encode JSON with `error_handler_t::replace` (a Chinese
IME's GB18030 bytes mustn't throw); replace em-dashes in my own prompt literals
with ASCII (MSVC on a CP936 locale was emitting non-UTF-8); and set
`response_format: json_object` so the small model stops dropping quotes. None
glamorous; all load-bearing.

**Shader Generator — and the symmetry I didn't expect.** This reused Phase 4
almost wholesale (the backlog estimated it 90% built; it was). English in,
compiled `.spv` out, with a retry loop that feeds glslc's stderr back into the
next attempt. But the real content is the prompt engineering, which took *five*
rewrites:

- **v0, rules only:** "use `#version 450`, oscillate with sin/cos." The model
  invented `cos(time) + 0.5` (range `[-0.5, 1.5]`, clamps to black, flickers)
  and a decaying alpha that made notes permanently invisible after one second.
- **v1–v3:** I kept adding prohibitions and a `time → ubo.time` lookup table.
  Each version fixed one failure and surfaced the next; on a retry it would drop
  `#version 450` entirely.
- **v4, a few-shot template:** I threw out the rules and embedded a *complete
  working shader*, with one instruction — "keep every `layout()` binding exactly
  as shown; modify ONLY the body of `main()`." It converged immediately.

Here is the realization that ties the month together. **A 3B model is
dramatically better at editing working code than writing it from constraints —
exactly what I'd learned about directing the agent that built the engine.** "I
want a game engine" failed in March for the same reason the rules-only shader
prompt failed in April: both ask the model to synthesize from an under-specified
wish. Give it precise structure — an architecture, a seven-method interface, a
template shader — and the output becomes reliable. Operating the build agent and
prompting the in-product model are the *same* skill, at two scales.

![A generated custom shader pulsing in Test Game](img/2026-04-19_shader-gen.png)

> 📸 **SCREENSHOT (capturable today) — `docs/blog/img/2026-04-19_shader-gen.png`.**
> The AI Generate block with a prompt, and the resulting shader on a note in
> Test Game (the verified case was a purple pulsing hold body). Ideally also
> show the prompt → generated GLSL → result, since that interaction *is* the
> evidence of the methodology.

**Chart Audit and Style Transfer — the principle, formalized.** Audit scans a
finished chart for density spikes, jacks, crossovers, and dead zones entirely in
C++; the model gets a capped, pre-digested block and only writes the summary and
orders issues by severity — citing the exact timestamps my scan produced because
it never had to find them. Style Transfer extracts a numeric *fingerprint* from a
reference chart (type ratios, lane histogram, NPS) and rebalances the current
chart toward it while preserving every note's *time*, with cross-track-count
resampling so a 12-lane reference reshapes a 7-lane chart without collapsing its
distribution; the model gets three fingerprint blocks and writes two sentences.
Math in the deterministic layer; words in the model.

The shape repeats five times: **the deterministic layer is the product; the LLM
is the interface.** Knowing which half of each problem to give the model is most
of the design — the same judgment as knowing which parts of the engine to specify
tightly before handing them to an agent.

---

## Hardening, and restraint as a decision

The last stretch was unglamorous and necessary: an editor-UI polish pass across
every page, UTF-8 plumbing so non-ASCII titles and paths stop corrupting on
Windows, and the safety net — **auto-save plus crash hooks.** Auto-save runs
every frame, debounced to 30 seconds, and is drag-safe (it skips while the mouse
is down and retries next frame) so it never fires mid-edit; crash hooks flush the
project on window-close, unhandled exception, and Ctrl+C, because losing an
author's work is the one failure mode with no excuse.

One small moment captured a design principle. I'd shipped the Shader Generator
with preset-prompt buttons and a Regenerate button, thinking I was being
helpful. The user rejected both: *"I don't think we need presets or regenerate
when I open it."* I reverted to a bare prompt and a Generate button and wrote the
rule down — new panels ship with core controls only. **Restraint is a feature.**
The same instinct produced the deliberately tiny six-op Copilot vocabulary and an
autocharter that scaffolds rather than finishes: doing less, and leaving room for
the author, was repeatedly the right call.

---

## What the back half of April established

1. **A clean foundation makes the flashy layer almost free — and controllable.**
   The Shader Generator was "90% built" because Phase 4's compiler and pipeline
   cache existed first. Every hour on the deterministic substrate paid for itself
   when the AI feature on top turned out to be a thin shell over it.

2. **Let the deterministic layer compute; let the model communicate.** Five
   assistants, one principle. The model that hallucinates timestamps is excellent
   at narrating timestamps you hand it. Choosing which half of the problem the LLM
   gets is the core design act — and it's the maturation of March's trust-but-
   verify: it's *knowing the tool's limits* and architecting around them.

3. **Specification beats wishes, at every scale.** Templates beat rules for the
   3B shader model for the same reason an architecture beats "I want a game
   engine" for the build agent. The unit of skill in AI-assisted engineering is
   how precisely you can constrain the model's job — whether the model is writing
   your renderer or a single fragment shader inside it.

4. **Restraint is a design decision, not an omission.** The reverted presets, the
   six-op vocabulary, the scaffolding autocharter — choosing to do less, on
   purpose, with room for the human, was a feature each time.

Five months in, a project I directed almost entirely through AI agents could
pick a song, hear its beat, scaffold a chart, let you sculpt it in plain English,
audit it, restyle it toward a reference, skin every note with shaders it could
write for you — and save itself if it crashed. The remaining entries are about
making that shippable: the Android player, and the road to a release.
