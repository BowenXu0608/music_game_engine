# First Light: Standing Up a Vulkan Engine I Would Mostly Not Type by Hand

*Build journal #1 — the architecture decisions of mid-March and the first commits on 2026-03-19 and 2026-03-21, for a C++20 / Vulkan music-game engine.*

---

## The methodology, stated up front

This is the first entry in a build journal for a C++20 / Vulkan rhythm-game
engine that targets four very different games — BanG Dream, Arcaea, Cytus, and
Lanota — as plugin game modes. The unusual part of how it was
built, stated plainly: I did not write most of this code by hand. I directed
AI coding agents to write it. The work this journal actually documents is the
part that doesn't compile — choosing the architecture, decomposing each
subsystem into tasks an agent could execute correctly, reviewing what came
back, and locating the bugs the agent couldn't see. The engine is the
artifact; the skill I'm documenting is *operating an AI agent at the scale of
a real systems project.*

That framing wasn't obvious on day one. My first attempt was the naive one — I
opened an agent and typed, in effect, "I want a game engine." The result was
useless and deserved to be: no target platform, no rendering API, no genre, no
constraints. The lesson wasn't "the tool is weak." It was that an agent's output
is a function of the precision of its input, and supplying that precision — the
architecture, the interfaces, the invariants — is the engineer's job, not the
model's. Everything that follows is about getting better at that.

A second false start shaped the method: I tried to read *Game Engine
Architecture* cover-to-cover first. That fails for the same reason the vague
prompt fails — a reference text assumes hands-on intuition you only get by
building. So I inverted it: skim for *shape*, then build the rendering system
first, because rendering is the subsystem where correctness is immediately
*visible*. You cannot bluff a frame. Starting where the feedback loop is tightest
is what turned a week of preparation into a working triangle.

---

## Deciding the architecture before the agent writes a line

The highest-leverage work happened before any code existed, and it was mine to
do. An AI agent will happily generate a renderer that assumes one kind of game.
The cost of that assumption is a rewrite once the second game mode arrives. So
the first artifact wasn't code — it was a decomposition: **eight subsystems**
(rendering, resource management, core engine, input, gameplay, game-mode
plugins, editor UI, Android packaging) and, inside rendering, a **two-layer
design** that I could hand to an agent as a set of bounded, well-specified
tasks.

```
            ┌─────────────────────────────────────────────┐
 game modes │  BandoriRenderer  CytusRenderer  ...  (×4)   │  implement 7 methods
            └───────────────┬─────────────────────────────┘
                            │  quads()/lines()/meshes()/particles()
            ┌───────────────▼─────────────────────────────┐
 batchers   │  QuadBatch  LineBatch  MeshRenderer          │  "draw this, I'll
            │  ParticleSystem  PostProcess                 │   handle the GPU"
            └───────────────┬─────────────────────────────┘
                            │  whiteView()/whiteSampler()/descriptors()
            ┌───────────────▼─────────────────────────────┐
 backend    │  VulkanContext Swapchain RenderPass Pipeline │  raw Vulkan, private
 (private)  │  BufferManager DescriptorManager CommandMgr  │
            │  SyncObjects TextureManager                  │
            └─────────────────────────────────────────────┘
```

> 📐 *Replace this ASCII sketch with a polished figure
> (`img/2026-03_architecture.png`) — three layers, narrowing interfaces, game
> modes that never touch the GPU.*

The load-bearing decision is the *narrowing of the interface*. The bottom layer
is the full raw Vulkan API; the top layer — what a game mode actually sees — is
three handles and four batchers:

```cpp
class Renderer {
public:
    QuadBatch&      quads()     { return m_quads; }     // game modes write to these
    LineBatch&      lines()     { return m_lines; }
    MeshRenderer&   meshes()    { return m_meshes; }
    ParticleSystem& particles() { return m_particles; }

    DescriptorManager& descriptors()       { return m_descMgr; }
    VkImageView        whiteView()    const { return m_whiteTexture.view; }
    VkSampler          whiteSampler() const { return m_whiteTexture.sampler; }

private:
    VulkanContext     m_ctx;        // the entire raw backend — private,
    Swapchain         m_swapchain;  // unreachable from any game mode
    RenderPass        m_renderPass;
    DescriptorManager m_descMgr;
    CommandManager    m_cmdMgr;
    SyncObjects       m_sync;
    QuadBatch m_quads; LineBatch m_lines; MeshRenderer m_meshes; ParticleSystem m_particles;
};
```

The invariant — **game modes never allocate Vulkan resources directly** — is
also what makes the codebase tractable for an agent. Each mode is a self-
contained task with a tiny surface area; the agent can't reach across a boundary
and create a subtle coupling, because the boundary exposes nothing to couple to.
The plugin contract is seven methods:

```cpp
class GameModeRenderer {
    virtual void onInit(Renderer&, const ChartData&, const GameModeConfig*) = 0;
    virtual void onResize(uint32_t w, uint32_t h) = 0;
    virtual void onUpdate(float dt, double songTime) = 0;
    virtual void onRender(Renderer&) = 0;
    virtual void onShutdown(Renderer&) = 0;
    virtual const Camera& getCamera() const = 0;
    virtual void showJudgment(int lane, Judgment, float timingDelta = 0.f) {}
};
```

…and adding a game is one `case` in a factory. Months later, that is still
literally all it is:

```cpp
std::unique_ptr<GameModeRenderer> Engine::createRenderer(const GameModeConfig& c) {
    switch (c.type) {
        case GameModeType::DropNotes:
            return c.dimension == DropDimension::ThreeD
                 ? std::make_unique<ArcaeaRenderer>()   // 3D arc-tracing
                 : std::make_unique<BandoriRenderer>();  // flat tap highway
        case GameModeType::Circle:   return std::make_unique<LanotaRenderer>();
        case GameModeType::ScanLine: return std::make_unique<CytusRenderer>();
    }
    return std::make_unique<BandoriRenderer>();
}
```

I also fixed the capacity envelope on day one and never revisited it — triple-
buffered frames, `MAX_QUADS = 8192`, `MAX_LINES = 4096` — because re-architecting
a batcher mid-feature is exactly the avoidable churn good up-front sizing
prevents. Chart density only ever grows, so I over-allocated once on purpose.

---

## Time is audio, not frames

One domain decision on day one prevented an entire class of bug I wouldn't have
known how to diagnose for weeks. In a rhythm game, the authoritative clock is
the audio DSP position, not the frame loop — if you let dropped frames define
song time, the chart desyncs from the music. So `GameClock` separates the two
from the start: wall time drives animation, song time is overridden from the
audio engine.

```cpp
class GameClock {                       // engine/GameClock.h
    float tick() {                      // per frame → smooth wall-clock dt
        auto cur = now();
        float dt = std::chrono::duration<float>(cur - m_lastTime).count();
        m_lastTime = cur;
        if (m_running) m_wallTime += dt;
        return m_running ? dt : 0.f;
    }
    void   setSongTime(double t) { m_songTime = t; }   // authoritative, from audio
    double songTime() const      { return m_songTime; }
    float  wallTime() const      { return m_wallTime; }
};
```

This is a decision the human has to make: an agent asked to "write a game clock"
produces a perfectly good frame timer — correct, and wrong for this domain.

---

## First pixels, and where generated code and correctness diverge

March 21 is the day the engine first hit the screen — which is to say the day
everything looked wrong and I had to reason about why. Three bugs are worth
keeping, because each is a different category of failure and the last is the kind
an AI agent reliably *introduces and does not flag.*

**Projection — a domain choice the agent couldn't make.** The first renderers
came out orthographic: the simplest thing that runs, and lifeless — ortho
flattens depth, and a rhythm game lives on notes rushing toward you. I rebuilt
`Camera` around perspective, which carries one Vulkan subtlety the agent got
wrong: clip space has Y pointing *down* (opposite of OpenGL, which GLM assumes),
so the projection's Y axis must be flipped or the world renders inverted:

```cpp
static Camera makePerspective(float fovYDeg, float aspect, float nearZ, float farZ) {
    Camera c;
    c.m_proj = glm::perspective(glm::radians(fovYDeg), aspect, nearZ, farZ);
    c.m_proj[1][1] *= -1.f;          // flip Y for Vulkan NDC (GLM is GL-handed)
    return c;
}
```

**The platform as the bug.** My projection variables were named `near` and
`far` — the obvious names. They silently broke, because `<windows.h>` defines
`near`/`far` as macros, so the preprocessor mangled the identifiers before the
compiler saw them (the same family of trap behind the `NOMINMAX` guard, where
Windows' `min`/`max` macros collide with standard code). The fix is a rename, but
the lesson is one an agent won't volunteer: *the platform itself is sometimes the
bug, and generated code that looks correct can be sabotaged by the environment it
compiles in.*

**A missing GPU synchronization edge — the deepest beat.** The post-process
pass was sampling garbage from the scene framebuffer: stale, partly-written
data, flickering frame to frame. No crash, no validation error — just wrong
pixels, which is the hardest class of GPU bug to chase. The render pass declared
only the conventional `EXTERNAL → 0` dependency, which every tutorial includes:

```cpp
// RenderPass.cpp — the swapchain pass: gates the START of the pass. Correct, here.
dep.srcSubpass = VK_SUBPASS_EXTERNAL; dep.dstSubpass = 0;
dep.srcStageMask = dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
dep.srcAccessMask = 0; dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
```

That dependency says "finish prior work before this pass *starts*." It says
nothing about the scene pass finishing before post-process *reads* its output.
Vulkan reorders and overlaps freely unless told otherwise, so post-process was
reading a framebuffer mid-write. The scene render pass — the one feeding the
bloom compute shader and the composite — needs the dependency in *both*
directions:

```cpp
// PostProcess.cpp — the scene pass: gate the start AND publish the writes.
VkSubpassDependency dep0{};   // EXTERNAL→0: wait for last frame's composite
dep0.srcSubpass = VK_SUBPASS_EXTERNAL; dep0.dstSubpass = 0;
dep0.srcStageMask = dep0.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
dep0.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

VkSubpassDependency dep1{};   // 0→EXTERNAL: make scene writes VISIBLE to readers
dep1.srcSubpass = 0; dep1.dstSubpass = VK_SUBPASS_EXTERNAL;
dep1.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
dep1.dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
dep1.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
dep1.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
```

This is where directing an agent stops being about typing speed and starts being
about systems knowledge. The generated pass had the textbook dependency; the
missing one only matters once an offscreen pass feeds another stage, and the
symptom — wrong pixels, no error — gives the agent nothing to react to.
Diagnosing it meant reasoning about the Vulkan execution and memory model. I
wrote "PostProcess needs BOTH dependencies" into the doc in bold, because an
undocumented sync invariant is one an agent will silently delete in a refactor.

I also pinned one constant by eye as a hard rule: the Bandori camera eye must sit
at **z ≥ 8**, or notes pop into existence already too large. It isn't derivable —
it's the product of moving the camera until the illusion held, then writing the
number down so a refactor doesn't quietly violate it:

```cpp
const glm::vec3 camEye{0.f, 5.f, 8.f};       // BandoriRenderer.cpp — the z=8 floor
const glm::vec3 camTarget{0.f, 0.f, -24.f};
```

![Ortho vs perspective — the same lanes, flat vs converging](img/2026-03-21_ortho-vs-perspective.png)

> 📸 **SCREENSHOT (capturable today) — `docs/blog/img/2026-03-21_ortho-vs-perspective.png`.**
> Stacked comparison of the Bandori playfield: orthographic (parallel rails, no
> depth) vs. perspective (lanes converging to a vanishing point). Reproduce the
> ortho frame by temporarily swapping `Camera::makePerspective` for
> `makeOrtho` in `BandoriRenderer::onResize`. This single image carries the
> whole "lifeless vs. alive" point.

![The finished perspective highway with notes converging](img/2026-03-21_bandori-highway.png)

> 📸 **SCREENSHOT (capturable today) — `docs/blog/img/2026-03-21_bandori-highway.png`.**
> The payoff: the 2D drop playfield in perspective, notes mid-flight, bloom on.

---

## What the start established

Three principles came out of the first two commits and held:

1. **Architecture is the human's job; the code is increasingly the agent's.** The
   two-layer renderer, the seven-method plugin contract, the audio-driven clock
   are the decisions that determine whether the next thousand lines an agent
   writes are tractable or a tangle. Get the decomposition right and the output
   stays bounded.
2. **Structure is cheap early and brutal late; correctness can only be debugged
   in.** No amount of structure told me the camera needed z ≥ 8, that two
   variable names were secretly macros, or that my render pass was missing half
   its synchronization — those surfaced only when real photons hit the screen.
3. **The bugs worth documenting are the ones the agent can't see** — a missing
   subpass dependency (no error), a macro collision (mangles correct-looking
   code), a domain mismatch (compiles perfectly). They demand systems knowledge
   the model doesn't apply on its own, and that's where the engineering in an
   AI-assisted build lives.

The next entry turns the engine into something you can author with — and covers
the first bugs that taught me to stop trusting that generated code is correct
just because it runs.
