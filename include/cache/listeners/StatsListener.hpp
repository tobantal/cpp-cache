#pragma once

#include <cache/listeners/ICacheListener.hpp>
#include <cstdint>
#include <atomic>

/**
 * @brief Слушатель для сбора статистики кэша
 * @tparam K Тип ключа
 * @tparam V Тип значения
 * 
 * Собирает:
 * - hits/misses — для расчёта hit rate
 * - inserts/updates/evictions/removes — для анализа поведения
 * 
 * Использование:
 *   auto stats = std::make_shared<StatsListener<std::string, int>>();
 *   cache.addListener(stats);
 *   // ... работа с кэшем ...
 *   std::cout << "Hit rate: " << stats->hitRate() << std::endl;
 * 
 * Примечание: счётчики atomic для потокобезопасности.
 */
template<typename K, typename V>
class StatsListener : public ICacheListener<K, V> {
public:
    void onHit(const K& key) override { 
        (void)key;
        ++hits_; 
    }

    void onMiss(const K& key) override { 
        (void)key;
        ++misses_; 
    }

    void onInsert(const K& key, const V& value) override { 
        (void)key; (void)value;
        ++inserts_; 
    }

    void onUpdate(const K& key, const V& oldValue, const V& newValue) override { 
        (void)key; (void)oldValue; (void)newValue;
        ++updates_; 
    }

    void onEvict(const K& key, const V& value) override { 
        (void)key; (void)value;
        ++evictions_; 
    }

    void onRemove(const K& key) override { 
        (void)key;
        ++removes_; 
    }

    void onClear(size_t count) override { 
        (void)count;
        ++clears_; 
    }

    // ==================== Геттеры ====================

    uint64_t hits() const { return hits_; }
    uint64_t misses() const { return misses_; }
    uint64_t inserts() const { return inserts_; }
    uint64_t updates() const { return updates_; }
    uint64_t evictions() const { return evictions_; }
    uint64_t removes() const { return removes_; }
    uint64_t clears() const { return clears_; }

    /**
     * @brief Общее количество запросов get()
     */
    uint64_t totalRequests() const { 
        return hits_ + misses_; 
    }

    /**
     * @brief Процент попаданий в кэш (0.0 - 1.0)
     * @return hit rate или 0.0 если запросов не было
     * 
     * Hit rate — ключевая метрика эффективности кэша.
     * Типичные значения:
     * - < 0.5 — кэш неэффективен, возможно мал capacity
     * - 0.7-0.9 — хороший показатель
     * - > 0.95 — отличный показатель
     */
    double hitRate() const {
        uint64_t total = totalRequests();
        if (total == 0) return 0.0;
        return static_cast<double>(hits_) / static_cast<double>(total);
    }

    /**
     * @brief Сбросить все счётчики
     */
    void reset() {
        hits_ = 0;
        misses_ = 0;
        inserts_ = 0;
        updates_ = 0;
        evictions_ = 0;
        removes_ = 0;
        clears_ = 0;
    }

private:
    std::atomic<uint64_t> hits_{0};
    std::atomic<uint64_t> misses_{0};
    std::atomic<uint64_t> inserts_{0};
    std::atomic<uint64_t> updates_{0};
    std::atomic<uint64_t> evictions_{0};
    std::atomic<uint64_t> removes_{0};
    std::atomic<uint64_t> clears_{0};
};