#include "Simulation.h"
#include <fstream>
#include <execution>
#include <algorithm>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

Simulation::Simulation(const std::string& configFile) {
    std::ifstream f(configFile);
    nlohmann::json config;
    if (f.is_open()) {
        config = nlohmann::json::parse(f);
    }
    
    width = config.value("grid_width", 256);
    height = config.value("grid_height", 256);
    sunlight_base = config.value("sunlight_base", 1.5f);
    fertility_decay = config.value("fertility_decay", 0.001f);
    mutation_step = config.value("mutation_step", 0.1f);
    replace_factor = config.value("replace_factor", 0.1f);
    initial_herbivore_ratio = config.value("initial_herbivore_ratio", 0.5f);
    record_interval = config.value("record_interval", 500);
    records_dir = config.value("records_dir", "records");
    saves_dir = config.value("saves_dir", "saves");
    
    int numAnimals = config.value("initial_animals", 1000);
    int numPlants = config.value("initial_plants", 5000);
    
    gridA.resize(width * height);
    gridB.resize(width * height);
    currentGrid = &gridA;
    nextGrid = &gridB;
    cellIndices.resize(width * height);
    std::iota(cellIndices.begin(), cellIndices.end(), 0);
    
    ensureDirectoriesExist();
    
    // Генерация уникального ID сессии (Timestamp)
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y%m%d_%H%M%S");
    session_id = ss.str();
    
    fs::create_directories(records_dir + "/" + session_id);
    
    addPlants(numPlants);
    addAnimals(numAnimals);
}

Simulation::~Simulation() {
    // Автосохранение при выходе
    std::string save_path = saves_dir + "/save_" + session_id + "_tick" + std::to_string(tick) + ".bin";
    saveSnapshot(save_path);
}

void Simulation::ensureDirectoriesExist() {
    fs::create_directories(records_dir);
    fs::create_directories(saves_dir);
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
            float rand_val = (float)(rng() % 100) / 100.0f;
            if (rand_val < initial_herbivore_ratio) {
                a.genes.dietBias = (float)(rng() % 30) / 100.0f;
            } else {
                a.genes.dietBias = 0.7f + (float)(rng() % 30) / 100.0f;
            }
            (*currentGrid)[idx].animal = a;
        }
    }
}

void Simulation::addPlants(int count) {
    std::mt19937 rng(std::random_device{}());
    for(int i = 0; i < count; ++i) {
        int idx = rng() % (width * height);
        Plant p; 
        p.alive = true;
        p.energy = 5.0f;
        (*currentGrid)[idx].plants.push_back(p);
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
    
    // Автоматическая запись кадров визуализации
    if (record_interval > 0 && tick % record_interval == 0) {
        std::string frame_path = records_dir + "/" + session_id + "/frame_" + std::to_string(tick) + ".bin";
        saveSnapshot(frame_path);
    }
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
}

// --- СЕРИАЛИЗАЦИЯ ---
void Simulation::saveSnapshot(const std::string& filepath) {
    std::ofstream out(filepath, std::ios::binary);
    if (!out) return;
    
    out.write(reinterpret_cast<const char*>(&tick), sizeof(tick));
    out.write(reinterpret_cast<const char*>(&width), sizeof(width));
    out.write(reinterpret_cast<const char*>(&height), sizeof(height));
    
    for (const auto& cell : *currentGrid) {
        out.write(reinterpret_cast<const char*>(&cell.animal), sizeof(Animal));
        size_t pSize = cell.plants.size();
        out.write(reinterpret_cast<const char*>(&pSize), sizeof(pSize));
        for (const auto& p : cell.plants) {
            out.write(reinterpret_cast<const char*>(&p), sizeof(Plant));
        }
        out.write(reinterpret_cast<const char*>(&cell.fertility), sizeof(float));
        out.write(reinterpret_cast<const char*>(&cell.carrion), sizeof(float));
    }
}

bool Simulation::loadSnapshot(const std::string& filepath) {
    std::ifstream in(filepath, std::ios::binary);
    if (!in) return false;
    
    int r_tick, r_w, r_h;
    in.read(reinterpret_cast<char*>(&r_tick), sizeof(r_tick));
    in.read(reinterpret_cast<char*>(&r_w), sizeof(r_w));
    in.read(reinterpret_cast<char*>(&r_h), sizeof(r_h));
    
    if (r_w != width || r_h != height) return false; // Защита от смены разрешения
    
    tick = r_tick;
    for (auto& cell : *currentGrid) {
        cell.plants.clear();
        in.read(reinterpret_cast<char*>(&cell.animal), sizeof(Animal));
        size_t pSize;
        in.read(reinterpret_cast<char*>(&pSize), sizeof(pSize));
        for (size_t i = 0; i < pSize; ++i) {
            Plant p;
            in.read(reinterpret_cast<char*>(&p), sizeof(Plant));
            cell.plants.push_back(p);
        }
        in.read(reinterpret_cast<char*>(&cell.fertility), sizeof(float));
        in.read(reinterpret_cast<char*>(&cell.carrion), sizeof(float));
    }
    
    // Создаем новую сессию, чтобы не перезаписывать старую историю кадров
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y%m%d_%H%M%S");
    session_id = ss.str() + "_continued";
    fs::create_directories(records_dir + "/" + session_id);
    
    return true;
}

std::map<std::string, GeneStats> Simulation::getGeneStatistics() const {
    std::vector<float> diet, size, speed, power;
    for (const auto& cell : *currentGrid) {
        if (cell.animal.alive) {
            diet.push_back(cell.animal.genes.dietBias);
            size.push_back(cell.animal.genes.size);
            speed.push_back(cell.animal.genes.speed);
            power.push_back(cell.animal.genes.power);
        }
    }
    
    auto calc = [](std::vector<float>& vec) -> GeneStats {
        if (vec.empty()) return {0, 0, 0};
        std::sort(vec.begin(), vec.end());
        return { vec.front(), vec.back(), vec[vec.size() / 2] };
    };
    
    return {
        {"Diet (0=Herb, 1=Carn)", calc(diet)},
        {"Size", calc(size)},
        {"Speed", calc(speed)},
        {"Power", calc(power)}
    };
}