#pragma once

#include "hardcoresnake.h"
#include "rendermenu.h"
#include "rendergame.h"
#include "multiplayer.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <vector>

enum class GameState {
    MENU,           // Initial menu - host/join choice
    LOBBY,          // Waiting for players
    COUNTDOWN,      // 3-2-1 countdown
    PLAYING,        // Active match
    MATCH_END       // Results screen
};

class Game {
private:
    // SDL components
    SDL_Window* window;
    SDL_Renderer* renderer;
    TTF_Font* font;
    TTF_Font* titleFont;
    
    // Rendering
    MenuRenderer* ui;
    GameRenderer* gameRenderer;
    
    // Game state
    GameContext ctx;
    Food food;
    std::vector<Position> occupiedPositions;
    
    // Game loop state
    bool quit;
    bool paused;
    bool gameOver;
    bool matchEnded;
    int winnerIndex;
    Uint32 lastUpdate;
    Uint32 matchStartTime;
    int updateInterval;

public:
    Game();
    ~Game();
    
    void run();

private:
    bool initSDL();
    void initMultiplayer();
    void initPlayers();
    
    void handleInput();
    void update();
    void render();
    
    void checkMatchTimer(Uint32 currentTime);
    void updatePlayers();
    void resetMatch();
    void respawnPlayer(int playerIndex);
    
    // Constants
    static constexpr int MULTIPLAYER_UPDATE_THROTTLE_MS = 100;
    static constexpr int MIN_UPDATE_INTERVAL = 50;
    static constexpr int SPEED_INCREASE = 2;
};
