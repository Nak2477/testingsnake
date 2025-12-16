#ifndef RENDERGAME_H
#define RENDERGAME_H

#include "hardcoresnake.h"
#include "rendermenu.h"
#include <array>
#include <string>

// Forward declaration
struct GameContext;

class GameRender {
private:
    SDL_Renderer* renderer;
    MenuRender* ui;
    SDL_Texture* gridTexture;
public:
    GameRender(SDL_Renderer* renderer, MenuRender* ui);
    ~GameRender();

    void renderGrid();
    void renderPlayers(const std::array<PlayerSlot, 4>& players);
    void renderFood(const Food& food);
    void renderHUD(int score, int remainingSeconds, const std::string& sessionId);
    void clearScreen();
    
    // High-level rendering
    void renderGame(const GameContext& ctx, bool matchEnded, Uint32 matchStartTime);
    void present();

private:
    void createGridTexture();
    
};

#endif // RENDERGAME_H
