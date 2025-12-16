#include "rendergame.h"
#include "multiplayer.h"
#include <algorithm>

GameRender::GameRender(SDL_Renderer* r, MenuRender* u) 
    : renderer(r),
            ui(u),
    gridTexture(nullptr) 
    {
    createGridTexture();
    }

GameRender::~GameRender()
{
    if (gridTexture) {
        SDL_DestroyTexture(gridTexture);
    }
}

void GameRender::createGridTexture()
{
    // Create texture with window dimensions
    gridTexture = SDL_CreateTexture(renderer, 
                                    SDL_PIXELFORMAT_RGBA8888,
                                    SDL_TEXTUREACCESS_TARGET,
                                    WINDOW_WIDTH, WINDOW_HEIGHT);
    if (!gridTexture) return;
    
    // gameState grid lines to texture once
    SDL_SetRenderTarget(renderer, gridTexture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);  // Transparent background
    SDL_RenderClear(renderer);
    
    SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
    for (int x = 0; x <= WINDOW_WIDTH; x += GRID_SIZE) {
        SDL_RenderDrawLine(renderer, x, 0, x, WINDOW_HEIGHT);
    }
    for (int y = 0; y <= WINDOW_HEIGHT; y += GRID_SIZE) {
        SDL_RenderDrawLine(renderer, 0, y, WINDOW_WIDTH, y);
    }
    
    SDL_SetRenderTarget(renderer, nullptr);  // Reset to screen
}

void GameRender::clearScreen()
{
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
}

void GameRender::renderGrid()
{
    // Just copy the pre-rendered texture (1 blit instead of 70 draw calls!)
    if (gridTexture)
    {
        SDL_RenderCopy(renderer, gridTexture, nullptr, nullptr);
    }
}

void GameRender::renderPlayers(const std::array<PlayerSlot, 4>& players)
{
    for (int p = 0; p < 4; p++)
    {
        if (!players[p].active || !players[p].snake) continue;
        
        const auto& body = players[p].snake->getBody();
        SDL_Color color = players[p].snake->getColor();
        
        for (size_t i = 0; i < body.size(); i++)
        {
            SDL_Rect rect = {
            body[i].x * GRID_SIZE,
            body[i].y * GRID_SIZE,
            GRID_SIZE - 1,
            GRID_SIZE - 1
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

void GameRender::renderFood(const Food& food)
{
    SDL_Color foodColor = food.getColor();
    Position foodPos = food.getPosition();
    SDL_Rect rect = {
        foodPos.x * GRID_SIZE,
        foodPos.y * GRID_SIZE,
        GRID_SIZE - 1,
        GRID_SIZE - 1
    };
    SDL_SetRenderDrawColor(renderer, foodColor.r, foodColor.g, foodColor.b, 255);
    SDL_RenderFillRect(renderer, &rect);
}

void GameRender::renderHUD(int score, int remainingSeconds, const std::string& sessionId)
{
    char text[64];
    // Score
    snprintf(text, sizeof(text), "Score: %d", score);
    ui->renderText(text, 10, 10, {255, 255, 255, 255});
    
    // Timer
    int minutes = remainingSeconds / 60;
    int seconds = remainingSeconds % 60;
    snprintf(text, sizeof(text), "Time: %02d:%02d", minutes, seconds);
    ui->renderText(text, WINDOW_WIDTH -150, 10, {255, 255, 0, 255});
    
    // Session ID
    if (!sessionId.empty())
    {
        ui->renderText(sessionId.c_str(), 10, 40, {255, 255, 0, 255});
    }
}

void GameRender::renderGame(const GameContext& ctx, bool matchEnded)
{
    // Clear screen
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    
    // gameState game elements
    renderGrid();
    renderPlayers(ctx.players);
    renderFood(*ctx.food);
    
    // Calculate score and remaining time for HUD
    int myScore = 0;
    if (ctx.myPlayerIndex >= 0 && ctx.players[ctx.myPlayerIndex].snake) {
        myScore = ctx.players[ctx.myPlayerIndex].snake->getScore();
    }
    
    int remainingSeconds = 0;
    if (!matchEnded)
    {
        // Use synced elapsed time from host (updated every second)
        Uint32 elapsedSeconds = ctx.syncedElapsedMs / 1000;
        remainingSeconds = MATCH_DURATION_SECONDS - elapsedSeconds;
    }
    
    renderHUD(myScore, remainingSeconds, ctx.sessionId);
}

void GameRender::present()
{
    SDL_RenderPresent(renderer);
}
