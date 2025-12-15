#include "../include/game.h"
#include <iostream>
#include <ctime>

int main() {
    // Initialize random seed once at program startup
    srand(time(nullptr));
    
    try {
        Game game;
        game.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
