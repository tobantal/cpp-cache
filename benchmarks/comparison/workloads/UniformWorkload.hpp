#pragma once

#include "Workload.hpp"
#include <random>

/**
 * @brief Workload с равномерным распределением (Uniform)
 * 
 * Все ключи имеют одинаковую вероятность быть выбранными.
 * Используется как baseline тест.
 * 
 * Пример:
 * - key_range = 200'000
 * - каждый ключ выбирается с вероятностью 1/200'000
 */
class UniformWorkload : public Workload<int> {
public:
    /**
     * @brief Конструктор
     * @param key_range диапазон ключей [0, key_range)
     * @param num_operations количество операций
     * @param seed seed для RNG
     */
    UniformWorkload(size_t key_range, size_t num_operations, uint32_t seed = 42)
        : key_range_(key_range), num_operations_(num_operations), seed_(seed) {
    }
    
    std::vector<int> generate() override {
        std::vector<int> keys;
        keys.reserve(num_operations_);
        
        std::mt19937 rng(seed_);
        std::uniform_int_distribution<int> dist(0, static_cast<int>(key_range_ - 1));
        
        for (size_t i = 0; i < num_operations_; ++i) {
            keys.push_back(dist(rng));
        }
        
        return keys;
    }
    
    std::string name() const override {
        return "uniform";
    }
    
    std::string description() const override {
        return "Uniform distribution: all keys have equal probability";
    }
    
    std::string parameters() const override {
        return "key_range=" + std::to_string(key_range_);
    }

private:
    size_t key_range_;
    size_t num_operations_;
    uint32_t seed_;
};
