#include "game.h"
#include <iostream>
#include <ctime>

Game::Game() 
    : ui(nullptr), gameRenderer(nullptr),
      state(GameState::MENU), quit(false),
      updateInterval(INITIAL_SPEED), menuSelection(0), pauseMenuSelection(0),
      inputHandler(&Game::handleMenuInput)
{
    // Initialize game context
    ctx.myPlayerIndex = -1;
    ctx.api = nullptr;
    ctx.food = &food;
    ctx.isHost = false;
    ctx.matchStartTime = 0;
    ctx.syncedElapsedMs = 0;
    ctx.winnerIndex = -1;
    ctx.totalPausedTime = 0;
    ctx.pauseStartTime = 0;
    ctx.gameStatePtr = &state;
    
    // Initialize SDL and rendering (throws on failure)
    ui = new MenuRender();
    gameRenderer = new GameRender(ui->getRenderer(), ui);
    
    // Initialize all player slots as inactive
    for (int i = 0; i < 4; i++) {
        ctx.players[i].active = false;
        ctx.players[i].clientId = "";
        ctx.players[i].snake = nullptr;
        ctx.players[i].paused = false;
    }
    
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
    if (ctx.api)
        mp_api_destroy(ctx.api);
    
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
            // Call through function pointer
            if (inputHandler) {
                (this->*inputHandler)(e.key.keysym.sym);
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
            ui->renderPauseMenu(pauseMenuSelection);
            break;
            
        case GameState::MATCH_END:
            gameRenderer->renderGame(ctx, true, matchStartTime);
            ui->renderMatchEnd(ctx.winnerIndex, ctx.players);
            break;
    }

    gameRenderer->present();
}

void Game::changeState(GameState newState)
{
    exitState(state);
    enterState(newState);
    state = newState;
}

void Game::exitState(GameState oldState)
{
    switch (oldState) {
        case GameState::PAUSED:
            // Finalize pause timing
            if (ctx.pauseStartTime > 0) {
                ctx.totalPausedTime += (SDL_GetTicks() - ctx.pauseStartTime);
                ctx.pauseStartTime = 0;
            }
            if (ctx.myPlayerIndex >= 0) {
                ctx.players[ctx.myPlayerIndex].paused = false;
            }
            // Broadcast unpause if multiplayer
            if (ctx.api && ctx.myPlayerIndex >= 0) {
                ctx.pausedByClientId = "";
                sendGlobalPauseState(ctx, false, ctx.players[ctx.myPlayerIndex].clientId);
            }
            break;
            
        default:
            break;
    }
}

void Game::enterState(GameState newState)
{
    switch (newState) {
        case GameState::MENU:
            // Reset everything
            resetGameState();
            inputHandler = &Game::handleMenuInput;
            break;
            
        case GameState::MULTIPLAYER:
            inputHandler = &Game::handleMultiplayerInput;
            break;
            
        case GameState::LOBBY:
            inputHandler = &Game::handleLobbyInput;
            break;
            
        case GameState::PAUSED:
            pauseMenuSelection = 0;
            ctx.pauseStartTime = SDL_GetTicks();
            if (ctx.myPlayerIndex >= 0) {
                ctx.players[ctx.myPlayerIndex].paused = true;
            }
            if (ctx.api && ctx.myPlayerIndex >= 0) {
                ctx.pausedByClientId = ctx.players[ctx.myPlayerIndex].clientId;
                sendGlobalPauseState(ctx, true, ctx.pausedByClientId);
            }
            inputHandler = &Game::handlePausedInput;
            break;
            
        case GameState::PLAYING:
            // Start match if coming from lobby (multiplayer)
            if (state == GameState::LOBBY) {
                // DON'T reset positions - they're already set when players joined
                // Just initialize timing
                if (ctx.isHost) {
                    matchStartTime = SDL_GetTicks();
                    ctx.matchStartTime = matchStartTime;
                    ctx.syncedElapsedMs = 0;
                    
                    // Spawn food for multiplayer match
                    buildCollisionMap();
                    food.spawn(occupiedPositions);
                    
                    // Broadcast game start with food position
                    if (ctx.api) {
                        json_t *startUpdate = json_object();
                        json_object_set_new(startUpdate, "type", json_string("state_sync"));
                        json_object_set_new(startUpdate, "gameState", json_string("PLAYING"));
                        json_object_set_new(startUpdate, "matchStartTime", json_integer(ctx.matchStartTime));
                        json_object_set_new(startUpdate, "foodX", json_integer(food.getPosition().x));
                        json_object_set_new(startUpdate, "foodY", json_integer(food.getPosition().y));
                        mp_api_game(ctx.api, startUpdate);
                        json_decref(startUpdate);
                    }
                } else {
                    // Client initializes to 0, will be synced by host
                    ctx.syncedElapsedMs = 0;
                }
            }
            inputHandler = &Game::handlePlayingInput;
            break;
            
        case GameState::MATCH_END:
            inputHandler = &Game::handleMatchEndInput;
            break;
            
        default:
            inputHandler = nullptr;
            break;
    }
}

void Game::handleMenuInput(SDL_Keycode key)
{
    switch (key)
    {
        case SDLK_UP:
            menuSelection = (menuSelection - 1 + 3) % 3;
            break;
        case SDLK_DOWN:
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
                matchStartTime = SDL_GetTicks();
                ctx.matchStartTime = matchStartTime;
                changeState(GameState::PLAYING);
                std::cout << "Started singleplayer mode" << std::endl;
            }
            else if (menuSelection == 1)
            {  // Multiplayer
                ctx.api = mp_api_create("kontoret.onvo.se", 9001);
                if (ctx.api)
                {
                    mp_api_listen(ctx.api, on_multiplayer_event, &ctx);
                    state = GameState::MULTIPLAYER;
                    inputHandler = &Game::handleMultiplayerInput;
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
                inputHandler = &Game::handleLobbyInput;
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
                    inputHandler = &Game::handleLobbyInput;
                }
            }
            break;
        case SDLK_ESCAPE:
            changeState(GameState::MENU);
            break;
    }
}

void Game::handleLobbyInput(SDL_Keycode key)
{
    switch (key)
    {
        case SDLK_SPACE:
            if (ctx.isHost) {
                changeState(GameState::PLAYING);
            }
            break;
        case SDLK_ESCAPE:
            changeState(GameState::MENU);
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
            mySnake->setDirection(Direction::UP);
            break;
        case SDLK_DOWN:
            mySnake->setDirection(Direction::DOWN);
            break;
        case SDLK_LEFT:
            mySnake->setDirection(Direction::LEFT);
            break;
        case SDLK_RIGHT:
            mySnake->setDirection(Direction::RIGHT);
            break;
        case SDLK_p:
        case SDLK_ESCAPE:
            changeState(GameState::PAUSED);
            break;
    }
}

void Game::handlePausedInput(SDL_Keycode key)
{
    switch (key)
    {
        case SDLK_UP:
            pauseMenuSelection = (pauseMenuSelection - 1 + 3) % 3;
            break;
        case SDLK_DOWN:
            pauseMenuSelection = (pauseMenuSelection + 1) % 3;
            break;
        case SDLK_RETURN:
        case SDLK_SPACE:
            if (!canUnpause()) break;
            // Execute selected option
            switch (pauseMenuSelection)
            {
                case 0: // Resume
                    changeState(GameState::PLAYING);
                    break;
                case 1: // Restart
                    if (ctx.isHost) {
                        resetMatch();
                    }
                    break;
                case 2: // Menu
                    changeState(GameState::MENU);
                    break;
            }
            break;
        case SDLK_ESCAPE:
        case SDLK_p:
            if (!canUnpause()) break;
            changeState(GameState::PLAYING);
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
            changeState(GameState::MENU);
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
        currentPausedTime += (currentTime - ctx.pauseStartTime);
    }
    
    Uint32 elapsedSeconds = (currentTime - matchStart - currentPausedTime) / 1000;
    
    // Only host checks for match end and broadcasts it
    if (elapsedSeconds >= MATCH_DURATION_SECONDS && ctx.isHost && ctx.api) {
        // Broadcast match end to all clients
        json_t *endUpdate = json_object();
        json_object_set_new(endUpdate, "type", json_string("state_sync"));
        json_object_set_new(endUpdate, "gameState", json_string("MATCH_END"));
        mp_api_game(ctx.api, endUpdate);
        json_decref(endUpdate);
        
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
    // STEP 1: Build collision map from ALL snakes
    buildCollisionMap();
    
    // STEP 2: Check food collision for ALL players
    bool foodEaten = false;
    for (int i = 0; i < 4; i++)
    {
        if (!isPlayerValid(i) || !ctx.players[i].snake->isAlive())
            continue;
        
        if (ctx.players[i].snake->getHead() == food.getPosition())
        {
            ctx.players[i].snake->grow();
            foodEaten = true;
            
            // Only host spawns new food and broadcasts it
            if (ctx.isHost) {
                buildCollisionMap();  // Rebuild with grown snake
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
        }
    }
    
    // STEP 3: Update local player only
    if (isPlayerValid(ctx.myPlayerIndex))
    {
        int i = ctx.myPlayerIndex;
        if (foodEaten) buildCollisionMap();  // Rebuild if food was eaten by someone
        
        // Remove my head from collision map temporarily (don't collide with own head position)
        Position oldHead = ctx.players[i].snake->getHead();
        int oldHeadKey = oldHead.y * GRID_WIDTH + oldHead.x;
        occupiedPositions.erase(oldHeadKey);
        
        ctx.players[i].snake->update();  // Move snake
        Position newHead = ctx.players[i].snake->getHead();
        int newHeadKey = newHead.y * GRID_WIDTH + newHead.x;
        
        // Send multiplayer update for local player
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
            ctx.players[i].snake->setScore(0); // Reset score to 0 for clean restart
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

bool Game::canUnpause() const
{
    if (!ctx.api) return true;
    if (ctx.myPlayerIndex < 0) return false;
    return ctx.pausedByClientId.empty() || 
           ctx.pausedByClientId == ctx.players[ctx.myPlayerIndex].clientId;
}

void Game::buildCollisionMap()
{
    occupiedPositions.clear();
    for (int k = 0; k < 4; k++) {
        if (isPlayerValid(k)) {
            for (const auto& segment : ctx.players[k].snake->getBody()) {
                occupiedPositions[segment.y * GRID_WIDTH + segment.x] = true;
            }
        }
    }
}

void Game::resetGameState()
{
    // Cleanup multiplayer API
    if (ctx.api) {
        mp_api_destroy(ctx.api);
        ctx.api = nullptr;
        ctx.sessionId.clear();
    }
    
    // Reset all players
    for (int i = 0; i < 4; i++) {
        ctx.players[i].active = false;
        ctx.players[i].snake = nullptr;
    }
    
    // Reset game state
    ctx.myPlayerIndex = -1;
    ctx.winnerIndex = -1;
    ctx.totalPausedTime = 0;
    ctx.pauseStartTime = 0;
    matchStartTime = 0;
    ctx.matchStartTime = 0;
}