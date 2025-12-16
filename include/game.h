#pragma once

#include "hardcoresnake.h"
#include "rendermenu.h"
#include "rendergame.h"
#include "multiplayer.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <unordered_map>

enum class GameState {
    MENU,
    SINGLEPLAYER,
    MULTIPLAYER,
    LOBBY,
    COUNTDOWN,
    PAUSED,
    PLAYING,
    MATCH_END
};

class Game {
private:
    // Rendering
    MenuRender* ui;
    GameRender* gameRenderer;
    
    // Game state
    GameContext ctx;
    Food food;
    std::unordered_map<int, bool> occupiedPositions;
    GameState state;
    
    // Game loop state
    bool quit;
    Uint32 lastUpdate;
    Uint32 matchStartTime;
    int updateInterval;
    int menuSelection;

public:
    Game();
    ~Game();
    
    void run();

private:
    void initPlayers();
    void handleInput();
    void update();
    void render();

    void handleMenuInput(SDL_Keycode key);
    void handleMultiplayerInput(SDL_Keycode key);
    void handleLobbyInput(SDL_Keycode key);
    void handlePlayingInput(SDL_Keycode key);
    void handlePausedInput(SDL_Keycode key);
    void handleMatchEndInput(SDL_Keycode key);
    
    void checkMatchTimer(Uint32 currentTime);
    void updatePlayers();
    void resetMatch();
    void respawnPlayer(int playerIndex);
    
};
