#ifndef RENDERMENU_H
#define RENDERMENU_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <vector>
#include <memory>
#include <map>
#include <string>
#include <array>
#include "hardcoresnake.h"

class MenuRender
{
    public:
        MenuRender();
        ~MenuRender();

        void renderText(const char* text, int x, int y, SDL_Color color, TTF_Font* textFont = nullptr);
        void renderCachedText(const char* text, int x, int y, SDL_Color color, TTF_Font* textFont = nullptr);

        // Menu screens for different game states
        void renderMenu(int menuSelection);           // Main menu (MENU state)
        void renderSessionBrowser(const std::vector<std::string>& sessions, int selectedIndex, bool isConnected);  // Session browser
        void renderLobby(const std::array<PlayerSlot, 4>& players, bool isHost);  // LOBBY state
        void renderCountdown(int seconds);            // COUNTDOWN state
        void renderPauseMenu(int selection);         // Pause overlay during PLAYING
        void renderMatchEnd(int winnerIndex, const std::array<PlayerSlot, 4>& players);  // MATCH_END state
        
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
        
        static bool sdlInitialized;

        // Cached textures for static text
        std::map<std::string, SDL_Texture*> textureCache;

        // Helper to create and cache texture
        SDL_Texture* createTextTexture(const char* text, SDL_Color color, TTF_Font* textFont);
        SDL_Texture* getCachedTexture(const char* text, SDL_Color color, TTF_Font* textFont);


};

#endif // UIRENDERING_H