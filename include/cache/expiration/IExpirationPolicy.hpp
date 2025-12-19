#pragma once

#include <chrono>
#include <vector>
#include <optional>

/**
 * @brief Интерфейс политики истечения срока действия (TTL)
 * @tparam K Тип ключа
 * 
 * Определяет контракт для проверки и управления временем жизни элементов кэша.
 * 
 * Точки интеграции с Cache:
 * - get(): вызывает isExpired() перед возвратом значения
 * - put(): вызывает onInsert() для регистрации времени
 * - remove(): вызывает onRemove() для очистки метаданных
 * 
 * Стратегия проверки:
 * - Lazy expiration: проверка при доступе (get)
 * - Не требует фонового потока
 * - Память освобождается при следующем обращении
 * 
 * @note Политика истечения ортогональна политике вытеснения (eviction).
 *       Элемент может быть удалён по TTL ИЛИ вытеснен при переполнении.
 */
template<typename K>
class IExpirationPolicy {
public:
    /// Тип для представления времени
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration = Clock::duration;

    virtual ~IExpirationPolicy() = default;

    /**
     * @brief Проверить, истёк ли срок действия ключа
     * @param key Ключ для проверки
     * @return true если элемент просрочен и должен быть удалён
     * 
     * Вызывается из Cache::get() перед возвратом значения.
     * Если возвращает true, Cache удаляет элемент и возвращает nullopt.
     */
    virtual bool isExpired(const K& key) const = 0;

    /**
     * @brief Уведомление о вставке нового ключа
     * @param key Ключ
     * @param customTtl Опциональный индивидуальный TTL (для PerKeyTTL)
     * 
     * Вызывается из Cache::put() для новых ключей.
     * Политика должна запомнить время вставки или время истечения.
     */
    virtual void onInsert(const K& key, 
                          std::optional<Duration> customTtl = std::nullopt) = 0;

    /**
     * @brief Уведомление о доступе к ключу
     * @param key Ключ
     * 
     * Вызывается из Cache::get() при успешном доступе.
     * Используется для Sliding TTL (сброс таймера при доступе).
     * Для фиксированного TTL — обычно пустая реализация.
     */
    virtual void onAccess(const K& key) = 0;

    /**
     * @brief Уведомление об удалении ключа
     * @param key Ключ
     * 
     * Вызывается из Cache::remove() и при вытеснении.
     * Политика должна очистить метаданные для этого ключа.
     */
    virtual void onRemove(const K& key) = 0;

    /**
     * @brief Очистить все данные политики
     * 
     * Вызывается из Cache::clear().
     */
    virtual void clear() = 0;

    /**
     * @brief Собрать список просроченных ключей
     * @return Вектор ключей, срок действия которых истёк
     * 
     * Опциональный метод для фоновой/периодической очистки.
     * Позволяет удалить все просроченные элементы за один проход.
     * 
     * @note По умолчанию возвращает пустой вектор.
     *       Переопределяется в политиках, поддерживающих batch cleanup.
     */
    virtual std::vector<K> collectExpired() const {
        return {};
    }

    /**
     * @brief Получить время до истечения ключа
     * @param key Ключ
     * @return Оставшееся время или nullopt если ключ не отслеживается/уже истёк
     * 
     * Полезно для отладки и мониторинга.
     */
    virtual std::optional<Duration> timeToLive(const K& key) const = 0;
};