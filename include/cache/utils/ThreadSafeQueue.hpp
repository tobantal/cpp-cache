#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <optional>

/**
 * @brief Потокобезопасная очередь с блокирующим извлечением
 * @tparam T Тип элементов очереди
 *
 * Особенности:
 * - push() — неблокирующий, O(1)
 * - pop() — блокирующий до появления элемента
 * - tryPop() — с таймаутом
 * - shutdown() — разблокирует все ожидающие потоки
 *
 * Используется для асинхронной обработки событий кэша.
 *
 * Пример:
 * @code
 *   ThreadSafeQueue<int> queue;
 *
 *   // Producer thread
 *   queue.push(42);
 *
 *   // Consumer thread
 *   int value;
 *   if (queue.tryPop(value, std::chrono::milliseconds(100))) {
 *       // обработка value
 *   }
 * @endcode
 */
template <typename T>
class ThreadSafeQueue
{
public:
    ThreadSafeQueue() = default;

    // Запрещаем копирование
    ThreadSafeQueue(const ThreadSafeQueue &) = delete;
    ThreadSafeQueue &operator=(const ThreadSafeQueue &) = delete;

    // Разрешаем перемещение
    ThreadSafeQueue(ThreadSafeQueue &&) = default;
    ThreadSafeQueue &operator=(ThreadSafeQueue &&) = default;

    /**
     * @brief Добавить элемент в очередь
     * @param item Элемент для добавления
     *
     * Неблокирующий. Уведомляет один ожидающий поток.
     * Потокобезопасен.
     */
    void push(T item)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(item));
        }
        condVar_.notify_one();
    }

    /**
     * @brief Добавить несколько элементов
     * @param items Вектор элементов
     *
     * Эффективнее чем несколько push() — один захват мьютекса.
     */
    void pushBatch(std::vector<T> items)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto &item : items)
            {
                queue_.push(std::move(item));
            }
        }
        condVar_.notify_all();
    }

    /**
     * @brief Извлечь элемент с ожиданием
     * @param item Куда записать извлечённый элемент
     * @param timeout Максимальное время ожидания
     * @return true если элемент извлечён, false если таймаут или shutdown
     *
     * Блокирует поток до:
     * - появления элемента в очереди
     * - истечения таймаута
     * - вызова shutdown()
     */
    bool tryPop(T &item, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        bool hasData = condVar_.wait_for(lock, timeout, [this]
                                         { return !queue_.empty() || shutdown_; });

        if (!hasData || (shutdown_ && queue_.empty()))
        {
            return false;
        }

        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    /**
     * @brief Извлечь элемент без ожидания
     * @param item Куда записать извлечённый элемент
     * @return true если элемент извлечён, false если очередь пуста
     */
    bool tryPopImmediate(T &item)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (queue_.empty())
        {
            return false;
        }

        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    /**
     * @brief Извлечь элемент с бесконечным ожиданием
     * @param item Куда записать извлечённый элемент
     * @return true если элемент извлечён, false если shutdown
     *
     * Блокирует до появления элемента или shutdown.
     */
    bool pop(T &item)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        condVar_.wait(lock, [this]
                      { return !queue_.empty() || shutdown_; });

        if (shutdown_ && queue_.empty())
        {
            return false;
        }

        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    /**
     * @brief Извлечь элемент (возвращает optional)
     * @param timeout Максимальное время ожидания
     * @return Элемент или nullopt если таймаут/shutdown
     */
    std::optional<T> tryPop(std::chrono::milliseconds timeout)
    {
        T item;
        if (tryPop(item, timeout))
        {
            return item;
        }
        return std::nullopt;
    }

    /**
     * @brief Остановить очередь
     *
     * Разблокирует все потоки, ожидающие в pop()/tryPop().
     * После shutdown() очередь продолжает работать:
     * - push() работает
     * - pop() возвращает оставшиеся элементы, потом false
     */
    void shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shutdown_ = true;
        }
        condVar_.notify_all();
    }

    /**
     * @brief Проверить, пуста ли очередь
     * @return true если очередь пуста
     *
     * Внимание: результат может устареть сразу после возврата.
     * Использовать только для диагностики.
     */
    bool empty() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    /**
     * @brief Получить размер очереди
     * @return Текущий размер
     *
     * Внимание: результат может устареть сразу после возврата.
     * Использовать только для диагностики.
     */
    size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    /**
     * @brief Проверить, была ли вызвана shutdown()
     */
    bool isShutdown() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return shutdown_;
    }

    /**
     * @brief Очистить очередь
     */
    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::queue<T> empty;
        std::swap(queue_, empty);
    }

private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable condVar_;
    bool shutdown_ = false;
};