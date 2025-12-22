#pragma once

#include "CacheStrategy.hpp"
#include <cache/Cache.hpp>
#include <cache/eviction/LRUPolicy.hpp>
#include <cache/eviction/LFUPolicy.hpp>
#include <memory>

/**
 * @brief Стратегия для нашей библиотеки кэширования
 * 
 * Обёртка над Cache<K,V> для использования в бенчмарках.
 * Поддерживает переключение между LRU и LFU политиками.
 */
template<typename K, typename V>
class OurCacheStrategy : public CacheStrategy<K, V> {
public:
    /**
     * @brief Конструктор
     * @param capacity размер кэша
     */
    explicit OurCacheStrategy(size_t capacity)
        : capacity_(capacity) {
        
        // Создаём кэш с выбранной политикой
        cache_ = std::make_unique<Cache<K, V>>(
                capacity,
                std::make_unique<LRUPolicy<K>>()
            );
    }
    
    void put(const K& key, const V& value) override {
        cache_->put(key, value);
    }
    
    std::optional<V> get(const K& key) override {
        return cache_->get(key);
    }
    
    bool remove(const K& key) override {
        return cache_->remove(key);
    }
    
    void clear() override {
        cache_->clear();
    }
    
    size_t size() const override {
        return cache_->size();
    }
    
    size_t capacity() const override {
        return capacity_;
    }
    
    std::string name() const override {
        return "OurCache";
    }
    
    std::vector<std::string> supportedPolicies() const override {
        return {"LRU", "LFU"};
    }
    
    bool supportsTTL() const override {
        return true;  // Наша библиотека поддерживает TTL
    }
    
    bool isThreadSafe() const override {
        return true;  // Наша библиотека thread-safe
    }
    
    bool isSingleThreaded() const override {
        return false;  // Наша библиотека НЕ одопоточная
    }

private:
    std::unique_ptr<Cache<K, V>> cache_;
    size_t capacity_;
};
