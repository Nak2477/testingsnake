#include "rendermenu.h"
#include <sstream>
#include <iomanip>
#include <iostream>

bool MenuRender::sdlInitialized = false;

MenuRender::MenuRender()
    : window(nullptr), renderer(nullptr), font(nullptr), titleFont(nullptr)
{
    if (sdlInitialized) {
        throw std::runtime_error("SDL already initialized - cannot create multiple MenuRender instances");
    }
    
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL init failed: " << SDL_GetError() << std::endl;
        throw std::runtime_error("SDL initialization failed");
    }

    if (TTF_Init() == -1) {
        std::cerr << "SDL_ttf init failed: " << TTF_GetError() << std::endl;
        SDL_Quit();
        throw std::runtime_error("SDL_ttf initialization failed");
    }
    
    sdlInitialized = true;
    
    window = SDL_CreateWindow(
        "Hardcore Snake",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    if (!window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        TTF_Quit();
        SDL_Quit();
        throw std::runtime_error("Window creation failed");
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        std::cerr << "Renderer creation failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        throw std::runtime_error("Renderer creation failed");
    }
    
    SDL_RenderSetLogicalSize(renderer, WINDOW_WIDTH, WINDOW_HEIGHT);

    font = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 24);
    titleFont = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 36);
    
    if (!font) {
        std::cerr << "Font load failed: " << TTF_GetError() << std::endl;
        font = nullptr;
    }
    if (!titleFont) titleFont = font;
}

MenuRender::~MenuRender()
{
    // Clean up all cached textures
    for (auto& pair : textureCache) {
        if (pair.second) {
            SDL_DestroyTexture(pair.second);
        }
    }
    textureCache.clear();
    
    // Cleanup SDL resources
    if (font) TTF_CloseFont(font);
    if (titleFont && titleFont != font) TTF_CloseFont(titleFont);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    
    sdlInitialized = false;
}

SDL_Texture* MenuRender::createTextTexture(const char* text, SDL_Color color, TTF_Font* textFont)
{
    if (!textFont) textFont = font;
    if (!textFont) return nullptr;
    
    SDL_Surface* surface = TTF_RenderText_Solid(textFont, text, color);
    if (!surface) return nullptr;
    
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    
    return texture;
}

SDL_Texture* MenuRender::getCachedTexture(const char* text, SDL_Color color, TTF_Font* textFont)
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

void MenuRender::renderText(const char* text, int x, int y, SDL_Color color, TTF_Font* textFont)
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

void MenuRender::renderCachedText(const char* text, int x, int y, SDL_Color color, TTF_Font* textFont)
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

void MenuRender::renderMenu(int menuSelection)
{
    // Draw title
    SDL_Rect titleRect = {WINDOW_WIDTH / 2 - 150, 100, 300, 60};
    //SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
    SDL_RenderFillRect(renderer, &titleRect);

    renderCachedText("HARDCORE SNAKE", WINDOW_WIDTH / 2 - 180, 100, {0, 255, 0, 255}, titleFont);
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
        renderCachedText("Use Arrow Keys/WASD  -  Enter to Select", WINDOW_WIDTH / 2 - 240, WINDOW_HEIGHT - 60, {150, 150, 150, 255});
}

void MenuRender::renderLobby(const std::array<PlayerSlot, 4>& players, bool isHost)
{
    // Draw title
    renderCachedText("WAITING FOR PLAYERS", WINDOW_WIDTH / 2 - 200, 80, {0, 255, 0, 255}, titleFont);
    
    // Draw player list
    int startY = 180;
    int spacing = 60;
    
    for (int i = 0; i < 4; i++) {
        char text[64];
        if (players[i].active && players[i].snake) {
            snprintf(text, sizeof(text), "Player %d: Ready", i + 1);
            renderCachedText(text, WINDOW_WIDTH / 2 - 100, startY + i * spacing, {0, 255, 0, 255});
        } else {
            snprintf(text, sizeof(text), "Player %d: Waiting...", i + 1);
            renderCachedText(text, WINDOW_WIDTH / 2 - 100, startY + i * spacing, {150, 150, 150, 255});
        }
    }
    
    // Instructions
    if (isHost) {
        renderCachedText("Press SPACE to start match", WINDOW_WIDTH / 2 - 150, WINDOW_HEIGHT - 80, {255, 255, 0, 255});
    } else {
        renderCachedText("Waiting for host to start...", WINDOW_WIDTH / 2 - 150, WINDOW_HEIGHT - 80, {255, 255, 0, 255});
    }
}

void MenuRender::renderCountdown(int seconds)
{
    // Semi-transparent overlay
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
    SDL_Rect overlay = {0, 0, WINDOW_WIDTH, WINDOW_HEIGHT};
    SDL_RenderFillRect(renderer, &overlay);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    
    // Draw countdown number
    char text[16];
    if (seconds > 0) {
        snprintf(text, sizeof(text), "%d", seconds);
    } else {
        snprintf(text, sizeof(text), "GO!");
    }
    
    renderText(text, WINDOW_WIDTH / 2 - 40, WINDOW_HEIGHT / 2 - 60, {0, 255, 0, 255}, titleFont);
}

void MenuRender::renderPauseMenu(int selection)
{
    // Semi-transparent overlay
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
    SDL_Rect overlay = {0, 0, WINDOW_WIDTH, WINDOW_HEIGHT};
    SDL_RenderFillRect(renderer, &overlay);
    
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    renderCachedText("PAUSED", WINDOW_WIDTH / 2 - 80, WINDOW_HEIGHT / 2 - 100, {0, 255, 0, 255}, titleFont);
    
    // Menu options with highlight
    SDL_Color normalColor = {255, 215, 0, 255};
    SDL_Color selectedColor = {0, 255, 0, 255};
    
    renderCachedText("Resume", WINDOW_WIDTH / 2 - 50, WINDOW_HEIGHT / 2, 
                     selection == 0 ? selectedColor : normalColor);
    renderCachedText("Restart", WINDOW_WIDTH / 2 - 45, WINDOW_HEIGHT / 2 + 50, 
                     selection == 1 ? selectedColor : normalColor);
    renderCachedText("Menu", WINDOW_WIDTH / 2 - 35, WINDOW_HEIGHT / 2 + 100, 
                     selection == 2 ? selectedColor : normalColor);
}

void MenuRender::renderMatchEnd(int winnerIndex, const std::array<PlayerSlot, 4>& players)
{
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
    SDL_Rect overlay = {0, 0, WINDOW_WIDTH, WINDOW_HEIGHT};
    SDL_RenderFillRect(renderer, &overlay);
    
    char text[128];

    if (winnerIndex >= 0 && winnerIndex < 4 && players.at(winnerIndex).snake)
    {
        snprintf(text, sizeof(text), "MATCH ENDED - Player %d WINS!", winnerIndex + 1);
        renderText(text, WINDOW_WIDTH/2 - 150, WINDOW_HEIGHT/2 - 60, {0, 255, 0, 255});
        
        snprintf(text, sizeof(text), "SCORE - %d", players.at(winnerIndex).snake->getScore());
        
        renderText(text, WINDOW_WIDTH/2 - 100, WINDOW_HEIGHT/2 - 20, {255, 255, 255, 255});
    } else {
        renderCachedText("MATCH ENDED - NO WINNER", WINDOW_WIDTH/2 - 120, WINDOW_HEIGHT/2 - 30, {255, 0, 0, 255});
    }
    
    renderCachedText("Press R to start new match", WINDOW_WIDTH/2 - 120, WINDOW_HEIGHT/2 + 30, {200, 200, 200, 255});
}