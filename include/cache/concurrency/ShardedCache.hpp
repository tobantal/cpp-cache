#pragma once

#include <cache/ICache.hpp>
#include <shared_mutex>
#include <array>
#include <memory>
#include <functional>
#include <mutex>

/**
 * @brief Шардированный кэш для высокой конкурентности
 * @tparam K Тип ключа (должен быть hashable)
 * @tparam V Тип значения
 * @tparam ShardCount Количество шардов (рекомендуется степень 2)
 * 
 * Распределяет ключи по независимым шардам через хэш-функцию.
 * Каждый шард имеет свой mutex — потоки, работающие с разными шардами,
 * не блокируют друг друга.
 * 
 * Масштабируемость:
 * - ThreadSafeCache: 1 mutex на весь кэш — высокая конкуренция
 * - ShardedCache<K,V,8>: 8 mutex — в 8 раз меньше конкуренции
 * - ShardedCache<K,V,64>: 64 mutex — почти линейная масштабируемость
 * 
 * Использование:
 * @code
 *   // Фабрика для создания внутренних кэшей
 *   auto cacheFactory = [](size_t capacity) {
 *       return std::make_unique<Cache<std::string, int>>(
 *           capacity, std::make_unique<LRUPolicy<std::string>>());
 *   };
 *   
 *   // Создаём шардированный кэш на 10000 элементов, 16 шардов
 *   auto cache = std::make_unique<ShardedCache<std::string, int, 16>>(
 *       10000, cacheFactory);
 * @endcode
 * 
 * @note capacity распределяется между шардами равномерно.
 *       При capacity=1000 и ShardCount=8 каждый шард получит ~125 элементов.
 */
template<typename K, typename V, size_t ShardCount = 16>
class ShardedCache : public ICache<K, V> {
public:
    static_assert(ShardCount > 0, "ShardCount must be greater than 0");

    /**
     * @brief Фабрика для создания внутренних кэшей
     * @param shardCapacity Ёмкость одного шарда
     * @return Уникальный указатель на кэш
     */
    using CacheFactory = std::function<std::unique_ptr<ICache<K, V>>(size_t shardCapacity)>;

    /**
     * @brief Конструктор
     * @param totalCapacity Общая ёмкость (распределяется по шардам)
     * @param factory Фабрика для создания кэшей шардов
     */
    ShardedCache(size_t totalCapacity, CacheFactory factory)
        : totalCapacity_(totalCapacity)
    {
        if (totalCapacity == 0) {
            throw std::invalid_argument("Total capacity must be greater than 0");
        }
        if (!factory) {
            throw std::invalid_argument("Cache factory cannot be null");
        }

        size_t shardCapacity = (totalCapacity + ShardCount - 1) / ShardCount;
        shardCapacity = std::max(shardCapacity, size_t(1));

        for (size_t i = 0; i < ShardCount; ++i) {
            shards_[i].cache = factory(shardCapacity);
            if (!shards_[i].cache) {
                throw std::runtime_error("Cache factory returned null");
            }
        }
    }

    /**
     * @brief Получить значение по ключу (exclusive lock на шард)
     * @note Хотя это логически "чтение", операция модифицирует
     *       внутреннее состояние (LRU порядок), поэтому нужен exclusive lock
     */
    std::optional<V> get(const K& key) override {
        auto& shard = getShard(key);
        std::unique_lock lock(shard.mutex);
        return shard.cache->get(key);
    }

    /**
     * @brief Добавить или обновить значение
     */
    void put(const K& key, const V& value) override {
        auto& shard = getShard(key);
        std::unique_lock lock(shard.mutex);
        shard.cache->put(key, value);
    }

    /**
     * @brief Удалить значение по ключу
     */
    bool remove(const K& key) override {
        auto& shard = getShard(key);
        std::unique_lock lock(shard.mutex);
        return shard.cache->remove(key);
    }

    /**
     * @brief Очистить весь кэш
     * @note Блокирует все шарды последовательно
     */
    void clear() override {
        for (auto& shard : shards_) {
            std::unique_lock lock(shard.mutex);
            shard.cache->clear();
        }
    }

    /**
     * @brief Получить общий размер кэша
     * @note Сумма размеров всех шардов (не атомарный snapshot)
     */
    size_t size() const override {
        size_t total = 0;
        for (const auto& shard : shards_) {
            std::shared_lock lock(shard.mutex);
            total += shard.cache->size();
        }
        return total;
    }

    /**
     * @brief Проверить наличие ключа
     */
    bool contains(const K& key) const override {
        const auto& shard = getShard(key);
        std::shared_lock lock(shard.mutex);
        return shard.cache->contains(key);
    }

    /**
     * @brief Получить общую ёмкость
     */
    size_t capacity() const override {
        return totalCapacity_;
    }

    /**
     * @brief Количество шардов
     */
    static constexpr size_t shardCount() { return ShardCount; }

    /**
     * @brief Получить размер конкретного шарда
     */
    size_t shardSize(size_t shardIndex) const {
        if (shardIndex >= ShardCount) {
            throw std::out_of_range("Shard index out of range");
        }
        std::shared_lock lock(shards_[shardIndex].mutex);
        return shards_[shardIndex].cache->size();
    }

    /**
     * @brief Выполнить операцию над конкретным шардом
     * @param key Ключ (определяет шард)
     * @param operation Функция, принимающая ссылку на кэш шарда
     */
    template<typename Func>
    auto withShardLock(const K& key, Func&& operation)
        -> decltype(operation(std::declval<ICache<K, V>&>())) {
        auto& shard = getShard(key);
        std::unique_lock lock(shard.mutex);
        return operation(*shard.cache);
    }

    /**
     * @brief Выполнить операцию над всеми шардами
     * @param operation Функция, вызываемая для каждого шарда
     * @note Шарды обрабатываются последовательно с блокировкой каждого
     */
    template<typename Func>
    void forEachShard(Func&& operation) {
        for (auto& shard : shards_) {
            std::unique_lock lock(shard.mutex);
            operation(*shard.cache);
        }
    }

private:
    struct Shard {
        std::unique_ptr<ICache<K, V>> cache;
        mutable std::shared_mutex mutex;
    };

    size_t totalCapacity_;
    std::array<Shard, ShardCount> shards_;

    /**
     * @brief Получить шард по ключу
     */
    Shard& getShard(const K& key) {
        return shards_[getShardIndex(key)];
    }

    const Shard& getShard(const K& key) const {
        return shards_[getShardIndex(key)];
    }

    /**
     * @brief Вычислить индекс шарда по ключу
     */
    size_t getShardIndex(const K& key) const {
        std::hash<K> hasher;
        return hasher(key) % ShardCount;
    }
};