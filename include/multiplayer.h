
#pragma once

#include <iostream>
#include <vector>
#include <memory>
#include <array>
#include <queue>
#include <mutex>
#include <string>
#include <functional>
#include "hardcoresnake.h"

extern "C" {
    #include "../libs/MultiplayerApi.h"
    #include "../libs/jansson/jansson.h"
}

enum class GameState;  // Avoid circular dependency with game.h

// RAII wrapper for json_t to prevent memory leaks
class JsonPtr {
private:
    json_t* ptr;
    
public:
    explicit JsonPtr(json_t* p = nullptr) : ptr(p) {}
    ~JsonPtr() { if (ptr) json_decref(ptr); }
    
    // Delete copy operations to prevent double-free
    JsonPtr(const JsonPtr&) = delete;
    JsonPtr& operator=(const JsonPtr&) = delete;
    
    // Move operations
    JsonPtr(JsonPtr&& other) noexcept : ptr(other.ptr) { other.ptr = nullptr; }
    JsonPtr& operator=(JsonPtr&& other) noexcept {
        if (this != &other) {
            if (ptr) json_decref(ptr);
            ptr = other.ptr;
            other.ptr = nullptr;
        }
        return *this;
    }
    
    // Access operators
    json_t* get() const { return ptr; }
    json_t* operator->() const { return ptr; }
    operator bool() const { return ptr != nullptr; }
    
    // Release ownership
    json_t* release() {
        json_t* temp = ptr;
        ptr = nullptr;
        return temp;
    }
    
    // Reset with new pointer
    void reset(json_t* p = nullptr) {
        if (ptr) json_decref(ptr);
        ptr = p;
    }
};

// Fluent builder for JSON objects - eliminates boilerplate
class JsonBuilder {
private:
    JsonPtr root;
    
public:
    JsonBuilder() : root(json_object()) {}
    
    // Chainable setters for different types
    JsonBuilder& set(const char* key, const char* value) {
        json_object_set_new(root.get(), key, json_string(value));
        return *this;
    }
    
    JsonBuilder& set(const char* key, const std::string& value) {
        json_object_set_new(root.get(), key, json_string(value.c_str()));
        return *this;
    }
    
    JsonBuilder& set(const char* key, int value) {
        json_object_set_new(root.get(), key, json_integer(value));
        return *this;
    }
    
    JsonBuilder& set(const char* key, json_int_t value) {
        json_object_set_new(root.get(), key, json_integer(value));
        return *this;
    }
    
    JsonBuilder& set(const char* key, Uint32 value) {
        json_object_set_new(root.get(), key, json_integer(static_cast<json_int_t>(value)));
        return *this;
    }
    
    JsonBuilder& set(const char* key, bool value) {
        json_object_set_new(root.get(), key, json_boolean(value));
        return *this;
    }
    
    JsonBuilder& set(const char* key, json_t* value) {
        json_object_set_new(root.get(), key, value);  // Takes ownership
        return *this;
    }
    
    // Build and transfer ownership to JsonPtr
    json_t* build() {
        return root.release();
    }
    
    // Build directly into JsonPtr
    JsonPtr buildPtr() {
        return JsonPtr(root.release());
    }
};

// Network message types for thread-safe queue
enum class NetworkMessageType {
    PLAYER_JOINED,
    PLAYER_LEFT,
    GAME_UPDATE,
    SYNC_REQUEST,
    HEARTBEAT,
    HOST_DISCONNECT
};

struct NetworkMessage {
    NetworkMessageType type;
    std::string clientId;
    std::string jsonData;  // Serialized JSON as string
};

class NetworkMessageQueue {
private:
    std::queue<NetworkMessage> messages;
    std::mutex mutex;
    
public:
    void push(const NetworkMessage& msg) {
        std::lock_guard<std::mutex> lock(mutex);
        messages.push(msg);
    }
    
    bool pop(NetworkMessage& msg) {
        std::lock_guard<std::mutex> lock(mutex);
        if (messages.empty()) return false;
        msg = messages.front();
        messages.pop();
        return true;
    }
    
    size_t size() {
        std::lock_guard<std::mutex> lock(mutex);
        return messages.size();
    }
};

// Network layer - handles all communication
struct NetworkContext {
    MultiplayerApi* api;
    std::string sessionId;
    std::string myClientId;  // My client ID from API
    std::string hostClientId;  // ClientId of the session host (for host disconnect detection)
    bool isHost;  // True if this client is hosting the session
    NetworkMessageQueue messageQueue;  // Thread-safe queue for network events
    std::vector<std::string> availableSessions;
    Uint32 lastStateSyncSent;  // Host: last time full state was broadcast
    Uint32 lastMessageReceived;  // Last time we received any message from server
    Uint32 connectionWarningTime;  // Time when we first detected connection issue
    bool connectionLost;  // Flag to trigger safe shutdown on next frame
    
    NetworkContext() : api(nullptr), isHost(false), lastStateSyncSent(0),
                       lastMessageReceived(0), connectionWarningTime(0), connectionLost(false) {}
};

// Match timing and state management
struct MatchState {
    Uint32 matchStartTime;  // When match started (synced from host)
    Uint32 syncedElapsedMs;  // Authoritative elapsed time from host
    Uint32 totalPausedTime;  // Total accumulated time paused (milliseconds)
    Uint32 pauseStartTime;  // When current pause started (0 if not paused)
    int winnerIndex;  // Index of match winner, -1 if no winner
    std::string pausedByClientId;  // ClientId of player who paused, empty if not paused
    
    MatchState() : matchStartTime(0), syncedElapsedMs(0), totalPausedTime(0), 
                   pauseStartTime(0), winnerIndex(-1) {}
    
    bool isPaused() const { return !pausedByClientId.empty(); }
};

// Player management with proper encapsulation
class PlayerManager {
private:
    std::array<PlayerSlot, Config::Game::MAX_PLAYERS> slots;
    int myIndex;
    
public:
    PlayerManager() : myIndex(-1) {}
    
    // Array-style access
    PlayerSlot& operator[](int i) { return slots[i]; }
    const PlayerSlot& operator[](int i) const { return slots[i]; }
    
    // Direct access to underlying array (for render functions that need it)
    std::array<PlayerSlot, Config::Game::MAX_PLAYERS>& getSlots() { return slots; }
    const std::array<PlayerSlot, Config::Game::MAX_PLAYERS>& getSlots() const { return slots; }
    
    // Convenience accessors for "my player"
    PlayerSlot& me() { return slots[myIndex]; }
    const PlayerSlot& me() const { return slots[myIndex]; }
    int myPlayerIndex() const { return myIndex; }
    void setMyPlayerIndex(int i) { myIndex = i; }
    bool hasMe() const { return myIndex >= 0; }
    
    // Validation
    bool isValid(int i) const { 
        return i >= 0 && i < Config::Game::MAX_PLAYERS && slots[i].active && slots[i].snake; 
    }
    
    // Search operations
    int findByClientId(const std::string& id) const {
        for (int i = 0; i < Config::Game::MAX_PLAYERS; i++) {
            if (slots[i].active && slots[i].clientId == id) return i;
        }
        return -1;
    }
    
    // Iteration support (allows range-based for loops)
    auto begin() { return slots.begin(); }
    auto end() { return slots.end(); }
    auto begin() const { return slots.begin(); }
    auto end() const { return slots.end(); }
    
    // Utility
    int activeCount() const {
        int count = 0;
        for (const auto& slot : slots) {
            if (slot.active) count++;
        }
        return count;
    }
};

struct GameContext {
    NetworkContext network;
    MatchState match;
    PlayerManager players;
    Food* food;
    std::function<void(int)> onStateChange;
    
    GameContext() : food(nullptr) {}
};

class NetworkManager {
private:
    GameContext* ctx;
    
public:
    explicit NetworkManager(GameContext* context) : ctx(context) {}
    ~NetworkManager();
    
    [[nodiscard]] bool initialize(const std::string& host, int port);
    void shutdown();
    bool isConnected() const;
    
    [[nodiscard]] bool hostSession();
    [[nodiscard]] bool listSessions();
    [[nodiscard]] bool joinSession(const std::string& sessionId);
    
    void processMessages();
    
    void sendPauseState(bool paused, const std::string& clientId);
    void sendGameMessage(json_t* message);
    
    void sendPlayerInput(Direction direction);
    void broadcastGameState(bool critical = false);
    void sendPeriodicStateSync();
    
    NetworkContext& getNetworkContext() { return ctx->network; }
    bool isHost() const { return isConnected() && ctx->network.isHost; }
    const NetworkContext& getNetworkContext() const { return ctx->network; }
};
