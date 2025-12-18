#include "../include/game.h"
#include <iostream>
#include <ctime>

int main() {
    
    srand(time(nullptr));
    
    try {
        Game game;
        game.run();
    } catch (const std::exception& e) {
        Logger::fatal("Error: ", e.what());
        return 1;
    }
    
    return 0;
}
