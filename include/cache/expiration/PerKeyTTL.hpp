#pragma once

#include "IExpirationPolicy.hpp"
#include <unordered_map>

/**
 * @brief Политика с индивидуальным TTL для каждого элемента
 * @tparam K Тип ключа
 * 
 * Каждый элемент может иметь собственное время жизни.
 * Поддерживает как относительный TTL (duration), так и абсолютное время (time_point).
 * 
 * Сценарии использования:
 * - Разные типы данных с разным TTL в одном кэше
 * - Кэширование с учётом заголовков Cache-Control из HTTP
 * - Абсолютное время истечения (например, "удалить в 18:45")
 * 
 * @code
 *   // С default TTL
 *   auto ttlPolicy = std::make_unique<PerKeyTTL<std::string>>(
 *       std::chrono::seconds(60)  // Default TTL
 *   );
 *   
 *   // Вставка с кастомным TTL
 *   cache.put("short-lived", value, std::chrono::seconds(5));
 *   cache.put("long-lived", value, std::chrono::hours(1));
 *   cache.put("default-ttl", value);  // Использует default TTL
 * @endcode
 * 
 * @note Если элемент вставлен без кастомного TTL, используется defaultTtl.
 *       Если defaultTtl не задан (nullopt), элемент живёт вечно.
 */
template<typename K>
class PerKeyTTL : public IExpirationPolicy<K> {
public:
    using typename IExpirationPolicy<K>::Clock;
    using typename IExpirationPolicy<K>::Duration;
    using typename IExpirationPolicy<K>::TimePoint;

    /**
     * @brief Конструктор с опциональным default TTL
     * @param defaultTtl TTL по умолчанию для элементов без явного TTL
     * 
     * Если defaultTtl = nullopt, элементы без явного TTL живут вечно.
     */
    explicit PerKeyTTL(std::optional<Duration> defaultTtl = std::nullopt)
        : defaultTtl_(defaultTtl) {}

    /**
     * @brief Конструктор с default TTL в секундах
     * @param defaultSeconds TTL по умолчанию в секундах
     */
    explicit PerKeyTTL(int64_t defaultSeconds)
        : defaultTtl_(std::chrono::seconds(defaultSeconds)) {}

    /**
     * @brief Проверить, истёк ли срок действия ключа
     * @param key Ключ для проверки
     * @return true если элемент просрочен
     */
    bool isExpired(const K& key) const override {
        auto it = expirationTimes_.find(key);
        if (it == expirationTimes_.end()) {
            // Ключ не отслеживается или имеет бесконечный TTL
            return false;
        }
        
        return Clock::now() > it->second;
    }

    /**
     * @brief Зарегистрировать время истечения для нового ключа
     * @param key Ключ
     * @param customTtl Индивидуальный TTL (если не задан, используется default)
     * 
     * Приоритет:
     * 1. customTtl если задан
     * 2. defaultTtl_ если задан
     * 3. Бесконечный TTL (не добавляем в карту)
     */
    void onInsert(const K& key, 
                  std::optional<Duration> customTtl = std::nullopt) override {
        // Определяем TTL: кастомный > дефолтный > бесконечный
        std::optional<Duration> ttl = customTtl.has_value() ? customTtl : defaultTtl_;
        
        if (!ttl.has_value()) {
            // Бесконечный TTL — не отслеживаем
            expirationTimes_.erase(key);  // На случай если был ранее
            return;
        }
        
        if (ttl.value() <= Duration::zero()) {
            // Невалидный TTL — игнорируем (можно бросить исключение)
            return;
        }
        
        TimePoint expireAt = Clock::now() + ttl.value();
        expirationTimes_[key] = expireAt;
    }

    /**
     * @brief Обработка доступа к ключу
     * @param key Ключ
     * 
     * В фиксированном TTL ничего не делает.
     */
    void onAccess(const K& key) override {
        (void)key;
        // Фиксированный TTL — не сбрасываем таймер
    }

    /**
     * @brief Удалить метаданные для ключа
     * @param key Ключ
     */
    void onRemove(const K& key) override {
        expirationTimes_.erase(key);
    }

    /**
     * @brief Очистить все метаданные
     */
    void clear() override {
        expirationTimes_.clear();
    }

    /**
     * @brief Собрать все просроченные ключи
     * @return Вектор ключей с истёкшим TTL
     */
    std::vector<K> collectExpired() const override {
        std::vector<K> expired;
        TimePoint now = Clock::now();
        
        for (const auto& [key, expireAt] : expirationTimes_) {
            if (now > expireAt) {
                expired.push_back(key);
            }
        }
        
        return expired;
    }

    /**
     * @brief Получить оставшееся время жизни ключа
     * @param key Ключ
     * @return Оставшееся время, nullopt если бесконечный TTL, zero если истёк
     */
    std::optional<Duration> timeToLive(const K& key) const override {
        auto it = expirationTimes_.find(key);
        if (it == expirationTimes_.end()) {
            return std::nullopt;  // Бесконечный TTL или не отслеживается
        }
        
        TimePoint now = Clock::now();
        if (now > it->second) {
            return Duration::zero();  // Уже истёк
        }
        
        return it->second - now;
    }

    // ==================== Дополнительные методы ====================

    /**
     * @brief Установить абсолютное время истечения для ключа
     * @param key Ключ
     * @param expireAt Время, когда элемент должен истечь
     * 
     * Полезно для сценария "удалить в конкретное время":
     * @code
     *   // Удалить в конце торгового дня
     *   policy->setExpireAt("SBER", todayAt(18, 45, 0));
     * @endcode
     */
    void setExpireAt(const K& key, TimePoint expireAt) {
        expirationTimes_[key] = expireAt;
    }

    /**
     * @brief Обновить TTL для существующего ключа
     * @param key Ключ
     * @param ttl Новый TTL (от текущего момента)
     * @return true если ключ найден и TTL обновлён
     * 
     * Позволяет продлить или сократить время жизни элемента.
     */
    bool updateTTL(const K& key, Duration ttl) {
        auto it = expirationTimes_.find(key);
        if (it == expirationTimes_.end()) {
            return false;  // Ключ не отслеживается
        }
        
        it->second = Clock::now() + ttl;
        return true;
    }

    /**
     * @brief Удалить TTL для ключа (сделать бессрочным)
     * @param key Ключ
     * @return true если ключ был найден
     */
    bool removeTTL(const K& key) {
        return expirationTimes_.erase(key) > 0;
    }

    /**
     * @brief Получить default TTL
     * @return Default TTL или nullopt если бесконечный
     */
    std::optional<Duration> getDefaultTTL() const {
        return defaultTtl_;
    }

    /**
     * @brief Установить default TTL
     * @param ttl Новый default TTL (nullopt для бесконечного)
     * 
     * Влияет только на новые элементы.
     */
    void setDefaultTTL(std::optional<Duration> ttl) {
        defaultTtl_ = ttl;
    }

    /**
     * @brief Количество ключей с конечным TTL
     */
    size_t trackedKeysCount() const {
        return expirationTimes_.size();
    }

    /**
     * @brief Проверить, имеет ли ключ конечный TTL
     */
    bool hasExpiration(const K& key) const {
        return expirationTimes_.find(key) != expirationTimes_.end();
    }

private:
    /// Default TTL для элементов без явного TTL
    std::optional<Duration> defaultTtl_;
    
    /// Карта: ключ → время истечения
    std::unordered_map<K, TimePoint> expirationTimes_;
};