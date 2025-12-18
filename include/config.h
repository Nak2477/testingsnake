#ifndef CONFIG_H
#define CONFIG_H

#include <SDL2/SDL.h>

// ============================================================
// GAME CONFIGURATION
// All magic numbers and constants in one place
// ============================================================

namespace Config {

// ============================================================
// WINDOW & GRID
// ============================================================
namespace Window {
    constexpr int WIDTH = 800;
    constexpr int HEIGHT = 600;
}

namespace Grid {
    constexpr int CELL_SIZE = 20;
    constexpr int WIDTH = Window::WIDTH / CELL_SIZE;   // 40 cells
    constexpr int HEIGHT = Window::HEIGHT / CELL_SIZE; // 30 cells
}

// ============================================================
// GAMEPLAY
// ============================================================
namespace Game {
    constexpr int INITIAL_SPEED_MS = 100;           // Snake update interval
    constexpr int MATCH_DURATION_SECONDS = 120;     // 2 minutes per match
    constexpr int MAX_FOOD_SPAWN_ATTEMPTS = 1000;   // Max attempts to find empty cell
    constexpr int FOOD_SCORE_VALUE = 10;            // Points per food eaten
    constexpr int DEATH_SCORE_PENALTY = 10;         // Points lost on death
    constexpr int MAX_PLAYERS = 4;                  // Maximum players in multiplayer
    constexpr int INITIAL_SNAKE_LENGTH = 3;         // Starting snake segments
}

// ============================================================
// NETWORK / MULTIPLAYER
// ============================================================
namespace Network {
    // Timing
    constexpr Uint32 STATE_SYNC_INTERVAL_MS = 5000;         // Host broadcasts full state
    constexpr Uint32 DIRECTION_CHANGE_THROTTLE_MS = 16;     // Max 60 updates/sec (~16ms)
    constexpr Uint32 CONNECTION_TIMEOUT_WARNING_MS = 15000; // Show warning after 15s
    constexpr Uint32 CONNECTION_TIMEOUT_DISCONNECT_MS = 30000; // Disconnect after 30s
    
    // Default server
    constexpr const char* DEFAULT_HOST = "kontoret.onvo.se";
    constexpr int DEFAULT_PORT = 9001;
    
    // Protocol
    constexpr const char* PROTOCOL_VERSION = "1.0.0";
    constexpr const char* CLIENT_IDENTIFIER = "HardcoreSnakeClient";
}

// ============================================================
// RENDERING
// ============================================================
namespace Render {
    constexpr int TARGET_FPS = 60;
    constexpr int FRAME_DELAY_MS = 1000 / TARGET_FPS;
    
    // Grid colors
    constexpr SDL_Color GRID_LINE_COLOR = {50, 50, 50, 255};
    constexpr SDL_Color BACKGROUND_COLOR = {0, 0, 0, 255};
    
    // Player colors (4 players)
    constexpr SDL_Color PLAYER_COLORS[4] = {
        {0, 255, 0, 255},    // Player 1: Green
        {0, 0, 255, 255},    // Player 2: Blue
        {255, 255, 0, 255},  // Player 3: Yellow
        {255, 0, 255, 255}   // Player 4: Magenta
    };
    
    // Food color
    constexpr SDL_Color FOOD_COLOR = {255, 0, 0, 255}; // Red
    
    // UI colors
    constexpr SDL_Color TEXT_COLOR = {255, 255, 255, 255}; // White
    constexpr SDL_Color SELECTED_COLOR = {255, 255, 0, 255}; // Yellow
}

// ============================================================
// PLAYER SPAWN POSITIONS
// ============================================================
namespace Spawn {
    // Spawn positions for 4 players (indexed 0-3)
    // Top-left, Top-right, Bottom-left, Bottom-right
    constexpr int PLAYER_SPAWN_X[4] = {
        Grid::WIDTH / 4,        // Player 1: Top-left
        3 * Grid::WIDTH / 4,    // Player 2: Top-right
        Grid::WIDTH / 4,        // Player 3: Bottom-left
        3 * Grid::WIDTH / 4     // Player 4: Bottom-right
    };
    
    constexpr int PLAYER_SPAWN_Y[4] = {
        Grid::HEIGHT / 4,       // Player 1: Top-left
        Grid::HEIGHT / 4,       // Player 2: Top-right
        3 * Grid::HEIGHT / 4,   // Player 3: Bottom-left
        3 * Grid::HEIGHT / 4    // Player 4: Bottom-right
    };
}

// ============================================================
// PERFORMANCE TUNING
// ============================================================
namespace Performance {
    constexpr int COLLISION_MAP_RESERVE_SIZE = 400; // Reserve for 4 players × ~100 segments
    constexpr int MAX_SNAKE_LENGTH = 400;           // Sanity check for network validation
    constexpr int MAX_SCORE = 10000;                // Sanity check for network validation
}

} // namespace Config

#endif // CONFIG_H
