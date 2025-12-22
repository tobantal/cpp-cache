#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @brief Конфигурация для бенчмарков
 * 
 * Позволяет настраивать размер кэша, количество операций,
 * распределение ключей и другие параметры.
 */
struct BenchmarkConfig {
    // ========== ОСНОВНЫЕ ПАРАМЕТРЫ ==========
    
    /// Размер кэша (количество элементов)
    size_t cache_size = 100'000;
    
    /// Количество операций для выполнения
    size_t num_operations = 1'000'000;
    
    /// Фактор для диапазона ключей (key_range = cache_size * factor)
    /// Например: cache_size=100K, factor=2 → key_range=200K
    size_t key_range_factor = 2;
    
    /// Seed для генератора случайных чисел (воспроизводимость)
    uint32_t random_seed = 42;
    
    // ========== ПАРАМЕТРЫ ДЛЯ TEMPORAL WORKLOAD ==========
    
    /// Размер "окна" недавних ключей
    /// Для Temporal: 70% операций из этого окна, 30% ко всем
    size_t temporal_window_size = 1000;
    
    /// Процент операций к "горячим" (недавним) ключам
    /// Например: 0.7 = 70% к recent, 30% к остальным
    double temporal_hot_ratio = 0.7;
    
    // ========== ПАРАМЕТРЫ ДЛЯ ZIPF WORKLOAD ==========
    
    /// Параметр s для распределения Zipf
    /// s=1.0 → классическое распределение (топ 20% = 80% обращений)
    /// s=0.5 → более плоское
    /// s=1.5 → более экстремальное
    double zipf_parameter = 1.0;
    
    // ========== ПРЕДУСТАНОВЛЕННЫЕ КОНФИГУРАЦИИ ==========
    
    /// Лёгкая конфигурация (быстрый тест)
    void setLight() {
        cache_size = 1'000;
        num_operations = 100'000;
    }
    
    /// Стандартная конфигурация (нормальный бенчмарк)
    void setStandard() {
        cache_size = 100'000;
        num_operations = 1'000'000;
    }
    
    /// Тяжёлая конфигурация (долгий бенчмарк)
    void setHeavy() {
        cache_size = 10'000;
        num_operations = 10'000'000;
    }
    
    /// Очень тяжёлая конфигурация (стресс-тест)
    void setVeryHeavy() {
        cache_size = 1'000;
        num_operations = 100'000'000;
    }
    
    // ========== ВСПОМОГАТЕЛЬНЫЕ МЕТОДЫ ==========
    
    /// Вычисляет диапазон ключей
    size_t getKeyRange() const {
        return cache_size * key_range_factor;
    }
    
    /// Вычисляет ожидаемое количество вытеснений
    /// (для key_range = 2x, ожидаем примерно половину операций)
    size_t getExpectedEvictions() const {
        if (key_range_factor <= 1) return 0;
        return num_operations / 2;  // Приблизительно
    }
};
