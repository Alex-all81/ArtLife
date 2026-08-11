#include "GUI.h"
#include <GL/glew.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>
#include <algorithm>
#include <cmath>

GUI::GUI(Simulation& sim) : simulation(sim) {
    SDL_Init(SDL_INIT_VIDEO);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    
    window = SDL_CreateWindow("ALife Sim v2.0", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1400, 800, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    gl_context = SDL_GL_CreateContext(window);
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
}

GUI::~GUI() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

void GUI::run() {
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
        }

        if (!isPaused) simulation.update();

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
    } else {
        g = static_cast<uint8_t>((1.0f - (value - 0.5f) * 2.0f) * 255.0f);
        r = static_cast<uint8_t>((value - 0.5f) * 2.0f * 255.0f);
    }
    return 0xFF000000 | (b << 16) | (g << 8) | r;
}

void GUI::drawLegend() {
    ImGui::Text("Legend:");
    if (currentViewMode == VIEW_CLASSIC || currentViewMode == VIEW_ANIMALS_ONLY) {
        ImGui::BulletText("Blue: Herbivore | Yellow: Omnivore | Red: Carnivore");
        ImGui::BulletText("Green: Plants | Gray: Fertility");
    } else if (currentViewMode >= VIEW_HEATMAP_ENERGY) {
        ImGui::Text("Low");
        ImGui::SameLine();
        
        ImVec2 p = ImGui::GetCursorScreenPos();
        float width = 200.0f;
        float height = 15.0f;
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        
        // Корректный градиент: Синий -> Зеленый
        draw_list->AddRectFilledMultiColor(p, ImVec2(p.x + width/2, p.y + height),
            IM_COL32(0, 0, 255, 255), IM_COL32(0, 255, 0, 255), 
            IM_COL32(0, 255, 0, 255), IM_COL32(0, 0, 255, 255));
        
        // Корректный градиент: Зеленый -> Красный
        draw_list->AddRectFilledMultiColor(ImVec2(p.x + width/2, p.y), ImVec2(p.x + width, p.y + height),
            IM_COL32(0, 255, 0, 255), IM_COL32(255, 0, 0, 255), 
            IM_COL32(255, 0, 0, 255), IM_COL32(0, 255, 0, 255));
            
        ImGui::InvisibleButton("heatmap_legend", ImVec2(width, height));
        
        // Интерактивное управление подсветкой при наведении и зажатии
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
    ImGui::Separator();
    ImGui::Text("Load Snapshot (.bin)");
    ImGui::InputText("Path", loadPathBuffer, IM_ARRAYSIZE(loadPathBuffer));
    if (ImGui::Button("Load State")) {
        if (simulation.loadSnapshot(loadPathBuffer)) {
            historyTicks.clear();
            historyAnimals.clear();
            historyPlants.clear();
            forceStatsUpdate = true;
        }
    }
    ImGui::End();
}

void GUI::renderGeneWindow() {
    ImGui::Begin("Gene Distribution", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    int currentTick = simulation.getTick();
    
    // Обновляем статистику каждый тик для плавности или принудительно (после добавления агентов)
    if (currentTick != lastStatTick || forceStatsUpdate) {
        geneStatsCache = simulation.getGeneStatistics();
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
    int currentTick = simulation.getTick();
    int animalCount = 0, plantCount = 0;
    const auto& grid = simulation.getGrid();
    
    // Сборка текстуры с учетом режима затемнения (Highlighting)
    for(size_t i = 0; i < grid.size(); ++i) {
        bool hasAnimal = grid[i].animal.alive;
        bool hasPlant = !grid[i].plants.empty();
        
        if (hasAnimal) animalCount++;
        if (hasPlant) plantCount += grid[i].plants.size();
        
        uint32_t color = 0xFF000000; 
        bool cellMatchesHighlight = false;

        if (currentViewMode >= VIEW_HEATMAP_ENERGY) {
            if (hasAnimal) {
                float val = 0.0f;
                const auto& g = grid[i].animal.genes;
                switch(currentViewMode) {
                    case VIEW_HEATMAP_ENERGY: val = grid[i].animal.energy / (g.size * 10.0f); break; 
                    case VIEW_HEATMAP_DIET: val = g.dietBias; break;
                    case VIEW_HEATMAP_SIZE: val = g.size / 10.0f; break;
                    case VIEW_HEATMAP_SPEED: val = g.speed; break;
                    case VIEW_HEATMAP_POWER: val = g.power / 2.0f; break;
                }
                color = getHeatmapColor(std::clamp(val, 0.0f, 1.0f));
                
                if (std::abs(val - highlightValue) <= highlightDeviation) {
                    cellMatchesHighlight = true;
                }
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
                float diet = grid[i].animal.genes.dietBias;
                uint8_t r = static_cast<uint8_t>(diet * 255.0f);
                uint8_t b = static_cast<uint8_t>((1.0f - diet) * 255.0f);
                color |= (r | (b << 16)); 
            } else if (drawPlant) {
                color |= (255 << 8);
            } else {
                int fertility_val = static_cast<int>(grid[i].fertility * 50.0f);
                uint8_t f = static_cast<uint8_t>(std::min(255, fertility_val));
                color |= (f << 8) | f;
            }
        }

        // Логика затемнения нерелевантных ячеек при зажатой шкале
        if (isHighlighting && !cellMatchesHighlight) {
            uint8_t r = (color & 0x000000FF);
            uint8_t g = (color & 0x0000FF00) >> 8;
            uint8_t b = (color & 0x00FF0000) >> 16;
            r = static_cast<uint8_t>(r * 0.3f);
            g = static_cast<uint8_t>(g * 0.3f);
            b = static_cast<uint8_t>(b * 0.3f);
            color = 0xFF000000 | (b << 16) | (g << 8) | r;
        }

        pixelBuffer[i] = color;
    }

    if (!isPaused && currentTick != lastRecordedTick) {
        lastRecordedTick = currentTick;
        historyTicks.push_back((float)currentTick);
        historyAnimals.push_back((float)animalCount);
        historyPlants.push_back((float)plantCount);
        if (historyTicks.size() > maxHistory) {
            historyTicks.erase(historyTicks.begin());
            historyAnimals.erase(historyAnimals.begin());
            historyPlants.erase(historyPlants.begin());
        }
    }

    // --- Control Panel ---
    ImGui::Begin("Simulation Control");
    ImGui::Text("Tick: %d", currentTick);
    ImGui::Text("Animals: %d | Plants: %d", animalCount, plantCount);
    ImGui::Separator();
    
    const char* viewModes[] = {
        "Classic (Both)", "Animals Only", "Plants Only", "Plant Density",
        "Heatmap: Energy", "Heatmap: Diet", "Heatmap: Size", "Heatmap: Speed", "Heatmap: Power"
    };
    ImGui::Combo("View Mode", &currentViewMode, viewModes, IM_ARRAYSIZE(viewModes));
    drawLegend(); 

    ImGui::Separator();
    ImGui::Text("Population Intervention:");
    
    // Компактное размещение кнопок "Добавить" в один ряд
    ImGui::PushItemWidth(80);
    ImGui::InputInt("##animals", &animalsToAdd);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Add Animals")) {
        simulation.addAnimals(std::max(1, animalsToAdd));
        forceStatsUpdate = true; // Принудительное обновление статистики генов
    }

    ImGui::SameLine(0, 20); // Отступ между блоками

    ImGui::PushItemWidth(80);
    ImGui::InputInt("##plants", &plantsToAdd);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Add Plants")) {
        simulation.addPlants(std::max(1, plantsToAdd));
        forceStatsUpdate = true; // Принудительное обновление статистики генов

    }

    ImGui::Separator();
    ImGui::Text("Environment Settings:");
    ImGui::SliderFloat("Sunlight", &simulation.sunlight_base, 0.1f, 5.0f, "%.2f");
    ImGui::SliderFloat("Fertility Decay", &simulation.fertility_decay, 0.0001f, 0.01f, "%.4f");
    ImGui::Separator();
    ImGui::Text("Mutation Genetics:");
    ImGui::SliderFloat("Mutation Speed (Step)", &simulation.mutation_step, 0.01f, 1.0f, "%.2f");
    ImGui::SliderFloat("Random Replace Factor", &simulation.replace_factor, 0.0f, 1.0f, "%.2f");
    ImGui::End();

    // --- World View (Карта) ---
    ImGui::Begin("World View");
    glBindTexture(GL_TEXTURE_2D, gridTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, simulation.getWidth(), simulation.getHeight(), 0, GL_RGBA, GL_UNSIGNED_BYTE, pixelBuffer.data());
    ImGui::Image((void*)(intptr_t)gridTexture, ImVec2(512, 512));
        
    // Реализация подробного инспектора ячейки при наведении курсора на карту
    if (ImGui::IsItemHovered()) {
        ImVec2 mousePos = ImGui::GetMousePos();
        ImVec2 imgMin = ImGui::GetItemRectMin();
        ImVec2 imgSize = ImGui::GetItemRectSize();
        
        int x = static_cast<int>((mousePos.x - imgMin.x) / imgSize.x * simulation.getWidth());
        int y = static_cast<int>((mousePos.y - imgMin.y) / imgSize.y * simulation.getHeight());
        
        if (x >= 0 && x < simulation.getWidth() && y >= 0 && y < simulation.getHeight()) {
            const Cell& cell = simulation.getGrid()[y * simulation.getWidth() + x];
            
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
                ImGui::Text("Energy: %.1f / %.1f", cell.animal.energy, cell.animal.genes.size * 10.0f);
                ImGui::Text("Diet (0=Herb, 1=Carn): %.2f", cell.animal.genes.dietBias);
                ImGui::Text("Size: %.2f | Speed: %.2f | Power: %.2f", cell.animal.genes.size, cell.animal.genes.speed, cell.animal.genes.power);
            }
            ImGui::EndTooltip();
        }
    }
    ImGui::End();

    // --- Analytics ---
    ImGui::Begin("Analytics");
    // Включение флагов AutoFit для автоматического масштабирования графика
    if (ImPlot::BeginPlot("Population Dynamics", ImVec2(-1, 300), ImPlotFlags_None)) {
        ImPlot::SetupAxes("Tick", "Population", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        if (!historyTicks.empty()) {
            ImPlot::PlotLine("Animals", historyTicks.data(), historyAnimals.data(), historyTicks.size());
            ImPlot::PlotLine("Plants", historyTicks.data(), historyPlants.data(), historyTicks.size());
        }
        ImPlot::EndPlot();
    }
    ImGui::End();
}