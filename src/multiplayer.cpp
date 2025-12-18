#include "multiplayer.h"
#include "config.h"
#include "game.h"
#include "logger.h"
#include <iostream>

// ========== INTERNAL FORWARD DECLARATIONS ==========
// These are implementation details not exposed in the header

// Network input validation
static bool isValidPosition(int x, int y) {
    return x >= 0 && x < Config::Grid::WIDTH && y >= 0 && y < Config::Grid::HEIGHT;
}

static void on_multiplayer_event(const char *event, int64_t messageId, const char *clientId, json_t *data, void *user_data);
static void processNetworkMessages(GameContext& ctx);
static void handlePlayerJoined(GameContext& ctx, const std::string& clientId);
static void handlePlayerLeft(GameContext& ctx, const std::string& clientId);
static void handleStateSync(GameContext& ctx, json_t* data);
static void handlePlayerInput(GameContext& ctx, const std::string& clientId, json_t* data);
static void handleGameState(GameContext& ctx, json_t* data);
static void sendGlobalPauseState(GameContext& ctx, bool paused, const std::string& pauserClientId);
static void add_player(GameContext& ctx, const std::string& clientId);
static void remove_player(GameContext& ctx, const std::string& clientId);
static void sendFullStateSync(GameContext& ctx);
static void handleHostDisconnect(GameContext& ctx);

// ========== CONSTANTS ==========



// Helper to build collision map from game context
static std::unordered_map<int, bool> buildCollisionMap(const GameContext& ctx) {
    std::unordered_map<int, bool> occupiedPositions;
    for (int k = 0; k < Config::Game::MAX_PLAYERS; k++) {
        if (ctx.players.isValid(k)) {
            for (const auto& segment : ctx.players[k].snake->getBody()) {
                occupiedPositions[segment.y * Config::Grid::WIDTH + segment.x] = true;
            }
        }
    }
    return occupiedPositions;
}

// Helper to build JSON array of player client IDs
static json_t* buildPlayerClientIdList(const GameContext& ctx) {
    json_t* playersArray = json_array();
    for (int i = 0; i < Config::Game::MAX_PLAYERS; i++) {
        if (ctx.players[i].active && !ctx.players[i].clientId.empty()) {
            json_array_append_new(playersArray, json_string(ctx.players[i].clientId.c_str()));
        }
    }
    return playersArray;
}

// Positions: Config::Spawn::PLAYER_SPAWN_X/Y[i]
// Colors: Config::Render::PLAYER_COLORS[i]

// ========== NETWORK MANAGER IMPLEMENTATION ==========

NetworkManager::~NetworkManager() {
    shutdown();
}

bool NetworkManager::initialize(const std::string& host, int port) {
    if (ctx->network.api) {
        std::cerr << "Network already initialized" << std::endl;
        return false;
    }
    
    ctx->network.api = mp_api_create(host.c_str(), port);
    if (!ctx->network.api) {
        std::cerr << "Failed to create multiplayer API" << std::endl;
        return false;
    }
    
    // Initialize connection timing
    ctx->network.lastMessageReceived = SDL_GetTicks();
    
    // Set up event listener
    mp_api_listen(ctx->network.api, on_multiplayer_event, ctx);
    std::cout << "Network initialized: " << host << ":" << port << std::endl;
    return true;
}

void NetworkManager::shutdown() {
    if (ctx->network.api) {
        mp_api_destroy(ctx->network.api);
        ctx->network.api = nullptr;
        ctx->network.sessionId.clear();
        ctx->network.myClientId.clear();
        ctx->network.hostClientId.clear();
        ctx->network.isHost = false;
        ctx->network.lastMessageReceived = 0;
        ctx->network.connectionWarningTime = 0;
    }
}

bool NetworkManager::isConnected() const {
    return ctx->network.api != nullptr;
}

bool NetworkManager::hostSession() {
    if (!ctx->network.api) {
        std::cerr << "Network not initialized" << std::endl;
        return false;
    }
    
    char* session = nullptr;
    char* clientId = nullptr;
    json_t* hostData = nullptr;
    
    std::cout << "Attempting to host session..." << std::endl;
    int rc = mp_api_host(ctx->network.api, &session, &clientId, &hostData);
    
    if (rc != MP_API_OK) {
        std::cerr << "Failed to host session: " << rc << std::endl;
        return false;
    }
    
    // Use RAII wrapper for automatic cleanup
    JsonPtr hostDataPtr(hostData);
    
    // Store session info
    ctx->network.sessionId = session;
    ctx->network.myClientId = clientId;
    ctx->network.isHost = true;
    ctx->network.lastStateSyncSent = SDL_GetTicks();
    
    std::cout << "Hosting session: " << session << " (clientId: " << clientId << ")" << std::endl;
    
    // Add myself as a player
    add_player(*ctx, clientId);
    ctx->players.setMyPlayerIndex(ctx->players.findByClientId(clientId));
    
    // Initialize match start time as host
    ctx->match.matchStartTime = SDL_GetTicks();
    
    // Cleanup
    free(session);
    free(clientId);
    
    return true;
}

bool NetworkManager::listSessions() {
    if (!ctx->network.api) {
        std::cerr << "Network not initialized" << std::endl;
        return false;
    }
    
    json_t* sessionList = nullptr;
    int rc = mp_api_list(ctx->network.api, &sessionList);
    
    if (rc != MP_API_OK) {
        std::cerr << "Failed to list sessions: " << rc << std::endl;
        return false;
    }
    
    // Use RAII wrapper
    JsonPtr sessionListPtr(sessionList);
    
    ctx->network.availableSessions.clear();
    
    if (json_array_size(sessionList) == 0) {
        std::cout << "No public sessions available." << std::endl;
    } else {
        std::cout << "Available sessions (total: " << json_array_size(sessionList) << "):" << std::endl;
        
        size_t index;
        json_t* value;
        json_array_foreach(sessionList, index, value) {
            json_t* sessVal = json_object_get(value, "id");
            if (json_is_string(sessVal)) {
                const char* sessionId = json_string_value(sessVal);
                ctx->network.availableSessions.push_back(sessionId);
                std::cout << " [" << (index + 1) << "] " << sessionId << std::endl;
            }
        }
    }
    
    return true;
}

bool NetworkManager::joinSession(const std::string& sessionId) {
    if (!ctx->network.api) {
        std::cerr << "Network not initialized" << std::endl;
        return false;
    }
    
    char* joinedSession = nullptr;
    char* joinedClientId = nullptr;
    json_t* joinPayload = json_object();
    json_object_set_new(joinPayload, "name", json_string("Player"));
    json_t* joinData = nullptr;
    
    int rc = mp_api_join(ctx->network.api, sessionId.c_str(), joinPayload, 
                         &joinedSession, &joinedClientId, &joinData);
    
    json_decref(joinPayload);
    
    // Use RAII wrapper
    JsonPtr joinDataPtr(joinData);
    
    if (rc != MP_API_OK) {
        std::cerr << "Failed to join session: " << rc << std::endl;
        return false;
    }
    
    // Store session info
    ctx->network.sessionId = joinedSession;
    ctx->network.myClientId = joinedClientId;
    ctx->network.isHost = false;
    
    std::cout << "Joined session: " << joinedSession << " (clientId: " << joinedClientId << ")" << std::endl;
    
    // Will be assigned player index when host sends state_sync
    ctx->players.setMyPlayerIndex(-1);
    
    // Cleanup
    free(joinedSession);
    free(joinedClientId);
    
    return true;
}

void NetworkManager::processMessages() {
    if (!ctx || !ctx->network.api)
        return;
    
    processNetworkMessages(*ctx);
    
    // Check for connection timeout (30 seconds without any message)
    if (ctx->network.lastMessageReceived > 0) {
        Uint32 currentTime = SDL_GetTicks();
        Uint32 timeSinceLastMessage = currentTime - ctx->network.lastMessageReceived;
        
        if (timeSinceLastMessage > Config::Network::CONNECTION_TIMEOUT_DISCONNECT_MS) {
            std::cerr << "Connection timeout! No messages for " << (timeSinceLastMessage / 1000) << " seconds" << std::endl;
            std::cerr << "Disconnecting and returning to menu..." << std::endl;
            
            // Set flag for safe shutdown on next frame (avoid use-after-free)
            ctx->network.connectionLost = true;
            
            if (ctx->onStateChange) {
                ctx->onStateChange(static_cast<int>(GameState::MENU));
            }
            return;  // Exit immediately, let game loop handle shutdown
        } else if (timeSinceLastMessage > Config::Network::CONNECTION_TIMEOUT_WARNING_MS && ctx->network.connectionWarningTime == 0) {
            ctx->network.connectionWarningTime = currentTime;
            std::cout << "Warning: No messages received for " << (timeSinceLastMessage / 1000) << " seconds" << std::endl;
        } else if (timeSinceLastMessage < Config::Network::CONNECTION_TIMEOUT_WARNING_MS) {
            // Reset warning if connection recovered
            ctx->network.connectionWarningTime = 0;
        }
    }
}

void NetworkManager::sendPauseState(bool paused, const std::string& clientId) {
    sendGlobalPauseState(*ctx, paused, clientId);
}

void NetworkManager::sendPlayerInput(Direction direction) {
    if (!ctx->network.api || ctx->network.sessionId.empty())
        return;
    
    auto inputMsg = JsonBuilder()
        .set("type", "player_input")
        .set("direction", directionToString(direction))
        .buildPtr();
    
    mp_api_game(ctx->network.api, inputMsg.get());
}

void NetworkManager::broadcastGameState(bool critical) {
    if (!ctx->network.api || !ctx->network.isHost)
        return;
    
    static Uint32 lastBroadcast = 0;
    Uint32 now = SDL_GetTicks();
    // The caller can force immediate broadcast by calling this directly after important events
    
    if (now - lastBroadcast < 100) {
        // Don't skip if last broadcast was very recent (< 10ms) - likely a critical update
        if (now - lastBroadcast > 10) {
            return;  // Too soon for regular update, skip
        }
    }
    lastBroadcast = now;
    
    // Build complete state message
    JsonBuilder stateMsg;
    stateMsg.set("type", "game_state");
    
    // Food position
    if (ctx->food) {
        stateMsg.set("foodX", ctx->food->getPosition().x);
        stateMsg.set("foodY", ctx->food->getPosition().y);
    }
    
    // All player positions
    JsonPtr playersArray(json_array());
    for (int i = 0; i < Config::Game::MAX_PLAYERS; i++) {
        if (!ctx->players[i].active || !ctx->players[i].snake)
            continue;
        
        // Get const reference to body first and check if empty
        const auto& body = ctx->players[i].snake->getBody();
        if (body.empty()) {
            std::cerr << "WARNING: Skipping player " << (i+1) << " with empty body in broadcastGameState" << std::endl;
            continue;
        }
        
        JsonPtr playerObj(json_object());
        json_object_set_new(playerObj.get(), "index", json_integer(i));
        json_object_set_new(playerObj.get(), "alive", json_boolean(ctx->players[i].snake->isAlive()));
        
        // Snake body
        JsonPtr bodyArray(json_array());
        for (const auto& segment : body) {
            JsonPtr segmentObj(json_object());
            json_object_set_new(segmentObj.get(), "x", json_integer(segment.x));
            json_object_set_new(segmentObj.get(), "y", json_integer(segment.y));
            json_array_append_new(bodyArray.get(), segmentObj.release());
        }
        json_object_set_new(playerObj.get(), "body", bodyArray.release());
        
        json_array_append_new(playersArray.get(), playerObj.release());
    }
    
    stateMsg.set("players", playersArray.release());
    
    // Sync timer information
    stateMsg.set("matchStartTime", ctx->match.matchStartTime);
    stateMsg.set("elapsedMs", ctx->match.syncedElapsedMs);
    
    // Send to all clients
    int result = mp_api_game(ctx->network.api, stateMsg.buildPtr().get());
    if (result != 0) {
        std::cerr << "ERROR: Failed to broadcast game state, result=" << result << std::endl;
    }
}

// ========== INTERNAL IMPLEMENTATION ==========

static void on_multiplayer_event( const char *event, int64_t messageId, const char *clientId, json_t *data, void *user_data)
{
    GameContext* ctx = (GameContext*)user_data;
    
    // Thread-safe: Just queue the message, don't process it here
    NetworkMessage msg;
    
    if (strcmp(event, "joined") == 0) {
        msg.type = NetworkMessageType::PLAYER_JOINED;
        msg.clientId = clientId ? clientId : "";
        msg.jsonData = "";  // No data for joined event
        ctx->network.messageQueue.push(msg);
        
    } else if (strcmp(event, "leaved") == 0) {
        msg.type = NetworkMessageType::PLAYER_LEFT;
        msg.clientId = clientId ? clientId : "";
        msg.jsonData = "";  // No data for left event
        
        // Check if host left (critical for clients)
        if (ctx->network.isHost == false && !ctx->network.hostClientId.empty()) {
            // Check if the leaving player is the host
            if (msg.clientId == ctx->network.hostClientId) {
                msg.type = NetworkMessageType::HOST_DISCONNECT;
                std::cout << "Host disconnected: " << msg.clientId << std::endl;
            }
        }
        
        ctx->network.messageQueue.push(msg);
        
    } else if (strcmp(event, "game") == 0) {
        if (clientId && data) {
            // Serialize JSON to string for thread-safe transfer
            // Note: json_dumps allocates memory, must be freed
            char* jsonStr = json_dumps(data, JSON_COMPACT);
            if (jsonStr) {
                msg.type = NetworkMessageType::GAME_UPDATE;
                msg.clientId = clientId;
                msg.jsonData = jsonStr;
                free(jsonStr);  // Free immediately after copying to std::string
                ctx->network.messageQueue.push(msg);
            }
        }
    }
}

// Process network messages in main thread (thread-safe)
static void processNetworkMessages(GameContext& ctx)
{
    NetworkMessage msg;
    
    // Update last message received time if we have messages
    if (ctx.network.messageQueue.size() > 0) {
        ctx.network.lastMessageReceived = SDL_GetTicks();
    }
    
    // Process all queued messages
    while (ctx.network.messageQueue.pop(msg)) {
        switch (msg.type) {
            case NetworkMessageType::HOST_DISCONNECT:
                handleHostDisconnect(ctx);
                break;
                
            case NetworkMessageType::PLAYER_JOINED:
                handlePlayerJoined(ctx, msg.clientId);
                break;
                
            case NetworkMessageType::PLAYER_LEFT:
                handlePlayerLeft(ctx, msg.clientId);
                break;
                
            case NetworkMessageType::GAME_UPDATE: {
                // Parse JSON from string
                json_error_t error;
                json_t* data = json_loads(msg.jsonData.c_str(), 0, &error);
                if (!data) continue;
                
                // Use RAII wrapper for automatic cleanup
                JsonPtr dataPtr(data);
                
                // Check message sub-type
                json_t *type_val = json_object_get(data, "type");
                const char* messageType = json_is_string(type_val) ? json_string_value(type_val) : "";
                
                if (strcmp(messageType, "state_sync") == 0) {
                    handleStateSync(ctx, data);
                } else if (strcmp(messageType, "player_input") == 0) {
                    handlePlayerInput(ctx, msg.clientId, data);
                } else if (strcmp(messageType, "game_state") == 0) {
                    handleGameState(ctx, data);
                }
                break;
            }
                
            default:
                break;
        }
    }
}

// ========== MESSAGE HANDLERS ==========

static void handlePlayerJoined(GameContext& ctx, const std::string& clientId)
{
    // Check if this is me joining
    bool isMe = (clientId == ctx.network.myClientId);
    
    if (isMe) {
        // I'm joining - add myself immediately
        add_player(ctx, clientId);
        ctx.players.setMyPlayerIndex(ctx.players.findByClientId(clientId));
        std::cout << "I joined as player " << (ctx.players.myPlayerIndex() + 1) << std::endl;
        
        // If I'm the first player and not explicitly host, I'm likely the host
        // (Server makes first joiner the host)
        if (ctx.players.myPlayerIndex() == 0 && !ctx.network.isHost) {
            ctx.network.hostClientId = clientId;
            std::cout << "Detected as session host (first player)" << std::endl;
        }
    } else {
        // Someone else joined - add them
        // If this is the first player joining and we don't know the host yet, they're likely the host
        if (ctx.network.hostClientId.empty() && ctx.players[0].active == false) {
            ctx.network.hostClientId = clientId;
            std::cout << "Detected host: " << clientId << std::endl;
        }
        add_player(ctx, clientId);
        std::cout << "Player joined: " << clientId << std::endl;
    }
    
    // If we're the host, send current game state to new player
    if (ctx.network.isHost && ctx.food) {
        JsonPtr gameUpdate(json_object());
        json_object_set_new(gameUpdate.get(), "type", json_string("state_sync"));
        json_object_set_new(gameUpdate.get(), "foodX", json_integer(ctx.food->getPosition().x));
        json_object_set_new(gameUpdate.get(), "foodY", json_integer(ctx.food->getPosition().y));
        json_object_set_new(gameUpdate.get(), "matchStartTime", json_integer(ctx.match.matchStartTime));
        
        // Add existing players info
        json_object_set_new(gameUpdate.get(), "players", buildPlayerClientIdList(ctx));
        
        mp_api_game(ctx.network.api, gameUpdate.get());
    }
}

static void handlePlayerLeft(GameContext& ctx, const std::string& clientId)
{
    remove_player(ctx, clientId);
}

static void handleStateSync(GameContext& ctx, json_t* data)
{
    // Sync game state from host
    // Sync food position
    json_t *foodX = json_object_get(data, "foodX");
    json_t *foodY = json_object_get(data, "foodY");
    if (json_is_integer(foodX) && json_is_integer(foodY) && ctx.food) {
        Position newFoodPos;
        newFoodPos.x = json_integer_value(foodX);
        newFoodPos.y = json_integer_value(foodY);
        ctx.food->setPosition(newFoodPos);
    }
    
    // Sync match timing
    json_t *matchTime = json_object_get(data, "matchStartTime");
    if (json_is_integer(matchTime)) {
        ctx.match.matchStartTime = json_integer_value(matchTime);
    }
    
    json_t *elapsedVal = json_object_get(data, "elapsedMs");
    if (json_is_integer(elapsedVal)) {
        ctx.match.syncedElapsedMs = json_integer_value(elapsedVal);
    }
    
    // Sync game state changes
    json_t *gameStateVal = json_object_get(data, "gameState");
    if (json_is_string(gameStateVal) && ctx.onStateChange) {
        const char* stateStr = json_string_value(gameStateVal);
        
        if (strcmp(stateStr, "PLAYING") == 0) {
            ctx.onStateChange(static_cast<int>(GameState::PLAYING));
            std::cout << "Host started the match!" << std::endl;
        } else if (strcmp(stateStr, "LOBBY") == 0) {
            ctx.onStateChange(static_cast<int>(GameState::LOBBY));
        } else if (strcmp(stateStr, "MATCH_END") == 0) {
            ctx.onStateChange(static_cast<int>(GameState::MATCH_END));
            std::cout << "Match ended!" << std::endl;
        }
    }
    
    // Sync pause state
    json_t *pausedVal = json_object_get(data, "globalPaused");
    json_t *pausedBy = json_object_get(data, "pausedBy");
    if (json_is_boolean(pausedVal) && json_is_string(pausedBy)) {
        bool isPaused = json_boolean_value(pausedVal);
        std::string pauserClientId = json_string_value(pausedBy);
        
        // Sync pause timing
        json_t *totalPausedVal = json_object_get(data, "totalPausedTime");
        json_t *pauseStartVal = json_object_get(data, "pauseStartTime");
        if (json_is_integer(totalPausedVal)) {
            ctx.match.totalPausedTime = json_integer_value(totalPausedVal);
        }
        if (json_is_integer(pauseStartVal)) {
            ctx.match.pauseStartTime = json_integer_value(pauseStartVal);
        }
        
        // Apply pause state to all players
        for (int i = 0; i < Config::Game::MAX_PLAYERS; i++) {
            if (ctx.players[i].active) {
                ctx.players[i].paused = isPaused;
            }
        }
        
        // Update game state using callback
        // The callback will handle the state change with fromNetwork=true
        if (ctx.onStateChange) {
            if (isPaused) {
                ctx.onStateChange(static_cast<int>(GameState::PAUSED));
            } else {
                ctx.onStateChange(static_cast<int>(GameState::PLAYING));
            }
        }
        
        // Update who paused
        ctx.match.pausedByClientId = isPaused ? pauserClientId : "";
        
        // Find player name for message
        int pauserIdx = ctx.players.findByClientId(pauserClientId);
        std::string playerName = pauserIdx >= 0 ? "Player " + std::to_string(pauserIdx + 1) : "Someone";
        std::cout << (isPaused ? (playerName + " paused the game") : (playerName + " resumed the game")) << std::endl;
    }
    
    // Sync player list
    json_t *playersArray = json_object_get(data, "players");
    if (json_is_array(playersArray)) {
        std::cout << "Client receiving player list from host..." << std::endl;
        size_t index;
        json_t *playerClientId;
        json_array_foreach(playersArray, index, playerClientId) {
            if (json_is_string(playerClientId)) {
                std::string pId = json_string_value(playerClientId);
                // Only add if not already present (avoid duplicates)
                if (ctx.players.findByClientId(pId) < 0) {
                    add_player(ctx, pId);
                    std::cout << "Added player from state_sync: " << pId << std::endl;
                    // If this is me, set my index
                    if (pId == ctx.network.myClientId && ctx.players.myPlayerIndex() < 0) {
                        ctx.players.setMyPlayerIndex(ctx.players.findByClientId(pId));
                        std::cout << "I am player " << (ctx.players.myPlayerIndex() + 1) << std::endl;
                    }
                }
            }
        }
    }
}

void NetworkManager::sendGameMessage(json_t* message)
{
    if (!ctx || !ctx->network.api || ctx->network.sessionId.empty() || !message)
        return;
    
    int rc = mp_api_game(ctx->network.api, message);
    if (rc != MP_API_OK) {
        std::cerr << "Failed to send game message: " << rc << std::endl;
    }
}

static void sendGlobalPauseState(GameContext& ctx, bool paused, const std::string& pauserClientId)
{
    if (!ctx.network.api || ctx.network.sessionId.empty())
        return;
    
    auto pauseUpdate = JsonBuilder()
        .set("type", "state_sync")
        .set("globalPaused", paused)
        .set("pausedBy", pauserClientId.c_str())
        .set("totalPausedTime", ctx.match.totalPausedTime)
        .set("pauseStartTime", ctx.match.pauseStartTime)
        .buildPtr();
    mp_api_game(ctx.network.api, pauseUpdate.get());
}

static void handlePlayerInput(GameContext& ctx, const std::string& clientId, json_t* data)
{
    // Only host processes inputs!
    if (!ctx.network.isHost) return;
    
    int playerIdx = ctx.players.findByClientId(clientId);
    if (playerIdx < 0 || !ctx.players[playerIdx].snake) return;
    
    json_t* dirVal = json_object_get(data, "direction");
    if (!json_is_string(dirVal)) return;
    
    Direction dir = stringToDirection(json_string_value(dirVal));
    
    if (dir != Direction::NONE)
    {
        ctx.players[playerIdx].snake->setDirection(dir);
    }
}

static void handleGameState(GameContext& ctx, json_t* data)
{
    if (ctx.network.isHost)
    return;
    
    json_t* foodX = json_object_get(data, "foodX");
    json_t* foodY = json_object_get(data, "foodY");
    if (foodX && foodY && ctx.food)
    {
        int x = (int)json_integer_value(foodX);
        int y = (int)json_integer_value(foodY);
        
        if (isValidPosition(x, y)) {
            ctx.food->setPosition(Position{x, y});
        } else {
            Logger::warn("Invalid food position from network: ", x, ",", y);
        }
    }
    
    json_t* playersArray = json_object_get(data, "players");
    if (json_is_array(playersArray))
    {
        size_t index;
        json_t* playerObj;
        
        json_array_foreach(playersArray, index, playerObj)
        {
            int playerIdx = (int)json_integer_value(json_object_get(playerObj, "index"));
            bool alive = json_boolean_value(json_object_get(playerObj, "alive"));
            
            if (playerIdx < 0 || playerIdx >= Config::Game::MAX_PLAYERS)
            continue;
            if (!ctx.players[playerIdx].snake)
            continue;
            
            json_t* bodyArray = json_object_get(playerObj, "body");
            if (json_is_array(bodyArray)) 
            {
                std::deque<Position> newBody;
                
                size_t i;
                json_t* segment;
                json_array_foreach(bodyArray, i, segment) 
                {
                    int x = (int)json_integer_value(json_object_get(segment, "x"));
                    int y = (int)json_integer_value(json_object_get(segment, "y"));
                    
                    if (!isValidPosition(x, y)) {
                        Logger::warn("Invalid snake position from network: ", x, ",", y, " - skipping segment");
                        continue;
                    }
                    
                    newBody.push_back(Position{x, y});
                }
                
                if (!newBody.empty())
                {
                    ctx.players[playerIdx].snake->setBody(newBody);
                }
            }
            if (!alive && ctx.players[playerIdx].snake->isAlive())
            {
                ctx.players[playerIdx].snake->setAlive(false);
            }
        }
    }
}

static void add_player(GameContext& ctx, const std::string& clientId)
{
    for (int i = 0; i < Config::Game::MAX_PLAYERS; i++)
    {
        if (!ctx.players[i].active)
        {
            // Build collision map and get random spawn position
            auto occupiedPositions = buildCollisionMap(ctx);
            Position spawnPos = getRandomSpawnPositionUtil(occupiedPositions);
            
            ctx.players[i].snake = std::make_unique<Snake>(Config::Render::PLAYER_COLORS[i], spawnPos);
            ctx.players[i].clientId = clientId;
            ctx.players[i].active = true;
            ctx.players[i].lastMpSent = 0;
            
            std::cout << "Player " << (i+1) << " joined: " << clientId << std::endl;
            break;
        }
    }
}

static void remove_player(GameContext& ctx, const std::string& clientId)
{
    for (int i = 0; i < Config::Game::MAX_PLAYERS; i++)
    {
        if (ctx.players[i].active && ctx.players[i].clientId == clientId)
        {
            ctx.players[i].active = false;
            ctx.players[i].snake = nullptr;
            ctx.players[i].clientId = "";
            std::cout << "Player " << (i+1) << " left" << std::endl;
            break;
        }
    }
}

static void sendFullStateSync(GameContext& ctx)
{
    if (!ctx.network.api || ctx.network.sessionId.empty() || !ctx.network.isHost)
        return;
    
    JsonPtr gameUpdate(json_object());
    json_object_set_new(gameUpdate.get(), "type", json_string("state_sync"));
    
    if (ctx.food)
    {
        json_object_set_new(gameUpdate.get(), "foodX", json_integer(ctx.food->getPosition().x));
        json_object_set_new(gameUpdate.get(), "foodY", json_integer(ctx.food->getPosition().y));
    }
    
    json_object_set_new(gameUpdate.get(), "matchStartTime", json_integer(ctx.match.matchStartTime));
    json_object_set_new(gameUpdate.get(), "elapsedMs", json_integer(ctx.match.syncedElapsedMs));
    
    json_object_set_new(gameUpdate.get(), "globalPaused", json_boolean(!ctx.match.pausedByClientId.empty()));
    json_object_set_new(gameUpdate.get(), "pausedBy", json_string(ctx.match.pausedByClientId.c_str()));
    json_object_set_new(gameUpdate.get(), "totalPausedTime", json_integer(ctx.match.totalPausedTime));
    json_object_set_new(gameUpdate.get(), "pauseStartTime", json_integer(ctx.match.pauseStartTime));
    
    json_object_set_new(gameUpdate.get(), "players", buildPlayerClientIdList(ctx));
    
    mp_api_game(ctx.network.api, gameUpdate.get());
    ctx.network.lastStateSyncSent = SDL_GetTicks();
    
    std::cout << "Sent periodic full state sync" << std::endl;
}

static void handleHostDisconnect(GameContext& ctx)
{
    std::cout << "HOST HAS DISCONNECTED!" << std::endl;
    if (ctx.onStateChange) {
        ctx.onStateChange(static_cast<int>(GameState::MENU));
    }
}

void NetworkManager::sendPeriodicStateSync()
{
    if (!ctx || !ctx->network.isHost)
    return;
    
    Uint32 currentTime = SDL_GetTicks();
    if (currentTime - ctx->network.lastStateSyncSent >= 1000)
    {
        sendFullStateSync(*ctx);
    }
}
