
#pragma once

#include <iostream>
#include <vector>
#include <memory>
#include <array>
#include "rendergame.h"

extern "C" {
    #include "../libs/MultiplayerApi.h"
    #include "../libs/jansson/jansson.h"
}

struct GameContext {
    MultiplayerApi* api;
    std::string sessionId;
    int myPlayerIndex;
    bool isHost;  // True if this client is hosting the session
    std::array<PlayerSlot, 4> players;
    std::vector<std::string> availableSessions;
    Food* food;
    Uint32 matchStartTime;  // Synced from host
};

// Forward declarations

void on_multiplayer_event( const char *event, int64_t messageId, const char *clientId, json_t *data, void *user_data);
void send_game_state(GameContext& ctx, const Snake& snake);
void sendPlayerUpdate(GameContext& ctx, int playerIndex);

int multiplayer_host(GameContext& ctx);
int multiplayer_list(GameContext& ctx);
int multiplayer_join(GameContext& ctx, const char* sessionId);

void add_player(GameContext& ctx, const std::string& clientId);
int find_player_by_client_id(const GameContext& ctx, const std::string& clientId);
void update_remote_player(GameContext& ctx, const std::string& clientId, json_t* data);
void remove_player(GameContext& ctx, const std::string& clientId);
