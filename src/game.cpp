#include "game.h"
#include <iostream>
#include <ctime>

Game::Game() 
    : window(nullptr), renderer(nullptr), font(nullptr), titleFont(nullptr),
      ui(nullptr), gameRenderer(nullptr),
      quit(false), paused(false), gameOver(false), matchEnded(false),
      winnerIndex(-1), lastUpdate(0), matchStartTime(0), updateInterval(INITIAL_SPEED)
{
    // Note: srand() is called in main(), not here
    
    ctx.myPlayerIndex = -1;
    ctx.api = nullptr;
    ctx.food = &food;
    ctx.isHost = false;
    ctx.matchStartTime = 0;
    
    if (!initSDL()) {
        throw std::runtime_error("Failed to initialize SDL");
    }
    
    initMultiplayer();
    initPlayers();
    
    // Spawn initial food
    std::vector<Position> occupiedPositions;
    food.spawn(occupiedPositions);
    
    // Create renderers
    ui = new MenuRenderer(renderer, font, titleFont);
    gameRenderer = new GameRenderer(renderer, ui);
    
    // Initialize timing
    lastUpdate = SDL_GetTicks();
    matchStartTime = SDL_GetTicks();
}

Game::~Game()
{
    // Cleanup multiplayer
    if (ctx.api) mp_api_destroy(ctx.api);
    
    // Cleanup renderers
    delete gameRenderer;
    delete ui;
    
    // Cleanup SDL
    if (font) TTF_CloseFont(font);
    if (titleFont && titleFont != font) TTF_CloseFont(titleFont);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    
    // Print final score
    if (ctx.myPlayerIndex >= 0 && ctx.players[ctx.myPlayerIndex].snake) {
        std::cout << "Final score: " << ctx.players[ctx.myPlayerIndex].snake->getScore() << std::endl;
    }
}

void Game::run()
{
    while (!quit) {
        handleInput();
        update();
        render();
        //SDL_Delay(16);
    }
}

bool Game::initSDL()
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL init failed: " << SDL_GetError() << std::endl;
        return false;
    }

    if (TTF_Init() == -1) {
        std::cerr << "SDL_ttf init failed: " << TTF_GetError() << std::endl;
        SDL_Quit();
        return false;
    }
    
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
        return false;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        std::cerr << "Renderer creation failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return false;
    }
    
    SDL_RenderSetLogicalSize(renderer, WINDOW_WIDTH, WINDOW_HEIGHT);

    font = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 24);
    titleFont = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 36);
    
    if (!font) {
        std::cerr << "Font load failed: " << TTF_GetError() << std::endl;
        font = nullptr;
    }
    if (!titleFont) titleFont = font;
    
    return true;
}

void Game::initMultiplayer()
{
    std::cout << "\n=== Hardcore Snake ===" << std::endl;
    ctx.api = mp_api_create("kontoret.onvo.se", 9001);
    if (!ctx.api) {
        std::cerr << "Failed to create multiplayer API" << std::endl;
        return;
    }
    
    std::cout << "Testing connection with list..." << std::endl;
    multiplayer_list(ctx);
    
    std::cout << "Setting up event listener..." << std::endl;
    mp_api_listen(ctx.api, on_multiplayer_event, &ctx);
}

void Game::initPlayers()
{
    // Initialize all player slots as inactive
    for (int i = 0; i < 4; i++) {
        ctx.players[i].active = false;
        ctx.players[i].clientId = "";
        ctx.players[i].snake = nullptr;
    }
}

void Game::handleInput()
{
    SDL_Event e;

    while (SDL_PollEvent(&e) != 0)
    {
        if (e.type == SDL_QUIT)
        {
            quit = true;
        }
        else if (e.type == SDL_KEYDOWN)
        {
            // Cache snake pointer to avoid repeated lookups
            Snake* mySnake = (ctx.myPlayerIndex >= 0 && ctx.players[ctx.myPlayerIndex].snake) 
                              ? ctx.players[ctx.myPlayerIndex].snake.get() 
                              : nullptr;
            bool canMove = !paused && !gameOver && mySnake;
            
            switch (e.key.keysym.sym) {
                case SDLK_UP:
                    if (canMove) mySnake->setDirection(Direction::UP);
                    break;
                case SDLK_DOWN:
                    if (canMove) mySnake->setDirection(Direction::DOWN);
                    break;
                case SDLK_LEFT:
                    if (canMove) mySnake->setDirection(Direction::LEFT);
                    break;
                case SDLK_RIGHT:
                    if (canMove) mySnake->setDirection(Direction::RIGHT);
                    break;
                case SDLK_r:
                    resetMatch();
                    break;
                case SDLK_p:
                    paused = !paused;
                    break;
                case SDLK_n:
                    // Start singleplayer mode
                    if (ctx.sessionId.empty() && !ctx.players[0].active) 
                    {
                        Position startPos = {GRID_WIDTH / 2, GRID_HEIGHT / 2};
                        ctx.players[0].snake = std::make_unique<Snake>(SDL_Color{0, 255, 0, 255}, startPos);
                        ctx.players[0].active = true;
                        ctx.players[0].clientId = "local_player";
                        ctx.myPlayerIndex = 0;
                        std::cout << "Started singleplayer mode - Press N to start" << std::endl;
                    }
                    break;
                case SDLK_h:
                    if (ctx.api && ctx.sessionId.empty()) multiplayer_host(ctx);
                    break;
                case SDLK_l:
                    if (ctx.api && ctx.sessionId.empty()) multiplayer_list(ctx);
                    break;
                case SDLK_1:
                case SDLK_2:
                case SDLK_3:
                case SDLK_4:
                    if (ctx.api && ctx.sessionId.empty() && !ctx.availableSessions.empty())
                    {
                        int idx = e.key.keysym.sym - SDLK_1;
                        if (idx < (int)ctx.availableSessions.size())
                        {
                            std::cout << "Joining session: " << ctx.availableSessions[idx] << std::endl;
                            multiplayer_join(ctx, ctx.availableSessions[idx].c_str());
                        }
                    }
                    break;
                case SDLK_ESCAPE:
                    quit = true;
                    break;
            }
        }
    }
}

void Game::update()
{
    Uint32 currentTime = SDL_GetTicks();
    
    checkMatchTimer(currentTime);
    
    if (!paused && !matchEnded && currentTime - lastUpdate >= (Uint32)updateInterval)
    {
        lastUpdate = currentTime;
        updatePlayers();
    }
}

void Game::render()
{
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    gameRenderer->renderGrid();
    gameRenderer->renderPlayers(ctx.players);
    gameRenderer->renderFood(food);
    
    // Calculate score and remaining time for HUD
    int myScore = 0;
    if (ctx.myPlayerIndex >= 0 && ctx.players[ctx.myPlayerIndex].snake) {
        myScore = ctx.players[ctx.myPlayerIndex].snake->getScore();
    }
    
    Uint32 currentTime = SDL_GetTicks();
    int remainingSeconds = 0;
    if (!matchEnded)
    {
        // Use synced match start time from context
        Uint32 matchStart = ctx.matchStartTime > 0 ? ctx.matchStartTime : matchStartTime;
        Uint32 elapsedSeconds = (currentTime - matchStart) / 1000;
        remainingSeconds = MATCH_DURATION_SECONDS - elapsedSeconds;
    }
    gameRenderer->renderHUD(myScore, remainingSeconds, ctx.sessionId);
    
    if (matchEnded)
    {
        gameRenderer->renderMatchEnd(winnerIndex, ctx.players);
    }
    
    if (paused)
    {
        gameRenderer->renderPauseMenu();
    }

    SDL_RenderPresent(renderer);
}

void Game::checkMatchTimer(Uint32 currentTime)
{
    if (matchEnded) return;
    
    // Use synced match start time from context
    Uint32 matchStart = ctx.matchStartTime > 0 ? ctx.matchStartTime : matchStartTime;
    Uint32 elapsedSeconds = (currentTime - matchStart) / 1000;
    if (elapsedSeconds >= MATCH_DURATION_SECONDS) {
        matchEnded = true;
        
        // Find winner - longest snake
        int maxLength = 0;
        winnerIndex = -1;
        std::vector<int> tiedPlayers;
        
        for (int i = 0; i < 4; i++)
        {
            if (!ctx.players[i].active || !ctx.players[i].snake)
                continue;

            int length = ctx.players[i].snake->getBody().size();
            
            if (length > maxLength) {
                maxLength = length;
                winnerIndex = i;
                tiedPlayers.clear();
                tiedPlayers.push_back(i);
            } else if (length == maxLength && maxLength > 0)
            {
                tiedPlayers.push_back(i);
            }
        }
        
        // Tie-breaker: score
        if (tiedPlayers.size() > 1)
        {
            int maxScore = -1;
            winnerIndex = -1;
            for (int idx : tiedPlayers)
            {
                if (ctx.players[idx].snake->getScore() > maxScore)
                {
                    maxScore = ctx.players[idx].snake->getScore();
                    winnerIndex = idx;
                }
            }
        }
        
        std::cout << "Match ended! ";
        if (winnerIndex >= 0)
        {
            std::cout << "Winner: Player " << (winnerIndex + 1) 
                     << " (Length: " << ctx.players[winnerIndex].snake->getBody().size()
                     << ", Score: " << ctx.players[winnerIndex].snake->getScore() << ")" << std::endl;
        } else {
            std::cout << "No winner (no active players)" << std::endl;
        }
    }
}

void Game::updatePlayers()
{
    for (int i = 0; i < 4; i++)
    {
        if (!ctx.players[i].active || !ctx.players[i].snake)
            continue;

        if (!ctx.players[i].snake->isAlive())
            continue;
        
        // Only update local player locally - remote players are updated via network
        if (i != ctx.myPlayerIndex)
            continue;
            
        ctx.players[i].snake->update();
        
        // Send multiplayer update for local player (throttled)
        sendPlayerUpdate(ctx, i);
        
        // Check boundary and self collision
        if (ctx.players[i].snake->checkBoundaryCollision() || 
            ctx.players[i].snake->checkSelfCollision()) {
            respawnPlayer(i);
            std::cout << "Player " << (i+1) << " died and respawned!" << std::endl;
            continue;
        }
        
        // Check collision with other snakes
        for (int j = 0; j < 4; j++)
        {
            if (i == j)
                continue;
            if (!ctx.players[j].active || !ctx.players[j].snake)
                continue;
            if (!ctx.players[j].snake->isAlive())
                continue;
            
            if (ctx.players[j].snake->checkCollision(ctx.players[i].snake->getHead()))
            {
                respawnPlayer(i);
                std::cout << "Player " << (i+1) << " hit player " << (j+1) << " and respawned!" << std::endl;
                break;
            }
        }
        
        // Check food collision
        if (ctx.players[i].snake->isAlive() && ctx.players[i].snake->getHead() == food.getPosition())
        {
            ctx.players[i].snake->grow();
            
            // Send multiplayer update for local player
            sendPlayerUpdate(ctx, i);
            
            // Only host spawns new food and broadcasts it
            if (ctx.isHost) {
                occupiedPositions.clear();
                for (int k = 0; k < 4; k++)
                {
                    if (ctx.players[k].active && ctx.players[k].snake)
                    {
                        for (const auto& segment : ctx.players[k].snake->getBody()) {
                            occupiedPositions.push_back(segment);
                        }
                    }
                }
                food.spawn(occupiedPositions);
                
                // Broadcast new food position
                json_t *foodUpdate = json_object();
                json_object_set_new(foodUpdate, "type", json_string("state_sync"));
                json_object_set_new(foodUpdate, "foodX", json_integer(food.getPosition().x));
                json_object_set_new(foodUpdate, "foodY", json_integer(food.getPosition().y));
                json_object_set_new(foodUpdate, "matchStartTime", json_integer(ctx.matchStartTime));
                mp_api_game(ctx.api, foodUpdate);
                json_decref(foodUpdate);
            }
            
            if (updateInterval > MIN_UPDATE_INTERVAL) updateInterval -= SPEED_INCREASE;
        }
    }
}

void Game::respawnPlayer(int playerIndex)
{
    Position randomPos;
    randomPos.x = rand() % GRID_WIDTH;
    randomPos.y = rand() % GRID_HEIGHT;
    ctx.players[playerIndex].snake->reset(randomPos);
    
    // Send multiplayer update for local player respawn
    if (playerIndex == ctx.myPlayerIndex) {
        sendPlayerUpdate(ctx, playerIndex);
    }
}

void Game::resetMatch()
{
    for (int i = 0; i < 4; i++)
    {
        if (ctx.players[i].active && ctx.players[i].snake) {
            Position spawnPos = {GRID_WIDTH/4 + (i%2)*(GRID_WIDTH/2), 
                                GRID_HEIGHT/4 + (i/2)*(GRID_HEIGHT/2)};
            ctx.players[i].snake->reset(spawnPos);
        }
    }
    gameOver = false;
    matchEnded = false;
    winnerIndex = -1;
    matchStartTime = SDL_GetTicks();
    occupiedPositions.clear();
    food.spawn(occupiedPositions);
    updateInterval = INITIAL_SPEED;
    paused = false;
    std::cout << "Game reset!" << std::endl;
}
