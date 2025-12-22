#pragma once

#include "Workload.hpp"
#include <random>
#include <vector>
#include <algorithm>
#include <cmath>

/**
 * @brief Workload с распределением Zipf
 * 
 * Реалистичное распределение степенного закона:
 * p(k) = (1/k^s) / sum(1/i^s for i=1..N)
 * 
 * При s=1.0:
 * - Топ 20% ключей = 80% всех обращений
 * - Остальные 80% ключей = 20% обращений
 * 
 * Real-world примеры:
 * - 80% трафика на 20% популярных статей
 * - 80% запросов к 20% акций
 * - 80% комментариев на 20% постов
 */
class ZipfWorkload : public Workload<int> {
public:
    /**
     * @brief Конструктор
     * @param key_range диапазон ключей [0, key_range)
     * @param num_operations количество операций
     * @param s параметр Zipf (1.0 = классическое)
     * @param seed seed для RNG
     */
    ZipfWorkload(size_t key_range, size_t num_operations, double s = 1.0, uint32_t seed = 42)
        : key_range_(key_range), num_operations_(num_operations), s_(s), seed_(seed) {
        
        // Предвычисляем кумулятивные вероятности
        precomputeProbabilities();
    }
    
    std::vector<int> generate() override {
        std::vector<int> keys;
        keys.reserve(num_operations_);
        
        std::mt19937 rng(seed_);
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        
        for (size_t i = 0; i < num_operations_; ++i) {
            double u = dist(rng);
            
            // Бинарный поиск в кумулятивных вероятностях
            auto it = std::lower_bound(cumulative_.begin(), cumulative_.end(), u);
            int key = std::distance(cumulative_.begin(), it);
            
            // На случай если u > последней кумулятивной вероятности (редко)
            if (key >= static_cast<int>(key_range_)) {
                key = static_cast<int>(key_range_) - 1;
            }
            
            keys.push_back(key);
        }
        
        return keys;
    }
    
    std::string name() const override {
        return "zipf";
    }
    
    std::string description() const override {
        return "Zipf distribution (power law): realistic workload";
    }
    
    std::string parameters() const override {
        std::string param = "key_range=" + std::to_string(key_range_);
        param += ", s=" + std::to_string(s_);
        return param;
    }

private:
    size_t key_range_;
    size_t num_operations_;
    double s_;  // Параметр Zipf
    uint32_t seed_;
    std::vector<double> cumulative_;  // Кумулятивные вероятности
    
    /// Предвычисляет кумулятивные вероятности для Zipf распределения
    void precomputeProbabilities() {
        // Вычисляем нормализующий множитель: sum(1/i^s for i=1..N)
        double sum = 0.0;
        for (size_t i = 1; i <= key_range_; ++i) {
            sum += 1.0 / std::pow(static_cast<double>(i), s_);
        }
        
        // Вычисляем кумулятивные вероятности
        cumulative_.reserve(key_range_);
        double cumsum = 0.0;
        for (size_t i = 1; i <= key_range_; ++i) {
            cumsum += (1.0 / std::pow(static_cast<double>(i), s_)) / sum;
            cumulative_.push_back(cumsum);
        }
    }
};
