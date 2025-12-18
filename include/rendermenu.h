#ifndef RENDERMENU_H
#define RENDERMENU_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <vector>
#include <memory>
#include <map>
#include <string>
#include <array>
#include <atomic>
#include <mutex>
#include "hardcoresnake.h"

class MenuRender
{
    public:
        MenuRender();
        ~MenuRender();
        
        // Game rendering methods (merged from GameRender)
        void renderGame(const struct GameContext& ctx, bool matchEnded);
        void renderPlayers(const std::array<PlayerSlot, Config::Game::MAX_PLAYERS>& players);
        void renderFood(const Food& food);
        void renderHUD(int score, int remainingSeconds, const std::string& sessionId);
        void clearScreen();
        void present();

        void renderText(const char* text, int x, int y, SDL_Color color, TTF_Font* textFont = nullptr, bool cache = false);

        // Menu screens for different game states
        void renderMenu(int menuSelection);           // Main menu (MENU state)
        void renderSessionBrowser(const std::vector<std::string>& sessions, int selectedIndex, bool isConnected);  // Session browser
        void renderLobby(const std::array<PlayerSlot, Config::Game::MAX_PLAYERS>& players, bool isHost);  // LOBBY state
        void renderCountdown(int seconds);            // COUNTDOWN state
        void renderPauseMenu(int selection);         // Pause overlay during PLAYING
        void renderMatchEnd(int winnerIndex, const std::array<PlayerSlot, Config::Game::MAX_PLAYERS>& players);  // MATCH_END state
        
        
        SDL_Renderer* getRenderer() { return renderer; }
        SDL_Window* getWindow() { return window; }
        TTF_Font* getFont() { return font; }
        TTF_Font* getTitleFont() { return titleFont; }

    private:
        SDL_Window* window;
        SDL_Renderer* renderer;
        TTF_Font* font;
        TTF_Font* titleFont;
        int menuSelection;
        
        static std::atomic<bool> sdlInitialized;
        static std::mutex sdlInitMutex;

        // Cached textures for static text
        std::map<std::string, SDL_Texture*> textureCache;

        // Helper to create and cache texture
        SDL_Texture* createTextTexture(const char* text, SDL_Color color, TTF_Font* textFont);
        SDL_Texture* getCachedTexture(const char* text, SDL_Color color, TTF_Font* textFont);

};

#endif // UIRENDERING_H