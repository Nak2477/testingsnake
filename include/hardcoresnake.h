#ifndef HARDCORESNAKE_H
#define HARDCORESNAKE_H

#include "config.h"
#include <SDL2/SDL_ttf.h>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <iostream>
#include <deque>
#include <memory>
#include <unordered_map>

enum class Direction {
    UP,
    DOWN,
    LEFT,
    RIGHT,
    NONE
};

struct Position {
    int x;
    int y;
    
    bool operator==(const Position& other) const {
        return x == other.x && y == other.y;
    }
};


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
    void reset(const Position& startPos);

    void setBody(const std::deque<Position>& newBody);
    const std::deque<Position>& getBody() const { return body; }

    Position getHead() const { return body.front(); }
    SDL_Color getColor() const { return color; }

    bool isAlive() const { return alive; }
    void setAlive(bool status) { alive = status; }
    int getScore() const { return score; }
    void setScore(int newScore) { score = newScore; }
};

struct PlayerSlot {
    std::unique_ptr<Snake> snake;
    std::string clientId;
    bool active;
    bool paused;
    Uint32 lastMpSent;
};

class Food {
private:
    Position pos;
    SDL_Color color = Config::Render::FOOD_COLOR;
    
public:
    Food();
    void spawn(const std::unordered_map<int, bool>& occupiedPositions);
    void setPosition(const Position& newPos) { pos = newPos; }
    Position getPosition() const { return pos; }
    SDL_Color getColor() const { return color; }
};

#endif // HARDCORESNAKE_H
