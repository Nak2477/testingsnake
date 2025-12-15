#ifndef GAME_RENDERER_H
#define GAME_RENDERER_H

#include "hardcoresnake.h"
#include "rendermenu.h"
#include <array>
#include <string>

class GameRenderer {
private:
    SDL_Renderer* renderer;
    MenuRenderer* ui;
    SDL_Texture* gridTexture;
public:
    GameRenderer(SDL_Renderer* renderer, MenuRenderer* ui);
    ~GameRenderer();

    void renderGrid();
    void renderPlayers(const std::array<PlayerSlot, 4>& players);
    void renderFood(const Food& food);
    void renderHUD(int score, int remainingSeconds, const std::string& sessionId);
    void renderMatchEnd(int winnerIndex, const std::array<PlayerSlot, 4>& players);
    void renderPauseMenu();
    void clearScreen();

private:
    void createGridTexture();
    
};

#endif // GAME_RENDERER_H
