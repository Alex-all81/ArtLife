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
    for (int i = 0; i < count; ++i) {
        int idx = rng() % (width * height);
        if (!(*currentGrid)[idx].animal.alive) {
            Animal a;
            a.alive = true;
            a.id = rng();
            a.energy = a.genes.size * 10.0f * 0.5f; // При рождении Energy = MaxEnergy * 0.5
            float rand_val = (float)(rng() % 100) / 100.0f;
            if (rand_val < initial_herbivore_ratio) {
                a.genes.dietBias = (float)(rng() % 30) / 100.0f;
            }
            else {
                a.genes.dietBias = 0.7f + (float)(rng() % 30) / 100.0f;
            }
            (*currentGrid)[idx].animal = a;
        }
    }
}

void Simulation::addPlants(int count) {
    std::mt19937 rng(std::random_device{}());
    for (int i = 0; i < count; ++i) {
        int idx = rng() % (width * height);
        if ((*currentGrid)[idx].plants.size() < 2) {
            Plant p;
            p.alive = true;
            p.energy = p.genes.size * 10.0f * 0.5f;
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
        (*nextGrid)[idx].carrion = (*currentGrid)[idx].carrion * 0.99f; // Carrion разлагается 1% в такт
        (*nextGrid)[idx].fertility += (*currentGrid)[idx].carrion * 0.01f; // 1% переходит в плодородие
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

    // Подсчет растительной тени (vegetation_shadow) для зрения
    float sum_plant_size = 0.0f;
    for (const auto& p : cCell.plants) sum_plant_size += p.genes.size;
    float veg_shadow = std::min(1.0f, sum_plant_size / 10.0f);
    float plant_shadow = std::min(1.0f, sum_plant_size / 15.0f); // MaxShadowConst = 15

    int plantEatenIndex = -1;

    // ================== ЖИВОТНЫЕ ==================
    if (cCell.animal.alive) {
        Animal a = cCell.animal;
        float maxEnergy = a.genes.size * 10.0f;

        a.age++;
        int animalMaxAge = this->maxAge + static_cast<int>(a.genes.size * 20);

        // Базовый расход энергии по формуле из ТЗ
        a.energy -= (a.genes.size * 0.05f + (a.genes.sight + a.genes.smell) * 0.2f + 0.3f);

        if (a.energy > 0 && a.age < animalMaxAge) {

            float HerbEff = 0.8f - 0.6f * a.genes.dietBias;
            float CarnEff = 0.2f + 0.6f * a.genes.dietBias;
            bool ate = false;

            // --- Питание растениями ---
            if (a.genes.dietBias < 1.0f && !cCell.plants.empty() && HerbEff > 0.0f) {
                int target_p = 0;
                // С вероятностью Impulsivity выбирается случайное, иначе оптимальное
                if (cCell.plants.size() > 1 && (float)(rng() % 100) / 100.0f > a.genes.impulsivity) {
                    float max_val = -1.0f;
                    for (size_t i = 0; i < cCell.plants.size(); ++i) {
                        float val = cCell.plants[i].energy * HerbEff;
                        if (val > max_val) { max_val = val; target_p = i; }
                    }
                }
                else if (cCell.plants.size() > 1) {
                    target_p = rng() % cCell.plants.size();
                }

                a.energy += cCell.plants[target_p].energy * HerbEff;
                plantEatenIndex = target_p;
                ate = true;
            }
            // --- Питание мясом (carrion) ---
            else if (a.genes.dietBias > 0.0f && cCell.carrion > 0.0f && CarnEff > 0.0f) {
                float eatAmount = std::min({ CarnEff * cCell.carrion, maxEnergy - a.energy, cCell.carrion });
                if (eatAmount > 0.0f) {
                    a.energy += eatAmount;
                    while ((*nextGrid)[idx].lock.test_and_set(std::memory_order_acquire));
                    (*nextGrid)[idx].carrion = std::max(0.0f, (*nextGrid)[idx].carrion - eatAmount);
                    (*nextGrid)[idx].lock.clear(std::memory_order_release);
                    ate = true;
                }
            }

            a.energy = std::min(a.energy, maxEnergy);

            // --- Размножение ---
            if (a.energy >= maxEnergy * a.genes.threshold) {
                float repro_cost = a.energy * 0.4f; // Родитель теряет 40% текущей энергии
                a.energy -= repro_cost;

                Animal child = a;
                child.id = rng();
                child.age = 0;
                child.energy = repro_cost; // Потомку передается эта энергия

                auto mutate_gene = [&](float& gene, float min_v, float max_v) {
                    float r1 = (float)(rng() % 1000) / 1000.0f;
                    if (r1 < child.genes.mutability * replace_factor) {
                        gene = min_v + ((float)(rng() % 1000) / 1000.0f) * (max_v - min_v);
                    }
                    else {
                        float r2 = (float)(rng() % 1000) / 1000.0f;
                        if (r2 < child.genes.mutability) {
                            float step = ((float)(rng() % 1000) / 1000.0f - 0.5f) * 2.0f * (mutation_step * (max_v - min_v));
                            gene = std::clamp(gene + step, min_v, max_v);
                        }
                    }
                    };

                mutate_gene(child.genes.size, 0.1f, 10.0f);
                mutate_gene(child.genes.speed, 0.0f, 1.0f);
                mutate_gene(child.genes.power, 0.1f, 2.0f);
                mutate_gene(child.genes.threshold, 0.3f, 0.9f);
                mutate_gene(child.genes.mutability, 0.05f, 0.5f);
                mutate_gene(child.genes.dietBias, 0.0f, 1.0f);
                mutate_gene(child.genes.impulsivity, 0.0f, 1.0f);
                mutate_gene(child.genes.sight, 0.0f, 1.0f);
                mutate_gene(child.genes.smell, 0.0f, 1.0f);

                int dx = (rng() % 3) - 1;
                int dy = (rng() % 3) - 1;
                int nIdx = ((y + dy + height) % height) * width + ((x + dx + width) % width);

                if (!(*nextGrid)[nIdx].lock.test_and_set(std::memory_order_acquire)) {
                    if (!(*nextGrid)[nIdx].animal.alive) (*nextGrid)[nIdx].animal = child;
                    (*nextGrid)[nIdx].lock.clear(std::memory_order_release);
                }
            }

            // --- Движение и Охота ---
            int dx = 0, dy = 0;
            // Если поел, может остаться с вероятностью (1 - Impulsivity)
            if (!ate || (float)(rng() % 100) / 100.0f < a.genes.impulsivity) {
                dx = (rng() % 3) - 1;
                dy = (rng() % 3) - 1;
            }

            int nIdx = ((y + dy + height) % height) * width + ((x + dx + width) % width);

            if (dx != 0 || dy != 0) {
                // Стоимость движения: Size * 0.1 + 0.5
                a.energy -= (a.genes.size * 0.1f + 0.5f);
            }

            if (a.energy > 0) {
                bool attacker_survived = true;
                bool moved = false;

                // Проверка боя (если хищник/всеядный шагнул на клетку с другим животным)
                if ((dx != 0 || dy != 0) && a.genes.dietBias > 0.0f && (*currentGrid)[nIdx].animal.alive && (*currentGrid)[nIdx].animal.id != a.id) {
                    Animal prey = (*currentGrid)[nIdx].animal;

                    float prey_sight_eff = prey.genes.sight * (1.0f - veg_shadow);
                    float detect_prob = std::max(prey_sight_eff, prey.genes.smell);

                    // ИСПРАВЛЕНО: Урон теперь = Сила * Масса. Хищники смогут пробивать крупных травоядных.
                    float dmg_att = a.genes.power * a.genes.size;
                    float dmg_def = 0.0f;

                    // Жертва контратакует, если обнаружила и не сработала импульсивность
                    if ((float)(rng() % 100) / 100.0f < detect_prob && (float)(rng() % 100) / 100.0f > prey.genes.impulsivity) {
                        // ИСПРАВЛЕНО: Урон в защите также зависит от массы
                        dmg_def = prey.genes.power * prey.genes.size;
                    }

                    bool prey_dead = dmg_att > prey.genes.size * 0.8f;
                    bool att_dead = dmg_def > a.genes.size * 0.8f;

                    moved = true; // Считаем действие перемещения/боя совершенным

                    if (prey_dead && !att_dead) {
                        a.energy += prey.energy * 0.6f;
                        while ((*nextGrid)[nIdx].lock.test_and_set(std::memory_order_acquire));
                        (*nextGrid)[nIdx].carrion += prey.energy * 0.2f;
                        // Атакующий занимает клетку убитой жертвы
                        if (!(*nextGrid)[nIdx].animal.alive) (*nextGrid)[nIdx].animal = a;
                        (*nextGrid)[nIdx].lock.clear(std::memory_order_release);
                    }
                    else if (!prey_dead && att_dead) {
                        attacker_survived = false;
                        while ((*nextGrid)[idx].lock.test_and_set(std::memory_order_acquire));
                        (*nextGrid)[idx].carrion += (a.energy) * 0.8f;
                        (*nextGrid)[idx].fertility += (a.energy) * 0.2f;
                        (*nextGrid)[idx].lock.clear(std::memory_order_release);
                    }
                    else if (prey_dead && att_dead) {
                        attacker_survived = false;
                        while ((*nextGrid)[idx].lock.test_and_set(std::memory_order_acquire));
                        (*nextGrid)[idx].carrion += (a.energy) * 0.8f;
                        (*nextGrid)[idx].lock.clear(std::memory_order_release);

                        while ((*nextGrid)[nIdx].lock.test_and_set(std::memory_order_acquire));
                        (*nextGrid)[nIdx].carrion += (prey.energy) * 0.8f;
                        (*nextGrid)[nIdx].lock.clear(std::memory_order_release);
                    }
                    else {
                        // Оба выжили: атакующий не может занять клетку, остается на своей
                        moved = false;
                    }
                }

                // Обычное перемещение, если боя не было или он закончился ничьей (moved = false)
                if (attacker_survived && !moved && (dx != 0 || dy != 0)) {
                    if (!(*nextGrid)[nIdx].lock.test_and_set(std::memory_order_acquire)) {
                        if (!(*nextGrid)[nIdx].animal.alive) {
                            (*nextGrid)[nIdx].animal = a;
                            moved = true;
                        }
                        (*nextGrid)[nIdx].lock.clear(std::memory_order_release);
                    }
                }

                // Если не двигался или движение не удалось
                if (attacker_survived && !moved) {
                    while ((*nextGrid)[idx].lock.test_and_set(std::memory_order_acquire));
                    if (!(*nextGrid)[idx].animal.alive) (*nextGrid)[idx].animal = a;
                    (*nextGrid)[idx].lock.clear(std::memory_order_release);
                }

            }
            else {
                // Смерть от истощения при попытке двигаться
                while ((*nextGrid)[idx].lock.test_and_set(std::memory_order_acquire));
                (*nextGrid)[idx].carrion += maxEnergy * 0.2f;
                (*nextGrid)[idx].fertility += maxEnergy * 0.2f;
                (*nextGrid)[idx].lock.clear(std::memory_order_release);
            }

        }
        else {
            // Смерть от старости или начального голода
            while ((*nextGrid)[idx].lock.test_and_set(std::memory_order_acquire));
            (*nextGrid)[idx].carrion += maxEnergy * 0.2f;
            (*nextGrid)[idx].fertility += maxEnergy * 0.2f;
            (*nextGrid)[idx].lock.clear(std::memory_order_release);
        }
    }

    // ================== РАСТЕНИЯ ==================
    for (size_t i = 0; i < cCell.plants.size(); ++i) {
        if (plantEatenIndex == (int)i) continue;

        Plant p = cCell.plants[i];

        // Energy_gain = (Sunlight + Fertility) * Power * (1 - shadow)
        p.energy += (sunlight_base + cCell.fertility) * p.genes.power * (1.0f - plant_shadow);
        p.energy -= p.genes.size * 0.01f;

        float pMaxEnergy = p.genes.size * 10.0f;

        if (p.energy >= pMaxEnergy * p.genes.threshold) {
            float repro_cost = p.energy * 0.4f;
            p.energy -= repro_cost;

            int dx = (rng() % 3) - 1;
            int dy = (rng() % 3) - 1;
            int nIdx = ((y + dy + height) % height) * width + ((x + dx + width) % width);

            while ((*nextGrid)[nIdx].lock.test_and_set(std::memory_order_acquire));
            if ((*nextGrid)[nIdx].plants.size() < 2) {
                Plant child = p;
                child.energy = repro_cost;

                auto mutate_gene = [&](float& gene, float min_v, float max_v) {
                    float r1 = (float)(rng() % 1000) / 1000.0f;
                    if (r1 < child.genes.mutability * replace_factor) {
                        gene = min_v + ((float)(rng() % 1000) / 1000.0f) * (max_v - min_v);
                    }
                    else {
                        float r2 = (float)(rng() % 1000) / 1000.0f;
                        if (r2 < child.genes.mutability) {
                            float step = ((float)(rng() % 1000) / 1000.0f - 0.5f) * 2.0f * (mutation_step * (max_v - min_v));
                            gene = std::clamp(gene + step, min_v, max_v);
                        }
                    }
                    };
                mutate_gene(child.genes.size, 0.1f, 10.0f);
                mutate_gene(child.genes.power, 0.1f, 2.0f);
                mutate_gene(child.genes.threshold, 0.3f, 0.9f);
                mutate_gene(child.genes.mutability, 0.05f, 0.5f);

                (*nextGrid)[nIdx].plants.push_back(child);
            }
            (*nextGrid)[nIdx].lock.clear(std::memory_order_release);
        }

        if (p.energy > 0) {
            while ((*nextGrid)[idx].lock.test_and_set(std::memory_order_acquire));
            if ((*nextGrid)[idx].plants.size() < 2) {
                (*nextGrid)[idx].plants.push_back(p);
            }
            (*nextGrid)[idx].lock.clear(std::memory_order_release);
        }
        else {
            // 10% энергии растения возвращается в плодородие клетки
            while ((*nextGrid)[idx].lock.test_and_set(std::memory_order_acquire));
            (*nextGrid)[idx].fertility += pMaxEnergy * 0.1f;
            (*nextGrid)[idx].lock.clear(std::memory_order_release);
        }
    }
}

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

    if (r_w != width || r_h != height) return false;

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