#ifndef HARDCORESNAKE_H
#define HARDCORESNAKE_H

#include <SDL2/SDL_ttf.h>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <iostream>
#include <deque>
#include <vector>

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

// Snake class
class Snake {
private:
    std::deque<Position> body;
    Direction direction;
    Direction nextDirection;
    SDL_Color color;
    int playerNum;
    bool alive;
    int score;
    
public:
    Snake(int playerNumber, SDL_Color snakeColor, Position startPos);
    
    void setDirection(Direction dir);
    void update();
    void grow();
    void reset(Position startPos);
    void setBody(const std::deque<Position>& newBody);
    bool checkCollision(const Position& pos) const;
    bool checkSelfCollision() const;
    bool checkBoundaryCollision() const;
    
    const std::deque<Position>& getBody() const { return body; }
    Position getHead() const { return body.front(); }
    SDL_Color getColor() const { return color; }
    bool isAlive() const { return alive; }
    void setAlive(bool status) { alive = status; }
    int getScore() const { return score; }
    void addScore(int points) { score += points; }
    int getPlayerNum() const { return playerNum; }
};

// Food class
class Food {
private:
    Position pos;
    SDL_Color color;
    
public:
    Food();
    void spawn(const std::vector<Position>& occupiedPositions);
    Position getPosition() const { return pos; }
    SDL_Color getColor() const { return color; }
};

#endif // HARDCORESNAKE_H
