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
    public:

        Game();
        ~Game();
        void run();

    private:

        void handleInput();
        void update();
        void render();
        void changeState(GameState newState);
        void exitState(GameState oldState);
        void enterState(GameState newState);   
        
        void handleMenuInput(SDL_Keycode key);
        void handleMultiplayerInput(SDL_Keycode key);
        void handleLobbyInput(SDL_Keycode key);
        void handlePlayingInput(SDL_Keycode key);
        void handlePausedInput(SDL_Keycode key);
        void handleMatchEndInput(SDL_Keycode key);
        
        void checkMatchTimer(Uint32 currentTime);

        void updatePlayers();
        void respawnPlayer(int playerIndex);
        void resetMatch();
        bool canUnpause() const;
        // Helpers
        bool isPlayerValid(int index) const { return ctx.players[index].active && ctx.players[index].snake; }
        void buildCollisionMap();
        void resetGameState();

 

private:


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
    int updateInterval;
    int menuSelection;
    int pauseMenuSelection;

    void (Game::*inputHandler)(SDL_Keycode);

};
