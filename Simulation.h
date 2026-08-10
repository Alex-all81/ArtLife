#pragma once
#include "Agent.h"
#include <vector>
#include <string>
#include <random>

class Simulation {
public:
    Simulation(const std::string& configFile);
    
    void update();
    const std::vector<Cell>& getGrid() const { return *currentGrid; }
    
    int getWidth() const { return width; }
    int getHeight() const { return height; }
    int getTick() const { return tick; }
    
    // Публичные параметры для Live Tuning из GUI
    float sunlight_base;
    float fertility_decay;
    float mutation_step;
    float replace_factor;
    float initial_herbivore_ratio;
    
    // Метод для ручного добавления животных во время симуляции
    void addAnimals(int count);
    
private:
    int width, height;
    int tick = 0;
    
    std::vector<Cell> gridA;
    std::vector<Cell> gridB;
    
    std::vector<Cell>* currentGrid;
    std::vector<Cell>* nextGrid;
    
    std::vector<int> cellIndices;
    
    void processCell(int x, int y, std::mt19937& rng);
};