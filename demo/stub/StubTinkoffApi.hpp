#pragma once

#include "../models/MarketModels.hpp"
#include <unordered_map>
#include <random>
#include <thread>
#include <chrono>
#include <stdexcept>

/**
 * @brief Заглушка Tinkoff Invest API для демонстрации
 * 
 * Имитирует поведение реального API:
 * - Rate limiting (100 запросов в минуту)
 * - Сетевые задержки (50-150 мс)
 * - Возвращает правдоподобные данные для захардкоженных инструментов
 * 
 * Захардкоженные инструменты:
 * - SBER (Сбербанк) — ~300 руб
 * - GAZP (Газпром) — ~150 руб  
 * - LKOH (Лукойл) — ~7000 руб
 * 
 * Цены генерируются случайно в диапазоне ±3% от базовой цены.
 */
class StubTinkoffApi {
public:
    /**
     * @param requestsPerMinute Лимит запросов (как в реальном API)
     * @param simulateDelay Имитировать сетевые задержки
     */
    explicit StubTinkoffApi(int requestsPerMinute = 100, bool simulateDelay = true)
        : requestsPerMinute_(requestsPerMinute)
        , simulateDelay_(simulateDelay)
        , rng_(std::random_device{}())
        , minuteStart_(std::chrono::steady_clock::now())
    {
        initializeInstruments();
    }

    /**
     * @brief Получить информацию об инструменте по FIGI
     * @throws RateLimitExceeded если превышен лимит
     * @throws std::runtime_error если инструмент не найден
     */
    InstrumentInfo getInstrumentByFigi(const std::string& figi) {
        checkRateLimit();
        simulateNetworkDelay();
        
        auto it = instruments_.find(figi);
        if (it == instruments_.end()) {
            throw std::runtime_error("Instrument not found: " + figi);
        }
        
        auto info = it->second;
        info.lastUpdated = std::chrono::system_clock::now();
        return info;
    }

    /**
     * @brief Получить информацию об инструменте по тикеру
     */
    InstrumentInfo getInstrumentByTicker(const std::string& ticker) {
        checkRateLimit();
        simulateNetworkDelay();
        
        for (const auto& [figi, info] : instruments_) {
            if (info.ticker == ticker) {
                auto result = info;
                result.lastUpdated = std::chrono::system_clock::now();
                return result;
            }
        }
        throw std::runtime_error("Instrument not found: " + ticker);
    }

    /**
     * @brief Получить текущую цену
     * 
     * Генерирует случайную цену в диапазоне ±3% от базовой.
     */
    MarketData getLastPrice(const std::string& figi) {
        checkRateLimit();
        simulateNetworkDelay();
        
        auto it = basePrices_.find(figi);
        if (it == basePrices_.end()) {
            throw std::runtime_error("Instrument not found: " + figi);
        }
        
        return generateMarketData(figi, it->second);
    }

    /**
     * @brief Получить стакан заявок
     */
    OrderBook getOrderBook(const std::string& figi, int depth = 10) {
        checkRateLimit();
        simulateNetworkDelay();
        
        auto it = basePrices_.find(figi);
        if (it == basePrices_.end()) {
            throw std::runtime_error("Instrument not found: " + figi);
        }
        
        return generateOrderBook(figi, it->second, depth);
    }

    // ==================== Статистика для демо ====================

    int getTotalRequests() const { return totalRequests_; }
    int getRateLimitHits() const { return rateLimitHits_; }
    
    void resetStats() {
        totalRequests_ = 0;
        rateLimitHits_ = 0;
    }

    /**
     * @brief Список доступных FIGI для демо
     */
    std::vector<std::string> getAvailableFigis() const {
        std::vector<std::string> result;
        for (const auto& [figi, _] : instruments_) {
            result.push_back(figi);
        }
        return result;
    }

private:
    int requestsPerMinute_;
    bool simulateDelay_;
    std::mt19937 rng_;
    
    // Rate limiting
    std::chrono::steady_clock::time_point minuteStart_;
    int requestsInCurrentMinute_ = 0;
    
    // Статистика
    int totalRequests_ = 0;
    int rateLimitHits_ = 0;
    
    // Данные инструментов
    std::unordered_map<std::string, InstrumentInfo> instruments_;
    std::unordered_map<std::string, double> basePrices_;

    /**
     * @brief Инициализация захардкоженных инструментов
     */
    void initializeInstruments() {
        // Сбербанк
        instruments_["BBG004730N88"] = {
            "BBG004730N88",     // figi
            "SBER",             // ticker
            "Сбербанк",         // name
            "RUB",              // currency
            10,                 // lot
            0.01,               // minPriceIncrement
            "TQBR",             // classCode
            {}                  // lastUpdated
        };
        basePrices_["BBG004730N88"] = 300.0;
        
        // Газпром
        instruments_["BBG004730RP0"] = {
            "BBG004730RP0",
            "GAZP",
            "Газпром",
            "RUB",
            10,
            0.01,
            "TQBR",
            {}
        };
        basePrices_["BBG004730RP0"] = 150.0;
        
        // Лукойл
        instruments_["BBG004731032"] = {
            "BBG004731032",
            "LKOH",
            "Лукойл",
            "RUB",
            1,
            0.5,
            "TQBR",
            {}
        };
        basePrices_["BBG004731032"] = 7000.0;
    }

    /**
     * @brief Проверка rate limit
     * 
     * Сбрасывает счётчик каждую минуту.
     * При превышении лимита выбрасывает исключение.
     */
    void checkRateLimit() {
        ++totalRequests_;
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - minuteStart_);
        
        // Сброс счётчика каждую минуту
        if (elapsed.count() >= 60) {
            minuteStart_ = now;
            requestsInCurrentMinute_ = 0;
        }
        
        ++requestsInCurrentMinute_;
        
        if (requestsInCurrentMinute_ > requestsPerMinute_) {
            ++rateLimitHits_;
            throw RateLimitExceeded(
                "Rate limit exceeded: " + std::to_string(requestsPerMinute_) + 
                " requests per minute"
            );
        }
    }

    /**
     * @brief Имитация сетевой задержки (50-150 мс)
     */
    void simulateNetworkDelay() {
        if (!simulateDelay_) return;
        
        std::uniform_int_distribution<int> dist(50, 150);
        std::this_thread::sleep_for(std::chrono::milliseconds(dist(rng_)));
    }

    /**
     * @brief Генерация рыночных данных с ценой ±3% от базовой
     */
    MarketData generateMarketData(const std::string& figi, double basePrice) {
        // Случайное отклонение ±3%
        std::uniform_real_distribution<double> priceDist(-0.03, 0.03);
        double deviation = priceDist(rng_);
        double currentPrice = basePrice * (1.0 + deviation);
        
        // Округляем до minPriceIncrement
        double increment = instruments_[figi].minPriceIncrement;
        currentPrice = std::round(currentPrice / increment) * increment;
        
        // High/Low немного шире текущей цены
        double dayHigh = basePrice * 1.02;
        double dayLow = basePrice * 0.98;
        
        // Случайный объём
        std::uniform_int_distribution<int64_t> volumeDist(100000, 5000000);
        
        return MarketData{
            figi,
            currentPrice,
            basePrice,          // closePrice = базовая цена
            dayHigh,
            dayLow,
            volumeDist(rng_),
            std::chrono::system_clock::now()
        };
    }

    /**
     * @brief Генерация стакана заявок
     */
    OrderBook generateOrderBook(const std::string& figi, double basePrice, int depth) {
        OrderBook book;
        book.figi = figi;
        book.timestamp = std::chrono::system_clock::now();
        
        double increment = instruments_[figi].minPriceIncrement;
        std::uniform_int_distribution<int64_t> qtyDist(10, 1000);
        
        // Генерируем текущую цену
        std::uniform_real_distribution<double> priceDist(-0.03, 0.03);
        double midPrice = basePrice * (1.0 + priceDist(rng_));
        midPrice = std::round(midPrice / increment) * increment;
        
        // Bids (заявки на покупку) — ниже midPrice
        for (int i = 0; i < depth; ++i) {
            double price = midPrice - (i + 1) * increment;
            book.bids.push_back({price, qtyDist(rng_)});
        }
        
        // Asks (заявки на продажу) — выше midPrice
        for (int i = 0; i < depth; ++i) {
            double price = midPrice + (i + 1) * increment;
            book.asks.push_back({price, qtyDist(rng_)});
        }
        
        return book;
    }
};