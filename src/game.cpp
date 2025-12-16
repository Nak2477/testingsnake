#include "game.h"
#include <iostream>
#include <ctime>

Game::Game() 
    : ui(nullptr), gameRenderer(nullptr),
      state(GameState::MENU), quit(false),
      updateInterval(INITIAL_SPEED), menuSelection(0), pauseMenuSelection(0),
      sessionSelection(0), inputHandler(&Game::handleMenuInput)
{
    // Initialize game context
    ctx.players.setMyPlayerIndex(-1);
    ctx.food = &food;
    ctx.match.matchStartTime = 0;
    ctx.match.syncedElapsedMs = 0;
    ctx.match.winnerIndex = -1;
    ctx.match.totalPausedTime = 0;
    ctx.match.pauseStartTime = 0;
    
    // Set up type-safe callback for state changes from network
    ctx.onStateChange = [this](int newStateInt) {
        GameState newState = static_cast<GameState>(newStateInt);
        // Use changeState to properly handle enter/exit transitions
        if (this->state != newState) {
            this->changeState(newState);
        }
    };
    
    // Initialize network manager
    networkManager = std::make_unique<NetworkManager>(&ctx);
    
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
    if (ctx.players.hasMe() && ctx.players.me().snake) {
        std::cout << "Final score: " << ctx.players.me().snake->getScore() << std::endl;
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
        }
        // Note: Paused state doesn't send updates - relies on periodic state sync from host
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
            // Transition state - render menu
            gameRenderer->clearScreen();
            ui->renderMenu(menuSelection);
            break;
            
        case GameState::MULTIPLAYER:
            gameRenderer->clearScreen();
            ui->renderSessionBrowser(
                networkManager->getAvailableSessions(), 
                sessionSelection,
                networkManager->isConnected()
            );
            break;
            
        case GameState::LOBBY:
            gameRenderer->clearScreen();
            ui->renderLobby(ctx.players.getSlots(), networkManager->isHost());
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
            ui->renderMatchEnd(ctx.match.winnerIndex, ctx.players.getSlots());
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
            if (ctx.players.hasMe()) {
                ctx.players.me().paused = false;
            }
            // Broadcast unpause if multiplayer
            if (networkManager->isConnected() && ctx.players.hasMe()) {
                ctx.match.pausedByClientId = "";
                networkManager->sendPauseState(false, ctx.players.me().clientId);
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
            if (ctx.players.hasMe()) {
                ctx.players.me().paused = true;
            }
            if (networkManager->isConnected() && ctx.players.hasMe()) {
                ctx.match.pausedByClientId = ctx.players.me().clientId;
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
                ctx.players[0].snake = std::make_unique<Snake>(SDL_Color{0, 255, 0, 255}, startPos);
                ctx.players[0].active = true;
                ctx.players[0].clientId = "local_player";
                ctx.players.setMyPlayerIndex(0);
                ctx.match.matchStartTime = SDL_GetTicks();
                ctx.match.syncedElapsedMs = 0;
                ctx.match.totalPausedTime = 0;
                ctx.match.pauseStartTime = 0;
                
                // Spawn food for singleplayer
                buildCollisionMap();
                food.spawn(occupiedPositions);
                
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
                sessionSelection = 0;  // Reset selection when listing
                networkManager->listSessions();
            }
            break;
        case SDLK_UP:
            if (!networkManager->getAvailableSessions().empty()) {
                sessionSelection = (sessionSelection - 1 + networkManager->getAvailableSessions().size()) 
                                  % networkManager->getAvailableSessions().size();
            }
            break;
        case SDLK_DOWN:
            if (!networkManager->getAvailableSessions().empty()) {
                sessionSelection = (sessionSelection + 1) % networkManager->getAvailableSessions().size();
            }
            break;
        case SDLK_RETURN:
            if (networkManager->isConnected() && !networkManager->isInSession() && 
                !networkManager->getAvailableSessions().empty())
            {
                const auto& sessions = networkManager->getAvailableSessions();
                if (sessionSelection < (int)sessions.size())
                {
                    std::cout << "Joining session: " << sessions[sessionSelection] << std::endl;
                    if (networkManager->joinSession(sessions[sessionSelection])) {
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
    Snake* mySnake = (ctx.players.hasMe() && ctx.players.me().snake) 
                      ? ctx.players.me().snake.get() 
                      : nullptr;
    
    if (!mySnake)
        return;
    
    switch (key)
    {
        case SDLK_UP:
            mySnake->setDirection(Direction::UP);
            // Throttle direction changes to 60 Hz max (prevents network spam)
            if (networkManager->isConnected()) {
                Uint32 currentTime = SDL_GetTicks();
                if (currentTime - ctx.players.me().lastMpSent >= DIRECTION_CHANGE_THROTTLE_MS) {
                    networkManager->sendPlayerUpdate(ctx.players.myPlayerIndex());
                }
            }
            break;
        case SDLK_DOWN:
            mySnake->setDirection(Direction::DOWN);
            if (networkManager->isConnected()) {
                Uint32 currentTime = SDL_GetTicks();
                if (currentTime - ctx.players.me().lastMpSent >= DIRECTION_CHANGE_THROTTLE_MS) {
                    networkManager->sendPlayerUpdate(ctx.players.myPlayerIndex());
                }
            }
            break;
        case SDLK_LEFT:
            mySnake->setDirection(Direction::LEFT);
            if (networkManager->isConnected()) {
                Uint32 currentTime = SDL_GetTicks();
                if (currentTime - ctx.players.me().lastMpSent >= DIRECTION_CHANGE_THROTTLE_MS) {
                    networkManager->sendPlayerUpdate(ctx.players.myPlayerIndex());
                }
            }
            break;
        case SDLK_RIGHT:
            mySnake->setDirection(Direction::RIGHT);
            if (networkManager->isConnected()) {
                Uint32 currentTime = SDL_GetTicks();
                if (currentTime - ctx.players.me().lastMpSent >= DIRECTION_CHANGE_THROTTLE_MS) {
                    networkManager->sendPlayerUpdate(ctx.players.myPlayerIndex());
                }
            }
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
                    if (!networkManager->isConnected() ||networkManager->isHost()) {
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
    
    // Singleplayer or multiplayer host: calculate timer locally
    // Multiplayer clients: receive timer via time_sync messages (don't calculate)
    if (!networkManager->isConnected() || networkManager->isHost()) {
        // Calculate elapsed time, subtracting paused time
        Uint32 currentPausedTime = ctx.match.totalPausedTime;
        if (state == GameState::PAUSED && ctx.match.pauseStartTime > 0) {
            currentPausedTime += (currentTime - ctx.match.pauseStartTime);
        }
        
        Uint32 elapsedMs = currentTime - ctx.match.matchStartTime - currentPausedTime;
        ctx.match.syncedElapsedMs = elapsedMs;
        Uint32 elapsedSeconds = elapsedMs / 1000;
        
        // Broadcast timer update if multiplayer host
        if (networkManager->isHost() && networkManager->isConnected()) {
            static Uint32 lastTimerBroadcast = 0;
            if (currentTime - lastTimerBroadcast >= 1000) {
                JsonPtr timerUpdate(json_object());
                json_object_set_new(timerUpdate.get(), "type", json_string("time_sync"));
                json_object_set_new(timerUpdate.get(), "elapsedMs", json_integer(elapsedMs));
                json_object_set_new(timerUpdate.get(), "totalPausedTime", json_integer(ctx.match.totalPausedTime));
                networkManager->sendGameMessage(timerUpdate.get());
                lastTimerBroadcast = currentTime;
            }
        }
        
        // Check for match end (singleplayer or host)
        if (elapsedSeconds >= MATCH_DURATION_SECONDS) {
            // Broadcast match end to clients if multiplayer host
            if (networkManager->isHost() && networkManager->isConnected()) {
                JsonPtr endUpdate(json_object());
                json_object_set_new(endUpdate.get(), "type", json_string("state_sync"));
                json_object_set_new(endUpdate.get(), "gameState", json_string("MATCH_END"));
                networkManager->sendGameMessage(endUpdate.get());
            }
            
            state = GameState::MATCH_END;
            
            // Find winner - longest snake
            int maxLength = 0;
            ctx.match.winnerIndex = -1;
            std::vector<int> tiedPlayers;
            
            for (int i = 0; i < 4; i++)
            {
                if (!ctx.players[i].active || !ctx.players[i].snake)
                    continue;

                int length = ctx.players[i].snake->getBody().size();
                
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
                    if (ctx.players[idx].snake->getScore() > maxScore)
                    {
                        maxScore = ctx.players[idx].snake->getScore();
                        ctx.match.winnerIndex = idx;
                    }
                }
            }
            
            std::cout << "Match ended! ";
            if (ctx.match.winnerIndex >= 0)
            {
                std::cout << "Winner: Player " << (ctx.match.winnerIndex + 1) 
                         << " (Length: " << ctx.players[ctx.match.winnerIndex].snake->getBody().size()
                         << ", Score: " << ctx.players[ctx.match.winnerIndex].snake->getScore() << ")" << std::endl;
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
        if (!isPlayerValid(i) || !ctx.players[i].snake->isAlive())
            continue;
        
        if (ctx.players[i].snake->getHead() == food.getPosition())
        {
            ctx.players[i].snake->grow();
            foodEaten = true;
            
            // Send immediate update if this is the local player (show growth)
            if (i == ctx.players.myPlayerIndex()) {
                networkManager->sendPlayerUpdate(i);
            }
            
            // Spawn new food (host spawns and broadcasts, singleplayer just spawns)
            buildCollisionMap();  // Rebuild with grown snake
            food.spawn(occupiedPositions);
            
            // Broadcast new food position if multiplayer host
            if (networkManager->isHost()) {
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
    if (isPlayerValid(ctx.players.myPlayerIndex()))
    {
        int i = ctx.players.myPlayerIndex();
        if (foodEaten) buildCollisionMap();  // Rebuild if food was eaten by someone
        
        // Remove my head from collision map temporarily (don't collide with own head position)
        Position oldHead = ctx.players[i].snake->getHead();
        int oldHeadKey = oldHead.y * GRID_WIDTH + oldHead.x;
        occupiedPositions.erase(oldHeadKey);
        
        ctx.players[i].snake->update();  // Move snake
        Position newHead = ctx.players[i].snake->getHead();
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
    // Find collision-free position (similar to food spawning logic)
    Position randomPos;
    const int MAX_ATTEMPTS = 1000;
    int attempts = 0;
    
    // Rebuild collision map to check for safe spawn
    buildCollisionMap();
    
    do {
        randomPos.x = rand() % GRID_WIDTH;
        randomPos.y = rand() % GRID_HEIGHT;
        int key = randomPos.y * GRID_WIDTH + randomPos.x;
        
        // Check if position is free
        if (occupiedPositions.count(key) == 0) {
            break;
        }
        attempts++;
    } while (attempts < MAX_ATTEMPTS);
    
    ctx.players[playerIndex].snake->reset(randomPos);
    
    // Send multiplayer update for local player respawn
    if (playerIndex == ctx.players.myPlayerIndex()) {
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
        if (ctx.players[i].active && ctx.players[i].snake) {
            Position spawnPos = {GRID_WIDTH/4 + (i%2)*(GRID_WIDTH/2), 
                                GRID_HEIGHT/4 + (i/2)*(GRID_HEIGHT/2)};
            ctx.players[i].snake->reset(spawnPos);
            ctx.players[i].snake->setScore(0); // Reset score to 0 for clean restart
        }
    }
    
    ctx.match.winnerIndex = -1;
    ctx.match.matchStartTime = SDL_GetTicks();
    ctx.match.totalPausedTime = 0;
    ctx.match.pauseStartTime = 0;
    ctx.match.syncedElapsedMs = 0;
    buildCollisionMap();  // Rebuild collision map with reset snake positions
    food.spawn(occupiedPositions);
    updateInterval = INITIAL_SPEED;
    
    // Use changeState to properly set input handler
    changeState(GameState::PLAYING);
    
    std::cout << "Game reset!" << std::endl;
}

bool Game::canUnpause() const
{
    // Allow anyone to pause/unpause in multiplayer
    // Host's authoritative timing keeps everything in sync
    return true;
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
    // Cleanup multiplayer
    if (networkManager) {
        networkManager->shutdown();
    }
    
    // Reset all players
    for (int i = 0; i < 4; i++) {
        ctx.players[i].active = false;
        ctx.players[i].snake = nullptr;
    }
    
    // Reset game state
    ctx.players.setMyPlayerIndex(-1);
    ctx.match.winnerIndex = -1;
    ctx.match.totalPausedTime = 0;
    ctx.match.pauseStartTime = 0;
    ctx.match.matchStartTime = 0;
}