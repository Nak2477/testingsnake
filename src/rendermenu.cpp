#include "rendermenu.h"
#include "multiplayer.h"
#include "config.h"
#include "logger.h"
#include <sstream>
#include <iomanip>
#include <iostream>
#include <algorithm>

std::atomic<bool> MenuRender::sdlInitialized(false);
std::mutex MenuRender::sdlInitMutex;

MenuRender::MenuRender()
    : window(nullptr), renderer(nullptr), font(nullptr), titleFont(nullptr)
{
    // Initialize SDL subsystems (thread-safe, safe to call multiple times)
    if (!sdlInitialized.load()) {
        std::lock_guard<std::mutex> lock(sdlInitMutex);
        if (!sdlInitialized.load()) {
            if (SDL_Init(SDL_INIT_VIDEO) < 0) {
                std::cerr << "SDL init failed: " << SDL_GetError() << std::endl;
                throw std::runtime_error("SDL initialization failed");
            }

            if (TTF_Init() == -1) {
                std::cerr << "SDL_ttf init failed: " << TTF_GetError() << std::endl;
                SDL_Quit();
                throw std::runtime_error("SDL_ttf initialization failed");
            }
            
            sdlInitialized.store(true);
        }
    }
    
    window = SDL_CreateWindow(
        "Hardcore Snake",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        Config::Window::WIDTH,
        Config::Window::HEIGHT,
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
    
    SDL_RenderSetLogicalSize(renderer, Config::Window::WIDTH, Config::Window::HEIGHT);

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

// ========== GAME RENDERING (merged from GameRender) ==========

void MenuRender::clearScreen()
{
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
}

void MenuRender::renderPlayers(const std::array<PlayerSlot, Config::Game::MAX_PLAYERS>& players)
{
    for (int p = 0; p < Config::Game::MAX_PLAYERS; p++)
    {
        if (!players[p].active || !players[p].snake) continue;
        
        const auto& body = players[p].snake->getBody();
        SDL_Color color = players[p].snake->getColor();
        
        for (size_t i = 0; i < body.size(); i++)
        {
            SDL_Rect rect = {
                body[i].x * Config::Grid::CELL_SIZE,
                body[i].y * Config::Grid::CELL_SIZE,
                Config::Grid::CELL_SIZE - 1,
                Config::Grid::CELL_SIZE - 1
            };
            
            if (i == 0) {  // Head - brighter
                SDL_SetRenderDrawColor(renderer, 
                    std::min(255, color.r + 50),
                    std::min(255, color.g + 50),
                    std::min(255, color.b + 50), 255);
            } else {
                SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);
            }
            SDL_RenderFillRect(renderer, &rect);
        }
    }
}

void MenuRender::renderFood(const Food& food)
{
    SDL_Color foodColor = food.getColor();
    Position foodPos = food.getPosition();
    SDL_Rect rect = {
        foodPos.x * Config::Grid::CELL_SIZE,
        foodPos.y * Config::Grid::CELL_SIZE,
        Config::Grid::CELL_SIZE - 1,
        Config::Grid::CELL_SIZE - 1
    };
    SDL_SetRenderDrawColor(renderer, foodColor.r, foodColor.g, foodColor.b, 255);
    SDL_RenderFillRect(renderer, &rect);
}

void MenuRender::renderHUD(int score, int remainingSeconds, const std::string& sessionId)
{
    char text[64];
    snprintf(text, sizeof(text), "Score: %d", score);
    renderText(text, 10, 10, {255, 255, 255, 255});
    
    int minutes = remainingSeconds / 60;
    int seconds = remainingSeconds % 60;
    snprintf(text, sizeof(text), "Time: %02d:%02d", minutes, seconds);
    renderText(text, Config::Window::WIDTH - 150, 10, {255, 255, 0, 255});
    
    if (!sessionId.empty())
    {
        renderText(sessionId.c_str(), 10, 40, {255, 255, 0, 255});
    }
}

void MenuRender::renderGame(const GameContext& ctx, bool matchEnded)
{
    clearScreen();
    renderPlayers(ctx.players.getSlots());
    renderFood(*ctx.food);
    
    int myScore = 0;
    if (ctx.players.hasMe() && ctx.players.me().snake) {
        myScore = ctx.players.me().snake->getScore();
    }
    
    int remainingSeconds = 0;
    if (!matchEnded)
    {
        Uint32 elapsedSeconds = ctx.match.syncedElapsedMs / 1000;
        remainingSeconds = Config::Game::MATCH_DURATION_SECONDS - elapsedSeconds;
    }
    
    renderHUD(myScore, remainingSeconds, ctx.network.sessionId);
}

void MenuRender::present()
{
    SDL_RenderPresent(renderer);
}

// ========== TEXT RENDERING HELPERS ==========

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

void MenuRender::renderText(const char* text, int x, int y, SDL_Color color, TTF_Font* textFont, bool cache)
{
    if (!textFont) textFont = font;
    if (!textFont) return;
    
    SDL_Texture* texture;
    int w, h;
    
    if (cache) {
        // Use cached texture
        texture = getCachedTexture(text, color, textFont);
        if (!texture) return;
        SDL_QueryTexture(texture, nullptr, nullptr, &w, &h);
    } else {
        // Create temporary texture
        SDL_Surface* surface = TTF_RenderText_Solid(textFont, text, color);
        if (!surface) return;
        
        texture = SDL_CreateTextureFromSurface(renderer, surface);
        w = surface->w;
        h = surface->h;
        SDL_FreeSurface(surface);
        
        if (!texture) return;
    }
    
    SDL_Rect destRect = {x, y, w, h};
    SDL_RenderCopy(renderer, texture, nullptr, &destRect);
    
    if (!cache) {
        SDL_DestroyTexture(texture);
    }
}

void MenuRender::renderMenu(int menuSelection)
{
    // Draw title
    SDL_Rect titleRect = {Config::Window::WIDTH / 2 - 150, 100, 300, 60};
    SDL_RenderFillRect(renderer, &titleRect);

    renderText("HARDCORE SNAKE", Config::Window::WIDTH / 2 - 180, 100, {0, 255, 0, 255}, titleFont, true);
    // Menu options
    const char* options[] = {"Single Player", "Multiplayer", "Quit"};
    int startY = 250;
    int spacing = 80;
    
    for (int i = 0; i < 3; i++) {
        SDL_Color textColor = (i == menuSelection) ? SDL_Color{255, 255, 255, 255} : SDL_Color{150, 150, 150, 255};
        
        // Draw option box
        SDL_Rect optionRect = {Config::Window::WIDTH / 2 - 120, startY + i * spacing, 240, 50};
        //SDL_SetRenderDrawColor(renderer, i == menuSelection ? 0 : 40, i == menuSelection ? 200 : 40, 0, 255);
        SDL_RenderFillRect(renderer, &optionRect);
        
        // Draw text (cached)
        renderText(options[i], Config::Window::WIDTH / 2 - 80, startY + i * spacing + 12, textColor, nullptr, true);
    }
        renderText("Use Arrow Keys/WASD  -  Enter to Select", Config::Window::WIDTH / 2 - 240, Config::Window::HEIGHT - 60, {150, 150, 150, 255}, nullptr, true);
}

void MenuRender::renderSessionBrowser(const std::vector<std::string>& sessions, int selectedIndex, bool isConnected)
{
    // Title
    renderText("MULTIPLAYER - SESSION BROWSER", Config::Window::WIDTH / 2 - 270, 50, {0, 255, 0, 255}, titleFont, true);
    
    if (!isConnected) {
        renderText("Connecting to server...", Config::Window::WIDTH / 2 - 150, Config::Window::HEIGHT / 2 - 50, {255, 255, 0, 255});
        renderText("Press ESC to return", Config::Window::WIDTH / 2 - 120, Config::Window::HEIGHT / 2 + 50, {200, 200, 200, 255});
        SDL_RenderPresent(renderer);
        return;
    }
    
    // Instructions
    renderText("H - Host Session   |   L - List Sessions   |   ESC - Back", 30, 120, {200, 200, 200, 255});
    
    if (sessions.empty()) {
        renderText("No sessions available", Config::Window::WIDTH / 2 - 150, Config::Window::HEIGHT / 2 - 50, {255, 255, 0, 255});
        renderText("Press H to host a new session", Config::Window::WIDTH / 2 - 170, Config::Window::HEIGHT / 2, {200, 200, 200, 255});
        renderText("Press L to refresh list", Config::Window::WIDTH / 2 - 140, Config::Window::HEIGHT / 2 + 50, {200, 200, 200, 255});
    } else {
        // Instructions for selection
        renderText("Use UP/DOWN arrows to select, ENTER to join", Config::Window::WIDTH / 2 - 250, 170, {150, 150, 150, 255});
        
        // Display sessions (max 10 visible at a time)
        int startY = 220;
        int spacing = 45;
        size_t maxVisible = 10;
        int startIdx = std::max(0, selectedIndex - (int)maxVisible / 2);
        int endIdx = std::min((int)sessions.size(), startIdx + (int)maxVisible);
        
        // Adjust start if near end
        if ((size_t)(endIdx - startIdx) < maxVisible && sessions.size() >= maxVisible) {
            startIdx = endIdx - (int)maxVisible;
        }
        
        for (int i = startIdx; i < endIdx; i++) {
            SDL_Color color = (i == selectedIndex) ? SDL_Color{255, 255, 0, 255} : SDL_Color{150, 150, 150, 255};
            
            // Draw selection indicator
            if (i == selectedIndex) {
                renderText(">", 80, startY + (i - startIdx) * spacing, {255, 255, 0, 255});
            }
            
            // Session number and ID
            char sessionText[128];
            snprintf(sessionText, sizeof(sessionText), "[%d] %s", i + 1, sessions[i].c_str());
            renderText(sessionText, 120, startY + (i - startIdx) * spacing, color);
        }
        
        // Show scroll indicator if needed
        if (sessions.size() > maxVisible) {
            char scrollInfo[64];
            snprintf(scrollInfo, sizeof(scrollInfo), "Showing %d-%d of %zu sessions", 
                    startIdx + 1, endIdx, sessions.size());
            renderText(scrollInfo, Config::Window::WIDTH / 2 - 120, startY + (int)maxVisible * spacing + 20, {100, 100, 100, 255});
        }
    }
    
    SDL_RenderPresent(renderer);
}

void MenuRender::renderLobby(const std::array<PlayerSlot, 4>& players, bool isHost)
{
    // Draw title
    renderText("WAITING FOR PLAYERS", Config::Window::WIDTH / 2 - 200, 80, {0, 255, 0, 255}, titleFont, true);
    
    // Draw player list
    int startY = 180;
    int spacing = 60;
    
    for (int i = 0; i < Config::Game::MAX_PLAYERS; i++) {
        char text[64];
        if (players[i].active && players[i].snake) {
            snprintf(text, sizeof(text), "Player %d: Ready", i + 1);
            renderText(text, Config::Window::WIDTH / 2 - 100, startY + i * spacing, {0, 255, 0, 255}, nullptr, true);
        } else {
            snprintf(text, sizeof(text), "Player %d: Waiting...", i + 1);
            renderText(text, Config::Window::WIDTH / 2 - 100, startY + i * spacing, {150, 150, 150, 255}, nullptr, true);
        }
    }
    
    // Instructions
    if (isHost) {
        renderText("Press SPACE to start match", Config::Window::WIDTH / 2 - 150, Config::Window::HEIGHT - 80, {255, 255, 0, 255}, nullptr, true);
    } else {
        renderText("Waiting for host to start...", Config::Window::WIDTH / 2 - 150, Config::Window::HEIGHT - 80, {255, 255, 0, 255}, nullptr, true);
    }
}

void MenuRender::renderCountdown(int seconds)
{
    // Semi-transparent overlay
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
    SDL_Rect overlay = {0, 0, Config::Window::WIDTH, Config::Window::HEIGHT};
    SDL_RenderFillRect(renderer, &overlay);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    
    // Draw countdown number
    char text[16];
    if (seconds > 0) {
        snprintf(text, sizeof(text), "%d", seconds);
    } else {
        snprintf(text, sizeof(text), "GO!");
    }
    
    renderText(text, Config::Window::WIDTH / 2 - 40, Config::Window::HEIGHT / 2 - 60, {0, 255, 0, 255}, titleFont);
}

void MenuRender::renderPauseMenu(int selection)
{
    // Semi-transparent overlay
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
    SDL_Rect overlay = {0, 0, Config::Window::WIDTH, Config::Window::HEIGHT};
    SDL_RenderFillRect(renderer, &overlay);
    
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    renderText("PAUSED", Config::Window::WIDTH / 2 - 80, Config::Window::HEIGHT / 2 - 100, {0, 255, 0, 255}, titleFont, true);
    
    // Menu options with highlight
    SDL_Color normalColor = {255, 215, 0, 255};
    SDL_Color selectedColor = {0, 255, 0, 255};
    
    renderText("Resume", Config::Window::WIDTH / 2 - 50, Config::Window::HEIGHT / 2, 
                     selection == 0 ? selectedColor : normalColor);
    renderText("Restart", Config::Window::WIDTH / 2 - 45, Config::Window::HEIGHT / 2 + 50, 
                     selection == 1 ? selectedColor : normalColor);
    renderText("Menu", Config::Window::WIDTH / 2 - 35, Config::Window::HEIGHT / 2 + 100, 
                     selection == 2 ? selectedColor : normalColor);
}

void MenuRender::renderMatchEnd(int winnerIndex, const std::array<PlayerSlot, 4>& players)
{
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
    SDL_Rect overlay = {0, 0, Config::Window::WIDTH, Config::Window::HEIGHT};
    SDL_RenderFillRect(renderer, &overlay);
    
    char text[128];

    if (winnerIndex >= 0 && winnerIndex < Config::Game::MAX_PLAYERS && players.at(winnerIndex).snake)
    {
        snprintf(text, sizeof(text), "MATCH ENDED - Player %d WINS!", winnerIndex + 1);
        renderText(text, Config::Window::WIDTH/2 - 150, Config::Window::HEIGHT/2 - 60, {0, 255, 0, 255});
        
        snprintf(text, sizeof(text), "SCORE - %d", players.at(winnerIndex).snake->getScore());
        
        renderText(text, Config::Window::WIDTH/2 - 100, Config::Window::HEIGHT/2 - 20, {255, 255, 255, 255});
    } else {
        renderText("MATCH ENDED - NO WINNER", Config::Window::WIDTH/2 - 120, Config::Window::HEIGHT/2 - 30, {255, 0, 0, 255}, nullptr, true);
    }
    
    renderText("Press R to start new match", Config::Window::WIDTH/2 - 120, Config::Window::HEIGHT/2 + 30, {200, 200, 200, 255}, nullptr, true);
}