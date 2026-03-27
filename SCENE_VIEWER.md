# Scene Viewer Front-End

## Current Implementation ✅

Your engine now has a **Unity-style editor** with ImGui UI overlay.

**Features**:
- 1600×900 editor window
- ImGui UI with movable/resizable panels:
  - **Scene window**: Displays rendering info and game mode
  - **Stats window**: Shows FPS, frame time, and song time
- Full Vulkan rendering underneath
- ESC to exit

## Architecture

### ImGui Integration

**Files**:
```
src/ui/
├── ImGuiLayer.h/.cpp      # ImGui initialization & rendering
└── SceneViewer.h/.cpp     # Scene window UI layout
```

**Engine Integration**:
- `Engine::init()` - Initializes ImGui with swapchain render pass
- `Engine::render()` - Renders game, then ImGui UI overlay
- `Engine::shutdown()` - Cleans up ImGui resources

### Render Flow

```
1. beginFrame() - Acquire swapchain image
2. Begin scene render pass (offscreen RGBA16F)
3. Game mode renders (quads, lines, meshes, particles)
4. End scene render pass
5. Bloom compute passes
6. Begin swapchain render pass
7. Composite scene to swapchain
8. ImGui renders UI overlay (same render pass)
9. End swapchain render pass
10. Submit & present
```

## Implementation Details

### ImGuiLayer

**Initialization**:
- Creates descriptor pool for ImGui
- Initializes ImGui context with keyboard navigation
- Sets up GLFW and Vulkan backends
- Uploads font textures

**Per-frame**:
- `beginFrame()` - Start new ImGui frame
- `endFrame()` - Finalize ImGui rendering
- `render(cmd)` - Record ImGui draw commands

### SceneViewer

**UI Layout**:
- Scene window at (0, 0), size 1200×800
- Stats window at (1220, 0), size 380×300
- Both windows movable and resizable

**Stats Display**:
- FPS counter
- Frame time in milliseconds
- Song time in seconds

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
