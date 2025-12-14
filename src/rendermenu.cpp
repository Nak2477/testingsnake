#include "rendermenu.h"
#include <sstream>
#include <iomanip>

MenuRenderer::MenuRenderer(SDL_Renderer* r, TTF_Font* f, TTF_Font* tf)
    : renderer(r), font(f), titleFont(tf) {}

MenuRenderer::~MenuRenderer()
{
    // Clean up all cached textures
    for (auto& pair : textureCache) {
        if (pair.second) {
            SDL_DestroyTexture(pair.second);
        }
    }
    textureCache.clear();
}

SDL_Texture* MenuRenderer::createTextTexture(const char* text, SDL_Color color, TTF_Font* textFont)
{
    if (!textFont) textFont = font;
    if (!textFont) return nullptr;
    
    SDL_Surface* surface = TTF_RenderText_Solid(textFont, text, color);
    if (!surface) return nullptr;
    
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    
    return texture;
}

SDL_Texture* MenuRenderer::getCachedTexture(const char* text, SDL_Color color, TTF_Font* textFont)
{
    // Create unique key: text + color + font
    std::stringstream ss;
    ss << text << "_" << (int)color.r << "_" << (int)color.g << "_" << (int)color.b 
       << "_" << (textFont == titleFont ? "title" : "normal");
    std::string key = ss.str();
    
    // Check if already cached
    auto it = textureCache.find(key);
    if (it != textureCache.end()) {
        return it->second;
    }
    
    // Create and cache new texture
    SDL_Texture* texture = createTextTexture(text, color, textFont);
    if (texture) {
        textureCache[key] = texture;
    }
    return texture;
}

void MenuRenderer::renderText(const char* text, int x, int y, SDL_Color color, TTF_Font* textFont)
{
    if (!textFont) textFont = font;
    if (!textFont) return;
    
    SDL_Surface* surface = TTF_RenderText_Solid(textFont, text, color);
    if (!surface) return;
    
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
        SDL_FreeSurface(surface);
        return;
    }
    
    SDL_Rect destRect = {x, y, surface->w, surface->h};
    SDL_RenderCopy(renderer, texture, nullptr, &destRect);
    
    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}

void MenuRenderer::renderCachedText(const char* text, int x, int y, SDL_Color color, TTF_Font* textFont)
{
    if (!textFont) textFont = font;
    if (!textFont) return;
    
    SDL_Texture* texture = getCachedTexture(text, color, textFont);
    if (!texture) return;
    
    int w, h;
    SDL_QueryTexture(texture, nullptr, nullptr, &w, &h);
    SDL_Rect destRect = {x, y, w, h};
    SDL_RenderCopy(renderer, texture, nullptr, &destRect);
}

void MenuRenderer::renderMenu(int menuSelection)
{
    // Draw title
    SDL_Rect titleRect = {WINDOW_WIDTH / 2 - 150, 100, 300, 60};
    //SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
    SDL_RenderFillRect(renderer, &titleRect);

    renderCachedText("HARDCORE SNAKE", WINDOW_WIDTH / 2 - 240, 100, {0, 255, 0, 255}, titleFont);
    // Menu options
    const char* options[] = {"Single Player", "Multiplayer", "Quit"};
    int startY = 250;
    int spacing = 80;
    
    for (int i = 0; i < 3; i++) {
        SDL_Color textColor = (i == menuSelection) ? SDL_Color{255, 255, 255, 255} : SDL_Color{150, 150, 150, 255};
        
        // Draw option box
        SDL_Rect optionRect = {WINDOW_WIDTH / 2 - 120, startY + i * spacing, 240, 50};
        //SDL_SetRenderDrawColor(renderer, i == menuSelection ? 0 : 40, i == menuSelection ? 200 : 40, 0, 255);
        SDL_RenderFillRect(renderer, &optionRect);
        
        // Draw text (cached)
        renderCachedText(options[i], WINDOW_WIDTH / 2 - 80, startY + i * spacing + 12, textColor);
    }
        renderCachedText("Use Arrow Keys/WASD  -  Enter to Select", WINDOW_WIDTH / 2 - 180, WINDOW_HEIGHT - 60, {150, 150, 150, 255});
}

void MenuRenderer::renderPauseMenu()
{
    // Semi-transparent overlay
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
    SDL_Rect overlay = {0, 0, WINDOW_WIDTH, WINDOW_HEIGHT};
    SDL_RenderFillRect(renderer, &overlay);
    
    // Pause box
    SDL_Rect pauseBox = {WINDOW_WIDTH / 2 - 150, WINDOW_HEIGHT / 2 - 100, 300, 200};
    SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);
    SDL_RenderFillRect(renderer, &pauseBox);
    //
    //SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
    //SDL_RenderDrawRect(renderer, &pauseBox);
    
    // Title
    SDL_Rect titleRect = {WINDOW_WIDTH / 2 - 80, WINDOW_HEIGHT / 2 - 70, 160, 40};
    //SDL_SetRenderDrawColor(renderer, 0, 200, 0, 255);
    SDL_RenderFillRect(renderer, &titleRect);
    
    // Options
    SDL_Rect resumeRect = {WINDOW_WIDTH / 2 - 100, WINDOW_HEIGHT / 2, 200, 35};
    SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);
    SDL_RenderFillRect(renderer, &resumeRect);
    
    SDL_Rect menuRect = {WINDOW_WIDTH / 2 - 100, WINDOW_HEIGHT / 2 + 50, 200, 35};
    SDL_RenderFillRect(renderer, &menuRect);
    
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    renderCachedText("PAUSED", WINDOW_WIDTH / 2 - 105, WINDOW_HEIGHT / 2 - 100, {0, 255, 0, 255}, titleFont);
    renderCachedText("ESC/P - Resume", WINDOW_WIDTH / 2 - 95, WINDOW_HEIGHT / 2, {255, 255, 255, 255});
    renderCachedText("M - Main Menu", WINDOW_WIDTH / 2 - 95, WINDOW_HEIGHT / 2 + 55, {255, 255, 255, 255});
}

void MenuRenderer::renderGameOver(const std::vector<std::unique_ptr<Snake>>& snakes)
{
    // Semi-transparent overlay
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
    SDL_Rect overlay = {0, 0, WINDOW_WIDTH, WINDOW_HEIGHT};
    SDL_RenderFillRect(renderer, &overlay);
    
    // Game over box
    SDL_Rect gameOverBox = {WINDOW_WIDTH / 2 - 180, WINDOW_HEIGHT / 2 - 120, 360, 240};
    //SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);
    SDL_RenderFillRect(renderer, &gameOverBox);
    SDL_RenderDrawRect(renderer, &gameOverBox);
    
    // Title
    SDL_Rect titleRect = {WINDOW_WIDTH / 2 - 100, WINDOW_HEIGHT / 2 - 90, 200, 50};
    //SDL_SetRenderDrawColor(renderer, 200, 0, 0, 255);
    SDL_RenderFillRect(renderer, &titleRect);
    
    //SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255); // behind GAME OVER
    renderCachedText("GAME OVER", WINDOW_WIDTH / 2 -160, WINDOW_HEIGHT / 2 -100, {255, 0, 0, 255}, titleFont);


    // Score display areas
    int yOffset = WINDOW_HEIGHT / 2 - 20;
    for (size_t i = 0; i < snakes.size(); i++) {
        char scoreText[50];
        SDL_Rect scoreRect = {WINDOW_WIDTH / 2 - 150, yOffset + (int)i * 40, 300, 30};
        //SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);
        SDL_RenderFillRect(renderer, &scoreRect);

        // Use dynamic renderText for scores (changes every game)
        snprintf(scoreText, sizeof(scoreText), "Player %d: %d", snakes[i]->getPlayerNum(), snakes[i]->getScore());
        renderText(scoreText, WINDOW_WIDTH / 2 - 80, WINDOW_HEIGHT / 2 - 40 + i * 40, {255, 255, 255, 255});
    }

    // Options
    SDL_Rect restartRect = {WINDOW_WIDTH / 2 - 120, WINDOW_HEIGHT / 2 + 60, 240, 35};
    SDL_SetRenderDrawColor(renderer, 0, 150, 0, 255);
    SDL_RenderFillRect(renderer, &restartRect);
    
    SDL_Rect menuRect = {WINDOW_WIDTH / 2 - 120, WINDOW_HEIGHT / 2 + 105, 240, 35};
    SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);
    SDL_RenderFillRect(renderer, &menuRect);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    renderCachedText("Enter - Play Again", WINDOW_WIDTH / 2 - 100, WINDOW_HEIGHT / 2 + 60, {200, 200, 200, 255});
    renderCachedText("M - Main Menu", WINDOW_WIDTH / 2 - 100, WINDOW_HEIGHT / 2 + 105, {200, 200, 200, 255});
}