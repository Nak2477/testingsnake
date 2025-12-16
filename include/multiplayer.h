
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

// Forward declarations
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

// Thread-safe message queue
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
    
    NetworkContext() : api(nullptr), isHost(false), lastStateSyncSent(0),
                       lastMessageReceived(0), connectionWarningTime(0) {}
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

// Player management
struct PlayerManager {
    std::array<PlayerSlot, 4> players;
    int myPlayerIndex;
    
    PlayerManager() : myPlayerIndex(-1) {}
};

// Main game context - composition of focused components
struct GameContext {
    NetworkContext network;
    MatchState match;
    PlayerManager players;
    Food* food;
    std::function<void(int)> onStateChange;  // Type-safe callback for state changes (receives GameState as int)
    
    GameContext() : food(nullptr) {}
};

// ========== INTERFACE ABSTRACTION ==========

// Interface for network management operations
// This allows for easier testing (mock implementations) and decoupling
class INetworkManager {
public:
    virtual ~INetworkManager() = default;
    
    // Connection management
    virtual bool initialize(const std::string& host, int port) = 0;
    virtual void shutdown() = 0;
    virtual bool isConnected() const = 0;
    
    // Session management
    virtual bool hostSession() = 0;
    virtual bool listSessions() = 0;
    virtual bool joinSession(const std::string& sessionId) = 0;
    virtual const std::vector<std::string>& getAvailableSessions() const = 0;
    
    // State queries
    virtual bool isHost() const = 0;
    virtual bool isInSession() const = 0;
    virtual const std::string& getSessionId() const = 0;
    virtual const std::string& getMyClientId() const = 0;
    
    // Message processing
    virtual void processMessages() = 0;
    
    // Game state updates
    virtual void sendPlayerUpdate(int playerIndex) = 0;
    virtual void sendPauseState(bool paused, const std::string& clientId) = 0;
    virtual void sendGameMessage(json_t* message) = 0;  // Send arbitrary game message
    
    // New methods for critical fixes
    virtual void sendPeriodicStateSync() = 0;  // Host: periodic full state broadcast
    
    // Access to internal context (for gradual migration)
    virtual NetworkContext& getNetworkContext() = 0;
    virtual const NetworkContext& getNetworkContext() const = 0;
};

// Concrete implementation of network manager
class NetworkManager : public INetworkManager {
private:
    GameContext* ctx;  // Pointer to game context
    
public:
    explicit NetworkManager(GameContext* context) : ctx(context) {}
    ~NetworkManager() override;
    
    // INetworkManager interface implementation
    bool initialize(const std::string& host, int port) override;
    void shutdown() override;
    bool isConnected() const override;
    
    bool hostSession() override;
    bool listSessions() override;
    bool joinSession(const std::string& sessionId) override;
    const std::vector<std::string>& getAvailableSessions() const override;
    
    bool isHost() const override;
    bool isInSession() const override;
    const std::string& getSessionId() const override;
    const std::string& getMyClientId() const override;
    
    void processMessages() override;
    
    void sendPlayerUpdate(int playerIndex) override;
    void sendPauseState(bool paused, const std::string& clientId) override;
    void sendGameMessage(json_t* message) override;
    
    // New methods for critical fixes
    void sendPeriodicStateSync();  // Host: periodic full state broadcast
    
    NetworkContext& getNetworkContext() override { return ctx->network; }
    const NetworkContext& getNetworkContext() const override { return ctx->network; }
};

