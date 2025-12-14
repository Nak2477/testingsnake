#ifndef UIRENDERING_H
#define UIRENDERING_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <vector>
#include <memory>
#include <map>
#include <string>
#include "hardcoresnake.h"

class MenuRenderer {
private:
    SDL_Renderer* renderer;
    TTF_Font* font;
    TTF_Font* titleFont;
    int menuSelection;
    
    // Cached textures for static text
    std::map<std::string, SDL_Texture*> textureCache;
    
    // Helper to create and cache texture
    SDL_Texture* createTextTexture(const char* text, SDL_Color color, TTF_Font* textFont);
    SDL_Texture* getCachedTexture(const char* text, SDL_Color color, TTF_Font* textFont);
    
public:

    MenuRenderer(SDL_Renderer* r, TTF_Font* f, TTF_Font* tf);
    ~MenuRenderer();

    void renderMenu(int menuSelection);
    void renderPauseMenu();
    void renderGameOver(const std::vector<std::unique_ptr<Snake>>& snakes);

    void renderText(const char* text, int x, int y, SDL_Color color, TTF_Font* textFont = nullptr);
    void renderCachedText(const char* text, int x, int y, SDL_Color color, TTF_Font* textFont = nullptr);
};

#endif // UIRENDERING_H