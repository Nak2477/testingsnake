#include "rendergame.h"
#include <algorithm>

GameRenderer::GameRenderer(SDL_Renderer* r, MenuRenderer* u) 
    : renderer(r),
            ui(u),
    gridTexture(nullptr) 
    {
    createGridTexture();
    }

GameRenderer::~GameRenderer()
{
    if (gridTexture) {
        SDL_DestroyTexture(gridTexture);
    }
}

void GameRenderer::createGridTexture()
{
    // Create texture with window dimensions
    gridTexture = SDL_CreateTexture(renderer, 
                                    SDL_PIXELFORMAT_RGBA8888,
                                    SDL_TEXTUREACCESS_TARGET,
                                    WINDOW_WIDTH, WINDOW_HEIGHT);
    if (!gridTexture) return;
    
    // Render grid lines to texture once
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

void GameRenderer::renderGrid()
{
    // Just copy the pre-rendered texture (1 blit instead of 70 draw calls!)
    if (gridTexture)
    {
        SDL_RenderCopy(renderer, gridTexture, nullptr, nullptr);
    }
}

void GameRenderer::renderPlayers(const std::array<PlayerSlot, 4>& players)
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

void GameRenderer::renderFood(const Food& food)
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

void GameRenderer::renderHUD(int score, int remainingSeconds, const std::string& sessionId)
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

void GameRenderer::renderMatchEnd(int winnerIndex, const std::array<PlayerSlot, 4>& players)
{
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
    SDL_Rect overlay = {0, 0, WINDOW_WIDTH, WINDOW_HEIGHT};
    SDL_RenderFillRect(renderer, &overlay);
    
    char text[128];

    if (winnerIndex >= 0 && players[winnerIndex].snake)
    {
        snprintf(text, sizeof(text), "MATCH ENDED - Player %d WINS!", winnerIndex + 1);
        ui->renderText(text, WINDOW_WIDTH/2 - 150, WINDOW_HEIGHT/2 - 60, {0, 255, 0, 255});
        
        snprintf(text, sizeof(text), "Length: %zu  Score: %d", 
                players[winnerIndex].snake->getBody().size(),
                players[winnerIndex].snake->getScore());
        ui->renderText(text, WINDOW_WIDTH/2 - 100, WINDOW_HEIGHT/2 - 20, {255, 255, 255, 255});
    } else {
        snprintf(text, sizeof(text), "MATCH ENDED - NO WINNER");
        ui->renderText(text, WINDOW_WIDTH/2 - 120, WINDOW_HEIGHT/2 - 30, {255, 0, 0, 255});
    }
    
    ui->renderText("Press R to start new match", WINDOW_WIDTH/2 - 120, WINDOW_HEIGHT/2 + 30, {200, 200, 200, 255});
}

void GameRenderer::renderPauseMenu()
{
    ui->renderPauseMenu();
}
