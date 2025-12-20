#pragma once

#include <cache/listeners/ICacheListener.hpp>
#include <cache/utils/ThreadSafeQueue.hpp>

#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <iostream>

/**
 * @brief Композитный слушатель: каждый слушатель в своём потоке
 * @tparam K Тип ключа
 * @tparam V Тип значения
 * 
 * Архитектура (паттерн Command):
 * - Один экземпляр регистрируется в кэше
 * - Внутри содержит несколько реальных слушателей
 * - Каждый слушатель имеет свою очередь команд и поток
 * - Методы onXxx создают лямбду и кладут в очереди
 * - Потоки независимо выполняют команды
 * 
 * Преимущества:
 * - Полная изоляция: медленный слушатель не блокирует быстрых
 * - Основной поток кэша не блокируется
 * - Существующие слушатели работают без изменений
 * - Нет промежуточных структур — команда сразу содержит вызов
 * 
 * Пример использования:
 * @code
 *   auto composite = std::make_shared<ThreadPerListenerComposite<std::string, int>>();
 *   
 *   composite->addListener(std::make_shared<StatsListener<std::string, int>>());
 *   composite->addListener(std::make_shared<LoggingListener<std::string, int>>());
 *   
 *   cache.addListener(composite);
 * @endcode
 */
template<typename K, typename V>
class ThreadPerListenerComposite : public ICacheListener<K, V> {
public:
    /// Тип команды — лямбда без аргументов
    using Command = std::function<void()>;
    
    /**
     * @brief Конструктор
     * @param drainTimeoutMs Таймаут ожидания при извлечении из очереди (мс)
     */
    explicit ThreadPerListenerComposite(size_t drainTimeoutMs = 100)
        : drainTimeout_(drainTimeoutMs)
    {}
    
    ~ThreadPerListenerComposite() {
        stop();
    }
    
    // Запрещаем копирование
    ThreadPerListenerComposite(const ThreadPerListenerComposite&) = delete;
    ThreadPerListenerComposite& operator=(const ThreadPerListenerComposite&) = delete;
    
    /**
     * @brief Добавить слушателя
     * @param listener Слушатель для добавления
     */
    void addListener(std::shared_ptr<ICacheListener<K, V>> listener) {
        if (!listener) {
            return;
        }
        
        auto entry = std::make_shared<ListenerEntry>();
        entry->listener = std::move(listener);
        entry->running = true;
        
        entry->thread = std::thread(&ThreadPerListenerComposite::workerLoop, this, entry);
        
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.push_back(entry);
    }
    
    /**
     * @brief Удалить слушателя
     */
    bool removeListener(std::shared_ptr<ICacheListener<K, V>> listener) {
        if (!listener) {
            return false;
        }
        
        std::shared_ptr<ListenerEntry> entryToRemove;
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = std::find_if(entries_.begin(), entries_.end(),
                [&listener](const auto& entry) {
                    return entry->listener == listener;
                });
            
            if (it == entries_.end()) {
                return false;
            }
            
            entryToRemove = *it;
            entries_.erase(it);
        }
        
        stopEntry(entryToRemove);
        return true;
    }
    
    /**
     * @brief Остановить все потоки и дождаться завершения
     */
    void stop() {
        std::vector<std::shared_ptr<ListenerEntry>> entriesToStop;
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            entriesToStop = std::move(entries_);
            entries_.clear();
        }
        
        for (auto& entry : entriesToStop) {
            stopEntry(entry);
        }
    }
    
    size_t listenerCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_.size();
    }
    
    size_t totalQueueSize() const {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t total = 0;
        for (const auto& entry : entries_) {
            total += entry->queue.size();
        }
        return total;
    }
    
    // ==================== ICacheListener ====================
    
    void onHit(const K& key) override {
        broadcast([key](std::shared_ptr<ICacheListener<K, V>>& listener) {
            listener->onHit(key);
        });
    }
    
    void onMiss(const K& key) override {
        broadcast([key](std::shared_ptr<ICacheListener<K, V>>& listener) {
            listener->onMiss(key);
        });
    }
    
    void onInsert(const K& key, const V& value) override {
        broadcast([key, value](std::shared_ptr<ICacheListener<K, V>>& listener) {
            listener->onInsert(key, value);
        });
    }
    
    void onUpdate(const K& key, const V& oldValue, const V& newValue) override {
        broadcast([key, oldValue, newValue](std::shared_ptr<ICacheListener<K, V>>& listener) {
            listener->onUpdate(key, oldValue, newValue);
        });
    }
    
    void onEvict(const K& key, const V& value) override {
        broadcast([key, value](std::shared_ptr<ICacheListener<K, V>>& listener) {
            listener->onEvict(key, value);
        });
    }
    
    void onRemove(const K& key) override {
        broadcast([key](std::shared_ptr<ICacheListener<K, V>>& listener) {
            listener->onRemove(key);
        });
    }
    
    void onClear(size_t count) override {
        broadcast([count](std::shared_ptr<ICacheListener<K, V>>& listener) {
            listener->onClear(count);
        });
    }

private:
    struct ListenerEntry {
        std::shared_ptr<ICacheListener<K, V>> listener;
        ThreadSafeQueue<Command> queue;
        std::thread thread;
        std::atomic<bool> running{false};
    };
    
    /**
     * @brief Разослать команду во все очереди
     */
    template<typename Action>
    void broadcast(Action&& action) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : entries_) {
            auto listener = entry->listener;
            entry->queue.push([listener, action]() mutable {
                action(listener);
            });
        }
    }
    
    void workerLoop(std::shared_ptr<ListenerEntry> entry) {
        while (entry->running) {
            Command command;
            if (entry->queue.tryPop(command, std::chrono::milliseconds(drainTimeout_))) {
                executeCommand(command);
            }
        }
        drainQueue(entry);
    }
    
    void drainQueue(std::shared_ptr<ListenerEntry>& entry) {
        Command command;
        while (entry->queue.tryPopImmediate(command)) {
            executeCommand(command);
        }
    }
    
    void executeCommand(Command& command) {
        try {
            command();
        } catch (const std::exception& e) {
            std::cerr << "[ThreadPerListenerComposite] Listener error: " 
                      << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[ThreadPerListenerComposite] Unknown listener error" 
                      << std::endl;
        }
    }
    
    void stopEntry(std::shared_ptr<ListenerEntry>& entry) {
        if (!entry) {
            return;
        }
        
        entry->running = false;
        entry->queue.shutdown();
        
        if (entry->thread.joinable()) {
            entry->thread.join();
        }
    }

private:
    std::vector<std::shared_ptr<ListenerEntry>> entries_;
    mutable std::mutex mutex_;
    size_t drainTimeout_;
};