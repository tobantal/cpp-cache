#pragma once

#include "CacheStrategy.hpp"
#include <lrucache.hpp>
#include <memory>

/**
 * @brief Стратегия для cpp-lru-cache библиотеки
 */
template<typename K, typename V>
class CppLRUStrategy : public CacheStrategy<K, V> {
public:
    explicit CppLRUStrategy(size_t capacity)
        : capacity_(capacity), cache_(capacity) {
    }
    
    void put(const K& key, const V& value) override {
        cache_.put(key, value);
    }
    
    std::optional<V> get(const K& key) override {
        try {
            return cache_.get(key);
        } catch (const std::range_error&) {
            return std::nullopt;
        }
    }
    
    bool remove(const K& key) override {
        (void)key;
        return false;
    }
    
    void clear() override {
        // cpp-lru-cache не имеет clear()
        // Пересоздаём через move-конструктор
        cache_ = cache::lru_cache<K, V>(capacity_);
    }
    
    size_t size() const override {
        return cache_.size();
    }
    
    size_t capacity() const override {
        return capacity_;
    }
    
    std::string name() const override {
        return "cpp-lru-cache";
    }
    
    std::vector<std::string> supportedPolicies() const override {
        return {"LRU"};
    }
    
    bool isThreadSafe() const override {
        return false;
    }
    
    bool isSingleThreaded() const override {
        return true;
    }

private:
    size_t capacity_;
    cache::lru_cache<K, V> cache_;
};
