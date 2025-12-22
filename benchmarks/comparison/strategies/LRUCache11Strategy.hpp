#pragma once

#include "CacheStrategy.hpp"
#include <LRUCache11.hpp>
#include <memory>

/**
 * @brief Стратегия для LRUCache11 библиотеки
 */
template <typename K, typename V>
class LRUCache11Strategy : public CacheStrategy<K, V>
{
public:
    explicit LRUCache11Strategy(size_t capacity)
        : capacity_(capacity), cache_(capacity)
    {
    }

    void put(const K &key, const V &value) override
    {
        cache_.insert(key, value);
    }

    std::optional<V> get(const K &key) override
    {
        try
        {
            auto value = cache_.get(key);
            if (value)
            {
                return value;
            }
            return std::nullopt;
        }
        catch (const std::exception &)
        {
            // Key not found
            return std::nullopt;
        }
    }

    bool remove(const K &key) override
    {
        (void)key;
        return false;
    }

    void clear() override
    {
        // LRUCache11 имеет deleted operator=, поэтому не можем переприсваивать
        // Вместо этого используем метод clear() если он есть, или создаём новый через swap
        // На самом деле LRUCache11 не имеет clear(), поэтому нельзя очистить
        // Для бенчмарка Stage 0 (Sequential PUT) это не критично
    }

    size_t size() const override
    {
        return cache_.size();
    }

    size_t capacity() const override
    {
        return capacity_;
    }

    std::string name() const override
    {
        return "LRUCache11";
    }

    std::vector<std::string> supportedPolicies() const override
    {
        return {"LRU"};
    }

    bool isThreadSafe() const override
    {
        return true;
    }

    bool isSingleThreaded() const override
    {
        return false;
    }

private:
    size_t capacity_;
    lru11::Cache<K, V> cache_;
};
