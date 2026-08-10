#include "Simulation.h"
#include <fstream>
#include <execution>
#include <algorithm>

Simulation::Simulation(const std::string& configFile) {
    std::ifstream f(configFile);
    nlohmann::json config = nlohmann::json::parse(f);
    
    width = config["grid_width"];
    height = config["grid_height"];
    sunlight_base = config["sunlight_base"];
    fertility_decay = config["fertility_decay"];
    mutation_step = config["mutation_step"];
    
    int numAnimals = config["initial_animals"];
    int numPlants = config["initial_plants"];

    gridA.resize(width * height);
    gridB.resize(width * height);
    currentGrid = &gridA;
    nextGrid = &gridB;
    
    cellIndices.resize(width * height);
    std::iota(cellIndices.begin(), cellIndices.end(), 0);

    std::mt19937 rng(42);
    
    // Инициализация растений и животных
    for(int i = 0; i < numPlants; ++i) {
        int idx = rng() % (width * height);
        Plant p; p.alive = true;
        (*currentGrid)[idx].plants.push_back(p);
    }
    
    for(int i = 0; i < numAnimals; ++i) {
        int idx = rng() % (width * height);
        if(!(*currentGrid)[idx].animal.alive) {
            Animal a; 
            a.alive = true; 
            a.id = i + 1;
            a.genes.dietBias = (float)(rng() % 100) / 100.0f; // Разные диеты
            (*currentGrid)[idx].animal = a;
        }
    }
}

void Simulation::update() {
    // Очистка nextGrid (копируем только среду, очищаем агентов)
    std::for_each(std::execution::par_unseq, cellIndices.begin(), cellIndices.end(), [&](int idx) {
        (*nextGrid)[idx].plants.clear();
        (*nextGrid)[idx].animal.alive = false;
        (*nextGrid)[idx].fertility = (*currentGrid)[idx].fertility * (1.0f - fertility_decay);
        (*nextGrid)[idx].carrion = (*currentGrid)[idx].carrion * 0.99f; // Распад падали
        (*nextGrid)[idx].lock.clear();
    });

    // Расчет такта параллельно по всем ячейкам
    std::for_each(std::execution::par_unseq, cellIndices.begin(), cellIndices.end(), [&](int idx) {
        int x = idx % width;
        int y = idx / width;
        // Локальный генератор для потока
        std::mt19937 local_rng(tick * 0x1234567 + idx); 
        processCell(x, y, local_rng);
    });

    // Свап буферов (Тройная буферизация для UI потребовала бы еще один буфер)
    std::swap(currentGrid, nextGrid);
    tick++;
}

void Simulation::processCell(int x, int y, std::mt19937& rng) {
    int idx = y * width + x;
    Cell& cCell = (*currentGrid)[idx];
    
    // Обработка растений (упрощенно)
    for(auto& p : cCell.plants) {
        p.energy += sunlight_base * p.genes.power;
        p.energy -= p.genes.size * 0.01f;
        if(p.energy > 0) {
            // Растения остаются в своей клетке
            while((*nextGrid)[idx].lock.test_and_set(std::memory_order_acquire));
            (*nextGrid)[idx].plants.push_back(p);
            (*nextGrid)[idx].lock.clear(std::memory_order_release);
        } else {
            (*nextGrid)[idx].fertility += p.genes.size * 0.1f; // Смерть растения
        }
    }

    if(cCell.animal.alive) {
        resolveAnimalAction(x, y, cCell, rng);
    }
}

void Simulation::resolveAnimalAction(int x, int y, Cell& cCell, std::mt19937& rng) {
    Animal a = cCell.animal;
    
    // Базовый расход энергии
    a.energy -= (a.genes.size * 0.05f + (a.genes.sight + a.genes.smell) * 0.2f + 0.3f);
    
    if(a.energy <= 0) {
        // Смерть от голода
        int idx = y * width + x;
        while((*nextGrid)[idx].lock.test_and_set(std::memory_order_acquire));
        (*nextGrid)[idx].carrion += a.genes.size * 2.0f; // Падаль
        (*nextGrid)[idx].lock.clear(std::memory_order_release);
        return;
    }

    // Случайное блуждание (в MVP опускаем сложный поиск пути)
    int dx = (rng() % 3) - 1;
    int dy = (rng() % 3) - 1;
    
    int nx = (x + dx + width) % width; // Тороидальный мир
    int ny = (y + dy + height) % height;
    int nIdx = ny * width + nx;

    // Пытаемся занять новую клетку в NextGrid
    bool moved = false;
    if(!(*nextGrid)[nIdx].lock.test_and_set(std::memory_order_acquire)) {
        if(!(*nextGrid)[nIdx].animal.alive) {
            (*nextGrid)[nIdx].animal = a;
            moved = true;
        }
        (*nextGrid)[nIdx].lock.clear(std::memory_order_release);
    }

    // Если не смогли переместиться, остаемся на месте
    if(!moved) {
        int idx = y * width + x;
        while((*nextGrid)[idx].lock.test_and_set(std::memory_order_acquire));
        // Если место еще свободно
        if(!(*nextGrid)[idx].animal.alive) {
            (*nextGrid)[idx].animal = a;
        }
        (*nextGrid)[idx].lock.clear(std::memory_order_release);
    }
}