#pragma once
#include <atomic>
#include <cstdint>
#include <vector>

struct Genes {
    float size = 1.0f;
    float speed = 0.5f;
    float power = 1.0f;
    float threshold = 0.7f;
    float mutability = 0.1f;
    float dietBias = 0.5f; // 0 = травоядное, 1 = хищник
    float impulsivity = 0.2f;
    float sight = 0.5f;
    float smell = 0.5f;
};

struct Animal {
    uint32_t id = 0;
    Genes genes;
    int age = 0;
    float energy = 10.0f;
    bool alive = false;
};

struct Plant {
    Genes genes;
    float energy = 5.0f;
    bool alive = false;
};

struct Cell {
    Animal animal;
    std::vector<Plant> plants;
    float fertility = 1.0f;
    float carrion = 0.0f;
    
    // Атомарный флаг нужен только для записи в NextGrid 
    // при конкуренции за ячейку (коллизии движения)
    std::atomic_flag lock = ATOMIC_FLAG_INIT;

    Cell() = default;
    Cell(const Cell& other) {
        animal = other.animal;
        plants = other.plants;
        fertility = other.fertility;
        carrion = other.carrion;
        // lock не копируется
    }
    Cell& operator=(const Cell& other) {
        if (this != &other) {
            animal = other.animal;
            plants = other.plants;
            fertility = other.fertility;
            carrion = other.carrion;
        }
        return *this;
    }
};