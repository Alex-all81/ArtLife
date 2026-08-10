#include "GUI.h"
#include <GL/glew.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>
#include <algorithm>

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

uint32_t GUI::getHeatmapColor(float value) {
    // Градиент: Синий (0.0) -> Зеленый (0.5) -> Красный (1.0)
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

void GUI::run() {
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
        }

        if (!isPaused) {
            simulation.update();
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

void GUI::drawLegend() {
    ImGui::Text("Legend:");
    if (currentViewMode == VIEW_CLASSIC || currentViewMode == VIEW_ANIMALS_ONLY) {
        ImGui::BulletText("Blue: Herbivore | Yellow: Omnivore | Red: Carnivore");
        ImGui::BulletText("Green: Plants | Gray: Fertility");
    } else if (currentViewMode >= VIEW_HEATMAP_ENERGY) {
        ImGui::Text("Low");
        ImGui::SameLine();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilledMultiColor(
            ImVec2(p.x, p.y), ImVec2(p.x + 200, p.y + 15),
            IM_COL32(0, 0, 255, 255),    // Синий
            IM_COL32(255, 0, 0, 255),    // Красный
            IM_COL32(255, 0, 0, 255),
            IM_COL32(0, 0, 255, 255)
        );
        ImGui::Dummy(ImVec2(200, 15)); // Резервируем место под градиент
        ImGui::SameLine();
        ImGui::Text("High");
    }
}

void GUI::renderFileMenu() {
    ImGui::Begin("File & States");
    
    if (ImGui::Button(isPaused ? "Resume Sim" : "Pause Sim")) {
        isPaused = !isPaused;
    }
    
    ImGui::Separator();
    ImGui::Text("Load Snapshot / Frame (.bin)");
    ImGui::InputText("Path", loadPathBuffer, IM_ARRAYSIZE(loadPathBuffer));
    if (ImGui::Button("Load State")) {
        if (simulation.loadSnapshot(loadPathBuffer)) {
            // Очистка истории при загрузке нового стейта
            historyTicks.clear();
            historyAnimals.clear();
            historyPlants.clear();
        }
    }
    ImGui::End();
}

void GUI::renderGeneWindow() {
    ImGui::Begin("Gene Distribution", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    
    int currentTick = simulation.getTick();
    if (currentTick - lastStatTick >= 10 || isPaused) { // Обновляем раз в 10 тиков для производительности
        geneStatsCache = simulation.getGeneStatistics();
        lastStatTick = currentTick;
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
    
    for(size_t i = 0; i < grid.size(); ++i) {
        bool hasAnimal = grid[i].animal.alive;
        bool hasPlant = !grid[i].plants.empty();
        
        if (hasAnimal) animalCount++;
        if (hasPlant) plantCount += grid[i].plants.size();
        
        uint32_t color = 0xFF000000; 

        if (currentViewMode >= VIEW_HEATMAP_ENERGY) {
            if (hasAnimal) {
                float val = 0.0f;
                const auto& g = grid[i].animal.genes;
                switch(currentViewMode) {
                    case VIEW_HEATMAP_ENERGY: val = grid[i].animal.energy / (g.size * 10.0f); break; // Нормализация к MaxEnergy
                    case VIEW_HEATMAP_DIET: val = g.dietBias; break;
                    case VIEW_HEATMAP_SIZE: val = g.size / 10.0f; break;
                    case VIEW_HEATMAP_SPEED: val = g.speed; break;
                    case VIEW_HEATMAP_POWER: val = g.power / 2.0f; break;
                }
                color = getHeatmapColor(std::clamp(val, 0.0f, 1.0f));
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
        pixelBuffer[i] = color;
    }

    // Обновление графиков (только если не на паузе)
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
    
    // --- Окно Настроек и Информации ---

    ImGui::Begin("Simulation Control");
    ImGui::Text("Tick: %d", currentTick);
    ImGui::Text("Animals: %d | Plants: %d", animalCount, plantCount);
    ImGui::Separator();
    
    // Combo Box для выбора режима отображения
    const char* viewModes[] = {
        "Classic (Both)", "Animals Only", "Plants Only", "Plant Density",
        "Heatmap: Energy", "Heatmap: Diet", "Heatmap: Size", "Heatmap: Speed", "Heatmap: Power",
        "Heatmap: Mutability", "Heatmap: Impulsivity", "Heatmap: Sight", "Heatmap: Smell"
    };
    ImGui::Combo("View Mode", &currentViewMode, viewModes, IM_ARRAYSIZE(viewModes));
    drawLegend(); // Вызов отрисовки легенды

    ImGui::Separator();
    ImGui::Text("Population Intervention:");
    
    ImGui::PushItemWidth(80);
    ImGui::InputInt("##animals", &animalsToAdd);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Add Animals")) simulation.addAnimals(std::max(1, animalsToAdd));

    ImGui::PushItemWidth(80);
    ImGui::InputInt("##plants", &plantsToAdd);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Add Plants")) simulation.addPlants(std::max(1, plantsToAdd));

    ImGui::Separator();
    ImGui::Text("Environment Settings:");
    ImGui::SliderFloat("Sunlight", &simulation.sunlight_base, 0.1f, 5.0f, "%.2f");
    ImGui::SliderFloat("Fertility Decay", &simulation.fertility_decay, 0.0001f, 0.01f, "%.4f");
    
    ImGui::Separator();
    ImGui::Text("Mutation Genetics:");
    ImGui::SliderFloat("Mutation Speed (Step)", &simulation.mutation_step, 0.01f, 1.0f, "%.2f");
    ImGui::SliderFloat("Random Replace Factor", &simulation.replace_factor, 0.0f, 1.0f, "%.2f");
    
    ImGui::End();
    
    // --- Окно визуализации мира ---

    ImGui::Begin("World View");
    glBindTexture(GL_TEXTURE_2D, gridTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, simulation.getWidth(), simulation.getHeight(), 0, GL_RGBA, GL_UNSIGNED_BYTE, pixelBuffer.data());
    ImGui::Image((void*)(intptr_t)gridTexture, ImVec2(512, 512));
    ImGui::End();
    
    // --- Окно Аналитики ---

    ImGui::Begin("Analytics");
    if (ImPlot::BeginPlot("Population Dynamics", ImVec2(-1, 300))) {
        ImPlot::SetupAxes("Tick", "Population");
        if (!historyTicks.empty()) {
            ImPlot::PlotLine("Animals", historyTicks.data(), historyAnimals.data(), historyTicks.size());
            ImPlot::PlotLine("Plants", historyTicks.data(), historyPlants.data(), historyTicks.size());
        }
        ImPlot::EndPlot();
    }
    ImGui::End();
}