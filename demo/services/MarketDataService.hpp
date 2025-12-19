#pragma once

#include "../models/MarketModels.hpp"
#include "../stub/StubTinkoffApi.hpp"
#include <cache/Cache.hpp>
#include <cache/policies/LRUPolicy.hpp>
#include <cache/listeners/StatsListener.hpp>
#include <memory>
#include <iostream>
#include <iomanip>

/**
 * @brief Сервис рыночных данных с кэшированием
 * 
 * Демонстрирует практическое применение кэша для работы с биржевыми данными.
 * 
 * Архитектура:
 * - instrumentCache_: кэш справочных данных (большой, долгоживущий)
 * - priceCache_: кэш цен (маленький, короткоживущий)
 * 
 * Экономия API-запросов:
 * - Без кэша: каждый запрос = обращение к API
 * - С кэшем: только первый запрос + запросы после истечения TTL
 */
class MarketDataService {
public:
    /**
     * @param api Клиент API (реальный или заглушка)
     * @param instrumentCacheSize Размер кэша инструментов
     * @param priceCacheSize Размер кэша цен
     * @param priceTtl Время жизни цены в кэше
     */
    MarketDataService(
        std::shared_ptr<StubTinkoffApi> api,
        size_t instrumentCacheSize = 1000,
        size_t priceCacheSize = 100,
        std::chrono::milliseconds priceTtl = std::chrono::milliseconds(1000)
    )
        : api_(std::move(api))
        , priceTtl_(priceTtl)
        , instrumentCache_(instrumentCacheSize, std::make_unique<LRUPolicy<std::string>>())
        , priceCache_(priceCacheSize, std::make_unique<LRUPolicy<std::string>>())
        , instrumentStats_(std::make_shared<StatsListener<std::string, InstrumentInfo>>())
        , priceStats_(std::make_shared<StatsListener<std::string, MarketData>>())
    {
        instrumentCache_.addListener(instrumentStats_);
        priceCache_.addListener(priceStats_);
    }

    /**
     * @brief Получить информацию об инструменте
     * 
     * Стратегия кэширования:
     * - Проверяем кэш
     * - Если есть — возвращаем (справочные данные редко меняются)
     * - Если нет — запрашиваем API и кэшируем
     */
    InstrumentInfo getInstrument(const std::string& figi) {
        // Пробуем получить из кэша
        auto cached = instrumentCache_.get(figi);
        if (cached.has_value()) {
            return cached.value();
        }
        
        // Кэш-промах — запрашиваем API
        auto info = api_->getInstrumentByFigi(figi);
        instrumentCache_.put(figi, info);
        return info;
    }

    /**
     * @brief Получить текущую цену
     * 
     * Стратегия кэширования с проверкой TTL:
     * - Проверяем кэш
     * - Если данные есть И не устарели — возвращаем
     * - Иначе — запрашиваем API
     * 
     * @note TTL реализован через проверку timestamp в MarketData.
     *       В полной версии библиотеки это вынесено в IExpirationPolicy.
     */
    MarketData getPrice(const std::string& figi) {
        // Пробуем получить из кэша
        auto cached = priceCache_.get(figi);
        if (cached.has_value() && cached.value().isValid(priceTtl_)) {
            return cached.value();
        }
        
        // Кэш-промах или данные устарели — запрашиваем API
        auto data = api_->getLastPrice(figi);
        priceCache_.put(figi, data);
        return data;
    }

    /**
     * @brief Обновить цену из внешнего источника (например, WebSocket)
     * 
     * Позволяет обновлять кэш без обращения к REST API.
     * В реальном приложении:
     * - REST API: первоначальная загрузка и fallback
     * - WebSocket: real-time обновления
     */
    void updatePrice(const std::string& figi, const MarketData& data) {
        priceCache_.put(figi, data);
    }

    /**
     * @brief Получить цену, даже если она устарела
     * 
     * Используется при graceful degradation:
     * - API недоступен или превышен rate limit
     * - Лучше показать устаревшую цену, чем ничего
     */
    std::optional<MarketData> getPriceOrStale(const std::string& figi) {
        return priceCache_.get(figi);
    }

    // ==================== Статистика ====================

    void printStats() const {
        std::cout << "\n=== MarketDataService Statistics ===\n\n";
        
        std::cout << "Instrument Cache:\n";
        std::cout << "  Hits:    " << instrumentStats_->hits() << "\n";
        std::cout << "  Misses:  " << instrumentStats_->misses() << "\n";
        std::cout << "  Hit Rate: " << std::fixed << std::setprecision(1)
                  << (instrumentStats_->hitRate() * 100) << "%\n\n";
        
        std::cout << "Price Cache:\n";
        std::cout << "  Hits:    " << priceStats_->hits() << "\n";
        std::cout << "  Misses:  " << priceStats_->misses() << "\n";
        std::cout << "  Hit Rate: " << std::fixed << std::setprecision(1)
                  << (priceStats_->hitRate() * 100) << "%\n\n";
        
        std::cout << "API Statistics:\n";
        std::cout << "  Total Requests:   " << api_->getTotalRequests() << "\n";
        std::cout << "  Rate Limit Hits:  " << api_->getRateLimitHits() << "\n";
    }

    void resetStats() {
        instrumentStats_->reset();
        priceStats_->reset();
        api_->resetStats();
    }

    /**
     * @brief Получить прямой доступ к API для демо
     */
    StubTinkoffApi& api() { return *api_; }

private:
    std::shared_ptr<StubTinkoffApi> api_;
    std::chrono::milliseconds priceTtl_;
    
    Cache<std::string, InstrumentInfo> instrumentCache_;
    Cache<std::string, MarketData> priceCache_;
    
    std::shared_ptr<StatsListener<std::string, InstrumentInfo>> instrumentStats_;
    std::shared_ptr<StatsListener<std::string, MarketData>> priceStats_;
};