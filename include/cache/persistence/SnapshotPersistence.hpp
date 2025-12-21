#pragma once

#include <cache/persistence/IPersistence.hpp>
#include <cache/serialization/ISerializer.hpp>
#include <fstream>
#include <mutex>
#include <memory>
#include <string>
#include <stdexcept>
#include <filesystem>

/**
 * @brief Персистентность на основе полного снимка (snapshot)
 * @tparam K Тип ключа
 * @tparam V Тип значения
 * 
 * Стратегия:
 * - При load() — читает весь файл
 * - При изменениях — накапливает в памяти текущее состояние
 * - При flush() или saveAll() — записывает полный снимок
 * 
 * Преимущества:
 * - Простая реализация
 * - Быстрая загрузка (один read)
 * - Компактный файл (нет дубликатов)
 * 
 * Недостатки:
 * - Медленное сохранение при большом кэше
 * - Потеря данных между flush() при сбое
 * 
 * Для частых изменений лучше использовать WAL.
 */
template<typename K, typename V>
class SnapshotPersistence : public IPersistence<K, V> {
public:
    /**
     * @brief Конструктор
     * @param filePath Путь к файлу snapshot
     * @param serializer Сериализатор данных
     * @param autoFlush Автоматически сохранять при каждом изменении
     */
    SnapshotPersistence(const std::string& filePath,
                        std::shared_ptr<ISerializer<K, V>> serializer,
                        bool autoFlush = false)
        : filePath_(filePath)
        , serializer_(std::move(serializer))
        , autoFlush_(autoFlush)
        , dirty_(false)
    {
        if (!serializer_) {
            throw std::invalid_argument("Serializer cannot be null");
        }
    }
    
    /**
     * @brief Загрузить данные из файла
     * @return Вектор пар ключ-значение
     */
    std::vector<std::pair<K, V>> load() override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!std::filesystem::exists(filePath_)) {
            return {};
        }
        
        // Читаем файл
        std::ifstream file(filePath_, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Failed to open file for reading: " + filePath_);
        }
        
        // Получаем размер файла
        file.seekg(0, std::ios::end);
        size_t fileSize = file.tellg();
        file.seekg(0, std::ios::beg);
        
        if (fileSize == 0) {
            return {};
        }
        
        // Читаем содержимое
        std::vector<uint8_t> data(fileSize);
        file.read(reinterpret_cast<char*>(data.data()), fileSize);
        
        if (!file) {
            throw std::runtime_error("Failed to read file: " + filePath_);
        }
        
        // Десериализуем
        auto entries = serializer_->deserializeAll(data);
        
        // Запоминаем текущее состояние
        currentState_ = entries;
        dirty_ = false;
        
        return entries;
    }
    
    /**
     * @brief Сохранить все данные
     */
    void saveAll(const std::vector<std::pair<K, V>>& entries) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        currentState_ = entries;
        writeToFile();
        dirty_ = false;
    }
    
    /**
     * @brief Обработка добавления/обновления элемента
     */
    void onPut(const K& key, const V& value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Обновляем текущее состояние
        bool found = false;
        for (auto& entry : currentState_) {
            if (entry.first == key) {
                entry.second = value;
                found = true;
                break;
            }
        }
        
        if (!found) {
            currentState_.emplace_back(key, value);
        }
        
        dirty_ = true;
        
        if (autoFlush_) {
            writeToFile();
            dirty_ = false;
        }
    }
    
    /**
     * @brief Обработка удаления элемента
     */
    void onRemove(const K& key) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = std::find_if(currentState_.begin(), currentState_.end(),
            [&key](const std::pair<K, V>& entry) {
                return entry.first == key;
            });
        
        if (it != currentState_.end()) {
            currentState_.erase(it);
            dirty_ = true;
            
            if (autoFlush_) {
                writeToFile();
                dirty_ = false;
            }
        }
    }
    
    /**
     * @brief Обработка очистки кэша
     */
    void onClear() override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        currentState_.clear();
        dirty_ = true;
        
        if (autoFlush_) {
            writeToFile();
            dirty_ = false;
        }
    }
    
    /**
     * @brief Принудительно сбросить изменения на диск
     */
    void flush() override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (dirty_) {
            writeToFile();
            dirty_ = false;
        }
    }
    
    /**
     * @brief Проверить существование файла
     */
    bool exists() const override {
        return std::filesystem::exists(filePath_);
    }
    
    /**
     * @brief Проверить, есть ли несохранённые изменения
     */
    bool isDirty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dirty_;
    }
    
    /**
     * @brief Получить путь к файлу
     */
    const std::string& filePath() const {
        return filePath_;
    }

private:
    /**
     * @brief Записать текущее состояние в файл
     * @note Вызывать под lock!
     */
    void writeToFile() {
        // Сериализуем
        auto data = serializer_->serializeAll(currentState_);
        
        // Записываем во временный файл
        std::string tempPath = filePath_ + ".tmp";
        
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file) {
            throw std::runtime_error("Failed to open temp file for writing: " + tempPath);
        }
        
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
        file.flush();
        
        if (!file) {
            throw std::runtime_error("Failed to write to temp file: " + tempPath);
        }
        
        file.close();
        
        // Атомарно заменяем файл
        std::filesystem::rename(tempPath, filePath_);
    }

private:
    std::string filePath_;
    std::shared_ptr<ISerializer<K, V>> serializer_;
    bool autoFlush_;
    
    mutable std::mutex mutex_;
    std::vector<std::pair<K, V>> currentState_;
    bool dirty_;
};