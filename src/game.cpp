#include "game.h"
#include <iostream>
#include <ctime>

Game::Game() 
    : state(GameState::MENU), quit(false),
      updateInterval(Config::Game::INITIAL_SPEED_MS), menuSelection(0), pauseMenuSelection(0),
      sessionSelection(0), countdownStartTime(0), inputHandler(&Game::handleMenuInput)
{
    // Initialize logger
    Logger::init("hardcoresnake.log", LogLevel::INFO, true);
    Logger::info("Game starting...");
    
    // Initialize game context
    ctx.players.setMyPlayerIndex(-1);
    ctx.food = &food;
    ctx.match.matchStartTime = 0;
    ctx.match.syncedElapsedMs = 0;
    ctx.match.winnerIndex = -1;
    ctx.match.totalPausedTime = 0;
    ctx.match.pauseStartTime = 0;
    
    ctx.onStateChange = [this](int newStateInt) {
        GameState newState = static_cast<GameState>(newStateInt);
        // Use changeState to properly handle enter/exit transitions
        // Pass fromNetwork=true to prevent sending pause messages back
        if (this->state != newState) {
            this->changeState(newState, true);
        }
    };
        networkManager = std::make_unique<NetworkManager>(&ctx);
        ui = std::make_unique<MenuRender>();
    
    for (int i = 0; i < Config::Game::MAX_PLAYERS; i++) {
        ctx.players[i].active = false;
        ctx.players[i].clientId = "";
        ctx.players[i].snake = nullptr;
        ctx.players[i].paused = false;
    }
    food.spawn(occupiedPositions);
    lastUpdate = SDL_GetTicks();
}

Game::~Game()
{
    if (ctx.players.hasMe() && ctx.players.me().snake)
    {
        Logger::info("Final score: ", ctx.players.me().snake->getScore());
    }
    
    networkManager.reset();
    // ui automatically cleaned up by unique_ptr
    
    Logger::info("Game shutting down...");
    Logger::shutdown();
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
        
        // Check for connection lost flag (safe shutdown point)
        if (networkManager->getNetworkContext().connectionLost) {
            networkManager->shutdown();
            return;
        }
        
        if (networkManager->getNetworkContext().isHost) {
            networkManager->sendPeriodicStateSync();  // Every 5 seconds
        }
    }
    
    // Handle countdown state transition
    if (state == GameState::COUNTDOWN) {
        Uint32 currentTime = SDL_GetTicks();
        Uint32 elapsed = currentTime - countdownStartTime;
        if (elapsed >= 3000) {  // 3 seconds countdown
            changeState(GameState::PLAYING);
        }
        return;
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
                networkManager->getNetworkContext().availableSessions, 
                sessionSelection,
                networkManager->isConnected()
            );
            break;
            
        case GameState::LOBBY:
            ui->clearScreen();
            ui->renderLobby(ctx.players.getSlots(), networkManager->getNetworkContext().isHost);
            break;
            
        case GameState::COUNTDOWN: {
            ui->renderGame(ctx, false);
            Uint32 elapsed = SDL_GetTicks() - countdownStartTime;
            int remaining = 3 - (elapsed / 1000);
            if (remaining < 0) remaining = 0;
            ui->renderCountdown(remaining);
            break;
        }
            
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

bool Game::isValidTransition(GameState from, GameState to) const
{
    // Allow staying in same state
    if (from == to) return true;
    
    // Valid state transition table
    switch (from) {
        case GameState::MENU:
            // From MENU: can go to SINGLEPLAYER, MULTIPLAYER setup, or directly to PLAYING (singleplayer)
            return to == GameState::SINGLEPLAYER || to == GameState::MULTIPLAYER || to == GameState::PLAYING;
            
        case GameState::SINGLEPLAYER:
            // From SINGLEPLAYER: can start playing or return to menu
            return to == GameState::PLAYING || to == GameState::MENU;
            
        case GameState::MULTIPLAYER:
            // From MULTIPLAYER: can join lobby or return to menu
            return to == GameState::LOBBY || to == GameState::MENU;
            
        case GameState::LOBBY:
            // From LOBBY: can start countdown, go directly to playing, or return to menu/multiplayer
            return to == GameState::COUNTDOWN || to == GameState::PLAYING || to == GameState::MENU || to == GameState::MULTIPLAYER;
            
        case GameState::COUNTDOWN:
            // From COUNTDOWN: must go to playing (or back to lobby if cancelled)
            return to == GameState::PLAYING || to == GameState::LOBBY;
            
        case GameState::PLAYING:
            // From PLAYING: can pause, end match, or return to menu (disconnect)
            return to == GameState::PAUSED || to == GameState::MATCH_END || to == GameState::MENU;
            
        case GameState::PAUSED:
            // From PAUSED: can resume or quit to menu
            return to == GameState::PLAYING || to == GameState::MENU;
            
        case GameState::MATCH_END:
            // From MATCH_END: can only return to menu or lobby (for rematch)
            return to == GameState::MENU || to == GameState::LOBBY;
    }
    
    return false;
}

void Game::changeState(GameState newState, bool fromNetwork)
{
    GameState oldState = state;
    
    // Validate transition
    if (!isValidTransition(oldState, newState)) {
        Logger::error("Invalid state transition: ", 
                     gameStateToString(oldState), " -> ", gameStateToString(newState));
        return;  // Reject invalid transition
    }
    
    Logger::info("State transition: ", 
                gameStateToString(oldState), " -> ", gameStateToString(newState));
    
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
        
        case GameState::COUNTDOWN:
            countdownStartTime = SDL_GetTicks();
            inputHandler = nullptr;  // No input during countdown
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
                if (networkManager->getNetworkContext().isHost) {
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
            navigateMenu(menuSelection, 3, true);
            break;
        case SDLK_DOWN:
            navigateMenu(menuSelection, 3, false);
            break;
        case SDLK_RETURN:
        case SDLK_SPACE:
            if (menuSelection == 0)
            {  // Single Player
                Position startPos = getRandomSpawnPosition();
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
                Logger::info("Started singleplayer mode");
            }
            else if (menuSelection == 1)
            {  // Multiplayer
                if (networkManager->initialize(Config::Network::DEFAULT_HOST, Config::Network::DEFAULT_PORT))
                {
                    changeState(GameState::MULTIPLAYER);
                    inputHandler = &Game::handleMultiplayerInput;
                    Logger::info("Multiplayer - Press H to host or L to list sessions");
                    return;
                }
                Logger::error("Failed to create multiplayer API");
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
            if (networkManager->isConnected() && networkManager->getNetworkContext().sessionId.empty()) {
                if (networkManager->hostSession()) {
                    changeState(GameState::LOBBY);
                    inputHandler = &Game::handleLobbyInput;
                }
            }
            break;
        case SDLK_l:
            if (networkManager->isConnected() && networkManager->getNetworkContext().sessionId.empty()) {
                sessionSelection = 0;  // Reset selection when listing
                if (!networkManager->listSessions()) {
                    Logger::error("Failed to request session list");
                }
            }
            break;
        case SDLK_UP:
            if (!networkManager->getNetworkContext().availableSessions.empty()) {
                navigateMenu(sessionSelection, networkManager->getNetworkContext().availableSessions.size(), true);
            }
            break;
        case SDLK_DOWN:
            if (!networkManager->getNetworkContext().availableSessions.empty()) {
                navigateMenu(sessionSelection, networkManager->getNetworkContext().availableSessions.size(), false);
            }
            break;
        case SDLK_RETURN:
            if (networkManager->isConnected() && networkManager->getNetworkContext().sessionId.empty() && 
                !networkManager->getNetworkContext().availableSessions.empty())
            {
                const auto& sessions = networkManager->getNetworkContext().availableSessions;
                if (sessionSelection < (int)sessions.size())
                {
                    Logger::info("Joining session: ", sessions[sessionSelection]);
                    if (networkManager->joinSession(sessions[sessionSelection])) {
                        changeState(GameState::LOBBY);
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
            if (networkManager->getNetworkContext().isHost) {
                changeState(GameState::COUNTDOWN);
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
    if (networkManager->isConnected() && !networkManager->getNetworkContext().isHost) {
        networkManager->sendPlayerInput(dir);
    }
    // Host processes inputs immediately via setDirection above
}

void Game::handlePausedInput(SDL_Keycode key)
{
    switch (key)
    {
        case SDLK_UP:
            navigateMenu(pauseMenuSelection, 3, true);
            break;
        case SDLK_DOWN:
            navigateMenu(pauseMenuSelection, 3, false);
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
                    if (!networkManager->isConnected() ||networkManager->getNetworkContext().isHost) {
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
    if (!networkManager->isConnected() || networkManager->getNetworkContext().isHost) {
        // Calculate elapsed time, subtracting paused time
        Uint32 currentPausedTime = ctx.match.totalPausedTime;
        if (state == GameState::PAUSED && ctx.match.pauseStartTime > 0) {
            currentPausedTime += (currentTime - ctx.match.pauseStartTime);
        }
        
        Uint32 elapsedMs = currentTime - ctx.match.matchStartTime - currentPausedTime;
        ctx.match.syncedElapsedMs = elapsedMs;
        Uint32 elapsedSeconds = elapsedMs / 1000;
        
        // Broadcast timer update if multiplayer host
        if (networkManager->getNetworkContext().isHost && networkManager->isConnected()) {
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
            if (networkManager->getNetworkContext().isHost && networkManager->isConnected()) {
                auto endUpdate = JsonBuilder()
                    .set("type", "state_sync")
                    .set("gameState", "MATCH_END")
                    .buildPtr();
                networkManager->sendGameMessage(endUpdate.get());
            }
            
            changeState(GameState::MATCH_END);
            
            // Find winner - longest snake
            int maxLength = 0;
            ctx.match.winnerIndex = -1;
            std::vector<int> tiedPlayers;
            
            for (int i = 0; i < Config::Game::MAX_PLAYERS; i++)
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
            
            Logger::info("Match ended!");
            if (ctx.match.winnerIndex >= 0 && 
                ctx.match.winnerIndex < Config::Game::MAX_PLAYERS &&
                ctx.players.isValid(ctx.match.winnerIndex))
            {
                Logger::info("Winner: Player ", (ctx.match.winnerIndex + 1), 
                         " (Length: ", ctx.players[ctx.match.winnerIndex].snake->getBody().size(),
                         ", Score: ", ctx.players[ctx.match.winnerIndex].snake->getScore(), ")");
            } else {
                Logger::info("No winner (no active players)");
            }
        }
    }
}

void Game::updatePlayers()
{    
    if (networkManager->getNetworkContext().isHost || !networkManager->isConnected())
    {
        if (state == GameState::PAUSED)
        {
            if (networkManager->isConnected())
            {
                networkManager->broadcastGameState();
            }
            return;
        }
        // Always rebuild collision map at start of each update to ensure it's current
        buildCollisionMap();
        
        struct MoveInfo {
            Position oldHead;
            Position oldTail;   
            Position newHead;
            bool willGrow;
            bool collision;
            bool processed;
        };
        MoveInfo moves[Config::Game::MAX_PLAYERS] = {};
        bool needRebuild = false;
        
        for (int i = 0; i < Config::Game::MAX_PLAYERS; i++)
        {
            moves[i].processed = false;
            if (!ctx.players.isValid(i) || !ctx.players[i].snake->isAlive())
                continue;
            moves[i].processed = true;
            
            const auto& body = ctx.players[i].snake->getBody();
            if (body.empty())
            {
                Logger::error("Player ", (i+1), " has empty snake body!");
                moves[i].processed = false;
                continue;
            }
            
            moves[i].oldHead = ctx.players[i].snake->getHead();
            moves[i].oldTail = body.back();
            moves[i].willGrow = (moves[i].oldHead == food.getPosition());
            
            ctx.players[i].snake->update();
            moves[i].newHead = ctx.players[i].snake->getHead();
            
            // Skip collision check if snake didn't move (direction not set yet)
            if (moves[i].oldHead.x == moves[i].newHead.x && moves[i].oldHead.y == moves[i].newHead.y) {
                moves[i].processed = false;
                continue;
            }
            
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
                if (occupiedPositions.count(newHeadKey) > 0) {
                    // Exception: if not growing, we can move into our own tail position
                    // because the tail will move away this frame
                    if (!moves[i].willGrow) {
                        int oldTailKey = moves[i].oldTail.y * Config::Grid::WIDTH + moves[i].oldTail.x;
                        if (newHeadKey == oldTailKey) {
                            // Moving into our own tail - allowed, tail will move
                        } else {
                            moves[i].collision = true;
                            Logger::debug("Player ", (i+1), " collision at (", 
                                      moves[i].newHead.x, ",", moves[i].newHead.y, ")");
                        }
                    } else {
                        // Growing: can't move into ANY occupied cell
                        moves[i].collision = true;
                        Logger::debug("Player ", (i+1), " collision at (", 
                                  moves[i].newHead.x, ",", moves[i].newHead.y, ")");
                    }
                }
            }
        }
        
        // Phase 2: Apply results and update collision map incrementally
        for (int i = 0; i < Config::Game::MAX_PLAYERS; i++) {
            if (!moves[i].processed)
                continue;
            
            if (moves[i].collision) {
                respawnPlayer(i);
                Logger::info("Player ", (i+1), " died and respawned!");
                needRebuild = true;
            } else {
                // Update collision map incrementally
                int newHeadKey = moves[i].newHead.y * Config::Grid::WIDTH + moves[i].newHead.x;
                
                // Add new head position
                occupiedPositions[newHeadKey] = true;
                
                // Handle tail - only remove if not growing
                if (!moves[i].willGrow) {
                    // Snake moved: remove old tail (it advanced)
                    int oldTailKey = moves[i].oldTail.y * Config::Grid::WIDTH + moves[i].oldTail.x;
                    occupiedPositions.erase(oldTailKey);
                }
                else {
                    // Snake grew - tail stays
                    ctx.players[i].snake->grow();
                    food.spawn(occupiedPositions);
                    Logger::debug("Player ", (i+1), " ate food!");
                    
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
    Position randomPos = getRandomSpawnPosition();
    ctx.players[playerIndex].snake->reset(randomPos);
}

Position Game::getRandomSpawnPosition()
{
    buildCollisionMap();
    return getRandomSpawnPositionUtil(occupiedPositions);
}

void Game::navigateMenu(int& selection, int maxItems, bool up)
{
    if (up) {
        selection = (selection - 1 + maxItems) % maxItems;
    } else {
        selection = (selection + 1) % maxItems;
    }
}

void Game::resetMatch()
{
    buildCollisionMap();

    for (int i = 0; i < Config::Game::MAX_PLAYERS; i++)
    {
        if (ctx.players.isValid(i))
        {
            Position spawnPos = getRandomSpawnPosition();
            ctx.players[i].snake->reset(spawnPos);
            ctx.players[i].snake->setScore(0);
            
            // Update collision map with new snake position
            for (const auto& segment : ctx.players[i].snake->getBody()) {
                occupiedPositions[segment.y * Config::Grid::WIDTH + segment.x] = true;
            }
        }
    }
    
    ctx.match.winnerIndex = -1;
    ctx.match.matchStartTime = SDL_GetTicks();
    ctx.match.totalPausedTime = 0;
    ctx.match.pauseStartTime = 0;
    ctx.match.syncedElapsedMs = 0;

    food.spawn(occupiedPositions);
    updateInterval = Config::Game::INITIAL_SPEED_MS;
    
    changeState(GameState::PLAYING);
    
    Logger::info("Game reset!");
}

void Game::buildCollisionMap()
{
    occupiedPositions.clear();
    for (int k = 0; k < Config::Game::MAX_PLAYERS; k++)
    {
        if (ctx.players.isValid(k))
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
    
    for (int i = 0; i < Config::Game::MAX_PLAYERS; i++) {
        ctx.players[i].active = false;
        ctx.players[i].snake = nullptr;
    }
        ctx.players.setMyPlayerIndex(-1);
    ctx.match.winnerIndex = -1;
    ctx.match.totalPausedTime = 0;
    ctx.match.pauseStartTime = 0;
    ctx.match.matchStartTime = 0;
}