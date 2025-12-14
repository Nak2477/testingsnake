#include "multiplayer.h"


void on_multiplayer_event( const char *event, int64_t messageId, const char *clientId, json_t *data, void *user_data)
{
    GameContext* ctx = (GameContext*)user_data;
	
	char* strData = NULL;
	
	if(data)
		strData = json_dumps(data, JSON_INDENT(2));

	printf("Multiplayer event: %s (msgId: %lld, clientId: %s)\n", event, (long long)messageId, clientId ? clientId : "null");
	if (strData)
	{
		printf("Data: %s\n", strData);
	}

    /* event: "joined", "leaved" (om servern skickar det), eller "game" */
    if (strcmp(event, "joined") == 0) {
        std::string myClientId = (ctx->myPlayerIndex >= 0) ? ctx->players[ctx->myPlayerIndex].clientId : "";
        if (clientId && std::string(clientId) != myClientId) {
            add_player(*ctx, clientId);
        }
	} else if (strcmp(event, "leaved") == 0) {
        if (clientId) {
            remove_player(*ctx, clientId);
        }
    } else if (strcmp(event, "game") == 0) {
        if (clientId && data) {
            update_remote_player(*ctx, clientId, data);
        }
    }

	free(strData);

    /* data är ett json_t* (object); anropa json_incref(data) om du vill spara det efter callbacken */
}

void send_game_state(GameContext& ctx, const Snake& snake)
{
    if (!ctx.api) {
        static bool warned = false;
        if (!warned) {
            std::cout << "[MP] API not initialized" << std::endl;
            warned = true;
        }
        return;
    }
    
    if (ctx.sessionId.empty()) {
        static bool warned = false;
        if (!warned) {
            std::cout << "[MP] Not in a session. Press H to host or L to list sessions" << std::endl;
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
    if (rc == MP_API_OK) {
        static int sendCount = 0;
        if (++sendCount % 10 == 0) {  // Print every 10 sends
            std::cout << "[MP] Sent game state #" << sendCount << std::endl;
        }
    } else {
        std::cerr << "[MP] Failed to send game state: " << rc << std::endl;
    }
    
    json_decref(gameData);
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
	printf("Du hostar session: %s (clientId: %s)\n", session, clientId);
	
	// Add myself as a player
	add_player(ctx, clientId);
	ctx.myPlayerIndex = find_player_by_client_id(ctx, clientId);
	
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
			if (json_is_string(sess_val)) {
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
		printf("Ansluten till session: %s (clientId: %s)\n", joinedSession, joinedClientId);
		
		// Add myself as a player
		add_player(ctx, joinedClientId);
		ctx.myPlayerIndex = find_player_by_client_id(ctx, joinedClientId);
		
		/* joinData kan innehålla status eller annan info */
		if (joinData) json_decref(joinData);
		free(joinedSession);
		free(joinedClientId);
	} else if (rc == MP_API_ERR_REJECTED) {
		/* t.ex. ogiltigt sessions‑ID, läs ev. joinData för mer info om du valde att ta emot det */
	} else {
		/* nätverksfel/protokollfel etc. */
	}

	return 0;
}

// Helper functions for player management
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
            
            ctx.players[i].snake = std::make_unique<Snake>(i+1, colors[i], spawnPos[i]);
            ctx.players[i].clientId = clientId;
            ctx.players[i].active = true;
            
            std::cout << "Player " << (i+1) << " joined: " << clientId << std::endl;
            break;
        }
    }
}

int find_player_by_client_id(const GameContext& ctx, const std::string& clientId)
{
    for (int i = 0; i < 4; i++) {
        if (ctx.players[i].active && ctx.players[i].clientId == clientId) {
            return i;
        }
    }
    return -1;
}

void update_remote_player(GameContext& ctx, const std::string& clientId, json_t* data)
{
    int idx = find_player_by_client_id(ctx, clientId);
    if (idx < 0 || !ctx.players[idx].snake) return;
    
    // Update snake body from network data
    json_t *bodyArray = json_object_get(data, "body");
    if (json_is_array(bodyArray) && json_array_size(bodyArray) > 0) {
        std::deque<Position> newBody;
        
        size_t index;
        json_t *segment;
        json_array_foreach(bodyArray, index, segment) {
            json_t *x_val = json_object_get(segment, "x");
            json_t *y_val = json_object_get(segment, "y");
            
            if (json_is_integer(x_val) && json_is_integer(y_val)) {
                Position pos;
                pos.x = json_integer_value(x_val);
                pos.y = json_integer_value(y_val);
                newBody.push_back(pos);
            }
        }
        
        if (!newBody.empty()) {
            ctx.players[idx].snake->setBody(newBody);
        }
    }
    
    // Update score
    json_t *score_val = json_object_get(data, "score");
    if (json_is_integer(score_val)) {
        int score = json_integer_value(score_val);
        // Snake doesn't have setScore, so we can't update it directly
        // But the body sync is the most important part
    }
    
    // Update alive status
    json_t *alive_val = json_object_get(data, "alive");
    if (json_is_boolean(alive_val)) {
        ctx.players[idx].snake->setAlive(json_boolean_value(alive_val));
    }
}

void remove_player(GameContext& ctx, const std::string& clientId)
{
    for (int i = 0; i < 4; i++) {
        if (ctx.players[i].active && ctx.players[i].clientId == clientId) {
            ctx.players[i].active = false;
            ctx.players[i].snake = nullptr;
            ctx.players[i].clientId = "";
            std::cout << "Player " << (i+1) << " left" << std::endl;
            break;
        }
    }
}
