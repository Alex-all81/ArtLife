#pragma once
#include "Simulation.h"
#include <SDL.h>

class GUI {
public:
    GUI(Simulation& sim);
    ~GUI();
    
    void run();

private:
    Simulation& simulation;
    SDL_Window* window;
    SDL_GLContext gl_context;
    
    void renderImGui();
    void renderGrid();
    
    // Текстура для быстрого вывода пикселей
    unsigned int gridTexture;
    std::vector<uint32_t> pixelBuffer;
};