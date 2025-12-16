#include "multiplayer.h"
#include <iostream>



void on_multiplayer_event( const char *event, int64_t messageId, const char *clientId, json_t *data, void *user_data)
{
    GameContext* ctx = (GameContext*)user_data;

    if (strcmp(event, "joined") == 0) {
        std::string myClientId = (ctx->myPlayerIndex >= 0) ? ctx->players[ctx->myPlayerIndex].clientId : "";
        if (clientId && std::string(clientId) != myClientId) {
            add_player(*ctx, clientId);
            
            // If we're the host, send current game state to new player
            if (ctx->isHost && ctx->food) {
                json_t *gameUpdate = json_object();
                json_object_set_new(gameUpdate, "type", json_string("state_sync"));
                json_object_set_new(gameUpdate, "foodX", json_integer(ctx->food->getPosition().x));
                json_object_set_new(gameUpdate, "foodY", json_integer(ctx->food->getPosition().y));
                json_object_set_new(gameUpdate, "matchStartTime", json_integer(ctx->matchStartTime));
                
                // Add existing players info
                json_t *playersArray = json_array();
                for (int i = 0; i < 4; i++) {
                    if (ctx->players[i].active && !ctx->players[i].clientId.empty()) {
                        json_array_append_new(playersArray, json_string(ctx->players[i].clientId.c_str()));
                    }
                }
                json_object_set_new(gameUpdate, "players", playersArray);
                
                mp_api_game(ctx->api, gameUpdate);
                json_decref(gameUpdate);
            }
        }
	} else if (strcmp(event, "leaved") == 0) {
        if (clientId) {
            remove_player(*ctx, clientId);
        }
    } else if (strcmp(event, "game") == 0) {
        // Process updates from other clients
        std::string myClientId = (ctx->myPlayerIndex >= 0) ? ctx->players[ctx->myPlayerIndex].clientId : "";
        if (clientId && data) {
            // Check if this is a state sync from host
            json_t *type_val = json_object_get(data, "type");
            if (json_is_string(type_val) && strcmp(json_string_value(type_val), "state_sync") == 0) {
                // Sync game state from host
                json_t *foodX = json_object_get(data, "foodX");
                json_t *foodY = json_object_get(data, "foodY");
                json_t *matchTime = json_object_get(data, "matchStartTime");
                
                if (json_is_integer(foodX) && json_is_integer(foodY) && ctx->food) {
                    Position newFoodPos;
                    newFoodPos.x = json_integer_value(foodX);
                    newFoodPos.y = json_integer_value(foodY);
                    ctx->food->setPosition(newFoodPos);
                }
                
                if (json_is_integer(matchTime)) {
                    ctx->matchStartTime = json_integer_value(matchTime);
                }
                
                // Receive global pause command from any player
                json_t *pausedVal = json_object_get(data, "globalPaused");
                json_t *pausedBy = json_object_get(data, "pausedBy");
                if (json_is_boolean(pausedVal) && json_is_string(pausedBy)) {
                    bool isPaused = json_boolean_value(pausedVal);
                    std::string pauserClientId = json_string_value(pausedBy);
                    
                    // Sync pause timing from pauser
                    json_t *totalPausedVal = json_object_get(data, "totalPausedTime");
                    json_t *pauseStartVal = json_object_get(data, "pauseStartTime");
                    if (json_is_integer(totalPausedVal)) {
                        ctx->totalPausedTime = json_integer_value(totalPausedVal);
                    }
                    if (json_is_integer(pauseStartVal)) {
                        ctx->pauseStartTime = json_integer_value(pauseStartVal);
                    }
                    
                    // Apply pause state to all players
                    for (int i = 0; i < 4; i++) {
                        if (ctx->players[i].active) {
                            ctx->players[i].paused = isPaused;
                        }
                    }
                    
                    // Update game state (use enum value 6 for PAUSED, 16 for PLAYING)
                    if (ctx->gameStatePtr) {
                        int* statePtr = static_cast<int*>(ctx->gameStatePtr);
                        
                        if (isPaused) {
                            *statePtr = 6;  // GameState::PAUSED
                        } else {
                            // Only change to PLAYING if we're currently PAUSED
                            if (*statePtr == 6) {
                                *statePtr = 16;  // GameState::PLAYING
                            }
                        }
                    }
                    
                    // Update who paused
                    ctx->pausedByClientId = isPaused ? pauserClientId : "";
                    
                    // Find player name for message
                    int pauserIdx = find_player_by_client_id(*ctx, pauserClientId);
                    std::string playerName = pauserIdx >= 0 ? "Player " + std::to_string(pauserIdx + 1) : "Someone";
                    std::cout << (isPaused ? (playerName + " paused the game") : (playerName + " resumed the game")) << std::endl;
                }
                
                // Add existing players from host
                json_t *playersArray = json_object_get(data, "players");
                if (json_is_array(playersArray)) {
                    size_t index;
                    json_t *playerClientId;
                    json_array_foreach(playersArray, index, playerClientId) {
                        if (json_is_string(playerClientId)) {
                            std::string pId = json_string_value(playerClientId);
                            std::string myClientId = (ctx->myPlayerIndex >= 0) ? ctx->players[ctx->myPlayerIndex].clientId : "";
                            if (pId != myClientId && find_player_by_client_id(*ctx, pId) < 0) {
                                add_player(*ctx, pId);
                            }
                        }
                    }
                }
            } else if (std::string(clientId) != myClientId) {
                update_remote_player(*ctx, clientId, data);
            }
        }
    }
}

void send_game_state(GameContext& ctx, const Snake& snake)
{
    if (!ctx.api) {
        static bool warned = false;
        if (!warned) {
            std::cout << "API not initialized" << std::endl;
            warned = true;
        }
        return;
    }
    
    if (ctx.sessionId.empty()) {
        static bool warned = false;
        if (!warned) {
            std::cout << "Not in a session. Press H to host or L to list sessions" << std::endl;
            warned = true;
        }
        return;
    }

    Position head = snake.getHead();
    json_t *gameData = json_object();
    json_object_set_new(gameData, "x", json_integer(head.x));
    json_object_set_new(gameData, "y", json_integer(head.y));
    json_object_set_new(gameData, "score", json_integer(snake.getScore()));
    json_object_set_new(gameData, "alive", json_boolean(snake.isAlive()));
    
    // Send paused status
    int playerIdx = -1;
    for (int i = 0; i < 4; i++) {
        if (ctx.players[i].active && ctx.players[i].snake.get() == &snake) {
            playerIdx = i;
            break;
        }
    }
    if (playerIdx >= 0) {
        json_object_set_new(gameData, "paused", json_boolean(ctx.players[playerIdx].paused));
    }
    
    // Send full body for better sync
    json_t *bodyArray = json_array();
    for (const auto& segment : snake.getBody()) {
        json_t *seg = json_object();
        json_object_set_new(seg, "x", json_integer(segment.x));
        json_object_set_new(seg, "y", json_integer(segment.y));
        json_array_append_new(bodyArray, seg);
    }
    json_object_set_new(gameData, "body", bodyArray);
    
    int rc = mp_api_game(ctx.api, gameData);
    if (rc != MP_API_OK) {
        std::cerr << "Failed to send game state: " << rc << std::endl;
    }
    
    json_decref(gameData);
}

void sendPlayerUpdate(GameContext& ctx, int playerIndex)
{
    // Validate player index and state
    if (playerIndex < 0 || playerIndex >= 4)
        return;
    if (!ctx.players[playerIndex].active || !ctx.players[playerIndex].snake)
        return;
    if (ctx.sessionId.empty() || !ctx.api)
        return;
    
    // Throttle to avoid network spam
    Uint32 currentTime = SDL_GetTicks();
    if (currentTime - ctx.players[playerIndex].lastMpSent < 100) {
        return;
    }
    
    // Send the update
    send_game_state(ctx, *ctx.players[playerIndex].snake);
    ctx.players[playerIndex].lastMpSent = currentTime;
}

void sendGlobalPauseState(GameContext& ctx, bool paused, const std::string& pauserClientId)
{
    if (!ctx.api || ctx.sessionId.empty())
        return;
    
    json_t *pauseUpdate = json_object();
    json_object_set_new(pauseUpdate, "type", json_string("state_sync"));
    json_object_set_new(pauseUpdate, "globalPaused", json_boolean(paused));
    json_object_set_new(pauseUpdate, "pausedBy", json_string(pauserClientId.c_str()));
    json_object_set_new(pauseUpdate, "totalPausedTime", json_integer(ctx.totalPausedTime));
    json_object_set_new(pauseUpdate, "pauseStartTime", json_integer(ctx.pauseStartTime));
    mp_api_game(ctx.api, pauseUpdate);
    json_decref(pauseUpdate);
}

int multiplayer_host(GameContext& ctx)
{
	char *session = NULL;
	char *clientId = NULL;
	json_t *hostData = NULL;

	printf("Attempting to host session...\n");
	int rc = mp_api_host(ctx.api, &session, &clientId, &hostData);
	if (rc != MP_API_OK) {
		printf("Kunde inte skapa session: %d\n", rc);
		printf("Error codes: OK=0, ARGUMENT=1, STATE=2, CONNECT=3, PROTOCOL=4, IO=5, REJECTED=6\n");
		return -1;
	}

	// Store session ID and client ID in context
	ctx.sessionId = session;
	ctx.isHost = true;  // Mark as host
	printf("Du hostar session: %s (clientId: %s)\n", session, clientId);
	
	// Add myself as a player
	add_player(ctx, clientId);
	ctx.myPlayerIndex = find_player_by_client_id(ctx, clientId);
	
	// Initialize match start time as host
	ctx.matchStartTime = SDL_GetTicks();
	
	/* hostData kan innehålla extra data från servern (oftast tomt objekt) */
	if (hostData) {
		char *dataStr = json_dumps(hostData, JSON_INDENT(2));
		if (dataStr) {
			printf("Host data: %s\n", dataStr);
			free(dataStr);
		}
		json_decref(hostData);
	}
	free(session);
	free(clientId);

	return 0;
}

int multiplayer_list(GameContext& ctx)
{
	printf("Hämtar lista över publika sessioner...\n");

	json_t *sessionList = NULL;
	int rc = mp_api_list(ctx.api, &sessionList);
	if (rc != MP_API_OK) {
		printf("Kunde inte hämta session-lista: %d\n", rc);
		return -1;
	}

	ctx.availableSessions.clear();

	if (json_array_size(sessionList) == 0) {
		printf("Inga publika sessioner tillgängliga.\n");

	} else{
		printf("Totalt %zu sessioner.\n", json_array_size(sessionList));

		size_t index;
		json_t *value;
		printf("Tillgängliga publika sessioner (Press 1-4 to join):\n");

		json_array_foreach(sessionList, index, value) {
			json_t *sess_val = json_object_get(value, "id");

			if (json_is_string(sess_val))
            {
				const char *sessionId = json_string_value(sess_val);
				ctx.availableSessions.push_back(sessionId);
				printf(" [%zu] %s\n", index + 1, sessionId);
			}
		}

		printf("\n");
	}


	json_decref(sessionList);
	return 0;
}

int multiplayer_join(GameContext& ctx, const char* sessionId)
{
	char *joinedSession = NULL;
	char *joinedClientId = NULL;
	json_t *joinPayload = json_object();          /* t.ex. namn, färg osv. */
	json_object_set_new(joinPayload, "name", json_string("Spelare 1"));

	json_t *joinData = NULL;
	int rc = mp_api_join(ctx.api, sessionId, joinPayload, &joinedSession, &joinedClientId, &joinData);

	json_decref(joinPayload);  /* vår lokala payload */

	if (rc == MP_API_OK) {
		ctx.sessionId = joinedSession;
		ctx.isHost = false;  // Mark as non-host
		printf("Ansluten till session: %s (clientId: %s)\n", joinedSession, joinedClientId);
		
		// Add myself as a player
		add_player(ctx, joinedClientId);
		ctx.myPlayerIndex = find_player_by_client_id(ctx, joinedClientId);
		
		/* joinData kan innehålla status eller annan info */
		if (joinData) {
			json_decref(joinData);
		}
		free(joinedSession);
		free(joinedClientId);
	} else if (rc == MP_API_ERR_REJECTED) {
		/* t.ex. ogiltigt sessions‑ID, läs ev. joinData för mer info om du valde att ta emot det */
	} else {
		/* nätverksfel/protokollfel etc. */
	}

	return 0;
}

void add_player(GameContext& ctx, const std::string& clientId)
{
    // Find first available slot
    for (int i = 0; i < 4; i++) {
        if (!ctx.players[i].active) {
            Position spawnPos[] = {
                {GRID_WIDTH/4, GRID_HEIGHT/4},
                {3*GRID_WIDTH/4, GRID_HEIGHT/4},
                {GRID_WIDTH/4, 3*GRID_HEIGHT/4},
                {3*GRID_WIDTH/4, 3*GRID_HEIGHT/4}
            };
            
            SDL_Color colors[] = {
                {0, 255, 0, 255},   // Green
                {0, 0, 255, 255},   // Blue
                {255, 255, 0, 255}, // Yellow
                {255, 0, 255, 255}  // Magenta
            };
            
            ctx.players[i].snake = std::make_unique<Snake>(colors[i], spawnPos[i]);
            ctx.players[i].clientId = clientId;
            ctx.players[i].active = true;
            ctx.players[i].lastMpSent = 0;
            
            std::cout << "Player " << (i+1) << " joined: " << clientId << std::endl;
            break;
        }
    }
}

int find_player_by_client_id(const GameContext& ctx, const std::string& clientId)
{
    for (int i = 0; i < 4; i++)
    {
        if (ctx.players[i].active && ctx.players[i].clientId == clientId)
        {
            return i;
        }
    }
    return -1;
}

void update_remote_player(GameContext& ctx, const std::string& clientId, json_t* data)
{
    int idx = find_player_by_client_id(ctx, clientId);
    
    if (idx < 0 || !ctx.players[idx].snake) {
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
                newBody.push_back(pos);
            }
        }
        
        if (!newBody.empty())
        {
            ctx.players[idx].snake->setBody(newBody);
        }
    }

    json_t *score_val = json_object_get(data, "score");
    if (json_is_integer(score_val))
    {
        ctx.players[idx].snake->setScore(json_integer_value(score_val));
    }
    
    json_t *alive_val = json_object_get(data, "alive");
    if (json_is_boolean(alive_val))
    {
        ctx.players[idx].snake->setAlive(json_boolean_value(alive_val));
    }
    
    // Receive paused status
    json_t *paused_val = json_object_get(data, "paused");
    if (json_is_boolean(paused_val))
    {
        ctx.players[idx].paused = json_boolean_value(paused_val);
    }
}

void remove_player(GameContext& ctx, const std::string& clientId)
{
    for (int i = 0; i < 4; i++)
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
