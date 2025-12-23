#pragma once

#include "../ICache.hpp"
#include <shared_mutex>
#include <memory>
#include <mutex>

/**
 * @brief Потокобезопасный декоратор кэша
 * @tparam K Тип ключа
 * @tparam V Тип значения
 * 
 * Оборачивает любой ICache и добавляет thread-safety через shared_mutex:
 * - Чтение (get, contains, size) — shared lock (много читателей)
 * - Запись (put, remove, clear) — exclusive lock (один писатель)
 * 
 * Использование:
 * @code
 *   // Создаём базовый кэш
 *   auto baseCache = std::make_unique<Cache<std::string, int>>(
 *       1000, std::make_unique<LRUPolicy<std::string>>());
 *   
 *   // Оборачиваем в thread-safe декоратор
 *   auto cache = std::make_unique<ThreadSafeCache<std::string, int>>(
 *       std::move(baseCache));
 *   
 *   // Теперь можно безопасно использовать из разных потоков
 *   cache->put("key", 42);
 * @endcode
 * 
 * @note Для высоконагруженных сценариев с большим количеством потоков
 *       рекомендуется использовать ShardedCache для лучшей масштабируемости.
 */
template<typename K, typename V>
class ThreadSafeCache : public ICache<K, V> {
public:
    /**
     * @brief Конструктор
     * @param inner Внутренний кэш (ownership передаётся)
     */
    explicit ThreadSafeCache(std::unique_ptr<ICache<K, V>> inner)
        : inner_(std::move(inner))
    {
        if (!inner_) {
            throw std::invalid_argument("Inner cache cannot be null");
        }
    }

    /**
     * @brief Получить значение по ключу (exclusive lock)
     * @note Хотя это логически "чтение", операция модифицирует
     *       внутреннее состояние (LRU порядок), поэтому нужен exclusive lock
     */
    std::optional<V> get(const K& key) override {
        std::unique_lock lock(mutex_);
        return inner_->get(key);
    }

    /**
     * @brief Добавить или обновить значение (exclusive lock)
     */
    void put(const K& key, const V& value) override {
        std::unique_lock lock(mutex_);
        inner_->put(key, value);
    }

    /**
     * @brief Удалить значение по ключу (exclusive lock)
     */
    bool remove(const K& key) override {
        std::unique_lock lock(mutex_);
        return inner_->remove(key);
    }

    /**
     * @brief Очистить весь кэш (exclusive lock)
     */
    void clear() override {
        std::unique_lock lock(mutex_);
        inner_->clear();
    }

    /**
     * @brief Получить текущий размер (shared lock)
     */
    size_t size() const override {
        std::shared_lock lock(mutex_);
        return inner_->size();
    }

    /**
     * @brief Проверить наличие ключа (shared lock)
     */
    bool contains(const K& key) const override {
        std::shared_lock lock(mutex_);
        return inner_->contains(key);
    }

    /**
     * @brief Получить ёмкость (shared lock)
     */
    size_t capacity() const override {
        std::shared_lock lock(mutex_);
        return inner_->capacity();
    }

    /**
     * @brief Получить доступ к внутреннему кэшу (для расширенных операций)
     * @warning Вызывающий код должен сам обеспечить синхронизацию!
     */
    ICache<K, V>* inner() { return inner_.get(); }
    const ICache<K, V>* inner() const { return inner_.get(); }

    /**
     * @brief Выполнить операцию под exclusive lock
     * @param operation Функция, принимающая ссылку на внутренний кэш
     * 
     * Полезно для атомарных операций типа "check-then-act":
     * @code
     *   cache.withExclusiveLock([](ICache<K, V>& inner) {
     *       if (!inner.contains("key")) {
     *           inner.put("key", computeValue());
     *       }
     *   });
     * @endcode
     */
    template<typename Func>
    auto withExclusiveLock(Func&& operation) 
        -> decltype(operation(std::declval<ICache<K, V>&>())) {
        std::unique_lock lock(mutex_);
        return operation(*inner_);
    }

    /**
     * @brief Выполнить операцию под shared lock
     */
    template<typename Func>
    auto withSharedLock(Func&& operation) const
        -> decltype(operation(std::declval<const ICache<K, V>&>())) {
        std::shared_lock lock(mutex_);
        return operation(*inner_);
    }

private:
    std::unique_ptr<ICache<K, V>> inner_;
    mutable std::shared_mutex mutex_;
};