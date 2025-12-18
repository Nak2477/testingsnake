#include "hardcoresnake.h"
#include "multiplayer.h"
#include <cstring>

// Direction utility functions
const char* directionToString(Direction dir) {
    switch (dir) {
        case Direction::UP:    return "UP";
        case Direction::DOWN:  return "DOWN";
        case Direction::LEFT:  return "LEFT";
        case Direction::RIGHT: return "RIGHT";
        case Direction::NONE:  return "NONE";
        default: return "NONE";
    }
}

Direction stringToDirection(const char* str) {
    if (strcmp(str, "UP") == 0)    return Direction::UP;
    if (strcmp(str, "DOWN") == 0)  return Direction::DOWN;
    if (strcmp(str, "LEFT") == 0)  return Direction::LEFT;
    if (strcmp(str, "RIGHT") == 0) return Direction::RIGHT;
    return Direction::NONE;
}

// Utility function for random spawn positions (shared by Game and Multiplayer)
Position getRandomSpawnPositionUtil(const std::unordered_map<int, bool>& occupiedPositions) {
    const int MAX_ATTEMPTS = Config::Game::MAX_FOOD_SPAWN_ATTEMPTS;
    int attempts = 0;
    
    Position randomPos;
    do {
        // Ensure spawn position has room for 3-segment snake extending left
        randomPos.x = (rand() % (Config::Grid::WIDTH - 2)) + 2;  // Range: 2 to WIDTH-1
        randomPos.y = rand() % Config::Grid::HEIGHT;
        int key = randomPos.y * Config::Grid::WIDTH + randomPos.x;
        
        // Check that spawn position and the 2 cells to the left are all empty
        int leftKey1 = randomPos.y * Config::Grid::WIDTH + (randomPos.x - 1);
        int leftKey2 = randomPos.y * Config::Grid::WIDTH + (randomPos.x - 2);
        
        if (occupiedPositions.count(key) == 0 && 
            occupiedPositions.count(leftKey1) == 0 && 
            occupiedPositions.count(leftKey2) == 0) {
            break;
        }
        attempts++;
    } while (attempts < MAX_ATTEMPTS);
    
    return randomPos;
}

Snake::Snake(SDL_Color snakeColor, Position startPos)
    :   direction(Direction::NONE),
    nextDirection(Direction::NONE),
        color(snakeColor),
        alive(true),
        score(0) {
    
    body.push_back(startPos);
    body.push_back({startPos.x - 1, startPos.y});
    body.push_back({startPos.x - 2, startPos.y});
}

void Snake::setDirection(Direction dir)
{
    if (direction == Direction::NONE && body.size() >= 2)
    {
        Position head = body[0];
        Position neck = body[1];
        
        bool facingRight = (head.x > neck.x);
        bool facingLeft = (head.x < neck.x);
        bool facingDown = (head.y > neck.y);
        bool facingUp = (head.y < neck.y);
        
        if ((dir == Direction::LEFT && facingRight) || (dir == Direction::RIGHT && facingLeft))
            {
            std::reverse(body.begin(), body.end());
        } 
        else if ((dir == Direction::UP && facingDown) || (dir == Direction::DOWN && facingUp))
        {
            std::reverse(body.begin(), body.end());
        }
        
        nextDirection = dir;
        return;
    }
    
    if ((dir == Direction::UP && direction != Direction::DOWN) ||
        (dir == Direction::DOWN && direction != Direction::UP) ||
        (dir == Direction::LEFT && direction != Direction::RIGHT) ||
        (dir == Direction::RIGHT && direction != Direction::LEFT)) {
        nextDirection = dir;
    }
}

void Snake::update()
{
    if (!alive) return;
    
    direction = nextDirection;
    
    if (direction == Direction::NONE) return;
    
    Position newHead = body.front();
    
    switch (direction)
    {
        case Direction::UP:    newHead.y--; break;
        case Direction::DOWN:  newHead.y++; break;
        case Direction::LEFT:  newHead.x--; break;
        case Direction::RIGHT: newHead.x++; break;
        case Direction::NONE:  break;
    }
    
    body.push_front(newHead);
    body.pop_back();
}

void Snake::grow()
{
    if (body.empty()) return;
    
    Position tail = body.back();
    body.push_back(tail);
    score += 10;
}

void Snake::reset(const Position& startPos)
{
    body.clear();
    body.push_back(startPos);
    body.push_back({startPos.x - 1, startPos.y});
    body.push_back({startPos.x - 2, startPos.y});
    
    direction = Direction::NONE;
    nextDirection = Direction::NONE;
    alive = true;
    score -= 10;
}

void Snake::setBody(const std::deque<Position>& newBody)
{
    if (!newBody.empty())
    {
        body = newBody;
    }
}

Food::Food()
{
}

void Food::spawn(const std::unordered_map<int, bool>& occupiedPositions)
{
    bool validPosition = false;
    int attempts = 0;
    const int MAX_ATTEMPTS = Config::Game::MAX_FOOD_SPAWN_ATTEMPTS;
    
    while (!validPosition && attempts < MAX_ATTEMPTS)
    {
        pos.x = std::rand() % Config::Grid::WIDTH;
        pos.y = std::rand() % Config::Grid::HEIGHT;
        
        int key = pos.y * Config::Grid::WIDTH + pos.x;
        validPosition = (occupiedPositions.count(key) == 0);  // O(1) lookup!
        attempts++;
    }
    
    if (!validPosition)
    {
        std::cerr << "WARNING: Could not find empty spot for food after " 
                  << MAX_ATTEMPTS << " attempts. Grid may be full." << std::endl;
        pos.x = std::rand() % Config::Grid::WIDTH;
        pos.y = std::rand() % Config::Grid::HEIGHT;
    }
}
