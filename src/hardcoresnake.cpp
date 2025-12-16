#include "hardcoresnake.h"
#include "multiplayer.h"

// Snake implementation
Snake::Snake(SDL_Color snakeColor, Position startPos)
    :   direction(Direction::NONE),
    nextDirection(Direction::NONE),
        color(snakeColor),
        alive(true),
        score(0) {
    
    // Initialize snake with 3 segments
    body.push_back(startPos);
    body.push_back({startPos.x - 1, startPos.y});
    body.push_back({startPos.x - 2, startPos.y});
}

void Snake::setDirection(Direction dir)
{
    // If not moving yet, flip body to match first direction if needed
    if (direction == Direction::NONE && body.size() >= 2) {
        Position head = body[0];
        Position neck = body[1];
        
        // Check current body orientation
        bool facingRight = (head.x > neck.x);
        bool facingLeft = (head.x < neck.x);
        bool facingDown = (head.y > neck.y);
        bool facingUp = (head.y < neck.y);
        
        // If player wants opposite direction, flip the body
        if ((dir == Direction::LEFT && facingRight) || 
            (dir == Direction::RIGHT && facingLeft)) {
            std::reverse(body.begin(), body.end());
        } else if ((dir == Direction::UP && facingDown) || 
                   (dir == Direction::DOWN && facingUp)) {
            std::reverse(body.begin(), body.end());
        }
        
        nextDirection = dir;
        return;
    }
    
    // Prevent reversing direction when already moving
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
    // Note: srand() is called in main(), not here
}

void Food::spawn(const std::unordered_map<int, bool>& occupiedPositions)
{
    bool validPosition = false;
    
    while (!validPosition)
    {
        pos.x = std::rand() % GRID_WIDTH;
        pos.y = std::rand() % GRID_HEIGHT;
        
        int key = pos.y * GRID_WIDTH + pos.x;
        validPosition = (occupiedPositions.count(key) == 0);  // O(1) lookup!
    }
}
