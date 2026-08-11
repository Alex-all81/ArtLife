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
    maxAge = config.value("base_max_age", 250);

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
        // Проверяем лимит ячейки перед посадкой
        if ((*currentGrid)[idx].plants.size() < 2) {
            Plant p; 
            p.alive = true;
            p.energy = 15.0f; // Большой стартовый запас энергии
            (*currentGrid)[idx].plants.push_back(p);
            
            // ВАЖНО: Добавляем удобрение при искусственной высадке, 
            // иначе на поздних этапах игры ростки умрут от голода
            (*currentGrid)[idx].fertility += 0.5f; 
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
    
    // ================== ЖИВОТНЫЕ ==================
    if(cCell.animal.alive) {
        Animal a = cCell.animal;
        float maxEnergy = a.genes.size * 10.0f;
        
        a.age++; // Животное стареет
        
        // Динамический максимальный возраст (крупные живут чуть дольше)
        int maxAgeFact = maxAge + static_cast<int>(a.genes.size * 20);
        
        // Базовый расход энергии на жизнь
        a.energy -= (a.genes.size * 0.05f + (a.genes.sight + a.genes.smell) * 0.2f + 0.3f);
        
        // Проверка: животное живо, если есть энергия И не наступила старость
        if(a.energy > 0 && a.age < maxAgeFact) {
            
            // --- Питание ---
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
                Animal child = a;
                child.id = rng();
                child.age = 0; // Потомок рождается с нулевым возрастом
                child.energy = a.energy * 0.5;
                a.energy -= child.energy;
                
                // Функция-помощник для мутации отдельного гена
                auto mutate_gene = [&](float& gene, float min_v, float max_v) {
                    float r1 = (float)(rng() % 1000) / 1000.0f;
                    if (r1 < child.genes.mutability * replace_factor) {
                        // Полная замена
                        gene = min_v + ((float)(rng() % 1000) / 1000.0f) * (max_v - min_v);
                    } else {
                        float r2 = (float)(rng() % 1000) / 1000.0f;
                        if (r2 < child.genes.mutability) {
                            auto const diapazon = max_v - min_v;
                            auto const base_step = mutation_step * diapazon;
                            // Сдвиг по Гауссу (аппроксимация случайным блужданием)
                            float step = ((float)(rng() % 1000) / 1000.0f - 0.5f) * 2.0f * base_step;
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
            
            // --- Движение ---
            int dx = (rng() % 3) - 1;
            int dy = (rng() % 3) - 1;
            int nIdx = ((y + dy + height) % height) * width + ((x + dx + width) % width);
            
            bool moved = false;
            
            // Если животное пытается двигаться (не стоит на месте)
            if (dx != 0 || dy != 0) {
                // Дополнительный штраф за вес при перемещении
                float moveCost = a.genes.size * 0.1f + 0.1f;
                a.energy -= moveCost;
            }
            
            // Перемещение (только если после штрафа за движение осталась энергия)
            if (a.energy > 0) {
                if(!(*nextGrid)[nIdx].lock.test_and_set(std::memory_order_acquire)) {
                    if(!(*nextGrid)[nIdx].animal.alive) {
                        (*nextGrid)[nIdx].animal = a;
                        moved = true;
                    }
                    (*nextGrid)[nIdx].lock.clear(std::memory_order_release);
                }
            }
            
            if(!moved && a.energy > 0) {
                while((*nextGrid)[idx].lock.test_and_set(std::memory_order_acquire));
                if(!(*nextGrid)[idx].animal.alive) {
                    (*nextGrid)[idx].animal = a;
                }
                (*nextGrid)[idx].lock.clear(std::memory_order_release);
            }
            
            // Если энергия упала ниже нуля при попытке сдвинуться:
            if (a.energy <= 0) {
                while((*nextGrid)[idx].lock.test_and_set(std::memory_order_acquire));
                (*nextGrid)[idx].carrion += a.genes.size * 0.2f; // Голодная смерть на ходу
                (*nextGrid)[idx].lock.clear(std::memory_order_release);
            }
            
        } else {
            // --- Смерть (от голода или старости) ---
            while((*nextGrid)[idx].lock.test_and_set(std::memory_order_acquire));
            
            if (a.energy <= 0) {
                // Смерть от истощения: мясо съедено самим организмом
                (*nextGrid)[idx].carrion += a.genes.size * 0.2f; 
            } else {
                // Смерть от старости: оставляет целую тушу
                (*nextGrid)[idx].carrion += a.genes.size * 5.0f; 
            }
            // В любом случае обогащает почву
            (*nextGrid)[idx].fertility += a.genes.size * 0.2f; 
            
            (*nextGrid)[idx].lock.clear(std::memory_order_release);
        }
    }
    
    // ================== РАСТЕНИЯ ==================
    for(size_t i = 0; i < cCell.plants.size(); ++i) {
        if (plantEaten && i == cCell.plants.size() - 1) continue; 
        
        Plant p = cCell.plants[i];
        
        // ВАЖНО: Добавлен базовый фотосинтез (0.1f + fertility). 
        // Теперь растения будут медленно расти даже на истощенной земле.
        p.energy += sunlight_base * p.genes.power * (0.1f + cCell.fertility);
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
    std::vector<float> diet, size, speed, power, threshold, mutab, impuls, sight, smell, energy, age;
    for (const auto& cell : *currentGrid) {
        if (cell.animal.alive) {
            const auto& g = cell.animal.genes;
            diet.push_back(g.dietBias);
            size.push_back(g.size);
            speed.push_back(g.speed);
            power.push_back(g.power);
            threshold.push_back(g.threshold);
            mutab.push_back(g.mutability);
            impuls.push_back(g.impulsivity);
            sight.push_back(g.sight);
            smell.push_back(g.smell);
            energy.push_back(cell.animal.energy);
            age.push_back(static_cast<float>(cell.animal.age));
        }
    }

    auto calc = [](std::vector<float>& vec) -> GeneStats {
        if (vec.empty()) return { 0, 0, 0 };
        std::sort(vec.begin(), vec.end());
        return { vec.front(), vec.back(), vec[vec.size() / 2] };
        };

    return {
        {"1. Diet (0=H, 1=C)", calc(diet)}, {"2. Size", calc(size)},
        {"3. Speed", calc(speed)},          {"4. Power", calc(power)},
        {"5. Threshold", calc(threshold)},  {"6. Mutability", calc(mutab)},
        {"7. Impulsivity", calc(impuls)},   {"8. Sight", calc(sight)},
        {"9. Smell", calc(smell)},          {"~ Energy", calc(energy)},
        {"~ Age", calc(age)}
    };
}

void Simulation::restart(const std::string& configFile) {
    std::ifstream f(configFile);
    nlohmann::json config;
    if (f.is_open()) config = nlohmann::json::parse(f);

    sunlight_base = config.value("sunlight_base", 1.5f);
    fertility_decay = config.value("fertility_decay", 0.001f);
    mutation_step = config.value("mutation_step", 0.1f);
    replace_factor = config.value("replace_factor", 0.1f);
    initial_herbivore_ratio = config.value("initial_herbivore_ratio", 0.5f);

    int numAnimals = config.value("initial_animals", 1000);
    int numPlants = config.value("initial_plants", 5000);

    tick = 0;
    std::fill(currentGrid->begin(), currentGrid->end(), Cell());
    std::fill(nextGrid->begin(), nextGrid->end(), Cell());

    // Новый ID сессии для автосохранений
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y%m%d_%H%M%S");
    session_id = ss.str() + "_restarted";
    std::filesystem::create_directories(records_dir + "/" + session_id);

    addPlants(numPlants);
    addAnimals(numAnimals);
}

void Simulation::removeAnimals(int count) {
    std::mt19937 rng(std::random_device{}());
    std::vector<int> alive_indices;
    for (size_t i = 0; i < currentGrid->size(); ++i) {
        if ((*currentGrid)[i].animal.alive) alive_indices.push_back(i);
    }
    std::shuffle(alive_indices.begin(), alive_indices.end(), rng);
    int to_remove = std::min(count, (int)alive_indices.size());
    for (int i = 0; i < to_remove; ++i) (*currentGrid)[alive_indices[i]].animal.alive = false;
}

void Simulation::removePlants(int count) {
    std::mt19937 rng(std::random_device{}());
    std::vector<int> plant_indices;
    for (size_t i = 0; i < currentGrid->size(); ++i) {
        for (size_t p = 0; p < (*currentGrid)[i].plants.size(); ++p) plant_indices.push_back(i);
    }
    std::shuffle(plant_indices.begin(), plant_indices.end(), rng);
    int to_remove = std::min(count, (int)plant_indices.size());
    for (int i = 0; i < to_remove; ++i) {
        if (!(*currentGrid)[plant_indices[i]].plants.empty()) {
            (*currentGrid)[plant_indices[i]].plants.pop_back();
        }
    }
}