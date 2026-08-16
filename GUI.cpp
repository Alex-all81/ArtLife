#include "GUI.h"
#include <GL/glew.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>
#include <algorithm>
#include <cmath>
#include <chrono>

GUI::GUI(Simulation& sim) : simulation(sim) {
    SDL_Init(SDL_INIT_VIDEO);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    
    window = SDL_CreateWindow("ALife Sim v2.0", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1400, 800, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    gl_context = SDL_GL_CreateContext(window);
    SDL_GL_SetSwapInterval(1); // 1 = включить VSync (ограничение кадров), 0 = снять лимит
    glewInit();
    
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 130");
    
    glGenTextures(1, &gridTexture);
    glBindTexture(GL_TEXTURE_2D, gridTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    pixelBuffer.resize(sim.getWidth() * sim.getHeight());

    // Инициализация локальных UI-переменных
    ui_sunlight = sim.sunlight_base;
    ui_fertility = sim.fertility_decay;
    ui_mutation = sim.mutation_step;
    ui_replace = sim.replace_factor;

    // Инициализация стартового снимка для рендера
    snapGrid = simulation.getGrid();
    
    // Запуск фонового потока расчетов
    simRunning = true;
    simThread = std::thread(&GUI::simLoop, this);
}

GUI::~GUI() {
    simRunning = false;
    if (simThread.joinable()) {
        simThread.join();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

// --- ФОНОВЫЙ ПОТОК РАСЧЕТОВ ЯДРА ---
void GUI::simLoop() {
    while (simRunning) {
        if (!isPaused) {
            auto start_time = std::chrono::high_resolution_clock::now();
            
            {
                std::lock_guard<std::mutex> lock(simMutex);
                simulation.update();
                
                // Делаем мгновенную копию сетки, если UI попросил
                if (snapshotRequested) {
                    std::lock_guard<std::mutex> snap_lock(snapMutex);
                    snapGrid = simulation.getGrid();
                    snapTick = simulation.getTick();
                    snapshotRequested = false;
                }
            } // Мьютекс ядра освобожден

            auto end_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> ms_double = end_time - start_time;
            lastTickTimeMs = ms_double.count(); 
            
            // КРИТИЧЕСКИ ВАЖНО: Даем ОС возможность передать мьютекс потоку интерфейса (кнопки, слайдеры)
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            if (snapshotRequested) {
                std::lock_guard<std::mutex> lock(simMutex);
                std::lock_guard<std::mutex> snap_lock(snapMutex);
                snapGrid = simulation.getGrid();
                snapTick = simulation.getTick();
                snapshotRequested = false;
            }
        }
    }
}

void GUI::run() {
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        renderImGui();
        renderGeneWindow();
        renderFileMenu();

        ImGui::Render();
        glViewport(0, 0, (int)ImGui::GetIO().DisplaySize.x, (int)ImGui::GetIO().DisplaySize.y);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }
}

uint32_t GUI::getHeatmapColor(float value) {
    uint8_t r = 0, g = 0, b = 0;
    if (value < 0.5f) {
        b = static_cast<uint8_t>((1.0f - value * 2.0f) * 255.0f);
        g = static_cast<uint8_t>(value * 2.0f * 255.0f);
        r = static_cast<uint8_t>((value) * 2.0f * 255.0f);
    } else {
        g = static_cast<uint8_t>((1.0f - (value - 0.5f) * 2.0f) * 255.0f);
        r = 255; 
    }
    return 0xFF000000 | (b << 16) | (g << 8) | r;
}

void GUI::drawLegend() {
    ImGui::Text("Legend:");    
    if(currentViewMode < VIEW_HEATMAP_ENERGY) {
        ImGui::BulletText("Blue: Herbivore | Yellow: Omnivore | Red: Carnivore");
        ImGui::BulletText("Green: Plants | Gray: Fertility");
    } else if (currentViewMode >= VIEW_HEATMAP_ENERGY) {
        ImGui::Text("Low");
        ImGui::SameLine();
        
        ImVec2 p = ImGui::GetCursorScreenPos();
        float width = 300.0f;
        float height = 15.0f;
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        
        draw_list->AddRectFilledMultiColor(p, ImVec2(p.x + width/2, p.y + height),
            IM_COL32(0, 0, 255, 255), IM_COL32(255, 255, 0, 255),
            IM_COL32(255, 255, 0, 255), IM_COL32(0, 0, 255, 255));
        
        draw_list->AddRectFilledMultiColor(ImVec2(p.x + width/2, p.y), ImVec2(p.x + width, p.y + height),
            IM_COL32(255, 255, 0, 255), IM_COL32(255, 0, 0, 255),
            IM_COL32(255, 0, 0, 255), IM_COL32(255, 255, 0, 255));
            
        ImGui::InvisibleButton("heatmap_legend", ImVec2(width, height));
        
        if (ImGui::IsItemHovered()) {
            float mouse_x = ImGui::GetMousePos().x - p.x;
            float normalized_val = std::clamp(mouse_x / width, 0.0f, 1.0f);
            
            ImGui::BeginTooltip();
            ImGui::Text("Value: %.2f", normalized_val);
            uint32_t col = getHeatmapColor(normalized_val);
            ImVec2 col_p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(col_p, ImVec2(col_p.x + 20, col_p.y + 20), col);
            ImGui::Dummy(ImVec2(20, 20));
            ImGui::Separator();
            ImGui::Text("Hold Left Click to isolate this value (+/- %.2f)", highlightDeviation);
            ImGui::EndTooltip();
            
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                isHighlighting = true;
                highlightValue = normalized_val;
            } else {
                isHighlighting = false;
            }
        } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            isHighlighting = false;
        }

        ImGui::SameLine();
        ImGui::Text("High");
    }
}

void GUI::renderFileMenu() {
    ImGui::Begin("File & States");

    if (ImGui::Button(isPaused ? "Resume Sim" : "Pause Sim")) isPaused = !isPaused;
    ImGui::SameLine();
    if (ImGui::Button("Restart")) {
        std::lock_guard<std::mutex> lock(simMutex);
        simulation.restart("config.json");
        // Синхронизируем UI с новыми настройками
        ui_sunlight = simulation.sunlight_base;
        ui_fertility = simulation.fertility_decay;
        ui_mutation = simulation.mutation_step;
        ui_replace = simulation.replace_factor;

        historyTicks.clear();
        historyAnimals.clear();
        historyHerbivores.clear();
        historyOmnivores.clear();
        historyCarnivores.clear();
        historyPlants.clear();
        forceStatsUpdate = true;
        snapshotRequested = true;
    }

    ImGui::Separator();
    ImGui::Text("Load Snapshot (.bin)");
    ImGui::InputText("Path", loadPathBuffer, IM_ARRAYSIZE(loadPathBuffer));
    if (ImGui::Button("Load State")) {
        std::lock_guard<std::mutex> lock(simMutex);
        if (simulation.loadSnapshot(loadPathBuffer)) {
            ui_sunlight = simulation.sunlight_base;
            ui_fertility = simulation.fertility_decay;
            ui_mutation = simulation.mutation_step;
            ui_replace = simulation.replace_factor;

            historyTicks.clear();
            historyAnimals.clear();
            historyHerbivores.clear();
            historyOmnivores.clear();
            historyCarnivores.clear();
            historyPlants.clear();
            forceStatsUpdate = true;
            snapshotRequested = true;
        }
    }
    ImGui::End();
}

void GUI::renderGeneWindow() {
    ImGui::Begin("Gene Distribution", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    
    int currentTick;
    {
        std::lock_guard<std::mutex> snap_lock(snapMutex);
        currentTick = snapTick;
    }
    
    if (currentTick - lastStatTick >= 5 || forceStatsUpdate) {
        // СБОР СТАТИСТИКИ ТЕПЕРЬ ПРОИСХОДИТ БЕЗ БЛОКИРОВКИ ЯДРА ИЗ КОПИИ
        std::vector<float> diet, size, speed, power, threshold, mutab, impuls, sight, smell, energy, age;
        
        {
            std::lock_guard<std::mutex> snap_lock(snapMutex);
            for (const auto& cell : snapGrid) {
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
        }

        auto calc = [](std::vector<float>& vec) -> GeneStats {
            if (vec.empty()) return { 0, 0, 0 };
            std::sort(vec.begin(), vec.end());
            return { vec.front(), vec.back(), vec[vec.size() / 2] };
        };

        geneStatsCache = {
            {"1. Diet (0=H, 1=C)", calc(diet)}, {"2. Size", calc(size)},
            {"3. Speed", calc(speed)},          {"4. Power", calc(power)},
            {"5. Threshold", calc(threshold)},  {"6. Mutability", calc(mutab)},
            {"7. Impulsivity", calc(impuls)},   {"8. Sight", calc(sight)},
            {"9. Smell", calc(smell)},          {"~ Energy", calc(energy)},
            {"~ Age", calc(age)}
        };

        lastStatTick = currentTick;
        forceStatsUpdate = false;
    }

    if (ImGui::BeginTable("GeneStatsTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Gene");
        ImGui::TableSetupColumn("Min");
        ImGui::TableSetupColumn("Median");
        ImGui::TableSetupColumn("Max");
        ImGui::TableHeadersRow();

        for (const auto& [name, stat] : geneStatsCache) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("%s", name.c_str());
            ImGui::TableSetColumnIndex(1); ImGui::Text("%.2f", stat.min_val);
            ImGui::TableSetColumnIndex(2); ImGui::Text("%.2f", stat.median);
            ImGui::TableSetColumnIndex(3); ImGui::Text("%.2f", stat.max_val);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void GUI::renderImGui() {
    int currentTick = 0;
    int herbCount = 0, omniCount = 0, carnCount = 0, plantCount = 0, animalCount = 0;
    
    auto getDietColor = [](float diet) -> uint32_t {
        uint8_t r, g, b;
        if (diet < 0.5f) {
            float t = diet * 2.0f; 
            r = static_cast<uint8_t>(t * 255.0f);
            g = static_cast<uint8_t>(t * 255.0f);
            b = static_cast<uint8_t>((1.0f - t) * 255.0f);
        } else {
            float t = (diet - 0.5f) * 2.0f; 
            r = 255;
            g = static_cast<uint8_t>((1.0f - t) * 255.0f);
            b = 0;
        }
        return 0xFF000000 | (b << 16) | (g << 8) | r;
    };

    { // Открываем безопасную зону для работы с фотоснимком кадра
        std::lock_guard<std::mutex> snap_lock(snapMutex);
        currentTick = snapTick;
        const auto& grid = snapGrid;

        for (size_t i = 0; i < grid.size(); ++i) {
            bool hasAnimal = grid[i].animal.alive;
            bool hasPlant = !grid[i].plants.empty();

            if (hasAnimal) {
                animalCount++;
                float d = grid[i].animal.genes.dietBias;
                if (d < 0.35f) herbCount++;
                else if (d > 0.65f) carnCount++;
                else omniCount++;
            }
            if (hasPlant) plantCount += grid[i].plants.size();

            uint32_t color = 0xFF000000;
            bool cellMatchesHighlight = false;

            if (currentViewMode >= VIEW_HEATMAP_ENERGY) {
                if (hasAnimal) {
                    float val = 0.0f;
                    const auto& g = grid[i].animal.genes;
                    switch (currentViewMode) {
                        case VIEW_HEATMAP_ENERGY: val = grid[i].animal.energy / (g.size * 10.0f); break;
                        case VIEW_HEATMAP_FERTILITY: val = grid[i].fertility / 255.0f; break;
                        case VIEW_HEATMAP_AGE: val = grid[i].animal.age / (simulation.maxAge + g.size * 20.0f); break;
                        case VIEW_HEATMAP_DIET: val = g.dietBias; break; 
                        case VIEW_HEATMAP_SIZE: val = g.size / 10.0f; break;
                        case VIEW_HEATMAP_SPEED: val = g.speed; break;
                        case VIEW_HEATMAP_POWER: val = g.power / 2.0f; break;
                        case VIEW_HEATMAP_THRESHOLD: val = (g.threshold - 0.3f) / 0.6f; break;
                        case VIEW_HEATMAP_MUTABILITY: val = g.mutability / 0.5f; break;
                        case VIEW_HEATMAP_IMPULSIVITY: val = g.impulsivity; break;
                        case VIEW_HEATMAP_SIGHT: val = g.sight; break;
                        case VIEW_HEATMAP_SMELL: val = g.smell; break;
                    }

                    if (currentViewMode == VIEW_HEATMAP_DIET) {
                        color = getDietColor(g.dietBias);
                    } else {
                        color = getHeatmapColor(std::clamp(val, 0.0f, 1.0f));
                    }

                    if (std::abs(val - highlightValue) <= highlightDeviation) cellMatchesHighlight = true;
                }
            } else if (currentViewMode == VIEW_PLANT_DENSITY) {
                if (hasPlant) {
                    float val = std::min((float)grid[i].plants.size() / 2.0f, 1.0f);
                    color |= (static_cast<uint8_t>(val * 255.0f) << 8);
                }
            } else {
                bool drawAnimal = hasAnimal && (currentViewMode == VIEW_CLASSIC || currentViewMode == VIEW_ANIMALS_ONLY);
                bool drawPlant = hasPlant && (currentViewMode == VIEW_CLASSIC || currentViewMode == VIEW_PLANTS_ONLY);

                if (drawAnimal) {
                    color = getDietColor(grid[i].animal.genes.dietBias);
                } else if (drawPlant) {
                    color |= (255 << 8);
                } else {
                    int fertility_val = static_cast<int>(grid[i].fertility * 50.0f);
                    uint8_t f = static_cast<uint8_t>(std::min(255, fertility_val));
                    color |= (f << 8) | f;
                }
            }

            if (isHighlighting && !cellMatchesHighlight && hasAnimal && currentViewMode >= VIEW_HEATMAP_ENERGY) {
                uint8_t r = (color & 0x000000FF);
                uint8_t g = (color & 0x0000FF00) >> 8;
                uint8_t b = (color & 0x00FF0000) >> 16;
                color = 0xFF000000 | (static_cast<uint8_t>(b * 0.3f) << 16) | (static_cast<uint8_t>(g * 0.3f) << 8) | static_cast<uint8_t>(r * 0.3f);
            }
            pixelBuffer[i] = color;
        }
    }     

    if (!isPaused && currentTick != lastRecordedTick) {
        lastRecordedTick = currentTick;
        historyTicks.push_back((float)currentTick);
        historyAnimals.push_back((float)animalCount);
        historyHerbivores.push_back((float)herbCount);
        historyOmnivores.push_back((float)omniCount);
        historyCarnivores.push_back((float)carnCount);
        historyPlants.push_back((float)plantCount);
        if (historyTicks.size() > maxHistory) {
            historyTicks.erase(historyTicks.begin());
            historyAnimals.erase(historyAnimals.begin());
            historyHerbivores.erase(historyHerbivores.begin());
            historyOmnivores.erase(historyOmnivores.begin());
            historyCarnivores.erase(historyCarnivores.begin());
            historyPlants.erase(historyPlants.begin());
        }
    }

    ImGui::Begin("Simulation Control");
    ImGui::Text("Tick: %d", currentTick);
    
    float currentFps = ImGui::GetIO().Framerate;
    ImGui::Text("GUI FPS: %.1f (%.2f ms/frame)", currentFps, currentFps > 0.0 ? 1000.0f / currentFps : 0.0);
    
    double coreMs = lastTickTimeMs.load();
    ImGui::Text("Core TPS: %.1f (%.3f ms/tick)", coreMs > 0.0 ? 1000.0f / coreMs : 0.0, coreMs);
    
    if (ImGui::Checkbox("VSync (Limit GUI FPS)", &vsyncEnabled)) {
        SDL_GL_SetSwapInterval(vsyncEnabled ? 1 : 0);
    }
    ImGui::Text("Animals: %d | Plants: %d", animalCount, plantCount);
    ImGui::Text("Herb: %d | Omni: %d | Carn: %d", herbCount, omniCount, carnCount);
    ImGui::Separator();

    const char* viewModes[] = {
        "Classic (Both)", "Animals Only", "Plants Only", "Plant Density",
        "Heatmap: Energy",  "Heatmap: Fertility",  "Heatmap: Age", "Heatmap: Diet", "Heatmap: Size", "Heatmap: Speed",
        "Heatmap: Power", "Heatmap: Threshold", "Heatmap: Mutability", "Heatmap: Impulsivity",
        "Heatmap: Sight", "Heatmap: Smell"
    };
    ImGui::Combo("View Mode", &currentViewMode, viewModes, IM_ARRAYSIZE(viewModes));
    drawLegend();

    ImGui::Separator();
    ImGui::Text("Population Intervention:");
    
    ImGui::PushItemWidth(100);
    ImGui::InputInt("##animals", &animalsToAdd);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::PushItemWidth(200);
    if (ImGui::Button("Add Animals")) { 
        std::lock_guard<std::mutex> lock(simMutex);
        simulation.addAnimals(std::max(1, animalsToAdd)); 
        forceStatsUpdate = true; 
    }
    ImGui::SameLine();
    if (ImGui::Button("Del Animals")) { 
        std::lock_guard<std::mutex> lock(simMutex);
        simulation.removeAnimals(std::max(1, animalsToAdd)); 
        forceStatsUpdate = true; 
    }

    ImGui::PushItemWidth(100);
    ImGui::InputInt("##plants", &plantsToAdd);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::PushItemWidth(200);
    if (ImGui::Button("Add Plants")) { 
        std::lock_guard<std::mutex> lock(simMutex);
        simulation.addPlants(std::max(1, plantsToAdd)); 
    }
    ImGui::SameLine();
    if (ImGui::Button("Del Plants")) { 
        std::lock_guard<std::mutex> lock(simMutex);
        simulation.removePlants(std::max(1, plantsToAdd)); 
    }

    ImGui::Separator();
    ImGui::Text("Environment Settings:");
    
    // Слайдеры используют ЛОКАЛЬНЫЕ переменные UI. 
    // Ядро блокируется только в тот момент, когда ползунок реально тянется мышкой
    if (ImGui::SliderFloat("Sunlight", &ui_sunlight, 0.1f, 5.0f, "%.2f")) {
        std::lock_guard<std::mutex> lock(simMutex);
        simulation.sunlight_base = ui_sunlight;
    }
    if (ImGui::SliderFloat("Fertility Decay", &ui_fertility, 0.0001f, 0.01f, "%.4f")) {
        std::lock_guard<std::mutex> lock(simMutex);
        simulation.fertility_decay = ui_fertility;
    }
    ImGui::Separator();
    ImGui::Text("Mutation Genetics:");
    if (ImGui::SliderFloat("Mutation Speed (Step)", &ui_mutation, 0.01f, 1.0f, "%.2f")) {
        std::lock_guard<std::mutex> lock(simMutex);
        simulation.mutation_step = ui_mutation;
    }
    if (ImGui::SliderFloat("Random Replace Factor", &ui_replace, 0.0f, 1.0f, "%.2f")) {
        std::lock_guard<std::mutex> lock(simMutex);
        simulation.replace_factor = ui_replace;
    }
    ImGui::End();

    ImGui::Begin("World View");
    glBindTexture(GL_TEXTURE_2D, gridTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, simulation.getWidth(), simulation.getHeight(), 0, GL_RGBA, GL_UNSIGNED_BYTE, pixelBuffer.data());
    ImGui::Image((void*)(intptr_t)gridTexture, ImVec2(512, 512));
    
    if (ImGui::IsItemHovered()) {
        ImVec2 mousePos = ImGui::GetMousePos();
        ImVec2 imgMin = ImGui::GetItemRectMin();
        ImVec2 imgSize = ImGui::GetItemRectSize();
        
        int x = static_cast<int>((mousePos.x - imgMin.x) / imgSize.x * simulation.getWidth());
        int y = static_cast<int>((mousePos.y - imgMin.y) / imgSize.y * simulation.getHeight());
        
        if (x >= 0 && x < simulation.getWidth() && y >= 0 && y < simulation.getHeight()) {
            std::lock_guard<std::mutex> snap_lock(snapMutex);
            const Cell& cell = snapGrid[y * simulation.getWidth() + x];
            
            ImGui::BeginTooltip();
            ImGui::Text("Cell: [%d, %d]", x, y);
            ImGui::Separator();
            ImGui::Text("Fertility: %.3f", cell.fertility);
            ImGui::Text("Carrion: %.2f", cell.carrion);
            ImGui::Text("Plants: %zu", cell.plants.size());
            if (!cell.plants.empty()) {
                ImGui::Text(" - First Plant Eng: %.1f", cell.plants[0].energy);
            }
            
            if (cell.animal.alive) {
                ImGui::Separator();
                ImGui::Text("Animal ID: %u", cell.animal.id);
                ImGui::Text("Age: %d", cell.animal.age);
                ImGui::Text("Energy: %.1f / %.1f", cell.animal.energy, cell.animal.genes.size * 10.0f);
                ImGui::Text("Diet (0=Herb, 1=Carn): %.2f", cell.animal.genes.dietBias);
                ImGui::Text("Size: %.2f | Speed: %.2f | Power: %.2f", cell.animal.genes.size, cell.animal.genes.speed, cell.animal.genes.power);
            }
            ImGui::EndTooltip();
        }
    }
    ImGui::End();

    ImGui::Begin("Analytics");
    if (ImPlot::BeginPlot("Population Dynamics", ImVec2(-1, 300), ImPlotFlags_None)) {
        ImPlot::SetupAxes("Tick", "Population", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        if (!historyTicks.empty()) {
            ImPlot::PlotLine("Herbivores", historyTicks.data(), historyHerbivores.data(), historyTicks.size());
            ImPlot::PlotLine("Omnivores(0.35-0.65)", historyTicks.data(), historyOmnivores.data(), historyTicks.size());
            ImPlot::PlotLine("Carnivores", historyTicks.data(), historyCarnivores.data(), historyTicks.size());
            ImPlot::PlotLine("Plants", historyTicks.data(), historyPlants.data(), historyTicks.size());
            ImPlot::PlotLine("Animals", historyTicks.data(), historyAnimals.data(), historyTicks.size());
        }
        ImPlot::EndPlot();
    }
    ImGui::End();
	
	snapshotRequested = true; // Запрашиваем новый кадр у ядра
}