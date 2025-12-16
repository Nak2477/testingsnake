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
    ctx.players.myPlayerIndex = -1;
    ctx.food = &food;
    ctx.match.matchStartTime = 0;
    ctx.match.syncedElapsedMs = 0;
    ctx.match.winnerIndex = -1;
    ctx.match.totalPausedTime = 0;
    ctx.match.pauseStartTime = 0;
    
    // Set up type-safe callback for state changes from network
    ctx.onStateChange = [this](int newStateInt) {
        this->state = static_cast<GameState>(newStateInt);
    };
    
    // Initialize network manager
    networkManager = std::make_unique<NetworkManager>(&ctx);
    
    // Initialize SDL and rendering (throws on failure)
    ui = new MenuRender();
    gameRenderer = new GameRender(ui->getRenderer(), ui);
    
    // Initialize all player slots as inactive
    for (int i = 0; i < 4; i++) {
        ctx.players.players[i].active = false;
        ctx.players.players[i].clientId = "";
        ctx.players.players[i].snake = nullptr;
        ctx.players.players[i].paused = false;
    }
    
    // Reserve capacity for collision detection map
    occupiedPositions.reserve(400);  // 4 players * ~100 segments max
    
    // Spawn initial food
    food.spawn(occupiedPositions);
    
    // Initialize timing
    lastUpdate = SDL_GetTicks();
}

Game::~Game()
{
    // Network manager cleanup (unique_ptr handles destruction)
    networkManager.reset();
    
    // Cleanup renderers (MenuRender destructor handles SDL cleanup)
    delete ui;
    delete gameRenderer;
    
    // Print final score
    if (ctx.players.myPlayerIndex >= 0 && ctx.players.players[ctx.players.myPlayerIndex].snake) {
        std::cout << "Final score: " << ctx.players.players[ctx.players.myPlayerIndex].snake->getScore() << std::endl;
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
    // Process queued network messages (thread-safe)
    if (networkManager && networkManager->isConnected()) {
        networkManager->processMessages();
        
        // CRITICAL FIX: Periodic state sync from host
        if (networkManager->isHost()) {
            networkManager->sendPeriodicStateSync();  // Every 5 seconds
        }
    }
    
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
            if (ctx.players.myPlayerIndex >= 0 && 
                ctx.players.players[ctx.players.myPlayerIndex].active && 
                ctx.players.players[ctx.players.myPlayerIndex].snake) {
                networkManager->sendPlayerUpdate(ctx.players.myPlayerIndex);
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
            ui->renderLobby(ctx.players.players, networkManager->isHost());
            break;
            
        case GameState::COUNTDOWN:
            gameRenderer->renderGame(ctx, false);
            // ui->renderCountdown(countdownSeconds); // TODO: Need to track countdown timer
            break;
            
        case GameState::PLAYING:
            gameRenderer->renderGame(ctx, false);
            break;
            
        case GameState::PAUSED:
            gameRenderer->renderGame(ctx, false);
            ui->renderPauseMenu(pauseMenuSelection);
            break;
            
        case GameState::MATCH_END:
            gameRenderer->renderGame(ctx, true);
            ui->renderMatchEnd(ctx.match.winnerIndex, ctx.players.players);
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
            if (ctx.match.pauseStartTime > 0) {
                ctx.match.totalPausedTime += (SDL_GetTicks() - ctx.match.pauseStartTime);
                ctx.match.pauseStartTime = 0;
            }
            if (ctx.players.myPlayerIndex >= 0) {
                ctx.players.players[ctx.players.myPlayerIndex].paused = false;
            }
            // Broadcast unpause if multiplayer
            if (networkManager->isConnected() && ctx.players.myPlayerIndex >= 0) {
                ctx.match.pausedByClientId = "";
                networkManager->sendPauseState(false, ctx.players.players[ctx.players.myPlayerIndex].clientId);
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
            ctx.match.pauseStartTime = SDL_GetTicks();
            if (ctx.players.myPlayerIndex >= 0) {
                ctx.players.players[ctx.players.myPlayerIndex].paused = true;
            }
            if (networkManager->isConnected() && ctx.players.myPlayerIndex >= 0) {
                ctx.match.pausedByClientId = ctx.players.players[ctx.players.myPlayerIndex].clientId;
                networkManager->sendPauseState(true, ctx.match.pausedByClientId);
            }
            inputHandler = &Game::handlePausedInput;
            break;
            
        case GameState::PLAYING:
            // Start match if coming from lobby (multiplayer)
            if (state == GameState::LOBBY) {
                // DON'T reset positions - they're already set when players joined
                // Just initialize timing
                if (networkManager->isHost()) {
                    // Host is authoritative for match timing
                    ctx.match.matchStartTime = SDL_GetTicks();
                    ctx.match.syncedElapsedMs = 0;
                    ctx.match.totalPausedTime = 0;
                    ctx.match.pauseStartTime = 0;
                    
                    // Spawn food for multiplayer match
                    buildCollisionMap();
                    food.spawn(occupiedPositions);
                    
                    // Broadcast game start with food position
                    if (networkManager->isConnected()) {
                        JsonPtr startUpdate(json_object());
                        json_object_set_new(startUpdate.get(), "type", json_string("state_sync"));
                        json_object_set_new(startUpdate.get(), "gameState", json_string("PLAYING"));
                        json_object_set_new(startUpdate.get(), "matchStartTime", json_integer(ctx.match.matchStartTime));
                        json_object_set_new(startUpdate.get(), "elapsedMs", json_integer(0));
                        json_object_set_new(startUpdate.get(), "totalPausedTime", json_integer(0));
                        json_object_set_new(startUpdate.get(), "foodX", json_integer(food.getPosition().x));
                        json_object_set_new(startUpdate.get(), "foodY", json_integer(food.getPosition().y));
                        networkManager->sendGameMessage(startUpdate.get());
                    }
                } else {
                    // Client initializes to 0, will be synced by host
                    ctx.match.syncedElapsedMs = 0;
                    ctx.match.totalPausedTime = 0;
                    ctx.match.pauseStartTime = 0;
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
                ctx.players.players[0].snake = std::make_unique<Snake>(SDL_Color{0, 255, 0, 255}, startPos);
                ctx.players.players[0].active = true;
                ctx.players.players[0].clientId = "local_player";
                ctx.players.myPlayerIndex = 0;
                ctx.match.matchStartTime = SDL_GetTicks();
                changeState(GameState::PLAYING);
                std::cout << "Started singleplayer mode" << std::endl;
            }
            else if (menuSelection == 1)
            {  // Multiplayer
                if (networkManager->initialize("kontoret.onvo.se", 9001))
                {
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
            if (networkManager->isConnected() && !networkManager->isInSession()) {
                if (networkManager->hostSession()) {
                    state = GameState::LOBBY;
                    inputHandler = &Game::handleLobbyInput;
                }
            }
            break;
        case SDLK_l:
            if (networkManager->isConnected() && !networkManager->isInSession()) {
                networkManager->listSessions();
            }
            break;
        case SDLK_1:
        case SDLK_2:
        case SDLK_3:
        case SDLK_4:
            if (networkManager->isConnected() && !networkManager->isInSession() && !networkManager->getAvailableSessions().empty())
            {
                int idx = key - SDLK_1;
                const auto& sessions = networkManager->getAvailableSessions();
                if (idx < (int)sessions.size())
                {
                    std::cout << "Joining session: " << sessions[idx] << std::endl;
                    if (networkManager->joinSession(sessions[idx])) {
                        state = GameState::LOBBY;
                        inputHandler = &Game::handleLobbyInput;
                    }
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
            if (networkManager->isHost()) {
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
    Snake* mySnake = (ctx.players.myPlayerIndex >= 0 && ctx.players.players[ctx.players.myPlayerIndex].snake) 
                      ? ctx.players.players[ctx.players.myPlayerIndex].snake.get() 
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
                    if (networkManager->isHost()) {
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
    
    // Only host tracks and broadcasts match time
    if (networkManager->isHost()) {
        // Calculate elapsed time, subtracting paused time
        Uint32 currentPausedTime = ctx.match.totalPausedTime;
        if (state == GameState::PAUSED && ctx.match.pauseStartTime > 0) {
            currentPausedTime += (currentTime - ctx.match.pauseStartTime);
        }
        
        Uint32 elapsedMs = currentTime - ctx.match.matchStartTime - currentPausedTime;
        ctx.match.syncedElapsedMs = elapsedMs;
        Uint32 elapsedSeconds = elapsedMs / 1000;
        
        // Broadcast timer update every second
        static Uint32 lastTimerBroadcast = 0;
        if (networkManager->isConnected() && (currentTime - lastTimerBroadcast >= 1000)) {
            JsonPtr timerUpdate(json_object());
            json_object_set_new(timerUpdate.get(), "type", json_string("time_sync"));
            json_object_set_new(timerUpdate.get(), "elapsedMs", json_integer(elapsedMs));
            json_object_set_new(timerUpdate.get(), "totalPausedTime", json_integer(ctx.match.totalPausedTime));
            networkManager->sendGameMessage(timerUpdate.get());
            lastTimerBroadcast = currentTime;
        }
        
        // Check for match end
        if (elapsedSeconds >= MATCH_DURATION_SECONDS && networkManager->isConnected()) {
            // Broadcast match end to all clients
            JsonPtr endUpdate(json_object());
            json_object_set_new(endUpdate.get(), "type", json_string("state_sync"));
            json_object_set_new(endUpdate.get(), "gameState", json_string("MATCH_END"));
            networkManager->sendGameMessage(endUpdate.get());
            
            state = GameState::MATCH_END;
            
            // Find winner - longest snake
            int maxLength = 0;
            ctx.match.winnerIndex = -1;
            std::vector<int> tiedPlayers;
            
            for (int i = 0; i < 4; i++)
            {
                if (!ctx.players.players[i].active || !ctx.players.players[i].snake)
                    continue;

                int length = ctx.players.players[i].snake->getBody().size();
                
                if (length > maxLength) {
                    maxLength = length;
                    ctx.match.winnerIndex = i;
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
                ctx.match.winnerIndex = -1;
                for (int idx : tiedPlayers)
                {
                    if (ctx.players.players[idx].snake->getScore() > maxScore)
                    {
                        maxScore = ctx.players.players[idx].snake->getScore();
                        ctx.match.winnerIndex = idx;
                    }
                }
            }
            
            std::cout << "Match ended! ";
            if (ctx.match.winnerIndex >= 0)
            {
                std::cout << "Winner: Player " << (ctx.match.winnerIndex + 1) 
                         << " (Length: " << ctx.players.players[ctx.match.winnerIndex].snake->getBody().size()
                         << ", Score: " << ctx.players.players[ctx.match.winnerIndex].snake->getScore() << ")" << std::endl;
            } else {
                std::cout << "No winner (no active players)" << std::endl;
            }
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
        if (!isPlayerValid(i) || !ctx.players.players[i].snake->isAlive())
            continue;
        
        if (ctx.players.players[i].snake->getHead() == food.getPosition())
        {
            ctx.players.players[i].snake->grow();
            foodEaten = true;
            
            // Only host spawns new food and broadcasts it
            if (networkManager->isHost()) {
                buildCollisionMap();  // Rebuild with grown snake
                food.spawn(occupiedPositions);
                
                // Broadcast new food position
                JsonPtr foodUpdate(json_object());
                json_object_set_new(foodUpdate.get(), "type", json_string("state_sync"));
                json_object_set_new(foodUpdate.get(), "foodX", json_integer(food.getPosition().x));
                json_object_set_new(foodUpdate.get(), "foodY", json_integer(food.getPosition().y));
                json_object_set_new(foodUpdate.get(), "matchStartTime", json_integer(ctx.match.matchStartTime));
                networkManager->sendGameMessage(foodUpdate.get());
            }
        }
    }
    
    // STEP 3: Update local player only
    if (isPlayerValid(ctx.players.myPlayerIndex))
    {
        int i = ctx.players.myPlayerIndex;
        if (foodEaten) buildCollisionMap();  // Rebuild if food was eaten by someone
        
        // Remove my head from collision map temporarily (don't collide with own head position)
        Position oldHead = ctx.players.players[i].snake->getHead();
        int oldHeadKey = oldHead.y * GRID_WIDTH + oldHead.x;
        occupiedPositions.erase(oldHeadKey);
        
        ctx.players.players[i].snake->update();  // Move snake
        Position newHead = ctx.players.players[i].snake->getHead();
        int newHeadKey = newHead.y * GRID_WIDTH + newHead.x;
        
        // Send multiplayer update for local player
        networkManager->sendPlayerUpdate(i);
        
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
    ctx.players.players[playerIndex].snake->reset(randomPos);
    
    // Send multiplayer update for local player respawn
    if (playerIndex == ctx.players.myPlayerIndex) {
        networkManager->sendPlayerUpdate(playerIndex);
    }
}

void Game::resetMatch()
{
    // Reset all active players to their original spawn positions
    // Spawn positions match the formula used in multiplayer:
    // Player i: {GRID_WIDTH/4 + (i%2)*(GRID_WIDTH/2), GRID_HEIGHT/4 + (i/2)*(GRID_HEIGHT/2)}
    for (int i = 0; i < 4; i++)
    {
        if (ctx.players.players[i].active && ctx.players.players[i].snake) {
            Position spawnPos = {GRID_WIDTH/4 + (i%2)*(GRID_WIDTH/2), 
                                GRID_HEIGHT/4 + (i/2)*(GRID_HEIGHT/2)};
            ctx.players.players[i].snake->reset(spawnPos);
            ctx.players.players[i].snake->setScore(0); // Reset score to 0 for clean restart
        }
    }
    state = GameState::PLAYING;
    ctx.match.winnerIndex = -1;
    ctx.match.matchStartTime = SDL_GetTicks();
    ctx.match.totalPausedTime = 0;
    ctx.match.pauseStartTime = 0;
    occupiedPositions.clear();
    food.spawn(occupiedPositions);
    updateInterval = INITIAL_SPEED;
    std::cout << "Game reset!" << std::endl;
}

bool Game::canUnpause() const
{
    if (!networkManager->isConnected()) return true;
    if (ctx.players.myPlayerIndex < 0) return false;
    return ctx.match.pausedByClientId.empty() || 
           ctx.match.pausedByClientId == ctx.players.players[ctx.players.myPlayerIndex].clientId;
}

void Game::buildCollisionMap()
{
    occupiedPositions.clear();
    for (int k = 0; k < 4; k++) {
        if (isPlayerValid(k)) {
            for (const auto& segment : ctx.players.players[k].snake->getBody()) {
                occupiedPositions[segment.y * GRID_WIDTH + segment.x] = true;
            }
        }
    }
}

void Game::resetGameState()
{
    // Cleanup multiplayer
    if (networkManager) {
        networkManager->shutdown();
    }
    
    // Reset all players
    for (int i = 0; i < 4; i++) {
        ctx.players.players[i].active = false;
        ctx.players.players[i].snake = nullptr;
    }
    
    // Reset game state
    ctx.players.myPlayerIndex = -1;
    ctx.match.winnerIndex = -1;
    ctx.match.totalPausedTime = 0;
    ctx.match.pauseStartTime = 0;
    ctx.match.matchStartTime = 0;
}