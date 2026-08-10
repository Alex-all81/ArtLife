#include "Simulation.h"
#include <fstream>
#include <execution>
#include <algorithm>
#include <nlohmann/json.hpp>

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
    
    // Инициализация растений
    for(int i = 0; i < numPlants; ++i) {
        int idx = rng() % (width * height);
        Plant p; 
        p.alive = true;
        p.energy = 5.0f;
        (*currentGrid)[idx].plants.push_back(p);
    }
    
    // Инициализация животных
    for(int i = 0; i < numAnimals; ++i) {
        int idx = rng() % (width * height);
        if(!(*currentGrid)[idx].animal.alive) {
            Animal a; 
            a.alive = true; 
            a.id = i + 1;
            a.energy = a.genes.size * 10.0f;
            // 0.0 - строгий веган, 1.0 - строгий хищник
            a.genes.dietBias = (float)(rng() % 100) / 100.0f; 
            (*currentGrid)[idx].animal = a;
        }
    }
}

void Simulation::update() {
    // 1. Очистка буфера T+1 (оставляем только среду, очищаем агентов)
    std::for_each(std::execution::par_unseq, cellIndices.begin(), cellIndices.end(), [&](int idx) {
        (*nextGrid)[idx].plants.clear();
        (*nextGrid)[idx].animal.alive = false;
        (*nextGrid)[idx].fertility = (*currentGrid)[idx].fertility * (1.0f - fertility_decay);
        (*nextGrid)[idx].carrion = (*currentGrid)[idx].carrion * 0.98f; // Постепенное разложение падали
        (*nextGrid)[idx].lock.clear();
    });
    
    // 2. Расчет такта параллельно по всем ячейкам
    std::for_each(std::execution::par_unseq, cellIndices.begin(), cellIndices.end(), [&](int idx) {
        int x = idx % width;
        int y = idx / width;
        std::mt19937 local_rng(tick * 0x1234567 + idx); // Изолированный RNG для потока
        processCell(x, y, local_rng);
    });
    
    std::swap(currentGrid, nextGrid);
    tick++;
}

void Simulation::processCell(int x, int y, std::mt19937& rng) {
    int idx = y * width + x;
    Cell& cCell = (*currentGrid)[idx];
    
    bool plantEaten = false; // Флаг, чтобы съесть только одно растение за такт
    
    // ==========================================
    // ЖИВОТНЫЕ: Жизненный цикл и принятие решений
    // ==========================================
    if(cCell.animal.alive) {
        Animal a = cCell.animal;
        float maxEnergy = a.genes.size * 10.0f;
        
        // Базовый расход энергии на жизнь
        a.energy -= (a.genes.size * 0.05f + (a.genes.sight + a.genes.smell) * 0.2f + 0.3f);
        
        if(a.energy > 0) {
            // --- Питание ---
            if (a.genes.dietBias < 0.5f) { 
                // Травоядные: поедают растение в своей клетке
                if (!cCell.plants.empty()) {
                    a.energy += 15.0f;
                    plantEaten = true;
                }
            } else {
                // Хищники/падальщики: питаются трупами (carrion)
                if (cCell.carrion > 0.5f) {
                    float eatAmount = std::min(cCell.carrion, 15.0f);
                    a.energy += eatAmount;
                    
                    // Уменьшаем объем падали в будущем кадре атомарно
                    while((*nextGrid)[idx].lock.test_and_set(std::memory_order_acquire));
                    (*nextGrid)[idx].carrion = std::max(0.0f, (*nextGrid)[idx].carrion - eatAmount);
                    (*nextGrid)[idx].lock.clear(std::memory_order_release);
                }
            }
            a.energy = std::min(a.energy, maxEnergy); // Ограничение сытости
            
            // --- Размножение ---
            if (a.energy > maxEnergy * a.genes.threshold) {
                a.energy -= maxEnergy * 0.4f; // Затраты родителя
                
                Animal child = a;
                child.id = rng();
                child.energy = maxEnergy * 0.4f;
                
                // Мутация гена диеты для наглядности (смещение на +/- 10%)
                float mut = ((float)(rng() % 100) / 100.0f - 0.5f) * 0.2f;
                child.genes.dietBias = std::clamp(child.genes.dietBias + mut, 0.0f, 1.0f);
                
                // Поиск клетки для потомка
                int dx = (rng() % 3) - 1;
                int dy = (rng() % 3) - 1;
                int nIdx = ((y + dy + height) % height) * width + ((x + dx + width) % width);
                
                if(!(*nextGrid)[nIdx].lock.test_and_set(std::memory_order_acquire)) {
                    if(!(*nextGrid)[nIdx].animal.alive) {
                        (*nextGrid)[nIdx].animal = child;
                    }
                    (*nextGrid)[nIdx].lock.clear(std::memory_order_release);
                }
            }
            
            // --- Движение ---
            int dx = (rng() % 3) - 1;
            int dy = (rng() % 3) - 1;
            int nIdx = ((y + dy + height) % height) * width + ((x + dx + width) % width);
            
            bool moved = false;
            // Попытка занять новую клетку в NextGrid
            if(!(*nextGrid)[nIdx].lock.test_and_set(std::memory_order_acquire)) {
                if(!(*nextGrid)[nIdx].animal.alive) {
                    (*nextGrid)[nIdx].animal = a;
                    moved = true;
                }
                (*nextGrid)[nIdx].lock.clear(std::memory_order_release);
            }
            
            // Если клетка занята, остаемся на месте
            if(!moved) {
                while((*nextGrid)[idx].lock.test_and_set(std::memory_order_acquire));
                if(!(*nextGrid)[idx].animal.alive) {
                    (*nextGrid)[idx].animal = a;
                }
                (*nextGrid)[idx].lock.clear(std::memory_order_release);
            }
        } else {
            // --- Смерть от голода ---
            while((*nextGrid)[idx].lock.test_and_set(std::memory_order_acquire));
            (*nextGrid)[idx].carrion += a.genes.size * 5.0f; // Превращается в питательную падаль
            (*nextGrid)[idx].lock.clear(std::memory_order_release);
        }
    }
    
    // ==========================================
    // РАСТЕНИЯ: Фотосинтез и размножение
    // ==========================================
    for(size_t i = 0; i < cCell.plants.size(); ++i) {
        // Если местное травоядное съело растение, удаляем последнее из списка
        if (plantEaten && i == cCell.plants.size() - 1) {
            continue; 
        }
        
        Plant p = cCell.plants[i];
        
        // Фотосинтез (получает энергию от солнца и плодородия почвы)
        p.energy += sunlight_base * p.genes.power * cCell.fertility;
        p.energy -= p.genes.size * 0.1f; // Расходы на поддержание биомассы
        
        // Размножение (если энергии много)
        if (p.energy > 15.0f) {
            p.energy -= 8.0f;
            
            // Выброс семени в соседнюю клетку
            int dx = (rng() % 3) - 1;
            int dy = (rng() % 3) - 1;
            int nIdx = ((y + dy + height) % height) * width + ((x + dx + width) % width);
            
            while((*nextGrid)[nIdx].lock.test_and_set(std::memory_order_acquire));
            if ((*nextGrid)[nIdx].plants.size() < 2) { // Строго не более 2 растений в клетке для оптимизации
                Plant child = p;
                child.energy = 5.0f;
                (*nextGrid)[nIdx].plants.push_back(child);
            }
            (*nextGrid)[nIdx].lock.clear(std::memory_order_release);
        }
        
        // Выживание
        if(p.energy > 0) {
            while((*nextGrid)[idx].lock.test_and_set(std::memory_order_acquire));
            if ((*nextGrid)[idx].plants.size() < 2) {
                (*nextGrid)[idx].plants.push_back(p);
            }
            (*nextGrid)[idx].lock.clear(std::memory_order_release);
        } else {
            // Смерть растения обогащает почву
            while((*nextGrid)[idx].lock.test_and_set(std::memory_order_acquire));
            (*nextGrid)[idx].fertility += p.genes.size * 0.2f; 
            (*nextGrid)[idx].lock.clear(std::memory_order_release);
        }
    }
}