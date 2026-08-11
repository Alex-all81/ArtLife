#pragma once
#include "Agent.h"
#include <vector>
#include <string>
#include <random>
#include <map>

// Структура для хранения статистики популяции (для GUI)
struct GeneStats {
    float min_val = 0.0f;
    float max_val = 0.0f;
    float median = 0.0f;
};

class Simulation {
public:
    Simulation(const std::string& configFile);
    ~Simulation();
    
    void update();
    const std::vector<Cell>& getGrid() const { return *currentGrid; }
    
    int getWidth() const { return width; }
    int getHeight() const { return height; }
    int getTick() const { return tick; }
    
    float sunlight_base;
    float fertility_decay;
    float mutation_step;
    float replace_factor;
    float initial_herbivore_ratio;
    int maxAge{250};
    
    int record_interval;
    std::string records_dir;
    std::string saves_dir;
    
    void addAnimals(int count);
    void addPlants(int count);
    void restart(const std::string& configFile);
    void removeAnimals(int count);
    void removePlants(int count);
    // Сериализация
    void saveSnapshot(const std::string& filepath);
    bool loadSnapshot(const std::string& filepath);
    
    // Статистика генов
    std::map<std::string, GeneStats> getGeneStatistics() const;
    
private:
    int width, height;
    int tick = 0;
    
    std::vector<Cell> gridA;
    std::vector<Cell> gridB;
    
    std::vector<Cell>* currentGrid;
    std::vector<Cell>* nextGrid;
    
    std::vector<int> cellIndices;
    std::string session_id;
    
    void processCell(int x, int y, std::mt19937& rng);
    void ensureDirectoriesExist();
};