#include "services/MarketDataService.hpp"
#include <iostream>
#include <iomanip>
#include <thread>
#include <vector>

/**
 * @brief Демонстрация библиотеки кэширования на примере биржевых данных
 * 
 * Сценарии:
 * 1. Экономия API-запросов — показываем как кэш снижает нагрузку
 * 2. Мульти-аккаунт торговля — общий кэш для нескольких счетов
 * 3. Разные стратегии для разных данных
 * 4. Graceful degradation при rate limit
 */

void printSeparator(const std::string& title) {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "  " << title << "\n";
    std::cout << std::string(60, '=') << "\n\n";
}

void printPrice(const std::string& ticker, const MarketData& data) {
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  " << ticker << ": " << data.lastPrice << " " 
              << "(close: " << data.closePrice << ", "
              << "high: " << data.dayHigh << ", "
              << "low: " << data.dayLow << ")\n";
}

/**
 * @brief Демо 1: Экономия API-запросов
 * 
 * Показываем разницу между работой с кэшем и без.
 * Без кэша 100 запросов = 100 обращений к API.
 * С кэшем 100 запросов = 1 обращение к API (при TTL > времени теста).
 */
void demoApiSavings() {
    printSeparator("Demo 1: API Request Savings");
    
    // Отключаем задержки для быстрого теста
    auto api = std::make_shared<StubTinkoffApi>(100, false);
    MarketDataService service(api, 100, 100, std::chrono::seconds(5));
    
    const std::string figi = "BBG004730N88";  // SBER
    const int requestCount = 50;
    
    std::cout << "Requesting price for SBER " << requestCount << " times...\n\n";
    
    for (int i = 0; i < requestCount; ++i) {
        auto price = service.getPrice(figi);
        if (i == 0) {
            std::cout << "First request (API call):\n";
            printPrice("SBER", price);
            std::cout << "\n";
        }
    }
    
    service.printStats();
    
    std::cout << "Result: " << requestCount << " price requests, but only "
              << api->getTotalRequests() << " API call(s)!\n";
    std::cout << "Cache saved " << (requestCount - api->getTotalRequests()) 
              << " API requests.\n";
}

/**
 * @brief Демо 2: Торговый робот на нескольких счетах
 * 
 * Три "счёта" (Иванов, Петров, Сидоров) торгуют одними бумагами.
 * Все используют общий кэш — экономия запросов к API.
 */
void demoMultiAccountTrading() {
    printSeparator("Demo 2: Multi-Account Trading");
    
    auto api = std::make_shared<StubTinkoffApi>(100, false);
    MarketDataService service(api, 100, 100, std::chrono::seconds(2));
    
    // Инструменты, которыми торгуем
    std::vector<std::string> figis = {
        "BBG004730N88",  // SBER
        "BBG004730RP0",  // GAZP
        "BBG004731032"   // LKOH
    };
    
    // "Счета"
    std::vector<std::string> accounts = {"Иванов", "Петров", "Сидоров"};
    
    std::cout << "Three accounts checking prices for SBER, GAZP, LKOH...\n\n";
    
    // Каждый счёт проверяет все инструменты
    for (const auto& account : accounts) {
        std::cout << "Account " << account << " checks prices:\n";
        
        for (const auto& figi : figis) {
            auto info = service.getInstrument(figi);
            auto price = service.getPrice(figi);
            printPrice(info.ticker, price);
        }
        std::cout << "\n";
    }
    
    service.printStats();
    
    std::cout << "Result: 3 accounts × 3 instruments = 9 logical requests\n";
    std::cout << "Actual API calls: " << api->getTotalRequests() << "\n";
    std::cout << "(First account fills the cache, others reuse it)\n";
}

/**
 * @brief Демо 3: TTL и устаревание данных
 * 
 * Показываем как работает TTL:
 * - Первый запрос — из API
 * - Повторные запросы в течение TTL — из кэша
 * - После истечения TTL — снова из API
 */
void demoTtlBehavior() {
    printSeparator("Demo 3: TTL Behavior");
    
    auto api = std::make_shared<StubTinkoffApi>(100, false);
    
    // Короткий TTL для демонстрации — 500 мс
    MarketDataService service(api, 100, 100, std::chrono::milliseconds(500));
    
    const std::string figi = "BBG004730N88";  // SBER
    
    std::cout << "Price TTL set to 500ms\n\n";
    
    // Первый запрос
    std::cout << "Request 1 (t=0ms):\n";
    auto price1 = service.getPrice(figi);
    printPrice("SBER", price1);
    std::cout << "  API calls: " << api->getTotalRequests() << "\n\n";
    
    // Через 200 мс — данные ещё актуальны
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::cout << "Request 2 (t=200ms, within TTL):\n";
    auto price2 = service.getPrice(figi);
    printPrice("SBER", price2);
    std::cout << "  API calls: " << api->getTotalRequests() << " (from cache)\n\n";
    
    // Через 600 мс от начала — TTL истёк
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    std::cout << "Request 3 (t=600ms, TTL expired):\n";
    auto price3 = service.getPrice(figi);
    printPrice("SBER", price3);
    std::cout << "  API calls: " << api->getTotalRequests() << " (fresh from API)\n\n";
    
    std::cout << "Notice: price changed between request 1 and 3 (±3% randomization)\n";
}

/**
 * @brief Демо 4: Graceful Degradation при Rate Limit
 * 
 * Показываем как кэш помогает пережить превышение лимита API:
 * - Пытаемся сделать много запросов
 * - При rate limit используем закэшированные данные
 */
void demoRateLimitHandling() {
    printSeparator("Demo 4: Rate Limit Handling");
    
    // Низкий лимит для демонстрации — 5 запросов в минуту
    auto api = std::make_shared<StubTinkoffApi>(5, false);
    MarketDataService service(api, 100, 100, std::chrono::seconds(60));
    
    const std::string figi = "BBG004730N88";
    
    std::cout << "API rate limit set to 5 requests per minute\n";
    std::cout << "Attempting 10 requests...\n\n";
    
    int successCount = 0;
    int rateLimitCount = 0;
    int fromCacheCount = 0;
    
    for (int i = 0; i < 10; ++i) {
        try {
            // getPrice внутри делает запрос к API если нет в кэше
            auto price = service.getPrice(figi);
            ++successCount;
            
            if (i == 0) {
                std::cout << "Request " << (i + 1) << ": ";
                printPrice("SBER", price);
            } else {
                ++fromCacheCount;
            }
        } catch (const RateLimitExceeded& e) {
            ++rateLimitCount;
            
            // Graceful degradation — используем устаревшие данные
            auto stale = service.getPriceOrStale(figi);
            if (stale.has_value()) {
                std::cout << "Request " << (i + 1) << ": Rate limited, using cached data: "
                          << stale.value().lastPrice << "\n";
            } else {
                std::cout << "Request " << (i + 1) << ": Rate limited, no cached data available\n";
            }
        }
    }
    
    std::cout << "\nResults:\n";
    std::cout << "  Successful API calls: " << api->getTotalRequests() << "\n";
    std::cout << "  Served from cache: " << fromCacheCount << "\n";
    std::cout << "  Rate limit hits: " << rateLimitCount << "\n";
    std::cout << "\nCache allowed to continue serving requests despite rate limit!\n";
}

/**
 * @brief Демо 5: Информация об инструментах
 * 
 * Показываем кэширование справочных данных.
 */
void demoInstrumentInfo() {
    printSeparator("Demo 5: Instrument Information Cache");
    
    auto api = std::make_shared<StubTinkoffApi>(100, false);
    MarketDataService service(api);
    
    std::vector<std::string> figis = {
        "BBG004730N88",  // SBER
        "BBG004730RP0",  // GAZP
        "BBG004731032"   // LKOH
    };
    
    std::cout << "Loading instrument info (first time — from API):\n\n";
    
    for (const auto& figi : figis) {
        auto info = service.getInstrument(figi);
        std::cout << "  " << info.ticker << " (" << info.name << ")\n";
        std::cout << "    FIGI: " << info.figi << "\n";
        std::cout << "    Currency: " << info.currency << "\n";
        std::cout << "    Lot: " << info.lot << "\n";
        std::cout << "    Min price increment: " << info.minPriceIncrement << "\n\n";
    }
    
    std::cout << "API calls after first load: " << api->getTotalRequests() << "\n\n";
    
    std::cout << "Loading same instruments again (from cache):\n";
    for (const auto& figi : figis) {
        auto info = service.getInstrument(figi);
        std::cout << "  " << info.ticker << " — loaded\n";
    }
    
    std::cout << "\nAPI calls after second load: " << api->getTotalRequests() 
              << " (no change — all from cache)\n";
    
    service.printStats();
}

int main() {
    std::cout << "=== Cache Library Demo: Stock Market Data ===\n";
    std::cout << "Demonstrating cache usage for Tinkoff Invest API\n";
    
    try {
        demoApiSavings();
        demoMultiAccountTrading();
        demoTtlBehavior();
        demoRateLimitHandling();
        demoInstrumentInfo();
        
        std::cout << "\n" << std::string(60, '=') << "\n";
        std::cout << "  Demo Complete!\n";
        std::cout << std::string(60, '=') << "\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}