# Scene Viewer Front-End

## Current Implementation ✅

Your engine now has a **Unity-style editor** with the game rendered inside an ImGui viewport.

**Features**:
- 1600×900 editor window
- ImGui UI with movable/resizable panels:
  - **Scene window**: Displays game as texture viewport with Play/Stop controls
  - **Stats window**: Shows FPS, frame time, song time, and play status
- Game scene contained within Scene window (not fullscreen)
- Play/Stop button to control game execution
- Bloom effects only on game scene, UI stays crisp
- Clean dark gray editor background
- ESC to exit

## Architecture

### ImGui Integration

**Files**:
```
src/ui/
├── ImGuiLayer.h/.cpp      # ImGui initialization & texture management
└── SceneViewer.h/.cpp     # Scene window UI with game viewport
```

**Engine Integration**:
- `Engine::init()` - Initializes ImGui, creates scene texture descriptor
- `Engine::render()` - Renders game (if playing), then ImGui UI overlay
- `Engine::shutdown()` - Cleans up ImGui resources

### Render Flow

```
1. beginFrame() - Acquire swapchain image, begin scene render pass
2. Game mode renders to offscreen scene (only if playing)
3. End scene render pass
4. Bloom compute passes on scene texture
5. Begin swapchain render pass (clear to dark gray, no composite)
6. ImGui renders UI with scene texture in viewport
7. End swapchain render pass
8. Submit & present
```

## Implementation Details

### ImGuiLayer

**Initialization**:
- Creates descriptor pool for ImGui (10 texture slots)
- Initializes ImGui context with keyboard navigation
- Sets up GLFW and Vulkan backends
- Uploads font textures

**Texture Management**:
- `addTexture(view, sampler)` - Creates ImGui descriptor for Vulkan textures
- Scene texture registered and passed to SceneViewer

**Per-frame**:
- `beginFrame()` - Start new ImGui frame
- `endFrame()` - Finalize ImGui rendering
- `render(cmd)` - Record ImGui draw commands

### SceneViewer

**UI Layout**:
- Scene window at (0, 0), size 1200×800
  - Play/Stop button at top
  - Game viewport displays scene texture
- Stats window at (1220, 0), size 380×300
- Both windows movable and resizable

**Play/Stop Control**:
- `m_playing` state controls game execution
- When stopped: game update/render paused, scene frozen
- When playing: game updates and renders normally

**Stats Display**:
- FPS counter
- Frame time in milliseconds
- Song time in seconds
- Play status (Playing/Stopped)

## Build Configuration

**CMakeLists.txt additions**:
```cmake
set(IMGUI_DIR "${TP}/imgui")

# ImGui sources
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)

# Include directories
${IMGUI_DIR}
${IMGUI_DIR}/backends
```

## Usage

**Running the editor**:
```bash
cd build/Debug
./MusicGameEngine.exe
```

**Controls**:
- ESC - Exit application
- Mouse - Move/resize UI windows

## Status

✅ ImGui integrated with Vulkan
✅ Scene and Stats windows functional
✅ Game rendering with UI overlay
✅ Movable/resizable panels
✅ FPS and performance stats

## Future Enhancements

Potential additions for a full editor:
- Render scene to texture, display in ImGui::Image()
- Hierarchy panel showing scene graph
- Inspector panel for object properties
- Timeline for chart editing
- Play/pause/seek controls
- Multiple game mode switcher
- Chart file browser
- Audio waveform display
- Note placement tools

## Notes

The current implementation renders the game fullscreen with UI overlay. For a true Unity-style scene viewport, you would need to:
1. Render scene to a texture
2. Display that texture in ImGui::Image() within the Scene window
3. Handle mouse input for scene interaction

This is a solid foundation for building a full-featured editor.
