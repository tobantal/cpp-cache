#pragma once

#include "IEvictionPolicy.hpp"
#include <list>
#include <unordered_map>
#include <stdexcept>
#include <cstdint>

/**
 * @brief Политика вытеснения LFU (Least Frequently Used)
 * @tparam K Тип ключа
 * 
 * Вытесняет элемент, к которому было меньше всего обращений.
 * При равной частоте — вытесняет наименее недавно использованный (LRU tie-breaker).
 * 
 * Структуры данных:
 * - frequencyMap_: Map<K, uint32_t> — ключ → частота обращений
 * - frequencyLists_: Map<uint32_t, List<K>> — частота → список ключей
 *   Внутри каждого списка: front() = MRU, back() = LRU
 * - keyToIterator_: Map<K, Iterator> — быстрый доступ к позиции в списке
 * - minFrequency_: минимальная частота среди всех ключей (для O(1) selectVictim)
 * 
 * Сложность операций: O(1) амортизированная
 * 
 * Пример работы:
 *   put(A), put(B), put(C)     -> freq: A=1, B=1, C=1, minFreq=1
 *   get(A), get(A)             -> freq: A=3, B=1, C=1, minFreq=1
 *   get(B)                     -> freq: A=3, B=2, C=1, minFreq=1
 *   selectVictim()             -> вернёт C (частота 1, единственный с minFreq)
 * 
 * @note При равной частоте используется LRU-порядок внутри группы.
 *       Это стандартное поведение LFU, известное как LFU-LRU.
 * 
 * @see https://en.wikipedia.org/wiki/Least_frequently_used
 */
template<typename K>
class LFUPolicy : public IEvictionPolicy<K> {
public:
    /**
     * @brief Конструктор
     * 
     * Инициализирует пустую политику.
     * minFrequency_ = 0 означает, что политика пуста.
     */
    LFUPolicy() : minFrequency_(0) {}

    /**
     * @brief Обработка доступа к существующему ключу (get или update)
     * 
     * Увеличивает частоту обращений к ключу на 1.
     * Перемещает ключ из списка старой частоты в список новой частоты.
     * 
     * Алгоритм:
     * 1. Получаем текущую частоту ключа
     * 2. Удаляем ключ из списка текущей частоты
     * 3. Если список стал пустым и это была минимальная частота — увеличиваем minFrequency_
     * 4. Увеличиваем частоту ключа
     * 5. Добавляем ключ в начало списка новой частоты (делаем MRU)
     * 
     * @param key Ключ, к которому произошёл доступ
     * 
     * @note Если ключ не существует, метод ничего не делает.
     *       Это безопасное поведение для случая вызова с несуществующим ключом.
     */
    void onAccess(const K& key) override {
        // Проверяем, существует ли ключ
        auto freqIt = frequencyMap_.find(key);
        if (freqIt == frequencyMap_.end()) {
            // Ключ не найден — ничего не делаем
            // Это может произойти при вызове onAccess для несуществующего ключа
            return;
        }

        // Получаем текущую и новую частоту
        uint32_t oldFreq = freqIt->second;
        uint32_t newFreq = oldFreq + 1;

        // Удаляем ключ из списка старой частоты
        removeFromFrequencyList(key, oldFreq);

        // Обновляем минимальную частоту, если нужно
        // Если список старой частоты стал пустым И это была минимальная частота
        if (frequencyLists_.find(oldFreq) == frequencyLists_.end() && 
            minFrequency_ == oldFreq) {
            // Новая минимальная частота = oldFreq + 1
            // Это корректно, потому что мы только что переместили ключ
            // из списка минимальной частоты в список с частотой +1
            minFrequency_ = newFreq;
        }

        // Обновляем частоту ключа
        freqIt->second = newFreq;

        // Добавляем ключ в список новой частоты (в начало — MRU)
        addToFrequencyList(key, newFreq);
    }

    /**
     * @brief Обработка вставки нового ключа
     * 
     * Новый ключ всегда получает частоту 1.
     * Минимальная частота сбрасывается на 1.
     * 
     * @param key Новый ключ
     * 
     * @note Вызывается только для НОВЫХ ключей.
     *       Для обновления существующих используется onAccess.
     */
    void onInsert(const K& key) override {
        // Начальная частота для нового ключа всегда 1
        const uint32_t initialFrequency = 1;

        // Записываем частоту ключа
        frequencyMap_[key] = initialFrequency;

        // Добавляем ключ в список частоты 1 (в начало — MRU)
        addToFrequencyList(key, initialFrequency);

        // Минимальная частота теперь точно 1
        // (новый элемент имеет частоту 1, меньше быть не может)
        minFrequency_ = initialFrequency;
    }

    /**
     * @brief Обработка удаления ключа
     * 
     * Удаляет ключ из всех внутренних структур.
     * 
     * @param key Удаляемый ключ
     * 
     * @note Если ключ не существует, метод ничего не делает.
     * @note minFrequency_ может стать некорректным после удаления,
     *       но это исправится при следующем onInsert.
     *       Для selectVictim это не проблема — мы проверяем наличие списка.
     */
    void onRemove(const K& key) override {
        // Проверяем, существует ли ключ
        auto freqIt = frequencyMap_.find(key);
        if (freqIt == frequencyMap_.end()) {
            // Ключ не найден — ничего не делаем
            return;
        }

        // Получаем частоту ключа
        uint32_t freq = freqIt->second;

        // Удаляем из списка частоты
        removeFromFrequencyList(key, freq);

        // Удаляем из карты частот
        frequencyMap_.erase(freqIt);

        // Примечание: minFrequency_ может стать некорректным
        // (указывать на пустой или несуществующий список).
        // Это нормально — при следующем onInsert он сбросится на 1,
        // а в selectVictim мы найдём реальный минимум при необходимости.
    }

    /**
     * @brief Выбор жертвы для вытеснения
     * 
     * Возвращает ключ с минимальной частотой обращений.
     * При равной частоте — наименее недавно использованный (LRU).
     * 
     * Алгоритм:
     * 1. Проверяем, что политика не пуста
     * 2. Находим список с минимальной частотой
     * 3. Возвращаем последний элемент списка (LRU среди равных по частоте)
     * 
     * @return Ключ элемента для вытеснения
     * @throws std::logic_error если политика пуста
     * 
     * @note Метод НЕ удаляет элемент — это делает Cache после получения значения.
     */
    K selectVictim() override {
        if (empty()) {
            throw std::logic_error("Cannot select victim from empty LFU policy");
        }

        // Находим реальную минимальную частоту
        // (minFrequency_ может быть некорректным после удалений)
        ensureValidMinFrequency();

        // Получаем список ключей с минимальной частотой
        auto& minFreqList = frequencyLists_.at(minFrequency_);

        // Возвращаем LRU элемент из этого списка (последний)
        // Не удаляем его — это сделает Cache::evict() через onRemove()
        return minFreqList.back();
    }

    /**
     * @brief Проверка, пуста ли политика
     * @return true если нет отслеживаемых ключей
     */
    bool empty() const override {
        return frequencyMap_.empty();
    }

    /**
     * @brief Очистка всех данных политики
     * 
     * Сбрасывает все структуры в начальное состояние.
     */
    void clear() override {
        frequencyMap_.clear();
        frequencyLists_.clear();
        keyToIterator_.clear();
        minFrequency_ = 0;
    }

    // ==================== Методы для отладки и тестирования ====================

    /**
     * @brief Получить частоту ключа (для тестирования)
     * @param key Ключ
     * @return Частота или 0 если ключ не найден
     */
    uint32_t getFrequency(const K& key) const {
        auto it = frequencyMap_.find(key);
        return (it != frequencyMap_.end()) ? it->second : 0;
    }

    /**
     * @brief Получить текущую минимальную частоту (для тестирования)
     * @return Минимальная частота
     */
    uint32_t getMinFrequency() const {
        return minFrequency_;
    }

private:
    /**
     * @brief Добавить ключ в список заданной частоты
     * 
     * Добавляет в начало списка (делает ключ MRU внутри группы частоты).
     * Сохраняет итератор для быстрого доступа.
     * 
     * @param key Ключ
     * @param frequency Частота
     */
    void addToFrequencyList(const K& key, uint32_t frequency) {
        // Добавляем в начало списка (MRU позиция)
        frequencyLists_[frequency].push_front(key);
        
        // Сохраняем итератор для O(1) удаления
        keyToIterator_[key] = frequencyLists_[frequency].begin();
    }

    /**
     * @brief Удалить ключ из списка заданной частоты
     * 
     * Удаляет ключ из списка и очищает пустые списки.
     * 
     * @param key Ключ
     * @param frequency Частота
     */
    void removeFromFrequencyList(const K& key, uint32_t frequency) {
        // Находим итератор на позицию ключа
        auto iterIt = keyToIterator_.find(key);
        if (iterIt == keyToIterator_.end()) {
            return;  // Ключ не найден в итераторах
        }

        // Находим список данной частоты
        auto listIt = frequencyLists_.find(frequency);
        if (listIt == frequencyLists_.end()) {
            return;  // Список не найден
        }

        // Удаляем из списка по итератору — O(1)
        listIt->second.erase(iterIt->second);

        // Удаляем из карты итераторов
        keyToIterator_.erase(iterIt);

        // Если список стал пустым — удаляем его
        if (listIt->second.empty()) {
            frequencyLists_.erase(listIt);
        }
    }

    /**
     * @brief Убедиться, что minFrequency_ указывает на существующий непустой список
     * 
     * После удалений minFrequency_ может указывать на пустой/удалённый список.
     * Этот метод находит реальную минимальную частоту.
     * 
     * @note Вызывается только из selectVictim(), когда политика не пуста.
     */
    void ensureValidMinFrequency() {
        // Если текущая минимальная частота валидна — ничего не делаем
        auto it = frequencyLists_.find(minFrequency_);
        if (it != frequencyLists_.end() && !it->second.empty()) {
            return;
        }

        // Ищем минимальную частоту среди существующих списков
        // Это O(n) в худшем случае, но происходит редко — только после
        // удаления всех элементов с минимальной частотой
        minFrequency_ = frequencyLists_.begin()->first;
        for (const auto& pair : frequencyLists_) {
            if (pair.first < minFrequency_) {
                minFrequency_ = pair.first;
            }
        }
    }

private:
    /// Карта: ключ → частота обращений
    std::unordered_map<K, uint32_t> frequencyMap_;

    /// Карта: частота → список ключей с этой частотой
    /// Внутри списка: front() = MRU, back() = LRU
    std::unordered_map<uint32_t, std::list<K>> frequencyLists_;

    /// Карта: ключ → итератор на позицию в списке (для O(1) удаления)
    std::unordered_map<K, typename std::list<K>::iterator> keyToIterator_;

    /// Минимальная частота среди всех ключей
    /// Используется для O(1) selectVictim в большинстве случаев
    uint32_t minFrequency_;
};