#pragma once

#include "hardcoresnake.h"
#include "rendermenu.h"
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
        void changeState(GameState newState, bool fromNetwork);
        void exitState(GameState oldState, bool fromNetwork);
        void enterState(GameState newState, bool fromNetwork);   
        
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
        // Helpers
        void navigateMenu(int& selection, int maxItems, bool up);
        Position getRandomSpawnPosition();
        void buildCollisionMap();
        void resetGameState();

private:

    GameContext ctx;
    std::unique_ptr<MenuRender> ui;
    std::unique_ptr<NetworkManager> networkManager;
    Food food;
    std::unordered_map<int, bool> occupiedPositions;
    GameState state;

    bool quit;
    Uint32 lastUpdate;
    int updateInterval;
    int menuSelection;
    int pauseMenuSelection;
    int sessionSelection;

    void (Game::*inputHandler)(SDL_Keycode);

};
