#pragma once

#include <cstddef>


/**
 * @brief Интерфейс слушателя событий кэша
 * @tparam K Тип ключа
 * @tparam V Тип значения
 */
template<typename K, typename V>
class ICacheListener {
public:
    virtual ~ICacheListener() = default;

    virtual void onHit(const K& key) { (void)key; }
    virtual void onMiss(const K& key) { (void)key; }
    virtual void onInsert(const K& key, const V& value) { (void)key; (void)value; }
    virtual void onUpdate(const K& key, const V& oldValue, const V& newValue) {
        (void)key; (void)oldValue; (void)newValue;
    }
    virtual void onEvict(const K& key, const V& value) { (void)key; (void)value; }
    virtual void onRemove(const K& key) { (void)key; }
    virtual void onClear(size_t count) { (void)count; }
};
