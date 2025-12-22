#pragma once

#include <vector>
#include <string>

/**
 * @brief Базовый интерфейс для workload'ов (паттернов доступа)
 * 
 * Workload генерирует последовательность ключей для операций
 * с определённым распределением (Uniform, Zipf, Temporal и т.д.)
 * 
 * @tparam K Тип ключа
 */
template<typename K>
class Workload {
public:
    virtual ~Workload() = default;
    
    /**
     * @brief Генерирует последовательность ключей
     * @return вектор из num_operations ключей
     */
    virtual std::vector<K> generate() = 0;
    
    /**
     * @brief Название workload'а
     * @return например: "uniform", "zipf", "temporal"
     */
    virtual std::string name() const = 0;
    
    /**
     * @brief Описание workload'а
     * @return подробное описание для вывода
     */
    virtual std::string description() const = 0;
    
    /**
     * @brief Параметры workload'а
     * @return строка с параметрами (например: "s=1.0" для Zipf)
     */
    virtual std::string parameters() const {
        return "";
    }
};
