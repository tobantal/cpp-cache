#pragma once

#include "ICacheListener.hpp"
#include "../persistence/IPersistence.hpp"
#include <memory>

/**
 * @brief Слушатель для персистентности данных кэша
 * @tparam K Тип ключа
 * @tparam V Тип значения
 * 
 * Связывает события кэша со стратегией персистентности.
 * 
 * Использование:
 * @code
 *   auto serializer = std::make_shared<BinarySerializer<K, V>>();
 *   auto persistence = std::make_shared<SnapshotPersistence<K, V>>("cache.bin", serializer);
 *   auto listener = std::make_shared<PersistenceListener<K, V>>(persistence);
 *   
 *   // Для тяжёлой персистентности — через async composite
 *   auto composite = std::make_shared<ThreadPerListenerComposite<K, V>>();
 *   composite->addListener(listener);
 *   cache.addListener(composite);
 * @endcode
 * 
 * @note Для загрузки данных используйте persistence->load() напрямую
 *       ДО добавления слушателя к кэшу.
 */
template<typename K, typename V>
class PersistenceListener : public ICacheListener<K, V> {
public:
    /**
     * @brief Конструктор
     * @param persistence Стратегия персистентности
     */
    explicit PersistenceListener(std::shared_ptr<IPersistence<K, V>> persistence)
        : persistence_(std::move(persistence))
    {
        if (!persistence_) {
            throw std::invalid_argument("Persistence cannot be null");
        }
    }
    
    /**
     * @brief Обработка попадания в кэш
     * Не требует сохранения — данные уже есть.
     */
    void onHit(const K& key) override {
        (void)key;
        // Hit не меняет данные — ничего не делаем
    }
    
    /**
     * @brief Обработка промаха
     * Не требует сохранения.
     */
    void onMiss(const K& key) override {
        (void)key;
        // Miss не меняет данные — ничего не делаем
    }
    
    /**
     * @brief Обработка вставки нового элемента
     */
    void onInsert(const K& key, const V& value) override {
        persistence_->onPut(key, value);
    }
    
    /**
     * @brief Обработка обновления существующего элемента
     */
    void onUpdate(const K& key, const V& oldValue, const V& newValue) override {
        (void)oldValue;
        persistence_->onPut(key, newValue);
    }
    
    /**
     * @brief Обработка вытеснения элемента
     * 
     * @note Вытесненный элемент удаляется из кэша,
     *       поэтому удаляем его из persistence.
     */
    void onEvict(const K& key, const V& value) override {
        (void)value;
        persistence_->onRemove(key);
    }
    
    /**
     * @brief Обработка явного удаления элемента
     */
    void onRemove(const K& key) override {
        persistence_->onRemove(key);
    }
    
    /**
     * @brief Обработка очистки кэша
     */
    void onClear(size_t count) override {
        (void)count;
        persistence_->onClear();
    }
    
    /**
     * @brief Принудительно сбросить изменения
     * 
     * Полезно вызывать перед завершением программы.
     */
    void flush() {
        persistence_->flush();
    }
    
    /**
     * @brief Получить доступ к persistence для загрузки данных
     */
    std::shared_ptr<IPersistence<K, V>> persistence() const {
        return persistence_;
    }

private:
    std::shared_ptr<IPersistence<K, V>> persistence_;
};