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
- [User Design Choices](design_choices.md) — UI/UX preferences (wheel style, image picker)

## Recent Milestones
- Cross-lane holds + Bandori-style sample-tick gating (2026-04-10)
- Scan Line mode end-to-end rebuild (2026-04-11)
- Circle mode disk animation (keyframed rotate/scale/move) (2026-04-12)
- Cross-mode integration audit + 10 bug fixes (2026-04-12)
- Scan Line: variable-speed + straight-line slides + multi-sweep holds (2026-04-12)
