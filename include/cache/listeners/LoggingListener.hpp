#pragma once

#include "ICacheListener.hpp"
#include <iostream>
#include <string>

/**
 * @brief Слушатель для логирования событий кэша в консоль
 * @tparam K Тип ключа (должен поддерживать вывод в ostream)
 * @tparam V Тип значения (должен поддерживать вывод в ostream)
 * 
 * Использование:
 *   auto logger = std::make_shared<LoggingListener<std::string, int>>();
 *   cache.addListener(logger);
 * 
 * Для отключения логирования в бенчмарках — просто не добавляем слушателя.
 */
template<typename K, typename V>
class LoggingListener : public ICacheListener<K, V> {
public:
    /**
     * @brief Конструктор
     * @param prefix Префикс для всех сообщений (например, имя кэша)
     * @param os Поток вывода (по умолчанию std::cout)
     */
    explicit LoggingListener(const std::string& prefix = "Cache", 
                             std::ostream& os = std::cout)
        : prefix_(prefix)
        , os_(os)
    {}

    void onHit(const K& key) override {
        os_ << "[" << prefix_ << "] HIT: " << key << "\n";
    }

    void onMiss(const K& key) override {
        os_ << "[" << prefix_ << "] MISS: " << key << "\n";
    }

    void onInsert(const K& key, const V& value) override {
        os_ << "[" << prefix_ << "] INSERT: " << key << " = " << value << "\n";
    }

    void onUpdate(const K& key, const V& oldValue, const V& newValue) override {
        os_ << "[" << prefix_ << "] UPDATE: " << key 
            << " (" << oldValue << " -> " << newValue << ")\n";
    }

    void onEvict(const K& key, const V& value) override {
        os_ << "[" << prefix_ << "] EVICT: " << key << " = " << value << "\n";
    }

    void onRemove(const K& key) override {
        os_ << "[" << prefix_ << "] REMOVE: " << key << "\n";
    }

    void onClear(size_t count) override {
        os_ << "[" << prefix_ << "] CLEAR: " << count << " elements\n";
    }

private:
    std::string prefix_;
    std::ostream& os_;
};