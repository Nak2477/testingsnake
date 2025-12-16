#include "game.h"
#include <iostream>
#include <ctime>

Game::Game() 
    : ui(nullptr), gameRenderer(nullptr),
      state(GameState::MENU), quit(false),
      updateInterval(INITIAL_SPEED), menuSelection(0)
{
    // Initialize game context
    ctx.myPlayerIndex = -1;
    ctx.api = nullptr;
    ctx.food = &food;
    ctx.isHost = false;
    ctx.matchStartTime = 0;
    ctx.winnerIndex = -1;
    ctx.totalPausedTime = 0;
    ctx.pauseStartTime = 0;
    ctx.gameStatePtr = &state;
    
    // Initialize SDL and rendering (throws on failure)
    ui = new MenuRender();
    gameRenderer = new GameRender(ui->getRenderer(), ui);
    

    initPlayers();
    
    // Reserve capacity for collision detection map
    occupiedPositions.reserve(400);  // 4 players * ~100 segments max
    
    // Spawn initial food
    food.spawn(occupiedPositions);
    
    // Initialize timing
    lastUpdate = matchStartTime = SDL_GetTicks();
}

Game::~Game()
{
    // Cleanup multiplayer
    if (ctx.api) mp_api_destroy(ctx.api);
    
    // Cleanup renderers (MenuRender destructor handles SDL cleanup)
    delete ui;
    delete gameRenderer;
    
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
        //SDL_Delay(16);  // ~60 FPS
    }
}

void Game::initPlayers()
{
    // Initialize all player slots as inactive
    for (int i = 0; i < 4; i++) {
        ctx.players[i].active = false;
        ctx.players[i].clientId = "";
        ctx.players[i].snake = nullptr;
        ctx.players[i].paused = false;
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
            continue;
        }
        
        if (e.type == SDL_KEYDOWN)
        {
            // Dispatch to state-specific handlers
            switch (state)
            {
                case GameState::MENU:
                    handleMenuInput(e.key.keysym.sym);
                    break;
                case GameState::MULTIPLAYER:
                    handleMultiplayerInput(e.key.keysym.sym);
                    break;
                case GameState::LOBBY:
                    handleLobbyInput(e.key.keysym.sym);
                    break;
                case GameState::PLAYING:
                    handlePlayingInput(e.key.keysym.sym);
                    break;
                case GameState::PAUSED:
                    handlePausedInput(e.key.keysym.sym);
                    break;
                case GameState::MATCH_END:
                    handleMatchEndInput(e.key.keysym.sym);
                    break;
                default:
                    break;
            }
        }
    }
}

void Game::update()
{
    // Only update game logic when playing or paused
    if (state != GameState::PLAYING && state != GameState::PAUSED)
        return;
    
    Uint32 currentTime = SDL_GetTicks();
    
    // Only check timer when actively playing (not paused)
    if (state == GameState::PLAYING) {
        checkMatchTimer(currentTime);
    }
    
    // Update players when playing, or send keepalive updates when paused
    if (currentTime - lastUpdate >= (Uint32)updateInterval)
    {
        lastUpdate = currentTime;
        
        if (state == GameState::PLAYING) {
            // Normal game update - move snakes, check collisions
            updatePlayers();
        } else if (state == GameState::PAUSED) {
            // Paused - just send position update to keep session alive
            if (ctx.myPlayerIndex >= 0 && 
                ctx.players[ctx.myPlayerIndex].active && 
                ctx.players[ctx.myPlayerIndex].snake) {
                sendPlayerUpdate(ctx, ctx.myPlayerIndex);
            }
        }
    }
}

void Game::render()
{
    switch (state)
    {
        case GameState::MENU:
            gameRenderer->clearScreen();
            ui->renderMenu(menuSelection);
            break;
            
        case GameState::SINGLEPLAYER:
        case GameState::MULTIPLAYER:
            // Transition states - render menu
            gameRenderer->clearScreen();
            ui->renderMenu(menuSelection);
            break;
            
        case GameState::LOBBY:
            gameRenderer->clearScreen();
            ui->renderLobby(ctx.players, ctx.isHost);
            break;
            
        case GameState::COUNTDOWN:
            gameRenderer->renderGame(ctx, false, matchStartTime);
            // ui->renderCountdown(countdownSeconds); // TODO: Need to track countdown timer
            break;
            
        case GameState::PLAYING:
            gameRenderer->renderGame(ctx, false, matchStartTime);
            break;
            
        case GameState::PAUSED:
            gameRenderer->renderGame(ctx, false, matchStartTime);
            ui->renderPauseMenu();
            break;
            
        case GameState::MATCH_END:
            gameRenderer->renderGame(ctx, true, matchStartTime);
            ui->renderMatchEnd(ctx.winnerIndex, ctx.players);
            break;
    }

    gameRenderer->present();
}

void Game::handleMenuInput(SDL_Keycode key)
{
    // Clean up multiplayer when in menu
    if (ctx.api) {
        mp_api_destroy(ctx.api);
        ctx.api = nullptr;
        ctx.sessionId.clear();
        ctx.myPlayerIndex = -1;
        for (int i = 0; i < 4; i++) {
            ctx.players[i].active = false;
            ctx.players[i].snake = nullptr;
        }
    }
    
    switch (key)
    {
        case SDLK_UP:
        case SDLK_w:
            menuSelection = (menuSelection - 1 + 3) % 3;
            break;
        case SDLK_DOWN:
        case SDLK_s:
            menuSelection = (menuSelection + 1) % 3;
            break;
        case SDLK_RETURN:
        case SDLK_SPACE:

            if (menuSelection == 0)
            {  // Single Player
                Position startPos = {GRID_WIDTH / 2, GRID_HEIGHT / 2};
                ctx.players[0].snake = std::make_unique<Snake>(SDL_Color{0, 255, 0, 255}, startPos);
                ctx.players[0].active = true;
                ctx.players[0].clientId = "local_player";
                ctx.myPlayerIndex = 0;
                ctx.isHost = true;
                state = GameState::PLAYING;
                std::cout << "Started singleplayer mode" << std::endl;
            }
            else if (menuSelection == 1)
            {  // Multiplayer
                ctx.api = mp_api_create("kontoret.onvo.se", 9001);
                if (ctx.api)
                {
                    mp_api_listen(ctx.api, on_multiplayer_event, &ctx);

                    state = GameState::MULTIPLAYER;
                    std::cout << "Multiplayer - Press H to host or L to list sessions" << std::endl;
                    return;
                }
                    std::cerr << "Failed to create multiplayer API" << std::endl;
                
            }
            else if (menuSelection == 2)
            {  // Quit
                quit = true;
            }
            break;
        case SDLK_ESCAPE:
            quit = true;
            break;
    }
}

void Game::handleMultiplayerInput(SDL_Keycode key)
{
    switch (key)
    {
        case SDLK_h:
            if (ctx.api && ctx.sessionId.empty()) {
                multiplayer_host(ctx);
                state = GameState::LOBBY;
            }
            break;
        case SDLK_l:
            if (ctx.api && ctx.sessionId.empty()) {
                multiplayer_list(ctx);
            }
            break;
        case SDLK_1:
        case SDLK_2:
        case SDLK_3:
        case SDLK_4:
            if (ctx.api && ctx.sessionId.empty() && !ctx.availableSessions.empty())
            {
                int idx = key - SDLK_1;
                if (idx < (int)ctx.availableSessions.size())
                {
                    std::cout << "Joining session: " << ctx.availableSessions[idx] << std::endl;
                    multiplayer_join(ctx, ctx.availableSessions[idx].c_str());
                    state = GameState::LOBBY;
                }
            }
            break;
        case SDLK_ESCAPE:
            state = GameState::MENU;
            break;
    }
}

void Game::handleLobbyInput(SDL_Keycode key)
{
    switch (key)
    {
        case SDLK_SPACE:
            if (ctx.isHost) {
                state = GameState::PLAYING;
                matchStartTime = SDL_GetTicks();
                ctx.matchStartTime = matchStartTime;
            }
            break;
        case SDLK_ESCAPE:
            state = GameState::MENU;
            break;
    }
}

void Game::handlePlayingInput(SDL_Keycode key)
{
    Snake* mySnake = (ctx.myPlayerIndex >= 0 && ctx.players[ctx.myPlayerIndex].snake) 
                      ? ctx.players[ctx.myPlayerIndex].snake.get() 
                      : nullptr;
    
    if (!mySnake)
        return;
    
    switch (key)
    {
        case SDLK_UP:
        case SDLK_w:
            mySnake->setDirection(Direction::UP);
            break;
        case SDLK_DOWN:
        case SDLK_s:
            mySnake->setDirection(Direction::DOWN);
            break;
        case SDLK_LEFT:
        case SDLK_a:
            mySnake->setDirection(Direction::LEFT);
            break;
        case SDLK_RIGHT:
        case SDLK_d:
            mySnake->setDirection(Direction::RIGHT);
            break;
        case SDLK_p:
        case SDLK_ESCAPE:
            // Pause the game - transition to PAUSED state
            state = GameState::PAUSED;
            if (ctx.myPlayerIndex >= 0) {
                ctx.players[ctx.myPlayerIndex].paused = true;
            }
            // Track pause start time
            ctx.pauseStartTime = SDL_GetTicks();
            // Broadcast pause
            if (ctx.api) {
                ctx.pausedByClientId = ctx.players[ctx.myPlayerIndex].clientId;
                sendGlobalPauseState(ctx, true, ctx.pausedByClientId);
            }
            break;
    }
}

void Game::handlePausedInput(SDL_Keycode key)
{
    // Helper to check unpause permission and get clientId
    auto canUnpause = [this]() -> bool {
        if (!ctx.api) return true;
        if (ctx.myPlayerIndex < 0) return false;
        std::string myClientId = ctx.players[ctx.myPlayerIndex].clientId;
        return ctx.pausedByClientId.empty() || ctx.pausedByClientId == myClientId;
    };
    
    // Helper to perform unpause
    auto doUnpause = [this]() {
        if (ctx.myPlayerIndex >= 0) {
            ctx.players[ctx.myPlayerIndex].paused = false;
        }
        // Update pause timing
        if (ctx.pauseStartTime > 0) {
            ctx.totalPausedTime += (SDL_GetTicks() - ctx.pauseStartTime);
            ctx.pauseStartTime = 0;
        }
        // Broadcast unpause
        if (ctx.api && ctx.myPlayerIndex >= 0) {
            ctx.pausedByClientId = "";
            sendGlobalPauseState(ctx, false, ctx.players[ctx.myPlayerIndex].clientId);
        }
    };
    
    switch (key)
    {
        case SDLK_p:
        case SDLK_SPACE:
            if (!canUnpause()) {
                std::cout << "Only the player who paused can unpause" << std::endl;
                break;
            }
            state = GameState::PLAYING;
            doUnpause();
            break;
        case SDLK_ESCAPE:
            if (!canUnpause()) {
                std::cout << "Only the player who paused can unpause" << std::endl;
                break;
            }
            state = GameState::MENU;
            doUnpause();
            break;
    }
}

void Game::handleMatchEndInput(SDL_Keycode key)
{
    switch (key)
    {
        case SDLK_r:
            resetMatch();
            break;
        case SDLK_ESCAPE:
            state = GameState::MENU;
            break;
    }
}

void Game::checkMatchTimer(Uint32 currentTime)
{
    if (state == GameState::MATCH_END) return;
    
    // Use synced match start time from context
    Uint32 matchStart = ctx.matchStartTime > 0 ? ctx.matchStartTime : matchStartTime;
    
    // Calculate elapsed time, subtracting paused time
    Uint32 currentPausedTime = ctx.totalPausedTime;
    if (state == GameState::PAUSED && ctx.pauseStartTime > 0) {
        // Add current pause duration to total
        currentPausedTime += (currentTime - ctx.pauseStartTime);
    }
    
    Uint32 elapsedSeconds = (currentTime - matchStart - currentPausedTime) / 1000;
    if (elapsedSeconds >= MATCH_DURATION_SECONDS) {
        state = GameState::MATCH_END;
        
        // Find winner - longest snake
        int maxLength = 0;
        ctx.winnerIndex = -1;
        std::vector<int> tiedPlayers;
        
        for (int i = 0; i < 4; i++)
        {
            if (!ctx.players[i].active || !ctx.players[i].snake)
                continue;

            int length = ctx.players[i].snake->getBody().size();
            
            if (length > maxLength) {
                maxLength = length;
                ctx.winnerIndex = i;
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
            ctx.winnerIndex = -1;
            for (int idx : tiedPlayers)
            {
                if (ctx.players[idx].snake->getScore() > maxScore)
                {
                    maxScore = ctx.players[idx].snake->getScore();
                    ctx.winnerIndex = idx;
                }
            }
        }
        
        std::cout << "Match ended! ";
        if (ctx.winnerIndex >= 0)
        {
            std::cout << "Winner: Player " << (ctx.winnerIndex + 1) 
                     << " (Length: " << ctx.players[ctx.winnerIndex].snake->getBody().size()
                     << ", Score: " << ctx.players[ctx.winnerIndex].snake->getScore() << ")" << std::endl;
        } else {
            std::cout << "No winner (no active players)" << std::endl;
        }
    }
}

void Game::updatePlayers()
{
    // STEP 1: Build collision map from ALL snakes (local + remote)
    occupiedPositions.clear();
    for (int k = 0; k < 4; k++)
    {
        if (ctx.players[k].active && ctx.players[k].snake)
        {
            for (const auto& segment : ctx.players[k].snake->getBody()) {
                int key = segment.y * GRID_WIDTH + segment.x;
                occupiedPositions[key] = true;
            }
        }
    }
    
    // STEP 2: Check food collision for ALL players
    for (int i = 0; i < 4; i++)
    {
        if (!ctx.players[i].active || !ctx.players[i].snake)
            continue;
        if (!ctx.players[i].snake->isAlive())
            continue;
        
        if (ctx.players[i].snake->getHead() == food.getPosition())
        {
            ctx.players[i].snake->grow();
            
            // Only host spawns new food and broadcasts it
            if (ctx.isHost) {
                // Rebuild map with new grown snake
                occupiedPositions.clear();
                for (int k = 0; k < 4; k++)
                {
                    if (ctx.players[k].active && ctx.players[k].snake)
                    {
                        for (const auto& segment : ctx.players[k].snake->getBody()) {
                            int key = segment.y * GRID_WIDTH + segment.x;
                            occupiedPositions[key] = true;
                        }
                    }
                }
                
                food.spawn(occupiedPositions);  // O(1) lookup in spawn!
                
                // Broadcast new food position
                json_t *foodUpdate = json_object();
                json_object_set_new(foodUpdate, "type", json_string("state_sync"));
                json_object_set_new(foodUpdate, "foodX", json_integer(food.getPosition().x));
                json_object_set_new(foodUpdate, "foodY", json_integer(food.getPosition().y));
                json_object_set_new(foodUpdate, "matchStartTime", json_integer(ctx.matchStartTime));
                mp_api_game(ctx.api, foodUpdate);
                json_decref(foodUpdate);
            }
        }
    }
    
    // STEP 3: Update local player only
    if (ctx.myPlayerIndex >= 0 && 
        ctx.players[ctx.myPlayerIndex].active && 
        ctx.players[ctx.myPlayerIndex].snake)
    {
        int i = ctx.myPlayerIndex;
        
        // Remove my head from collision map temporarily (don't collide with own head position)
        Position oldHead = ctx.players[i].snake->getHead();
        int oldHeadKey = oldHead.y * GRID_WIDTH + oldHead.x;
        occupiedPositions.erase(oldHeadKey);
        
        ctx.players[i].snake->update();  // Move snake
        Position newHead = ctx.players[i].snake->getHead();
        int newHeadKey = newHead.y * GRID_WIDTH + newHead.x;
        
        // Send multiplayer update for local player (throttled)
        sendPlayerUpdate(ctx, i);
        
        // Check ALL collisions with O(1) map lookup!
        bool collision = false;
        
        // Boundary collision
        if (newHead.x < 0 || newHead.x >= GRID_WIDTH || 
            newHead.y < 0 || newHead.y >= GRID_HEIGHT) {
            collision = true;
        }
        // Collision with any snake (including self-collision)
        else if (occupiedPositions.count(newHeadKey) > 0) {
            collision = true;
        }
        
        if (collision) {
            respawnPlayer(i);
            std::cout << "Player " << (i+1) << " died and respawned!" << std::endl;
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
    state = GameState::PLAYING;
    ctx.winnerIndex = -1;
    matchStartTime = SDL_GetTicks();
    ctx.matchStartTime = matchStartTime;
    ctx.totalPausedTime = 0;
    ctx.pauseStartTime = 0;
    occupiedPositions.clear();
    food.spawn(occupiedPositions);
    updateInterval = INITIAL_SPEED;
    std::cout << "Game reset!" << std::endl;
}
