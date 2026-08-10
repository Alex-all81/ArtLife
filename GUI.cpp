#include "GUI.h"
#include <GL/glew.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>

GUI::GUI(Simulation& sim) : simulation(sim) {
    SDL_Init(SDL_INIT_VIDEO);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    
    window = SDL_CreateWindow("ALife Sim v2.0", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
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

        simulation.update(); // Шаг симуляции

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        renderImGui(); // Отрисовка панелей управления

        ImGui::Render();
        glViewport(0, 0, 1280, 720);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }
}

void GUI::renderImGui() {
    ImGui::Begin("Simulation Control");
    ImGui::Text("Tick: %d", simulation.getTick());
    
    // Формирование текстуры для визуализации
    const auto& grid = simulation.getGrid();
    for(int i=0; i<grid.size(); ++i) {
        uint32_t color = 0xFF000000; // Alpha
        if(grid[i].animal.alive) {
            float diet = grid[i].animal.genes.dietBias;
            // Травоядные - синие, Хищники - красные
            uint8_t r = (uint8_t)(diet * 255);
            uint8_t b = (uint8_t)((1.0f - diet) * 255);
            color |= (r | (b << 16)); 
        } else if (!grid[i].plants.empty()) {
            color |= (255 << 8); // Зеленые растения
        } else {
            // Отображение плодородия
            uint8_t f = std::min(255, int(grid[i].fertility) * 50);
            color |= (f << 8) | f;
        }
        pixelBuffer[i] = color;
    }

    glBindTexture(GL_TEXTURE_2D, gridTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, simulation.getWidth(), simulation.getHeight(), 0, GL_RGBA, GL_UNSIGNED_BYTE, pixelBuffer.data());

    ImGui::Image((void*)(intptr_t)gridTexture, ImVec2(512, 512));
    ImGui::End();

    // Пример статистики ImPlot
    ImGui::Begin("Analytics");
    if (ImPlot::BeginPlot("Population")) {
        // Сюда передаются собранные массивы истории численности
        ImPlot::EndPlot();
    }
    ImGui::End();
}