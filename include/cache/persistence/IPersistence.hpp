#pragma once

#include <vector>
#include <utility>

/**
 * @brief Интерфейс персистентности данных кэша
 * @tparam K Тип ключа
 * @tparam V Тип значения
 * 
 * Отвечает за стратегию сохранения/загрузки.
 * Использует ISerializer для формата данных.
 * 
 * Реализации:
 * - SnapshotPersistence — полный дамп при каждом изменении или периодически
 * - WALPersistence — журнал операций (Write-Ahead Log) — будущее
 */
template<typename K, typename V>
class IPersistence {
public:
    virtual ~IPersistence() = default;
    
    /**
     * @brief Загрузить все данные
     * @return Вектор пар ключ-значение
     * @throws std::runtime_error при ошибке чтения
     */
    virtual std::vector<std::pair<K, V>> load() = 0;
    
    /**
     * @brief Сохранить все данные (полный snapshot)
     * @param entries Вектор пар ключ-значение
     * @throws std::runtime_error при ошибке записи
     */
    virtual void saveAll(const std::vector<std::pair<K, V>>& entries) = 0;
    
    /**
     * @brief Уведомление о добавлении/обновлении элемента
     * @param key Ключ
     * @param value Значение
     * 
     * Реализация решает, нужно ли сразу сохранять или накапливать.
     */
    virtual void onPut(const K& key, const V& value) = 0;
    
    /**
     * @brief Уведомление об удалении элемента
     * @param key Ключ
     */
    virtual void onRemove(const K& key) = 0;
    
    /**
     * @brief Уведомление об очистке кэша
     */
    virtual void onClear() = 0;
    
    /**
     * @brief Принудительно сбросить изменения на диск
     * 
     * Для стратегий с отложенной записью.
     */
    virtual void flush() = 0;
    
    /**
     * @brief Проверить, существует ли файл с данными
     */
    virtual bool exists() const = 0;
};