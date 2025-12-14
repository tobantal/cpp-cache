#pragma once


/**
 * @brief Интерфейс политики вытеснения
 * @tparam K Тип ключа
 */
template<typename K>
class IEvictionPolicy {
public:
    virtual ~IEvictionPolicy() = default;

    /**
     * @brief Уведомление о доступе к ключу (get или update)
     */
    virtual void onAccess(const K& key) = 0;

    /**
     * @brief Уведомление о добавлении нового ключа
     */
    virtual void onInsert(const K& key) = 0;

    /**
     * @brief Уведомление об удалении ключа
     */
    virtual void onRemove(const K& key) = 0;

    /**
     * @brief Выбрать ключ для вытеснения
     * @return Ключ элемента, который следует удалить
     */
    virtual K selectVictim() = 0;

    /**
     * @brief Проверить, отслеживает ли политика какие-либо ключи
     */
    virtual bool empty() const = 0;

    /**
     * @brief Очистить все данные политики
     */
    virtual void clear() = 0;
};
