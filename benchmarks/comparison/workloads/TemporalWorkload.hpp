#pragma once

#include "Workload.hpp"
#include <random>
#include <deque>

/**
 * @brief Workload с временной локальностью (Temporal Locality)
 * 
 * Недавно добавленные элементы более вероятны для доступа.
 * 
 * Логика:
 * - Поддерживаем "окно" из recent_window элементов
 * - hot_ratio процентов операций идёт к recent ключам
 * - остальные операции идут ко всем ключам
 * 
 * Пример (hot_ratio=0.7):
 * - 70% операций к последним 1000 добавленным ключам
 * - 30% операций к остальным ключам
 * 
 * Real-world примеры:
 * - Новостная лента (новые посты в топе)
 * - Session хранилище (активные сессии недавние)
 * - Логи (последние события чаще смотрят)
 */
class TemporalWorkload : public Workload<int> {
public:
    /**
     * @brief Конструктор
     * @param key_range диапазон ключей [0, key_range)
     * @param num_operations количество операций
     * @param recent_window размер окна недавних ключей
     * @param hot_ratio процент операций к recent ключам (0.0 - 1.0)
     * @param seed seed для RNG
     */
    TemporalWorkload(size_t key_range, 
                     size_t num_operations,
                     size_t recent_window = 1000,
                     double hot_ratio = 0.7,
                     uint32_t seed = 42)
        : key_range_(key_range), 
          num_operations_(num_operations),
          recent_window_(recent_window),
          hot_ratio_(hot_ratio),
          seed_(seed) {
        
        // Валидация параметров
        if (hot_ratio < 0.0 || hot_ratio > 1.0) {
            throw std::invalid_argument("hot_ratio должен быть в диапазоне [0.0, 1.0]");
        }
    }
    
    std::vector<int> generate() override {
        std::vector<int> keys;
        keys.reserve(num_operations_);
        
        std::mt19937 rng(seed_);
        std::uniform_int_distribution<int> all_keys_dist(0, static_cast<int>(key_range_ - 1));
        std::uniform_real_distribution<double> hot_dist(0.0, 1.0);
        
        std::deque<int> recent_keys;  // Последние добавленные ключи
        
        for (size_t i = 0; i < num_operations_; ++i) {
            int key;
            
            // Решаем: обратиться к recent или ко всем?
            if (recent_keys.size() > 0 && hot_dist(rng) < hot_ratio_) {
                // Выбираем случайный ключ из recent
                std::uniform_int_distribution<size_t> recent_idx_dist(0, recent_keys.size() - 1);
                size_t idx = recent_idx_dist(rng);
                key = recent_keys[idx];
            } else {
                // Выбираем случайный ключ из всех
                key = all_keys_dist(rng);
            }
            
            keys.push_back(key);
            
            // Обновляем окно recent ключей
            // (предполагаем что любой обращённый ключ "свежий")
            recent_keys.push_back(key);
            if (recent_keys.size() > recent_window_) {
                recent_keys.pop_front();
            }
        }
        
        return keys;
    }
    
    std::string name() const override {
        return "temporal";
    }
    
    std::string description() const override {
        return "Temporal locality: recent keys are accessed more frequently";
    }
    
    std::string parameters() const override {
        std::string param = "key_range=" + std::to_string(key_range_);
        param += ", window=" + std::to_string(recent_window_);
        param += ", hot_ratio=" + std::to_string(hot_ratio_);
        return param;
    }

private:
    size_t key_range_;
    size_t num_operations_;
    size_t recent_window_;
    double hot_ratio_;
    uint32_t seed_;
};
