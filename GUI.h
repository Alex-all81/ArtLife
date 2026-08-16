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
	float currentHighlightPercentage = 0.0f;

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
    template <typename T>
    struct RingBuffer {
        std::vector<T> data;
        int offset = 0;
        
        void push_back(const T& value, int max_size) {
            if (data.size() < max_size) {
                data.push_back(value);
            } else {
                data[offset] = value;
                offset = (offset + 1) % max_size;
            }
        }
        
        void clear() {
            data.clear();
            offset = 0;
        }
        
        int size() const {
            return static_cast<int>(data.size());
        }
        
        T* get_data() {
            return data.data();
        }
    };

	struct GeneHistory {
        RingBuffer<float> min_vals;
        RingBuffer<float> median_vals;
        RingBuffer<float> max_vals;
    };
    std::map<std::string, GeneHistory> geneHistoryCache; // История для графиков
    std::map<std::string, bool> genePlotExpanded;

    // Векторы истории для графика
    int maxHistory = 1000;
    int lastRecordedTick = -1;
    RingBuffer<float> historyTicks;
    RingBuffer<float> historyAnimals;
    RingBuffer<float> historyHerbivores;
    RingBuffer<float> historyOmnivores;
    RingBuffer<float> historyCarnivores;
    RingBuffer<float> historyPlants;
};