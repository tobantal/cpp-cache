#pragma once

#include "IExpirationPolicy.hpp"
#include <unordered_map>

/**
 * @brief Политика с единым TTL для всех элементов
 * @tparam K Тип ключа
 * 
 * Все элементы кэша имеют одинаковое время жизни от момента вставки.
 * Это самый распространённый и простой вариант TTL.
 * 
 * Алгоритм:
 * - При вставке сохраняем время: expireAt = now + globalTtl
 * - При доступе проверяем: now > expireAt -> expired
 * 
 * Примеры использования:
 * - Кэш биржевых цен: TTL = 1-5 секунд (цены быстро устаревают)
 * - Кэш сессий: TTL = 30 минут
 * - Кэш DNS: TTL = 5 минут
 * 
 * @code
 *   // Создание политики с TTL 5 секунд
 *   auto ttlPolicy = std::make_unique<GlobalTTL<std::string>>(
 *       std::chrono::seconds(5)
 *   );
 *   
 *   auto cache = Cache<std::string, int>(
 *       1000,
 *       std::make_unique<LRUPolicy<std::string>>(),
 *       std::move(ttlPolicy)
 *   );
 * @endcode
 * 
 * @note Время хранится как time_point момента истечения, а не вставки.
 *       Это упрощает проверку: просто сравниваем с now().
 */
template<typename K>
class GlobalTTL : public IExpirationPolicy<K> {
public:
    using typename IExpirationPolicy<K>::Clock;
    using typename IExpirationPolicy<K>::Duration;
    using typename IExpirationPolicy<K>::TimePoint;

    /**
     * @brief Конструктор
     * @param ttl Время жизни элементов
     * 
     * @throws std::invalid_argument если ttl <= 0
     */
    explicit GlobalTTL(Duration ttl) : globalTtl_(ttl) {
        if (ttl <= Duration::zero()) {
            throw std::invalid_argument("TTL must be positive");
        }
    }

    /**
     * @brief Конструктор с TTL в секундах (удобство)
     * @param seconds Время жизни в секундах
     */
    explicit GlobalTTL(int64_t seconds) 
        : GlobalTTL(std::chrono::seconds(seconds)) {}

    /**
     * @brief Проверить, истёк ли срок действия ключа
     * @param key Ключ для проверки
     * @return true если элемент просрочен
     * 
     * Сравнивает текущее время с сохранённым временем истечения.
     * Если ключ не найден в карте (не отслеживается), считаем не истёкшим.
     */
    bool isExpired(const K& key) const override {
        auto it = expirationTimes_.find(key);
        if (it == expirationTimes_.end()) {
            // Ключ не отслеживается — считаем не истёкшим
            // Это может произойти если политика добавлена после вставки
            return false;
        }
        
        return Clock::now() > it->second;
    }

    /**
     * @brief Зарегистрировать время истечения для нового ключа
     * @param key Ключ
     * @param customTtl Игнорируется — используется глобальный TTL
     * 
     * Вычисляет время истечения: now + globalTtl
     * 
     * @note customTtl игнорируется в GlobalTTL.
     *       Для индивидуального TTL используйте PerKeyTTL.
     */
    void onInsert(const K& key, 
                  std::optional<Duration> customTtl = std::nullopt) override {
        (void)customTtl;  // В GlobalTTL игнорируем индивидуальный TTL
        
        TimePoint expireAt = Clock::now() + globalTtl_;
        expirationTimes_[key] = expireAt;
    }

    /**
     * @brief Обработка доступа к ключу
     * @param key Ключ
     * 
     * В фиксированном TTL ничего не делает.
     * Время истечения не сбрасывается при доступе.
     * 
     * @note Для Sliding TTL (сброс при доступе) нужна отдельная политика.
     */
    void onAccess(const K& key) override {
        (void)key;
        // Фиксированный TTL — не сбрасываем таймер при доступе
    }

    /**
     * @brief Удалить метаданные для ключа
     * @param key Ключ
     * 
     * Вызывается при удалении элемента из кэша (явном или при вытеснении).
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
     * 
     * Полезно для периодической фоновой очистки:
     * @code
     *   for (const auto& key : ttlPolicy->collectExpired()) {
     *       cache.remove(key);
     *   }
     * @endcode
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
     * @return Оставшееся время или nullopt если ключ не отслеживается/истёк
     */
    std::optional<Duration> timeToLive(const K& key) const override {
        auto it = expirationTimes_.find(key);
        if (it == expirationTimes_.end()) {
            return std::nullopt;  // Ключ не отслеживается
        }
        
        TimePoint now = Clock::now();
        if (now > it->second) {
            return Duration::zero();  // Уже истёк
        }
        
        return it->second - now;
    }

    // ==================== Дополнительные методы ====================

    /**
     * @brief Получить глобальный TTL
     * @return Время жизни элементов
     */
    Duration getGlobalTTL() const {
        return globalTtl_;
    }

    /**
     * @brief Изменить глобальный TTL
     * @param ttl Новое время жизни
     * 
     * Влияет только на новые элементы.
     * Существующие элементы сохраняют своё время истечения.
     * 
     * @throws std::invalid_argument если ttl <= 0
     */
    void setGlobalTTL(Duration ttl) {
        if (ttl <= Duration::zero()) {
            throw std::invalid_argument("TTL must be positive");
        }
        globalTtl_ = ttl;
    }

    /**
     * @brief Количество отслеживаемых ключей
     * @return Размер карты времён истечения
     * 
     * Для отладки — должно совпадать с размером кэша.
     */
    size_t trackedKeysCount() const {
        return expirationTimes_.size();
    }

private:
    /// Глобальное время жизни для всех элементов
    Duration globalTtl_;
    
    /// Карта: ключ → время истечения (expireAt, не insertTime)
    std::unordered_map<K, TimePoint> expirationTimes_;
};