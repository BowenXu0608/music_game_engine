---
name: User Design Choices
description: UI/UX decisions made by the user for the music selection and editor systems
type: feedback
date: 2026-04-03
---

## Music Selection Page Layout
- **Wheel style**: Arcaea-style card stacks with perspective tilt — NOT flat lists or circular carousels.
  **Why:** User referenced Arcaea song select screenshot as design target.
  **How to apply:** Use AddQuadFilled/AddImageQuad with skew transforms, painter's order sorting.

- **Hierarchy panel**: Far-right panel (70/30 split) — NOT bottom panel or toggle overlay.
  **Why:** User prefers always-visible hierarchy alongside preview.

- **Song card content**: Name + score + achievement badge. Difficulty is under the cover photo, not on cards.
  **Why:** User wants clean card layout with key info; difficulty selection is a separate interaction.

## Image/File Selection
- **Cover picker**: Both file dialog AND drag-drop support.
  **Why:** User wants maximum flexibility — quick drag from asset panel or precise file picker.
  **How to apply:** Always provide both Browse button + BeginDragDropTarget for file path fields.

## Song Editor Layout
- **Top = preview (Scene/Editor tabs), Bottom = assets (left) + config/audio (right).**
  **Why:** User wants preview to be prominent at the top, with controls underneath.
- Chart paths removed from properties — charts are edited in the Editor tab.
- Score/achievement deferred (see score_reminder.md).

## Per-Song Game Mode Config
- Game mode settings (style, dimension, tracks) are **per-song**, NOT per-project.
  **Why:** User wants different songs within the same music set to use different game styles.
  **How to apply:** `GameModeConfig` lives in `SongInfo`, saved in `music_selection.json` per song.

## Drop Notes 2D vs 3D
- **2D**: Perspective highway with converging lanes + single ground judge line. This IS the perspective view (NOT flat top-down).
- **3D**: Same perspective highway but with TWO judge lines at different heights — ground track + sky input (like Arcaea).
  **Why:** User clarified with Arcaea screenshot. The "Sky Input" line is the key 3D differentiator.
  **How to apply:** 2D = single hit zone; 3D = ground hit zone + elevated sky hit zone.

## Game Flow Preview / Test Game
- Replaced per-page "Game Preview" tab with a global green "Test Game" button at top-right of all pages.
  **Why:** User wants "Test Game" to mean testing the whole game, not just a per-page preview.
  **How to apply:** Button on all 3 editor pages (StartScreen, MusicSelection, SongEditor). Old tab toggle removed.

- **Transition effects MUST match properties**: The preview must read and apply the exact TransitionEffect configured in Start Screen Editor properties.
  **Why:** User reported "the real special effect cannot match the option!" when effects were hardcoded.

## Asset System
- ALL editor pages must use the asset panel system (thumbnails, drag-drop, import).
  **Why:** User explicitly stated "all the editor should use the asset system" — consistency across editors.
- **Unified import function** `importAssetsToProject()` in `AssetBrowser.h` — all editors share one function.
  **Why:** User reported assets imported on one page not visible on others. Single function ensures consistent routing.
- **All "Open File..." dialogs default to "All Files"** filter — not Images or Audio.
  **Why:** User couldn't find .mp3 files because dialog defaulted to Images filter.
