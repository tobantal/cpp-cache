#pragma once

#include <optional>
#include <string>
#include <vector>

/**
 * @brief Базовый интерфейс для всех кэш-стратегий
 * 
 * Позволяет тестировать разные библиотеки (LRUCache11, cpp-lru-cache, 
 * CacheLib, наша) через единый интерфейс.
 * 
 * @tparam K Тип ключа (int, string и т.д.)
 * @tparam V Тип значения (int, string и т.д.)
 */
template<typename K, typename V>
class CacheStrategy {
public:
    virtual ~CacheStrategy() = default;
    
    // ========== ОСНОВНЫЕ ОПЕРАЦИИ ==========
    
    /// Добавить элемент в кэш
    /// @param key ключ
    /// @param value значение
    virtual void put(const K& key, const V& value) = 0;
    
    /// Получить элемент из кэша
    /// @param key ключ
    /// @return значение если найдено, nullopt если нет
    virtual std::optional<V> get(const K& key) = 0;
    
    /// Удалить элемент из кэша
    /// @param key ключ
    /// @return true если удалён, false если не было
    virtual bool remove(const K& key) = 0;
    
    /// Очистить весь кэш
    virtual void clear() = 0;
    
    /// Получить текущий размер кэша (количество элементов)
    virtual size_t size() const = 0;
    
    /// Получить ёмкость кэша (максимальное количество элементов)
    virtual size_t capacity() const = 0;
    
    // ========== ИНФОРМАЦИЯ О СТРАТЕГИИ ==========
    
    /// Название библиотеки/стратегии
    /// Например: "OurCache", "LRUCache11", "CacheLib"
    virtual std::string name() const = 0;
    
    /// Какие политики вытеснения поддерживает
    /// Например: {"LRU"} или {"LRU", "LFU"}
    virtual std::vector<std::string> supportedPolicies() const = 0;
    
    /// Поддерживает ли TTL
    virtual bool supportsTTL() const {
        return false;
    }
    
    /// Поддерживает ли многопоточность
    virtual bool isThreadSafe() const {
        return false;
    }
    
    /// Одопоточная ли библиотека (не поддерживает MT)
    virtual bool isSingleThreaded() const {
        return false;
    }
};
