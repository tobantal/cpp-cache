#pragma once

#include <algorithm>
#include <cache/ICache.hpp>
#include <cache/eviction/IEvictionPolicy.hpp>
#include <cache/listeners/ICacheListener.hpp>
#include <cache/expiration/IExpirationPolicy.hpp>
#include <cache/expiration/NoExpiration.hpp>
#include <unordered_map>
#include <memory>
#include <vector>
#include <stdexcept>

/**
 * @brief Основной класс кэша с поддержкой сменных политик вытеснения и TTL
 * @tparam K Тип ключа (должен быть hashable для unordered_map)
 * @tparam V Тип значения
 * 
 * Архитектура:
 * - Данные хранятся в std::unordered_map<K, V> — O(1) доступ
 * - Политика вытеснения инжектируется через конструктор (Strategy pattern)
 * - Политика истечения (TTL) опциональна — по умолчанию NoExpiration
 * - Слушатели получают уведомления о событиях (Observer pattern)
 * 
 * Два независимых механизма удаления:
 * 1. Eviction (вытеснение) — при переполнении кэша, по политике LRU/LFU/FIFO
 * 2. Expiration (истечение) — по времени жизни (TTL)
 * 
 * Пример использования:
 * @code
 *   // Без TTL
 *   auto cache = Cache<std::string, int>(100, 
 *       std::make_unique<LRUPolicy<std::string>>());
 *   
 *   // С TTL 5 секунд
 *   auto cache = Cache<std::string, int>(100, 
 *       std::make_unique<LRUPolicy<std::string>>(),
 *       std::make_unique<GlobalTTL<std::string>>(std::chrono::seconds(5)));
 * @endcode
 */
template<typename K, typename V>
class Cache : public ICache<K, V> {
public:
    using Duration = typename IExpirationPolicy<K>::Duration;

    /**
     * @brief Конструктор без TTL (элементы живут вечно)
     * @param capacity Максимальная ёмкость кэша (должна быть > 0)
     * @param evictionPolicy Политика вытеснения (ownership передаётся кэшу)
     */
    Cache(size_t capacity, std::unique_ptr<IEvictionPolicy<K>> evictionPolicy)
        : Cache(capacity, std::move(evictionPolicy), 
                std::make_unique<NoExpiration<K>>())
    {}

    /**
     * @brief Конструктор с TTL политикой
     * @param capacity Максимальная ёмкость кэша (должна быть > 0)
     * @param evictionPolicy Политика вытеснения
     * @param expirationPolicy Политика истечения (TTL)
     */
    Cache(size_t capacity, 
          std::unique_ptr<IEvictionPolicy<K>> evictionPolicy,
          std::unique_ptr<IExpirationPolicy<K>> expirationPolicy)
        : capacity_(capacity)
        , evictionPolicy_(std::move(evictionPolicy))
        , expirationPolicy_(std::move(expirationPolicy))
    {
        if (capacity_ == 0) {
            throw std::invalid_argument("Cache capacity must be greater than 0");
        }
        if (!evictionPolicy_) {
            throw std::invalid_argument("Eviction policy cannot be null");
        }
        if (!expirationPolicy_) {
            // Если не передана — используем NoExpiration
            expirationPolicy_ = std::make_unique<NoExpiration<K>>();
        }
    }

    /**
     * @brief Получить значение по ключу
     * 
     * Логика:
     * 1. Проверяем наличие ключа в data_
     * 2. Если есть — проверяем TTL через expirationPolicy_
     * 3. Если TTL истёк — удаляем элемент и возвращаем nullopt
     * 4. Если не истёк — уведомляем политики и возвращаем значение
     */
    std::optional<V> get(const K& key) override {
        auto it = data_.find(key);
        if (it == data_.end()) {
            notifyMiss(key);
            return std::nullopt;
        }
        
        // Проверяем TTL
        if (expirationPolicy_->isExpired(key)) {
            // Элемент просрочен — удаляем
            removeInternal(key, it);
            notifyExpire(key);
            notifyMiss(key);  // С точки зрения клиента — это miss
            return std::nullopt;
        }
        
        // Hit: уведомляем политики и слушателей
        evictionPolicy_->onAccess(key);
        expirationPolicy_->onAccess(key);  // Для SlidingTTL
        notifyHit(key);
        return it->second;
    }

    /**
     * @brief Добавить или обновить значение с TTL по умолчанию
     * 
     * Логика:
     * 1. Если ключ существует — обновляем значение и TTL
     * 2. Если ключ новый и кэш полон — вытесняем жертву
     * 3. Вставляем новый ключ, регистрируем в политиках
     */
    void put(const K& key, const V& value) override {
        put(key, value, std::nullopt);  // Без кастомного TTL
    }

    /**
     * @brief Добавить или обновить значение с кастомным TTL
     * @param key Ключ
     * @param value Значение
     * @param ttl Индивидуальный TTL (для PerKeyTTL), игнорируется для GlobalTTL
     */
    void put(const K& key, const V& value, std::optional<Duration> ttl) {
        auto it = data_.find(key);
        
        if (it != data_.end()) {
            // Update существующего ключа
            V oldValue = it->second;
            it->second = value;
            evictionPolicy_->onAccess(key);
            // При обновлении обновляем и TTL
            expirationPolicy_->onRemove(key);
            expirationPolicy_->onInsert(key, ttl);
            notifyUpdate(key, oldValue, value);
            return;
        }
        
        // Новый ключ — проверяем, нужно ли вытеснение
        if (data_.size() >= capacity_) {
            evict();
        }
        
        // Вставка нового элемента
        data_[key] = value;
        evictionPolicy_->onInsert(key);
        expirationPolicy_->onInsert(key, ttl);
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
        
        removeInternal(key, it);
        notifyRemove(key);
        return true;
    }

    /**
     * @brief Очистить весь кэш
     */
    void clear() override {
        size_t count = data_.size();
        data_.clear();
        evictionPolicy_->clear();
        expirationPolicy_->clear();
        notifyClear(count);
    }

    size_t size() const override {
        return data_.size();
    }

    bool contains(const K& key) const override {
        auto it = data_.find(key);
        if (it == data_.end()) {
            return false;
        }
        // Проверяем TTL (но не удаляем — contains должен быть const)
        return !expirationPolicy_->isExpired(key);
    }

    size_t capacity() const override {
        return capacity_;
    }

    // ==================== TTL-специфичные методы ====================

    /**
     * @brief Получить оставшееся время жизни ключа
     * @param key Ключ
     * @return Оставшееся время или nullopt если бессрочный/не найден
     */
    std::optional<Duration> timeToLive(const K& key) const {
        if (!contains(key)) {
            return std::nullopt;
        }
        return expirationPolicy_->timeToLive(key);
    }

    /**
     * @brief Удалить все просроченные элементы
     * @return Количество удалённых элементов
     * 
     * Полезно для периодической фоновой очистки:
     * @code
     *   // Каждую минуту
     *   scheduler.every(std::chrono::minutes(1), [&]() {
     *       size_t removed = cache.removeExpired();
     *       log("Removed {} expired entries", removed);
     *   });
     * @endcode
     */
    size_t removeExpired() {
        std::vector<K> expired = expirationPolicy_->collectExpired();
        size_t count = 0;
        
        for (const K& key : expired) {
            auto it = data_.find(key);
            if (it != data_.end()) {
                removeInternal(key, it);
                notifyExpire(key);
                ++count;
            }
        }
        
        return count;
    }

    // ==================== Управление политиками ====================

    /**
     * @brief Динамическая смена политики вытеснения
     * @param policy Новая политика
     */
    void setEvictionPolicy(std::unique_ptr<IEvictionPolicy<K>> policy) {
        if (!policy) {
            throw std::invalid_argument("Eviction policy cannot be null");
        }
        
        evictionPolicy_ = std::move(policy);
        
        // Регистрируем существующие ключи в новой политике
        for (const auto& pair : data_) {
            evictionPolicy_->onInsert(pair.first);
        }
    }

    /**
     * @brief Динамическая смена политики истечения (TTL)
     * @param policy Новая политика
     * 
     * @note При смене политики все существующие элементы
     *       регистрируются в новой политике с её default TTL.
     */
    void setExpirationPolicy(std::unique_ptr<IExpirationPolicy<K>> policy) {
        if (!policy) {
            expirationPolicy_ = std::make_unique<NoExpiration<K>>();
        } else {
            expirationPolicy_ = std::move(policy);
        }
        
        // Регистрируем существующие ключи в новой политике
        for (const auto& pair : data_) {
            expirationPolicy_->onInsert(pair.first, std::nullopt);
        }
    }

    /**
     * @brief Получить указатель на политику истечения (для настройки)
     * @return Указатель на текущую политику TTL
     */
    IExpirationPolicy<K>* expirationPolicy() {
        return expirationPolicy_.get();
    }

    // ==================== Управление слушателями ====================

    /**
     * @brief Добавить слушателя событий
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
     * @brief Внутреннее удаление элемента (без уведомления listeners)
     */
    void removeInternal(const K& key, 
                        typename std::unordered_map<K, V>::iterator it) {
        data_.erase(it);
        evictionPolicy_->onRemove(key);
        expirationPolicy_->onRemove(key);
    }

    /**
     * @brief Вытеснить один элемент по политике
     */
    void evict() {
        K victim = evictionPolicy_->selectVictim();
        
        auto it = data_.find(victim);
        if (it != data_.end()) {
            V value = it->second;
            removeInternal(victim, it);
            notifyEvict(victim, value);
        }
    }

    // ==================== Уведомления слушателей ====================

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

    /**
     * @brief Уведомление об истечении TTL (отдельно от evict/remove)
     */
    void notifyExpire(const K& key) {
        // Можно добавить отдельный callback в ICacheListener
        // Пока просто логируем как remove
        (void)key;
    }

private:
    size_t capacity_;
    std::unordered_map<K, V> data_;
    std::unique_ptr<IEvictionPolicy<K>> evictionPolicy_;
    std::unique_ptr<IExpirationPolicy<K>> expirationPolicy_;
    std::vector<std::shared_ptr<ICacheListener<K, V>>> listeners_;
};