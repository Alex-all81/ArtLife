# ArtLife: High-Performance Artificial Life & Evolution Simulator
ArtLife is a high-performance artificial life and cellular automaton simulator written in modern C++. The project models a closed ecosystem on a 2D grid where agents (plants and animals) struggle for survival, reproduce, mutate, and evolve.

## 🧠 Project Philosophy & History
The core vision behind ALife Sim is to strike a perfect balance between blazing-fast, high-performance simulation and deep, accessible tools for real-time visualization, analysis, and control.

Initially, the project was conceived as a massively complex sandbox. However, as the underlying simulation complexity rapidly outpaced the visualization capabilities, the decision was made to pivot and release a streamlined Minimum Viable Product (MVP). This MVP ensures that the code remains highly stable, easy to build, and accessible for anyone to run.

> **Note:** Artificial Intelligence (AI) was actively utilized to assist in the development, architecture, and coding of this project.

### ✨ Key Features (current state):

*   **Genetic Diversity:** Each agent has a set of 9 mutating genes (Mass, Speed, Power, Metabolism, Diet, Impulsivity etc.).

*   **Diet Spectrum:** There is no rigid division into species. The DietBias gene (ranging from 0.0 to 1.0) makes individuals herbivores, omnivores, or strict carnivores/scavengers.

*   **GUI & Live Tuning:** An interactive interface powered by Dear ImGui + ImPlot allows you to tweak the laws of physics on the fly and track heatmaps for any gene in real time.

---

## 🗺️ Roadmap
The project is under active development. Future updates are categorized into four main pillars to ensure the simulation remains both scientifically fascinating and computationally efficient:

### 1. Enhanced Visualization, Analysis & Control
- [ ] **Advanced Analytics:** Expand the UI to include more sophisticated real-time graphs, phase portraits, and genetic drift tracking.
- [ ] **Improved UX:** Make the runtime control mechanisms more intuitive, allowing users to seamlessly pause, inspect, and intervene in the ecosystem.

### 2. Evolutionary Realism & World Diversity
- [ ] **New Mechanics:** Introduce biomes, dynamic weather seasons, non-linear energy physics, chemical trails (pheromones), and sexual reproduction.
- [ ] **Strict Development Rule:** Any addition to the world's complexity *must* be developed in parallel with its corresponding visualization and analytical tools. If a new mechanic is added, the user must have a way to visually track and measure it.

### 3. Technical Enhancements & Optimization
- [ ] **Performance Tuning:** Continuous optimization of the multithreaded core to handle massively larger grid sizes and agent counts.
- [ ] **History Storage Optimization:** Develop a custom, highly efficient binary compression algorithm specifically tailored for storing world states and simulation history, minimizing RAM and disk space usage.

### 4. Sandbox & Creator Tools
- [ ] **Custom Worlds:** Transition the simulator into a flexible engine where end-users can design custom worlds, define their own physics, and introduce entirely new gene sets using simple JSON configurations.
- [ ] **Developer-Friendly:** Maintain clean, modular code to ensure that developers can easily fork, extend, and mod the simulator.


---

## 🛠️ Dependencies
The core and visualizer are designed with cross-platform compatibility in mind.

Compiler: C++17 support (GCC, Clang, MSVC).

Build System: CMake 3.17+.

System Libraries: SDL2, OpenGL 3.0+, GLEW, TBB (optional, for multithreading).

Embedded Dependencies (fetched automatically via CMake FetchContent): nlohmann/json, Dear ImGui, ImPlot.

## 🚀 Build and Installation
### 🐧 Linux 
Install the required packages:

```
Bash
sudo apt update
sudo apt install build-essential cmake git libsdl2-dev libglew-dev libgl1-mesa-dev libtbb-dev
```
Build the project:
```
Bash
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)
```
Copy config.json to the directory containing the executable and run: ./ALifeSim

### 🪟 Windows (via vcpkg + MSVC)
Install libraries using vcpkg:
```
vcpkg install sdl2:x64-windows glew:x64-windows tbb:x64-windows
```
Build the project using CMake:
```
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE="C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake" -A x64
cmake --build . --config Release
```
Copy config.json into build/Release and run ALifeSim.exe.

### 🍏 macOS (Homebrew)
```
brew install cmake sdl2 glew tbb
mkdir build && cd build
cmake ..
cmake --build . -j$(sysctl -n hw.ncpu)
```
(Remember to place config.json next to the executable).

## 🎮 Controls
Use the Simulation Control panel to change view modes (Classic, Heatmaps).

Hold the left mouse button on the Legend gradient to filter cells by a specific gene value.

Hover your cursor over any cell in the World View window to open an inspector with detailed information about the agent.
