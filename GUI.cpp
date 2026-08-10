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
        
        simulation.update();
        
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        
        renderImGui();
        
        ImGui::Render();
        glViewport(0, 0, (int)ImGui::GetIO().DisplaySize.x, (int)ImGui::GetIO().DisplaySize.y);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }
}

void GUI::renderImGui() {
    int currentTick = simulation.getTick();
    int animalCount = 0;
    int plantCount = 0;
    
    const auto& grid = simulation.getGrid();
    
    // Сборка текстуры с поддержкой тепловых карт
    for(size_t i = 0; i < grid.size(); ++i) {
        bool hasAnimal = grid[i].animal.alive;
        bool hasPlant = !grid[i].plants.empty();
        
        if (hasAnimal) animalCount++;
        if (hasPlant) plantCount += grid[i].plants.size();
        
        uint32_t color = 0xFF000000; 
        
        if (currentViewMode >= VIEW_HEATMAP_DIET && currentViewMode <= VIEW_HEATMAP_SMELL) {
            if (hasAnimal) {
                float val = 0.0f;
                const auto& g = grid[i].animal.genes;
                // Нормализация генов к диапазону 0.0 ... 1.0
                switch(currentViewMode) {
                case VIEW_HEATMAP_DIET: val = g.dietBias; break;
                case VIEW_HEATMAP_SIZE: val = g.size / 10.0f; break;
                case VIEW_HEATMAP_SPEED: val = g.speed; break;
                case VIEW_HEATMAP_POWER: val = g.power / 2.0f; break;
                case VIEW_HEATMAP_MUTABILITY: val = g.mutability / 0.5f; break;
                case VIEW_HEATMAP_IMPULSIVITY: val = g.impulsivity; break;
                case VIEW_HEATMAP_SIGHT: val = g.sight; break;
                case VIEW_HEATMAP_SMELL: val = g.smell; break;
                }
                color = getHeatmapColor(std::clamp(val, 0.0f, 1.0f));
            }
        } else if (currentViewMode == VIEW_PLANT_DENSITY) {
            if (hasPlant) {
                // Максимум 2 растения на клетку по логике ядра, так что / 2.0f
                float val = std::min((float)grid[i].plants.size() / 2.0f, 1.0f);
                color |= (static_cast<uint8_t>(val * 255.0f) << 8); // Интенсивно зеленый
            }
        } else {
            // Классические режимы
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
    
    if (currentTick != lastRecordedTick) {
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
        "Heatmap: Diet", "Heatmap: Size", "Heatmap: Speed", "Heatmap: Power",
        "Heatmap: Mutability", "Heatmap: Impulsivity", "Heatmap: Sight", "Heatmap: Smell"
    };
    ImGui::Combo("View Mode", &currentViewMode, viewModes, IM_ARRAYSIZE(viewModes));
    
    ImGui::Separator();
    ImGui::Text("Population Intervention:");
    ImGui::PushItemWidth(100);
    ImGui::InputInt("Count", &animalsToAdd);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Add Animals")) {
        simulation.addAnimals(std::max(1, animalsToAdd)); // Безопасное добавление вне цикла
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