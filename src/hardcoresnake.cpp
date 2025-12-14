#include "hardcoresnake.h"

// Snake implementation
Snake::Snake(int playerNumber, SDL_Color snakeColor, Position startPos)
    :   direction(Direction::NONE),
    nextDirection(Direction::NONE),
        color(snakeColor),
        playerNum(playerNumber),
        alive(true),
        score(0) {
    
    // Initialize snake with 3 segments
    body.push_back(startPos);
    body.push_back({startPos.x - 1, startPos.y});
    body.push_back({startPos.x - 2, startPos.y});
}

void Snake::setDirection(Direction dir)
{
    // Prevent reversing direction
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
    body.push_back(tail); // Add segment at tail position
    score += 10;
}

void Snake::reset(Position startPos)
{
    // 1. Reset body to 2 segments (or starting length)
    body.clear();
    body.push_back(startPos);
    body.push_back({startPos.x - 1, startPos.y});
    
    // 2. Reset state
    direction = Direction::NONE;
    nextDirection = Direction::NONE;
    alive = true;
    score = 0;
}

void Snake::setBody(const std::deque<Position>& newBody)
{
    if (!newBody.empty()) {
        body = newBody;
    }
}

bool Snake::checkCollision(const Position& pos) const
{
    for (const auto& segment : body) 
    {
        if (segment == pos) {
            return true;
        }
    }
    return false;
}

bool Snake::checkSelfCollision() const
{
    const Position& head = body.front();
    
    for (size_t i = 1; i < body.size(); i++)
    {
        if (body[i] == head) {
            return true;
        }
    }
    return false;
}

bool Snake::checkBoundaryCollision() const
{
    const Position& head = body.front();
    return head.x < 0 || head.x >= GRID_WIDTH || 
           head.y < 0 || head.y >= GRID_HEIGHT;
}

// Food implementation
Food::Food() : color{255, 0, 0, 255}
{
    std::srand(std::time(nullptr));
}

void Food::spawn(const std::vector<Position>& occupiedPositions)
{
    bool validPosition = false;
    
    while (!validPosition)
    {
        pos.x = std::rand() % GRID_WIDTH;
        pos.y = std::rand() % GRID_HEIGHT;
        
        validPosition = true;
        for (const auto& occupied : occupiedPositions)
        {
            if (pos == occupied) {
                validPosition = false;
                break;
            }
        }
    }
}
