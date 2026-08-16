#pragma once
#include "Simulation.h"
#include <SDL.h>
#include <vector>
#include <cstdint>
#include <string>
#include <map>

class GUI {
public:
    GUI(Simulation& sim);
    ~GUI();
    
    void run();

private:
    Simulation& simulation;
    SDL_Window* window;
    SDL_GLContext gl_context;
    
    void renderImGui(bool drawWorld);
    uint32_t getHeatmapColor(float value); 
    void drawLegend(); 
    void renderGeneWindow(); 
    void renderFileMenu(); 
    
    unsigned int gridTexture; 
    std::vector<uint32_t> pixelBuffer;

       enum ViewMode {
        VIEW_CLASSIC = 0,
        VIEW_ANIMALS_ONLY,
        VIEW_PLANTS_ONLY,
        VIEW_PLANT_DENSITY,
        VIEW_HEATMAP_ENERGY,
        VIEW_HEATMAP_FERTILITY,
        VIEW_HEATMAP_AGE,
        VIEW_HEATMAP_DIET,
        VIEW_HEATMAP_SIZE,
        VIEW_HEATMAP_SPEED,
        VIEW_HEATMAP_POWER,
        VIEW_HEATMAP_THRESHOLD,
        VIEW_HEATMAP_MUTABILITY,
        VIEW_HEATMAP_IMPULSIVITY,
        VIEW_HEATMAP_SIGHT,
        VIEW_HEATMAP_SMELL
    };
    int currentViewMode = VIEW_CLASSIC;
        
    int animalsToAdd = 500;
    int plantsToAdd = 5000;
    
    bool isPaused = false;
	bool vsyncEnabled = true;
	double lastTickTimeMs = 0.0;
    char loadPathBuffer[256] = "";

    // --- Переменные для интерактивной подсветки тепловых карт ---
    bool isHighlighting = false;
    float highlightValue = 0.5f;
    float highlightDeviation = 0.1f;

    // Кеш статистики генов
    int lastStatTick = -1;
    bool forceStatsUpdate = true;
    std::map<std::string, GeneStats> geneStatsCache;

    // Векторы истории для графика
    int maxHistory = 1000;
    int lastRecordedTick = -1;
    std::vector<float> historyTicks;
    std::vector<float> historyAnimals;
    std::vector<float> historyHerbivores;
    std::vector<float> historyOmnivores;
    std::vector<float> historyCarnivores;
    std::vector<float> historyPlants;
};