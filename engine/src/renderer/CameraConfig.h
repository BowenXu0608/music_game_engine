#pragma once
#include "renderer/Camera.h"

struct GameModeConfig;

// Builds the gameplay Camera from a song's free-camera config (position +
// Euler rotation + projection), shared by Drop2DRenderer (2D), Drop3DRenderer
// (3D) and the SongEditor scene preview so they can never diverge.
Camera buildGameplayCamera(const GameModeConfig& gm, float aspect);
