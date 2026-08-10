#pragma once
#include "Simulation.h"
#include <SDL.h>
#include <vector>
#include <cstdint>

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
    uint32_t getHeatmapColor(float value); // Генератор цвета тепловой карты
    
    unsigned int gridTexture; 
    std::vector<uint32_t> pixelBuffer;
    
    // Расширенные режимы отображения
    enum ViewMode {
        VIEW_CLASSIC = 0,
        VIEW_ANIMALS_ONLY,
        VIEW_PLANTS_ONLY,
        VIEW_PLANT_DENSITY,
        VIEW_HEATMAP_DIET,
        VIEW_HEATMAP_SIZE,
        VIEW_HEATMAP_SPEED,
        VIEW_HEATMAP_POWER,
        VIEW_HEATMAP_MUTABILITY,
        VIEW_HEATMAP_IMPULSIVITY,
        VIEW_HEATMAP_SIGHT,
        VIEW_HEATMAP_SMELL
    };
    int currentViewMode = VIEW_CLASSIC;
    
    // UI стейт
    int animalsToAdd = 500;
    
    int maxHistory = 1000;
    int lastRecordedTick = -1;
    std::vector<float> historyTicks;
    std::vector<float> historyAnimals;
    std::vector<float> historyPlants;
};