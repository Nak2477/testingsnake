#include "game.h"
#include <iostream>
#include <ctime>

Game::Game() 
    : ui(nullptr),
      state(GameState::MENU), quit(false),
      updateInterval(Config::Game::INITIAL_SPEED_MS), menuSelection(0), pauseMenuSelection(0),
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
        // Pass fromNetwork=true to prevent sending pause messages back
        if (this->state != newState) {
            this->changeState(newState, true);
        }
    };
    
    // Initialize network manager
    networkManager = std::make_unique<NetworkManager>(&ctx);
    
    // Initialize SDL and rendering (throws on failure)
    ui = new MenuRender();
    
    // Initialize all player slots as inactive
    for (int i = 0; i < 4; i++) {
        ctx.players[i].active = false;
        ctx.players[i].clientId = "";
        ctx.players[i].snake = nullptr;
        ctx.players[i].paused = false;
    }
    
    // Reserve capacity for collision detection map
    occupiedPositions.reserve(Config::Performance::COLLISION_MAP_RESERVE_SIZE);
    
    // Spawn initial food
    food.spawn(occupiedPositions);
    
    // Initialize timing
    lastUpdate = SDL_GetTicks();
}

Game::~Game()
{
    // Network manager cleanup (unique_ptr handles destruction)
    networkManager.reset();
    
    // Cleanup renderer (MenuRender destructor handles SDL cleanup)
    delete ui;
    
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
            ui->clearScreen();
            ui->renderMenu(menuSelection);
            break;
            
        case GameState::SINGLEPLAYER:
            // Transition state - render menu
            ui->clearScreen();
            ui->renderMenu(menuSelection);
            break;
            
        case GameState::MULTIPLAYER:
            ui->clearScreen();
            ui->renderSessionBrowser(
                networkManager->getAvailableSessions(), 
                sessionSelection,
                networkManager->isConnected()
            );
            break;
            
        case GameState::LOBBY:
            ui->clearScreen();
            ui->renderLobby(ctx.players.getSlots(), networkManager->isHost());
            break;
            
        case GameState::COUNTDOWN:
            ui->renderGame(ctx, false);
            // ui->renderCountdown(countdownSeconds); // TODO: Need to track countdown timer
            break;
            
        case GameState::PLAYING:
            ui->renderGame(ctx, false);
            break;
            
        case GameState::PAUSED:
            ui->renderGame(ctx, false);
            ui->renderPauseMenu(pauseMenuSelection);
            break;
            
        case GameState::MATCH_END:
            ui->renderGame(ctx, true);
            ui->renderMatchEnd(ctx.match.winnerIndex, ctx.players.getSlots());
            break;
    }

    ui->present();
}

void Game::changeState(GameState newState)
{
    changeState(newState, false);
}

void Game::changeState(GameState newState, bool fromNetwork)
{
    GameState oldState = state;
    exitState(oldState, fromNetwork);
    enterState(newState, fromNetwork);
    state = newState;
}

void Game::exitState(GameState oldState, bool fromNetwork)
{
    switch (oldState)
    {
        case GameState::PAUSED:

            if (ctx.match.pauseStartTime > 0)
            {
                ctx.match.totalPausedTime += (SDL_GetTicks() - ctx.match.pauseStartTime);
                ctx.match.pauseStartTime = 0;
            }
            if (ctx.players.hasMe())
            {
                ctx.players.me().paused = false;
            }

            // Only send unpause message if WE initiated the unpause (not from network sync)
            if (!fromNetwork && networkManager->isConnected() && ctx.players.hasMe()) {
                ctx.match.pausedByClientId = "";
                networkManager->sendPauseState(false, ctx.players.me().clientId);
            }
            break;
            
        default:
            break;
    }
}

void Game::enterState(GameState newState, bool fromNetwork)
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
            // Only send pause message if WE initiated the pause (not from network sync)
            if (!fromNetwork && networkManager->isConnected() && ctx.players.hasMe()) {
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
                        auto startUpdate = JsonBuilder()
                            .set("type", "state_sync")
                            .set("gameState", "PLAYING")
                            .set("matchStartTime", ctx.match.matchStartTime)
                            .set("elapsedMs", 0)
                            .set("totalPausedTime", 0)
                            .set("foodX", food.getPosition().x)
                            .set("foodY", food.getPosition().y)
                            .buildPtr();
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
                Position startPos = {Config::Grid::WIDTH / 2, Config::Grid::HEIGHT / 2};
                ctx.players[0].snake = std::make_unique<Snake>(Config::Render::PLAYER_COLORS[0], startPos);
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
                if (networkManager->initialize(Config::Network::DEFAULT_HOST, Config::Network::DEFAULT_PORT))
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

    Direction dir = Direction::NONE;
    switch (key)
    {
        case SDLK_UP:
        case SDLK_w:
            dir = Direction::UP;
            break;
        case SDLK_DOWN:
        case SDLK_s:
            dir = Direction::DOWN;
            break;
        case SDLK_LEFT:
        case SDLK_a:
            dir = Direction::LEFT;
            break;
        case SDLK_RIGHT:
        case SDLK_d:
            dir = Direction::RIGHT;
            break;
        case SDLK_p:
        case SDLK_ESCAPE:
            changeState(GameState::PAUSED);
            return;
        default:
            return;
    }

    // Apply direction locally (immediate response for host, prediction for clients)
    mySnake->setDirection(dir);
    
    // If multiplayer client, send input to host
    if (networkManager->isConnected() && !networkManager->isHost()) {
        networkManager->sendPlayerInput(dir);
    }
    // Host processes inputs immediately via setDirection above
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
                auto timerUpdate = JsonBuilder()
                    .set("type", "time_sync")
                    .set("elapsedMs", elapsedMs)
                    .set("totalPausedTime", ctx.match.totalPausedTime)
                    .buildPtr();
                networkManager->sendGameMessage(timerUpdate.get());
                lastTimerBroadcast = currentTime;
            }
        }
        
        // Check for match end (singleplayer or host)
        if (elapsedSeconds >= Config::Game::MATCH_DURATION_SECONDS) {
            // Broadcast match end to clients if multiplayer host
            if (networkManager->isHost() && networkManager->isConnected()) {
                auto endUpdate = JsonBuilder()
                    .set("type", "state_sync")
                    .set("gameState", "MATCH_END")
                    .buildPtr();
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
    if (networkManager->isHost() || !networkManager->isConnected())
    {
        if (state == GameState::PAUSED)
        {
            if (networkManager->isConnected())
            {
                networkManager->broadcastGameState();
            }
            return;
        }
        if (occupiedPositions.empty())
        {
            buildCollisionMap();
        }
        
        struct MoveInfo {           //NEEDED?
            Position oldHead;
            Position oldTail;   
            Position newHead;
            bool willGrow;
            bool collision;
            bool processed;
        };
        MoveInfo moves[4] = {};
        bool needRebuild = false;
        
        for (int i = 0; i < 4; i++)
        {
            moves[i].processed = false;
            if (!isPlayerValid(i) || !ctx.players[i].snake->isAlive())
                continue;
            moves[i].processed = true;
            
            const auto& body = ctx.players[i].snake->getBody();
            if (body.empty())
            {
                std::cerr << "ERROR: Player " << (i+1) << " has empty snake body!" << std::endl;
                moves[i].processed = false;
                continue;
            }
            
            moves[i].oldHead = ctx.players[i].snake->getHead();
            moves[i].oldTail = body.back();
            moves[i].willGrow = (moves[i].oldHead == food.getPosition());
            
            ctx.players[i].snake->update();
            moves[i].newHead = ctx.players[i].snake->getHead();
            
            // Check collisions against UNCHANGED map (all tails still present)
            moves[i].collision = false;
            
            // Boundary collision
            if (moves[i].newHead.x < 0 || moves[i].newHead.x >= Config::Grid::WIDTH || 
                moves[i].newHead.y < 0 || moves[i].newHead.y >= Config::Grid::HEIGHT) {
                moves[i].collision = true;
            }
            // Snake collision - check against original map state
            else {
                int newHeadKey = moves[i].newHead.y * Config::Grid::WIDTH + moves[i].newHead.x;
                
                // Check if new head hits any occupied cell in the collision map
                // The map contains ALL snake segments including all tails
                if (occupiedPositions.count(newHeadKey) > 0) {
                    // Hit something - but is it our own old head? That's OK (moving forward into where we were)
                    int oldHeadKey = moves[i].oldHead.y * Config::Grid::WIDTH + moves[i].oldHead.x;
                    if (newHeadKey != oldHeadKey) {
                        moves[i].collision = true;
                        std::cout << "Player " << (i+1) << " collision at (" 
                                  << moves[i].newHead.x << "," << moves[i].newHead.y << ")" << std::endl;
                    }
                }
            }
        }
        
        // Phase 2: Apply results and update collision map incrementally
        for (int i = 0; i < 4; i++) {
            if (!moves[i].processed)
                continue;
            
            if (moves[i].collision) {
                respawnPlayer(i);
                std::cout << "Player " << (i+1) << " died and respawned!" << std::endl;
                needRebuild = true;
            } else {
                // Update collision map incrementally
                int oldHeadKey = moves[i].oldHead.y * Config::Grid::WIDTH + moves[i].oldHead.x;
                int newHeadKey = moves[i].newHead.y * Config::Grid::WIDTH + moves[i].newHead.x;
                
                // Remove old head (it's now neck), add new head
                occupiedPositions.erase(oldHeadKey);
                occupiedPositions[newHeadKey] = true;
                
                // Handle tail
                if (!moves[i].willGrow) {
                    // Remove old tail (snake moved, tail advanced)
                    int oldTailKey = moves[i].oldTail.y * Config::Grid::WIDTH + moves[i].oldTail.x;
                    occupiedPositions.erase(oldTailKey);
                }
                else {
                    // Snake grew - tail stays
                    ctx.players[i].snake->grow();
                    food.spawn(occupiedPositions);
                    std::cout << "Player " << (i+1) << " ate food!" << std::endl;
                    
                    if (networkManager->isConnected()) {
                        networkManager->broadcastGameState();
                    }
                }
            }
        }
        if (needRebuild) {
            buildCollisionMap();
        }
        if (networkManager->isConnected()) {
            networkManager->broadcastGameState();
        }
    } else {
        // CLIENT: Do nothing - state updated by handleGameState()
        // Snakes already moved by host, positions set via network
    }
}

void Game::respawnPlayer(int playerIndex)
{
    Position randomPos;
    const int MAX_ATTEMPTS = Config::Game::MAX_FOOD_SPAWN_ATTEMPTS;
    int attempts = 0;
    
    buildCollisionMap();
    do {
        randomPos.x = rand() % Config::Grid::WIDTH;
        randomPos.y = rand() % Config::Grid::HEIGHT;
        int key = randomPos.y * Config::Grid::WIDTH + randomPos.x;
        
        if (occupiedPositions.count(key) == 0) {
            break;
        }
        attempts++;
    } while (attempts < MAX_ATTEMPTS);
    
    ctx.players[playerIndex].snake->reset(randomPos);
}

void Game::resetMatch()
{
    for (int i = 0; i < 4; i++)
    {
        if (ctx.players[i].active && ctx.players[i].snake)
        {
            Position spawnPos = {Config::Spawn::PLAYER_SPAWN_X[i], Config::Spawn::PLAYER_SPAWN_Y[i]};
            ctx.players[i].snake->reset(spawnPos);
            ctx.players[i].snake->setScore(0);
        }
    }
    
    ctx.match.winnerIndex = -1;
    ctx.match.matchStartTime = SDL_GetTicks();
    ctx.match.totalPausedTime = 0;
    ctx.match.pauseStartTime = 0;
    ctx.match.syncedElapsedMs = 0;

    buildCollisionMap();
    food.spawn(occupiedPositions);
    updateInterval = Config::Game::INITIAL_SPEED_MS;
    
    changeState(GameState::PLAYING);
    
    std::cout << "Game reset!" << std::endl;
}

void Game::buildCollisionMap()
{
    occupiedPositions.clear();
    for (int k = 0; k < 4; k++)
    {
        if (isPlayerValid(k))
        {
            for (const auto& segment : ctx.players[k].snake->getBody())
            {
                occupiedPositions[segment.y * Config::Grid::WIDTH + segment.x] = true;
            }
        }
    }
}

void Game::resetGameState()
{
    if (networkManager) {
        networkManager->shutdown();
    }
    
    for (int i = 0; i < 4; i++) {
        ctx.players[i].active = false;
        ctx.players[i].snake = nullptr;
    }
        ctx.players.setMyPlayerIndex(-1);
    ctx.match.winnerIndex = -1;
    ctx.match.totalPausedTime = 0;
    ctx.match.pauseStartTime = 0;
    ctx.match.matchStartTime = 0;
}