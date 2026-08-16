#pragma once
#include "Simulation.h"
#include <SDL.h>
#include <vector>
#include <cstdint>
#include <string>
#include <map>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <functional>

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
    std::atomic<double> lastTickTimeMs{0.0};
    char loadPathBuffer[256] = "";

    // Локальные копии настроек для отвязки UI от ядра
    float ui_sunlight;
    float ui_fertility;
    float ui_mutation;
    float ui_replace;

    // --- Переменные для интерактивной подсветки тепловых карт ---
    bool isHighlighting = false;
    float highlightValue = 0.5f;
    float highlightDeviation = 0.1f;

    // --- АСИНХРОННОСТЬ, МНОГОПОТОЧНОСТЬ И ОЧЕРЕДЬ КОМАНД ---
    std::thread simThread;
    std::atomic<bool> simRunning{false};
    
    std::mutex actionMutex; // Очень быстрый мутекс только для очереди команд
    std::queue<std::function<void()>> actionQueue; // Очередь задач для ядра
    
    std::mutex snapMutex; // Блокировка копии для отрисовки
    std::vector<Cell> snapGrid;
    int snapTick = 0;
    std::atomic<bool> snapshotRequested{true};
    
    void simLoop(); // Фоновый бесконечный цикл расчетов
    // ---------------------------------------

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