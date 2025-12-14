#pragma once

#include <algorithm>
#include <cache/ICache.hpp>
#include <cache/IEvictionPolicy.hpp>
#include <cache/ICacheListener.hpp>
#include <unordered_map>
#include <memory>
#include <vector>
#include <stdexcept>

/**
 * @brief Основной класс кэша с поддержкой сменных политик вытеснения
 * @tparam K Тип ключа (должен быть hashable для unordered_map)
 * @tparam V Тип значения
 * 
 * Архитектура:
 * - Данные хранятся в std::unordered_map<K, V> — O(1) доступ
 * - Политика вытеснения инжектируется через конструктор (Strategy pattern)
 * - Слушатели получают уведомления о событиях (Observer pattern)
 * 
 * Пример использования:
 *   auto cache = Cache<std::string, int>(100, std::make_unique<LRUPolicy<std::string>>());
 *   cache.put("key", 42);
 *   auto value = cache.get("key");  // std::optional<int>(42)
 */
template<typename K, typename V>
class Cache : public ICache<K, V> {
public:
    /**
     * @brief Конструктор
     * @param capacity Максимальная ёмкость кэша (должна быть > 0)
     * @param policy Политика вытеснения (ownership передаётся кэшу)
     */
    Cache(size_t capacity, std::unique_ptr<IEvictionPolicy<K>> policy)
        : capacity_(capacity)
        , policy_(std::move(policy))
    {
        if (capacity_ == 0) {
            throw std::invalid_argument("Cache capacity must be greater than 0");
        }
        if (!policy_) {
            throw std::invalid_argument("Eviction policy cannot be null");
        }
    }

    /**
     * @brief Получить значение по ключу
     * 
     * При попадании (hit) уведомляем политику о доступе — 
     * это обновит позицию ключа (например, в LRU переместит в начало).
     */
    std::optional<V> get(const K& key) override {
        auto it = data_.find(key);
        if (it == data_.end()) {
            notifyMiss(key);
            return std::nullopt;
        }
        
        // Hit: уведомляем политику и слушателей
        policy_->onAccess(key);
        notifyHit(key);
        return it->second;
    }

    /**
     * @brief Добавить или обновить значение
     * 
     * Логика:
     * 1. Если ключ существует — обновляем значение, уведомляем политику о доступе
     * 2. Если ключ новый и кэш полон — вытесняем жертву
     * 3. Вставляем новый ключ, уведомляем политику о вставке
     */
    void put(const K& key, const V& value) override {
        auto it = data_.find(key);
        
        if (it != data_.end()) {
            // Update существующего ключа
            V oldValue = it->second;
            it->second = value;
            policy_->onAccess(key);
            notifyUpdate(key, oldValue, value);
            return;
        }
        
        // Новый ключ — проверяем, нужно ли вытеснение
        if (data_.size() >= capacity_) {
            evict();
        }
        
        // Вставка нового элемента
        data_[key] = value;
        policy_->onInsert(key);
        notifyInsert(key, value);
    }

    /**
     * @brief Удалить значение по ключу
     * @return true если элемент существовал и был удалён
     */
    bool remove(const K& key) override {
        auto it = data_.find(key);
        if (it == data_.end()) {
            return false;
        }
        
        data_.erase(it);
        policy_->onRemove(key);
        notifyRemove(key);
        return true;
    }

    /**
     * @brief Очистить весь кэш
     */
    void clear() override {
        size_t count = data_.size();
        data_.clear();
        policy_->clear();
        notifyClear(count);
    }

    size_t size() const override {
        return data_.size();
    }

    bool contains(const K& key) const override {
        return data_.find(key) != data_.end();
    }

    size_t capacity() const override {
        return capacity_;
    }

    // ==================== Управление политикой ====================

    /**
     * @brief Динамическая смена политики вытеснения
     * @param policy Новая политика
     * 
     * При смене политики:
     * - Старая статистика теряется (упрощённый подход для MVP)
     * - Новая политика инициализируется текущими ключами
     * 
     * Альтернатива: можно было бы мигрировать данные из старой политики,
     * но это усложняет код и не всегда имеет смысл (разные политики
     * хранят разные метаданные — частоту, время и т.д.)
     */
    void setEvictionPolicy(std::unique_ptr<IEvictionPolicy<K>> policy) {
        if (!policy) {
            throw std::invalid_argument("Eviction policy cannot be null");
        }
        
        policy_ = std::move(policy);
        
        // Регистрируем существующие ключи в новой политике
        for (const auto& pair : data_) {
            policy_->onInsert(pair.first);
        }
    }

    // ==================== Управление слушателями ====================

    /**
     * @brief Добавить слушателя событий
     * 
     * shared_ptr позволяет одному слушателю подписаться на несколько кэшей
     * и безопасно удалиться когда больше не нужен.
     */
    void addListener(std::shared_ptr<ICacheListener<K, V>> listener) {
        if (listener) {
            listeners_.push_back(listener);
        }
    }

    /**
     * @brief Удалить слушателя
     */
    void removeListener(std::shared_ptr<ICacheListener<K, V>> listener) {
        listeners_.erase(
            std::remove(listeners_.begin(), listeners_.end(), listener),
            listeners_.end()
        );
    }

private:
    /**
     * @brief Вытеснить один элемент по политике
     * 
     * Порядок важен:
     * 1. Получаем жертву от политики
     * 2. Сохраняем значение для уведомления
     * 3. Удаляем из data_
     * 4. Уведомляем политику об удалении
     * 5. Уведомляем слушателей
     */
    void evict() {
        K victim = policy_->selectVictim();
        
        auto it = data_.find(victim);
        if (it != data_.end()) {
            V value = it->second;
            data_.erase(it);
            policy_->onRemove(victim);
            notifyEvict(victim, value);
        }
    }

    // ==================== Уведомления слушателей ====================
    // Проверка listeners_.empty() — оптимизация для бенчмарков:
    // если слушателей нет, не тратим время на цикл

    void notifyHit(const K& key) {
        if (listeners_.empty()) return;
        for (auto& listener : listeners_) {
            listener->onHit(key);
        }
    }

    void notifyMiss(const K& key) {
        if (listeners_.empty()) return;
        for (auto& listener : listeners_) {
            listener->onMiss(key);
        }
    }

    void notifyInsert(const K& key, const V& value) {
        if (listeners_.empty()) return;
        for (auto& listener : listeners_) {
            listener->onInsert(key, value);
        }
    }

    void notifyUpdate(const K& key, const V& oldValue, const V& newValue) {
        if (listeners_.empty()) return;
        for (auto& listener : listeners_) {
            listener->onUpdate(key, oldValue, newValue);
        }
    }

    void notifyEvict(const K& key, const V& value) {
        if (listeners_.empty()) return;
        for (auto& listener : listeners_) {
            listener->onEvict(key, value);
        }
    }

    void notifyRemove(const K& key) {
        if (listeners_.empty()) return;
        for (auto& listener : listeners_) {
            listener->onRemove(key);
        }
    }

    void notifyClear(size_t count) {
        if (listeners_.empty()) return;
        for (auto& listener : listeners_) {
            listener->onClear(count);
        }
    }

private:
    size_t capacity_;
    std::unordered_map<K, V> data_;
    std::unique_ptr<IEvictionPolicy<K>> policy_;
    std::vector<std::shared_ptr<ICacheListener<K, V>>> listeners_;
};