#include "multiplayer.h"
#include "game.h"
#include <iostream>

// ========== INTERNAL FORWARD DECLARATIONS ==========
// These are implementation details not exposed in the header

static void on_multiplayer_event(const char *event, int64_t messageId, const char *clientId, json_t *data, void *user_data);
static void processNetworkMessages(GameContext& ctx);
static void handlePlayerJoined(GameContext& ctx, const std::string& clientId);
static void handlePlayerLeft(GameContext& ctx, const std::string& clientId);
static void handleStateSync(GameContext& ctx, json_t* data);
static void handlePlayerUpdate(GameContext& ctx, const std::string& clientId, json_t* data);
static void send_game_state(GameContext& ctx, const Snake& snake);
static void sendPlayerUpdate(GameContext& ctx, int playerIndex);
static void sendGlobalPauseState(GameContext& ctx, bool paused, const std::string& pauserClientId);
static void add_player(GameContext& ctx, const std::string& clientId);
static int find_player_by_client_id(const GameContext& ctx, const std::string& clientId);
static void update_remote_player(GameContext& ctx, const std::string& clientId, json_t* data);
static void remove_player(GameContext& ctx, const std::string& clientId);
static void sendFullStateSync(GameContext& ctx);
static void handleHostDisconnect(GameContext& ctx);

// ========== CONSTANTS ==========

// Player spawn positions and colors (indexed by player slot 0-3)
static const Position PLAYER_SPAWN_POSITIONS[4] = {
    {GRID_WIDTH/4, GRID_HEIGHT/4},          // Player 1: Top-left
    {3*GRID_WIDTH/4, GRID_HEIGHT/4},        // Player 2: Top-right
    {GRID_WIDTH/4, 3*GRID_HEIGHT/4},        // Player 3: Bottom-left
    {3*GRID_WIDTH/4, 3*GRID_HEIGHT/4}       // Player 4: Bottom-right
};

static const SDL_Color PLAYER_COLORS[4] = {
    {0, 255, 0, 255},    // Green
    {0, 0, 255, 255},    // Blue
    {255, 255, 0, 255},  // Yellow
    {255, 0, 255, 255}   // Magenta
};

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
    ctx->players.myPlayerIndex = find_player_by_client_id(*ctx, clientId);
    
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
    ctx->players.myPlayerIndex = -1;
    
    // Cleanup
    free(joinedSession);
    free(joinedClientId);
    
    return true;
}

const std::vector<std::string>& NetworkManager::getAvailableSessions() const {
    return ctx->network.availableSessions;
}

bool NetworkManager::isHost() const {
    return ctx->network.isHost;
}

bool NetworkManager::isInSession() const {
    return !ctx->network.sessionId.empty();
}

const std::string& NetworkManager::getSessionId() const {
    return ctx->network.sessionId;
}

const std::string& NetworkManager::getMyClientId() const {
    return ctx->network.myClientId;
}

void NetworkManager::processMessages() {
    if (!ctx || !ctx->network.api)
        return;
    
    processNetworkMessages(*ctx);
    
    // Check for connection timeout (30 seconds without any message)
    if (ctx->network.lastMessageReceived > 0) {
        Uint32 currentTime = SDL_GetTicks();
        Uint32 timeSinceLastMessage = currentTime - ctx->network.lastMessageReceived;
        
        if (timeSinceLastMessage > CONNECTION_TIMEOUT_DISCONNECT_MS) {
            std::cerr << "Connection timeout! No messages for " << (timeSinceLastMessage / 1000) << " seconds" << std::endl;
            std::cerr << "Disconnecting and returning to menu..." << std::endl;
            
            // Trigger disconnect
            if (ctx->onStateChange) {
                ctx->onStateChange(static_cast<int>(GameState::MENU));
            }
            shutdown();
        } else if (timeSinceLastMessage > CONNECTION_TIMEOUT_WARNING_MS && ctx->network.connectionWarningTime == 0) {
            ctx->network.connectionWarningTime = currentTime;
            std::cout << "Warning: No messages received for " << (timeSinceLastMessage / 1000) << " seconds" << std::endl;
        } else if (timeSinceLastMessage < CONNECTION_TIMEOUT_WARNING_MS) {
            // Reset warning if connection recovered
            ctx->network.connectionWarningTime = 0;
        }
    }
}

void NetworkManager::sendPlayerUpdate(int playerIndex) {
    ::sendPlayerUpdate(*ctx, playerIndex);
}

void NetworkManager::sendPauseState(bool paused, const std::string& clientId) {
    sendGlobalPauseState(*ctx, paused, clientId);
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
                } else if (msg.clientId != ctx.network.myClientId) {
                    handlePlayerUpdate(ctx, msg.clientId, data);
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
        ctx.players.myPlayerIndex = find_player_by_client_id(ctx, clientId);
        std::cout << "I joined as player " << (ctx.players.myPlayerIndex + 1) << std::endl;
        
        // If I'm the first player and not explicitly host, I'm likely the host
        // (Server makes first joiner the host)
        if (ctx.players.myPlayerIndex == 0 && !ctx.network.isHost) {
            ctx.network.hostClientId = clientId;
            std::cout << "Detected as session host (first player)" << std::endl;
        }
    } else {
        // Someone else joined - add them
        // If this is the first player joining and we don't know the host yet, they're likely the host
        if (ctx.network.hostClientId.empty() && ctx.players.players[0].active == false) {
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
        JsonPtr playersArray(json_array());
        for (int i = 0; i < 4; i++) {
            if (ctx.players.players[i].active && !ctx.players.players[i].clientId.empty()) {
                json_array_append_new(playersArray.get(), json_string(ctx.players.players[i].clientId.c_str()));
            }
        }
        json_object_set_new(gameUpdate.get(), "players", playersArray.release());
        
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
        for (int i = 0; i < 4; i++) {
            if (ctx.players.players[i].active) {
                ctx.players.players[i].paused = isPaused;
            }
        }
        
        // Update game state using callback
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
        int pauserIdx = find_player_by_client_id(ctx, pauserClientId);
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
                if (find_player_by_client_id(ctx, pId) < 0) {
                    add_player(ctx, pId);
                    std::cout << "Added player from state_sync: " << pId << std::endl;
                    // If this is me, set my index
                    if (pId == ctx.network.myClientId && ctx.players.myPlayerIndex < 0) {
                        ctx.players.myPlayerIndex = find_player_by_client_id(ctx, pId);
                        std::cout << "I am player " << (ctx.players.myPlayerIndex + 1) << std::endl;
                    }
                }
            }
        }
    }
}

static void handlePlayerUpdate(GameContext& ctx, const std::string& clientId, json_t* data)
{
    std::cout << "Updating remote player: " << clientId << std::endl;
    update_remote_player(ctx, clientId, data);
}

static void send_game_state(GameContext& ctx, const Snake& snake)
{
    if (!ctx.network.api) {
        static bool warned = false;
        if (!warned) {
            std::cout << "API not initialized" << std::endl;
            warned = true;
        }
        return;
    }
    
    if (ctx.network.sessionId.empty()) {
        static bool warned = false;
        if (!warned) {
            std::cout << "Not in a session. Press H to host or L to list sessions" << std::endl;
            warned = true;
        }
        return;
    }

    Position head = snake.getHead();
    JsonPtr gameData(json_object());
    json_object_set_new(gameData.get(), "x", json_integer(head.x));
    json_object_set_new(gameData.get(), "y", json_integer(head.y));
    json_object_set_new(gameData.get(), "score", json_integer(snake.getScore()));
    json_object_set_new(gameData.get(), "alive", json_boolean(snake.isAlive()));
    
    // Send paused status
    int playerIdx = -1;
    for (int i = 0; i < 4; i++) {
        if (ctx.players.players[i].active && ctx.players.players[i].snake.get() == &snake) {
            playerIdx = i;
            break;
        }
    }
    if (playerIdx >= 0) {
        json_object_set_new(gameData.get(), "paused", json_boolean(ctx.players.players[playerIdx].paused));
    }
    
    // Send full body for better sync
    JsonPtr bodyArray(json_array());
    for (const auto& segment : snake.getBody()) {
        JsonPtr seg(json_object());
        json_object_set_new(seg.get(), "x", json_integer(segment.x));
        json_object_set_new(seg.get(), "y", json_integer(segment.y));
        json_array_append_new(bodyArray.get(), seg.release());
    }
    json_object_set_new(gameData.get(), "body", bodyArray.release());
    
    int rc = mp_api_game(ctx.network.api, gameData.get());
    if (rc != MP_API_OK) {
        std::cerr << "Failed to send game state: " << rc << std::endl;
    }
}

static void sendPlayerUpdate(GameContext& ctx, int playerIndex)
{
    // Validate player index and state
    if (playerIndex < 0 || playerIndex >= 4)
        return;
    if (!ctx.players.players[playerIndex].active || !ctx.players.players[playerIndex].snake)
        return;
    if (ctx.network.sessionId.empty() || !ctx.network.api)
        return;
    
    // Send the update (no throttle - game tick rate naturally limits frequency)
    send_game_state(ctx, *ctx.players.players[playerIndex].snake);
    ctx.players.players[playerIndex].lastMpSent = SDL_GetTicks();
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
    
    JsonPtr pauseUpdate(json_object());
    json_object_set_new(pauseUpdate.get(), "type", json_string("state_sync"));
    json_object_set_new(pauseUpdate.get(), "globalPaused", json_boolean(paused));
    json_object_set_new(pauseUpdate.get(), "pausedBy", json_string(pauserClientId.c_str()));
    json_object_set_new(pauseUpdate.get(), "totalPausedTime", json_integer(ctx.match.totalPausedTime));
    json_object_set_new(pauseUpdate.get(), "pauseStartTime", json_integer(ctx.match.pauseStartTime));
    mp_api_game(ctx.network.api, pauseUpdate.get());
}

// ========== PLAYER MANAGEMENT HELPERS ==========

static void add_player(GameContext& ctx, const std::string& clientId)
{
    // Find first available slot
    for (int i = 0; i < 4; i++) {
        if (!ctx.players.players[i].active) {
            ctx.players.players[i].snake = std::make_unique<Snake>(PLAYER_COLORS[i], PLAYER_SPAWN_POSITIONS[i]);
            ctx.players.players[i].clientId = clientId;
            ctx.players.players[i].active = true;
            ctx.players.players[i].lastMpSent = 0;
            
            std::cout << "Player " << (i+1) << " joined: " << clientId << std::endl;
            break;
        }
    }
}

static int find_player_by_client_id(const GameContext& ctx, const std::string& clientId)
{
    for (int i = 0; i < 4; i++)
    {
        if (ctx.players.players[i].active && ctx.players.players[i].clientId == clientId)
        {
            return i;
        }
    }
    return -1;
}

static void update_remote_player(GameContext& ctx, const std::string& clientId, json_t* data)
{
    int idx = find_player_by_client_id(ctx, clientId);
    
    if (idx < 0 || !ctx.players.players[idx].snake) {
        return;
    }

    json_t *bodyArray = json_object_get(data, "body");

    if (json_is_array(bodyArray) && json_array_size(bodyArray) > 0)
    {
        std::deque<Position> newBody;
        
        size_t index;
        json_t *segment;

        json_array_foreach(bodyArray, index, segment)
        {
        json_t *x_val = json_object_get(segment, "x");
        json_t *y_val = json_object_get(segment, "y");
            
            if (json_is_integer(x_val) && json_is_integer(y_val))
            {
                Position pos;
                pos.x = json_integer_value(x_val);
                pos.y = json_integer_value(y_val);
                
                // CLIENT VALIDATION: Check position is within valid bounds
                if (pos.x < 0 || pos.x >= GRID_WIDTH || pos.y < 0 || pos.y >= GRID_HEIGHT) {
                    std::cerr << "WARNING: Invalid position from client " << clientId 
                              << " (" << pos.x << "," << pos.y << ") - rejecting update" << std::endl;
                    return;  // Reject entire update if any position is invalid
                }
                
                newBody.push_back(pos);
            }
        }
        
        if (newBody.size() > 400) {
            std::cerr << "WARNING: Suspicious body length from client " << clientId 
                      << " (" << newBody.size() << " segments) - rejecting" << std::endl;
            return;
        }
        
        if (!newBody.empty())
        {
            ctx.players.players[idx].snake->setBody(newBody);
        }
    }

    json_t *score_val = json_object_get(data, "score");
    if (json_is_integer(score_val))
    {
        int score = json_integer_value(score_val);
        // Validate score is reasonable (0 to 10000)
        if (score >= 0 && score <= 10000) {
            ctx.players.players[idx].snake->setScore(score);
        } else {
            std::cerr << "WARNING: Invalid score from client " << clientId 
                      << " (" << score << ") - ignoring" << std::endl;
        }
    }
    
    json_t *alive_val = json_object_get(data, "alive");
    if (json_is_boolean(alive_val))
    {
        ctx.players.players[idx].snake->setAlive(json_boolean_value(alive_val));
    }
    
    // Receive paused status
    json_t *paused_val = json_object_get(data, "paused");
    if (json_is_boolean(paused_val))
    {
        ctx.players.players[idx].paused = json_boolean_value(paused_val);
    }
}

static void remove_player(GameContext& ctx, const std::string& clientId)
{
    for (int i = 0; i < 4; i++)
    {
        if (ctx.players.players[i].active && ctx.players.players[i].clientId == clientId)
        {
            ctx.players.players[i].active = false;
            ctx.players.players[i].snake = nullptr;
            ctx.players.players[i].clientId = "";
            std::cout << "Player " << (i+1) << " left" << std::endl;
            break;
        }
    }
}

// ========== CRITICAL FIX IMPLEMENTATIONS ==========

static void sendFullStateSync(GameContext& ctx)
{
    if (!ctx.network.api || ctx.network.sessionId.empty() || !ctx.network.isHost)
        return;
    
    JsonPtr gameUpdate(json_object());
    json_object_set_new(gameUpdate.get(), "type", json_string("state_sync"));
    
    // Food position
    if (ctx.food) {
        json_object_set_new(gameUpdate.get(), "foodX", json_integer(ctx.food->getPosition().x));
        json_object_set_new(gameUpdate.get(), "foodY", json_integer(ctx.food->getPosition().y));
    }
    
    // Match timing
    json_object_set_new(gameUpdate.get(), "matchStartTime", json_integer(ctx.match.matchStartTime));
    json_object_set_new(gameUpdate.get(), "elapsedMs", json_integer(ctx.match.syncedElapsedMs));
    
    // Pause state
    json_object_set_new(gameUpdate.get(), "globalPaused", json_boolean(!ctx.match.pausedByClientId.empty()));
    json_object_set_new(gameUpdate.get(), "pausedBy", json_string(ctx.match.pausedByClientId.c_str()));
    json_object_set_new(gameUpdate.get(), "totalPausedTime", json_integer(ctx.match.totalPausedTime));
    json_object_set_new(gameUpdate.get(), "pauseStartTime", json_integer(ctx.match.pauseStartTime));
    
    // Player list
    JsonPtr playersArray(json_array());
    for (int i = 0; i < 4; i++) {
        if (ctx.players.players[i].active && !ctx.players.players[i].clientId.empty()) {
            json_array_append_new(playersArray.get(), json_string(ctx.players.players[i].clientId.c_str()));
        }
    }
    json_object_set_new(gameUpdate.get(), "players", playersArray.release());
    
    // Send the full state
    mp_api_game(ctx.network.api, gameUpdate.get());
    ctx.network.lastStateSyncSent = SDL_GetTicks();
    
    std::cout << "Sent periodic full state sync" << std::endl;
}

static void handleHostDisconnect(GameContext& ctx)
{
    std::cout << "========================================" << std::endl;
    std::cout << "HOST HAS DISCONNECTED!" << std::endl;
    std::cout << "Returning to menu..." << std::endl;
    std::cout << "========================================" << std::endl;
    
    // Trigger state change back to menu
    if (ctx.onStateChange) {
        ctx.onStateChange(static_cast<int>(GameState::MENU));
    }
}

// NetworkManager implementations for critical fixes

void NetworkManager::sendPeriodicStateSync()
{
    if (!ctx || !ctx->network.isHost) return;
    
    Uint32 currentTime = SDL_GetTicks();
    // Send full state every 5 seconds
    if (currentTime - ctx->network.lastStateSyncSent >= 5000) {
        sendFullStateSync(*ctx);
    }
}
