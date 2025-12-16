#ifndef HARDCORESNAKE_H
#define HARDCORESNAKE_H

#include <SDL2/SDL_ttf.h>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <iostream>
#include <deque>
#include <memory>
#include <unordered_map>

// Game constants
const int WINDOW_WIDTH = 800;
const int WINDOW_HEIGHT = 600;
const int GRID_SIZE = 20;
const int GRID_WIDTH = WINDOW_WIDTH / GRID_SIZE;
const int GRID_HEIGHT = WINDOW_HEIGHT / GRID_SIZE;
const int INITIAL_SPEED = 150; // milliseconds per update
const int MATCH_DURATION_SECONDS = 120; // 2 minutes per match (requirement 5.1)

// Direction enum
enum class Direction {
    UP,
    DOWN,
    LEFT,
    RIGHT,
    NONE
};

// Position structure
struct Position {
    int x;
    int y;
    
    bool operator==(const Position& other) const {
        return x == other.x && y == other.y;
    }
};

// Forward declaration
//struct GameContext;

// Snake class
class Snake {
private:
    std::deque<Position> body;
    Direction direction;
    Direction nextDirection;
    SDL_Color color;
    bool alive;
    int score;
    
public:
    Snake(SDL_Color snakeColor, Position startPos);
    
    void setDirection(Direction dir);
    void update();
    void grow();
    void reset(Position startPos);

    bool checkCollision(const Position& pos) const;
    bool checkSelfCollision() const;
    bool checkBoundaryCollision() const;
    
    void setBody(const std::deque<Position>& newBody);
    const std::deque<Position>& getBody() const { return body; }

    Position getHead() const { return body.front(); }
    SDL_Color getColor() const { return color; }

    bool isAlive() const { return alive; }
    void setAlive(bool status) { alive = status; }
    int getScore() const { return score; }
    void setScore(int newScore) { score = newScore; }
};

// PlayerSlot structure - represents a game slot that can hold a player/snake
struct PlayerSlot {
    std::unique_ptr<Snake> snake;
    std::string clientId;
    bool active;
    bool paused;  // Whether this player is paused
    Uint32 lastMpSent;  // Track last multiplayer send time for throttling
};

// Food class
class Food {
private:
    Position pos;
    SDL_Color color;
    
public:
    Food();
    void spawn(const std::unordered_map<int, bool>& occupiedPositions);
    void setPosition(const Position& newPos) { pos = newPos; }
    Position getPosition() const { return pos; }
    SDL_Color getColor() const { return color; }
};

#endif // HARDCORESNAKE_H
