#include "Simulation.h"
#include "GUI.h"
#include <iostream>

int main(int argc, char* argv[]) {
    try {
        Simulation sim("config.json");
        GUI gui(sim);
        gui.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}