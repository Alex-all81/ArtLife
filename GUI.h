#pragma once
#include "Simulation.h"
#include <SDL.h>
#include <vector>
#include <cstdint>
#include <string>

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
    uint32_t getHeatmapColor(float value); 
    void drawLegend(); // Отрисовка легенды
    void renderGeneWindow(); // Окно распределения генов
    void renderFileMenu(); // Меню загрузки сохранений
    
    unsigned int gridTexture; 
    std::vector<uint32_t> pixelBuffer;
    
    enum ViewMode {
        VIEW_CLASSIC = 0,
        VIEW_ANIMALS_ONLY,
        VIEW_PLANTS_ONLY,
        VIEW_PLANT_DENSITY,
        VIEW_HEATMAP_ENERGY,  // Новое: энергия
        VIEW_HEATMAP_DIET,
        VIEW_HEATMAP_SIZE,
        VIEW_HEATMAP_SPEED,
        VIEW_HEATMAP_POWER
    };
    int currentViewMode = VIEW_CLASSIC;
    
    int animalsToAdd = 500;
    int plantsToAdd = 1000;
    
    bool isPaused = false;
    char loadPathBuffer[256] = "";
    
    // Кеш статистики для окна справа
    int lastStatTick = -1;
    std::map<std::string, GeneStats> geneStatsCache;
    
    int maxHistory = 1000;
    int lastRecordedTick = -1;
    std::vector<float> historyTicks;
    std::vector<float> historyAnimals;
    std::vector<float> historyPlants;
};