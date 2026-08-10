#include "Simulation.h"
#include <fstream>
#include <execution>
#include <algorithm>
#include <nlohmann/json.hpp>

Simulation::Simulation(const std::string& configFile) {
    std::ifstream f(configFile);
    nlohmann::json config = nlohmann::json::parse(f);
    
    width = config.value("grid_width", 256);
    height = config.value("grid_height", 256);
    sunlight_base = config.value("sunlight_base", 1.5f);
    fertility_decay = config.value("fertility_decay", 0.001f);
    mutation_step = config.value("mutation_step", 0.1f);
    replace_factor = config.value("replace_factor", 0.1f);
    initial_herbivore_ratio = config.value("initial_herbivore_ratio", 0.5f);
    
    int numAnimals = config.value("initial_animals", 1000);
    int numPlants = config.value("initial_plants", 5000);
    
    gridA.resize(width * height);
    gridB.resize(width * height);
    currentGrid = &gridA;
    nextGrid = &gridB;
    
    cellIndices.resize(width * height);
    std::iota(cellIndices.begin(), cellIndices.end(), 0);
    
    std::mt19937 rng(42);
    
    for(int i = 0; i < numPlants; ++i) {
        int idx = rng() % (width * height);
        Plant p; 
        p.alive = true;
        p.energy = 5.0f;
        (*currentGrid)[idx].plants.push_back(p);
    }
    
    // Инициализация животных через общий метод
    addAnimals(numAnimals);
}

void Simulation::addAnimals(int count) {
    std::mt19937 rng(std::random_device{}());
    for(int i = 0; i < count; ++i) {
        int idx = rng() % (width * height);
        if(!(*currentGrid)[idx].animal.alive) {
            Animal a; 
            a.alive = true; 
            a.id = rng();
            a.energy = a.genes.size * 10.0f;
            
            // Распределение диеты в зависимости от конфига
            float rand_val = (float)(rng() % 100) / 100.0f;
            if (rand_val < initial_herbivore_ratio) {
                a.genes.dietBias = (float)(rng() % 30) / 100.0f; // 0.0 - 0.3 (травоядные)
            } else {
                a.genes.dietBias = 0.7f + (float)(rng() % 30) / 100.0f; // 0.7 - 1.0 (хищники)
            }
            (*currentGrid)[idx].animal = a;
        }
    }
}

void Simulation::update() {
    std::for_each(std::execution::par_unseq, cellIndices.begin(), cellIndices.end(), [&](int idx) {
        (*nextGrid)[idx].plants.clear();
        (*nextGrid)[idx].animal.alive = false;
        (*nextGrid)[idx].fertility = (*currentGrid)[idx].fertility * (1.0f - fertility_decay);
        (*nextGrid)[idx].carrion = (*currentGrid)[idx].carrion * 0.98f;
        (*nextGrid)[idx].lock.clear();
    });
    
    std::for_each(std::execution::par_unseq, cellIndices.begin(), cellIndices.end(), [&](int idx) {
        int x = idx % width;
        int y = idx / width;
        std::mt19937 local_rng(tick * 0x1234567 + idx); 
        processCell(x, y, local_rng);
    });
    
    std::swap(currentGrid, nextGrid);
    tick++;
}

void Simulation::processCell(int x, int y, std::mt19937& rng) {
    int idx = y * width + x;
    Cell& cCell = (*currentGrid)[idx];
    bool plantEaten = false; 
    
    // ЖИВОТНЫЕ
    if(cCell.animal.alive) {
        Animal a = cCell.animal;
        float maxEnergy = a.genes.size * 10.0f;
        a.energy -= (a.genes.size * 0.05f + (a.genes.sight + a.genes.smell) * 0.2f + 0.3f);
        
        if(a.energy > 0) {
            // Питание
            if (a.genes.dietBias < 0.5f) { 
                if (!cCell.plants.empty()) {
                    a.energy += 15.0f;
                    plantEaten = true;
                }
            } else {
                if (cCell.carrion > 0.5f) {
                    float eatAmount = std::min(cCell.carrion, 15.0f);
                    a.energy += eatAmount;
                    while((*nextGrid)[idx].lock.test_and_set(std::memory_order_acquire));
                    (*nextGrid)[idx].carrion = std::max(0.0f, (*nextGrid)[idx].carrion - eatAmount);
                    (*nextGrid)[idx].lock.clear(std::memory_order_release);
                }
            }
            a.energy = std::min(a.energy, maxEnergy);
            
            // Размножение и Мутации
            if (a.energy > maxEnergy * a.genes.threshold) {
                a.energy -= maxEnergy * 0.4f;
                Animal child = a;
                child.id = rng();
                child.energy = maxEnergy * 0.4f;
                
                // Функция-помощник для мутации отдельного гена
                auto mutate_gene = [&](float& gene, float min_v, float max_v) {
                    float r1 = (float)(rng() % 1000) / 1000.0f;
                    if (r1 < child.genes.mutability * replace_factor) {
                        // Полная замена
                        gene = min_v + ((float)(rng() % 1000) / 1000.0f) * (max_v - min_v);
                    } else {
                        float r2 = (float)(rng() % 1000) / 1000.0f;
                        if (r2 < child.genes.mutability) {
                            // Сдвиг по Гауссу (аппроксимация случайным блужданием)
                            float step = ((float)(rng() % 1000) / 1000.0f - 0.5f) * 2.0f * mutation_step;
                            gene = std::clamp(gene + step, min_v, max_v);
                        }
                    }
                };
                
                mutate_gene(child.genes.size, 0.1f, 10.0f);
                mutate_gene(child.genes.speed, 0.0f, 1.0f);
                mutate_gene(child.genes.power, 0.1f, 2.0f);
                mutate_gene(child.genes.threshold, 0.3f, 0.9f);
                mutate_gene(child.genes.mutability, 0.0f, 0.5f);
                mutate_gene(child.genes.dietBias, 0.0f, 1.0f);
                mutate_gene(child.genes.impulsivity, 0.0f, 1.0f);
                mutate_gene(child.genes.sight, 0.0f, 1.0f);
                mutate_gene(child.genes.smell, 0.0f, 1.0f);
                
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
            
            // Движение
            int dx = (rng() % 3) - 1;
            int dy = (rng() % 3) - 1;
            int nIdx = ((y + dy + height) % height) * width + ((x + dx + width) % width);
            
            bool moved = false;
            if(!(*nextGrid)[nIdx].lock.test_and_set(std::memory_order_acquire)) {
                if(!(*nextGrid)[nIdx].animal.alive) {
                    (*nextGrid)[nIdx].animal = a;
                    moved = true;
                }
                (*nextGrid)[nIdx].lock.clear(std::memory_order_release);
            }
            
            if(!moved) {
                while((*nextGrid)[idx].lock.test_and_set(std::memory_order_acquire));
                if(!(*nextGrid)[idx].animal.alive) {
                    (*nextGrid)[idx].animal = a;
                }
                (*nextGrid)[idx].lock.clear(std::memory_order_release);
            }
        } else {
            // Смерть от голода
            while((*nextGrid)[idx].lock.test_and_set(std::memory_order_acquire));
            (*nextGrid)[idx].carrion += a.genes.size * 5.0f;
            (*nextGrid)[idx].lock.clear(std::memory_order_release);
        }
    }
    
    // РАСТЕНИЯ
    for(size_t i = 0; i < cCell.plants.size(); ++i) {
        if (plantEaten && i == cCell.plants.size() - 1) continue; 
        
        Plant p = cCell.plants[i];
        p.energy += sunlight_base * p.genes.power * cCell.fertility;
        p.energy -= p.genes.size * 0.1f;
        
        if (p.energy > 15.0f) {
            p.energy -= 8.0f;
            int dx = (rng() % 3) - 1;
            int dy = (rng() % 3) - 1;
            int nIdx = ((y + dy + height) % height) * width + ((x + dx + width) % width);
            
            while((*nextGrid)[nIdx].lock.test_and_set(std::memory_order_acquire));
            if ((*nextGrid)[nIdx].plants.size() < 2) { 
                Plant child = p;
                child.energy = 5.0f;
                (*nextGrid)[nIdx].plants.push_back(child);
            }
            (*nextGrid)[nIdx].lock.clear(std::memory_order_release);
        }
        
        if(p.energy > 0) {
            while((*nextGrid)[idx].lock.test_and_set(std::memory_order_acquire));
            if ((*nextGrid)[idx].plants.size() < 2) {
                (*nextGrid)[idx].plants.push_back(p);
            }
            (*nextGrid)[idx].lock.clear(std::memory_order_release);
        } else {
            while((*nextGrid)[idx].lock.test_and_set(std::memory_order_acquire));
            (*nextGrid)[idx].fertility += p.genes.size * 0.2f; 
            (*nextGrid)[idx].lock.clear(std::memory_order_release);
        }
    }
}