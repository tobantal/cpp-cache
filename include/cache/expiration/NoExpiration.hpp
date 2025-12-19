#pragma once

#include "IExpirationPolicy.hpp"

/**
 * @brief Политика без истечения срока действия
 * @tparam K Тип ключа
 * 
 * Элементы живут вечно (пока не будут вытеснены или явно удалены).
 * Это поведение по умолчанию для кэша без TTL.
 * 
 * Все методы — пустые заглушки с минимальными накладными расходами.
 * 
 * Использование:
 * @code
 *   auto cache = Cache<std::string, int>(
 *       1000,
 *       std::make_unique<LRUPolicy<std::string>>(),
 *       std::make_unique<NoExpiration<std::string>>()  // Опционально
 *   );
 * @endcode
 * 
 * @note Это null-object pattern — безопасная заглушка вместо nullptr.
 */
template<typename K>
class NoExpiration : public IExpirationPolicy<K> {
public:
    using typename IExpirationPolicy<K>::Duration;
    using typename IExpirationPolicy<K>::TimePoint;

    /**
     * @brief Элементы никогда не истекают
     * @return Всегда false
     */
    bool isExpired(const K& key) const override {
        (void)key;  // Подавляем warning о неиспользуемом параметре
        return false;
    }

    /**
     * @brief Ничего не делает — нет метаданных для хранения
     */
    void onInsert(const K& key, 
                  std::optional<Duration> customTtl = std::nullopt) override {
        (void)key;
        (void)customTtl;
    }

    /**
     * @brief Ничего не делает
     */
    void onAccess(const K& key) override {
        (void)key;
    }

    /**
     * @brief Ничего не делает — нет метаданных для очистки
     */
    void onRemove(const K& key) override {
        (void)key;
    }

    /**
     * @brief Ничего не делает
     */
    void clear() override {
        // Нет внутреннего состояния
    }

    /**
     * @brief Нет просроченных элементов
     * @return Пустой вектор
     */
    std::vector<K> collectExpired() const override {
        return {};
    }

    /**
     * @brief Время жизни бесконечно
     * @return nullopt (интерпретируется как "бесконечно")
     */
    std::optional<Duration> timeToLive(const K& key) const override {
        (void)key;
        return std::nullopt;  // Бесконечный TTL
    }
};