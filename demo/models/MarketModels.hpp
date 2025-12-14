#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <stdexcept>

/**
 * @brief Модели данных для работы с биржевыми данными Tinkoff Invest API
 * 
 * Структуры соответствуют реальным данным API:
 * - FIGI (Financial Instrument Global Identifier) — глобальный идентификатор
 * - Ticker — короткое биржевое обозначение (SBER, GAZP)
 * - Цены передаются в валюте инструмента
 * 
 * @see https://tinkoff.github.io/investAPI/instruments/
 */

/**
 * @brief Справочные данные инструмента
 * 
 * Меняются редко (раз в день или реже):
 * - При корпоративных действиях (сплит, допэмиссия)
 * - При изменении торговых параметров биржей
 * 
 * Рекомендуемый TTL для кэширования: 24 часа
 */
struct InstrumentInfo {
    std::string figi;               // "BBG004730N88"
    std::string ticker;             // "SBER"
    std::string name;               // "Сбербанк"
    std::string currency;           // "RUB"
    int lot;                        // 10 — количество бумаг в 1 лоте
    double minPriceIncrement;       // 0.01 — минимальный шаг цены
    std::string classCode;          // "TQBR" — режим торгов
    
    std::chrono::system_clock::time_point lastUpdated;
};

/**
 * @brief Рыночные данные (цены)
 * 
 * Меняются постоянно во время торговой сессии.
 * 
 * Рекомендуемый TTL для кэширования: 1-5 секунд
 * (зависит от стратегии торговли)
 * 
 * @note Для высокочастотной торговли кэширование цен может быть
 *       неприемлемо — нужны данные в реальном времени через WebSocket.
 *       Наш кэш подходит для стратегий с горизонтом от нескольких секунд.
 */
struct MarketData {
    std::string figi;
    double lastPrice;           // Цена последней сделки
    double closePrice;          // Цена закрытия предыдущего дня
    double dayHigh;             // Максимум за день
    double dayLow;              // Минимум за день
    int64_t volume;             // Объём торгов (в лотах)
    
    /// Время получения данных — используется для проверки актуальности
    std::chrono::system_clock::time_point timestamp;
    
    /**
     * @brief Проверка актуальности данных
     * 
     * @param maxAge Максимальный возраст данных
     * @return true если данные ещё актуальны
     * 
     * @note Это упрощённая реализация TTL без отдельной политики.
     *       В полной версии библиотеки TTL реализуется через
     *       IExpirationPolicy (GlobalTTL, PerKeyTTL) — см. спецификацию.
     * 
     * TODO: Реализовать полноценную TTL-политику как декоратор кэша:
     *       - GlobalTTL — единый TTL для всех ключей
     *       - PerKeyTTL — индивидуальный TTL для каждого ключа
     *       Это позволит вынести логику проверки из бизнес-кода.
     */
    bool isValid(std::chrono::milliseconds maxAge) const {
        auto now = std::chrono::system_clock::now();
        auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - timestamp);
        return age <= maxAge;
    }
};

/**
 * @brief Уровень в стакане заявок
 */
struct OrderBookLevel {
    double price;
    int64_t quantity;   // В лотах
};

/**
 * @brief Стакан заявок (Order Book / DOM — Depth of Market)
 * 
 * Показывает текущие заявки на покупку и продажу.
 * Меняется очень часто (десятки раз в секунду).
 * 
 * Кэширование стакана имеет смысл только для отображения в UI,
 * для принятия торговых решений нужны актуальные данные.
 */
struct OrderBook {
    std::string figi;
    std::vector<OrderBookLevel> bids;   // Заявки на покупку (по убыванию цены)
    std::vector<OrderBookLevel> asks;   // Заявки на продажу (по возрастанию цены)
    
    std::chrono::system_clock::time_point timestamp;
    
    /**
     * @brief Лучшая цена покупки (максимальный bid)
     */
    double bestBid() const {
        return bids.empty() ? 0.0 : bids.front().price;
    }
    
    /**
     * @brief Лучшая цена продажи (минимальный ask)
     */
    double bestAsk() const {
        return asks.empty() ? 0.0 : asks.front().price;
    }
    
    /**
     * @brief Спред (разница между лучшим ask и лучшим bid)
     */
    double spread() const {
        return bestAsk() - bestBid();
    }
};

/**
 * @brief Исключение при превышении лимита запросов API
 * 
 * Tinkoff Invest API имеет ограничения:
 * - 100 запросов в минуту для большинства методов
 * - 50 запросов в минуту для отмены заявок
 * 
 * @see https://tinkoff.github.io/investAPI/limits/
 */
class RateLimitExceeded : public std::runtime_error {
public:
    explicit RateLimitExceeded(const std::string& message)
        : std::runtime_error(message) {}
};