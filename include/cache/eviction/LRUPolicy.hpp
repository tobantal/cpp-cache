#pragma once

#include "IEvictionPolicy.hpp"
#include <list>
#include <unordered_map>
#include <stdexcept>

/**
 * @brief Политика вытеснения LRU (Least Recently Used)
 * @tparam K Тип ключа
 * 
 * Вытесняет элемент, к которому дольше всего не было обращений.
 * 
 * Структуры данных:
 * - std::list<K> order_ — двусвязный список ключей в порядке использования
 *   front() = самый свежий (MRU), back() = самый старый (LRU)
 * - std::unordered_map<K, iterator> keyToIterator_ — маппинг ключа на позицию в списке
 * 
 * Сложность всех операций: O(1)
 * 
 * Пример работы:
 *   put(A), put(B), put(C) -> список: [C, B, A]
 *   get(A)                 -> список: [A, C, B]  (A переместился в начало)
 *   selectVictim()         -> вернёт B (последний в списке)
 */
template<typename K>
class LRUPolicy : public IEvictionPolicy<K> {
public:
    /**
     * @brief Обработка доступа к существующему ключу
     * 
     * Перемещает ключ в начало списка (делает его MRU).
     * std::list::splice перемещает узел без копирования/аллокации — O(1).
     */
    void onAccess(const K& key) override {
        auto it = keyToIterator_.find(key);
        if (it != keyToIterator_.end()) {
            // splice(куда, откуда, что) — перемещает узел внутри списка
            // Итератор it->second остаётся валидным после splice
            order_.splice(order_.begin(), order_, it->second);
        }
    }

    /**
     * @brief Обработка вставки нового ключа
     * 
     * Новый ключ — самый свежий, добавляем в начало.
     * Сохраняем итератор для быстрого доступа при onAccess/onRemove.
     */
    void onInsert(const K& key) override {
        order_.push_front(key);
        keyToIterator_[key] = order_.begin();
    }

    /**
     * @brief Обработка удаления ключа
     * 
     * Удаляем из списка и из мапы.
     * Вызывается как при явном remove(), так и после вытеснения.
     */
    void onRemove(const K& key) override {
        auto it = keyToIterator_.find(key);
        if (it != keyToIterator_.end()) {
            order_.erase(it->second);  // O(1) — удаление по итератору
            keyToIterator_.erase(it);
        }
    }

    /**
     * @brief Выбор жертвы для вытеснения
     * @return Ключ элемента, который дольше всего не использовался
     * 
     * Возвращает последний элемент списка (LRU).
     * Не удаляет его — удаление делает Cache после получения значения.
     */
    K selectVictim() override {
        if (order_.empty()) {
            throw std::logic_error("Cannot select victim from empty policy");
        }
        return order_.back();
    }

    bool empty() const override {
        return order_.empty();
    }

    void clear() override {
        order_.clear();
        keyToIterator_.clear();
    }

private:
    /// Порядок использования: front() = MRU, back() = LRU
    std::list<K> order_;
    
    /// Быстрый доступ к позиции ключа в списке для O(1) перемещения
    std::unordered_map<K, typename std::list<K>::iterator> keyToIterator_;
};