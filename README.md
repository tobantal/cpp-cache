# 🚀 Advanced C++ Cache Library

Высокопроизводительная header-only библиотека кэширования на C++17 с поддержкой множественных политик вытеснения, TTL, многопоточности и event listener'ов.

**📜 Лицензия:** MIT  
**👤 Автор:** Тоболкин Антон  
**📌 Статус:** v1.0 Release  

---

## ✨ Основные возможности

### 🎯 Политики вытеснения (Eviction Policies)
- **LRU (Least Recently Used)** — вытесняет давно не использовавшиеся элементы
- **LFU (Least Frequently Used)** — вытесняет редко используемые элементы
- Выбор зависит от паттерна доступа вашего приложения

### ⏱️ Управление временем жизни (Time-To-Live)
- **GlobalTTL** — единый TTL для всех элементов
- **PerKeyTTL** — индивидуальный TTL для каждого ключа
- **NoExpiration** — отключение автоматического удаления
- Автоматическое удаление просроченных ключей при доступе

### 🔒 Многопоточность
- **ThreadSafeCache** — wrapper с std::mutex для синхронизации
- **ShardedCache** — распределённый кэш для снижения конкуренции за блокировку
- Полная потокобезопасность всех операций

### 📡 Event Listener'ы (Observer Pattern)
- **StatsListener** — сбор статистики (hits, misses, evictions)
- **LoggingListener** — логирование всех операций
- **PersistenceListener** — асинхронное сохранение на диск
- **ThreadPerListenerComposite** — асинхронное выполнение listener'ов в отдельных потоках

### 💾 Сохранение и восстановление
- **BinarySerializer** — бинарная сериализация с минимальными накладными расходами
- **SnapshotPersistence** — snapshot-based persistence для защиты от потери данных

---

## 🚀 Быстрый старт

### 📥 Установка

Скопируйте директорию `include/cache` в ваш проект:

```bash
git clone https://github.com/tobantal/cpp-cache.git
cp -r cpp-cache/include/cache /path/to/your/project/include/
```

Добавьте в CMakeLists.txt:

```cmake
target_include_directories(your_target PRIVATE /path/to/include)
```

**Требования:**
- C++17 и выше
- GCC 7+, Clang 5+, MSVC 2017+

### 📝 Базовый пример

```cpp
#include <cache/Cache.hpp>
#include <cache/eviction/LRUPolicy.hpp>

int main() {
    // Создаём LRU кэш на 1000 элементов
    Cache<std::string, int> cache(
        1000,
        std::make_unique<LRUPolicy<std::string>>()
    );

    // Добавляем элементы
    cache.put("user:123", 456);
    cache.put("config:timeout", 30);

    // Получаем элементы
    auto value = cache.get("user:123");
    if (value.has_value()) {
        std::cout << "Found: " << value.value() << std::endl;
    }

    // Удаляем элемент
    cache.remove("user:123");

    // Очищаем кэш
    cache.clear();

    return 0;
}
```

---

## 📚 API Reference

### 🔧 Основные операции

#### `Cache<K, V>::Cache(size_t capacity, std::unique_ptr<IEvictionPolicy<K>> policy)`

Создаёт новый кэш с указанной ёмкостью и политикой вытеснения.

```cpp
Cache<int, std::string> cache(
    5000,
    std::make_unique<LFUPolicy<int>>()
);
```

#### `std::optional<V> get(const K& key)`

Получает значение из кэша. Возвращает `nullopt` если ключ не найден.

```cpp
auto result = cache.get("key");
if (result.has_value()) {
    std::string value = result.value();
}
```

#### `void put(const K& key, const V& value)`

Добавляет или обновляет элемент в кэше. Если кэш переполнен, вытесняет элемент согласно политике.

```cpp
cache.put("key", "value");
```

#### `bool remove(const K& key)`

Удаляет элемент из кэша. Возвращает `true` если элемент был найден и удалён.

```cpp
if (cache.remove("key")) {
    std::cout << "Element removed" << std::endl;
}
```

#### `void clear()`

Очищает весь кэш.

```cpp
cache.clear();
```

#### `size_t size() const`

Возвращает текущее количество элементов в кэше.

```cpp
std::cout << "Cache size: " << cache.size() << std::endl;
```

#### `size_t capacity() const`

Возвращает максимальную ёмкость кэша.

```cpp
std::cout << "Capacity: " << cache.capacity() << std::endl;
```

---

## 🎓 Продвинутые примеры

### ⏱️ С TTL (Time-To-Live)

**GlobalTTL работает в конструкторе Cache:**

```cpp
#include <cache/expiration/GlobalTTL.hpp>
#include <chrono>

// Создаём кэш с TTL 5 минут для ВСЕх элементов
Cache<std::string, int> cache(
    1000,
    std::make_unique<LRUPolicy<std::string>>(),
    std::make_unique<GlobalTTL<std::string>>(std::chrono::minutes(5))
);

cache.put("session:abc", 123);
// Будет доступно 5 минут, потом при доступе будет помечен как expired
```

**PerKeyTTL для разных TTL на разные ключи:**

```cpp
#include <cache/expiration/PerKeyTTL.hpp>

// Без дефолтного TTL
Cache<std::string, int> cache(
    1000,
    std::make_unique<LRUPolicy<std::string>>(),
    std::make_unique<PerKeyTTL<std::string>>()  // Опционально можно передать defaultTtl
);

// Разные TTL для разных операций
cache.put("short-lived", 100, std::chrono::seconds(5));    // 5 секунд
cache.put("long-lived", 200, std::chrono::hours(24));      // 24 часа
cache.put("no-ttl", 300);                                   // Нет TTL (если не задан default)
```

**Проверка времени жизни:**

```cpp
auto ttl = cache.timeToLive("key");
if (ttl.has_value()) {
    std::cout << "Seconds remaining: " 
              << std::chrono::duration_cast<std::chrono::seconds>(ttl.value()).count() 
              << std::endl;
}
```

### 📊 С listener'ами (отслеживание событий)

```cpp
#include <cache/listeners/StatsListener.hpp>
#include <cache/listeners/LoggingListener.hpp>

Cache<int, int> cache(1000, std::make_unique<LRUPolicy<int>>());

// Добавляем listener для сбора статистики
auto stats = std::make_shared<StatsListener<int, int>>();
cache.addListener(stats);

// Добавляем listener для логирования
auto logger = std::make_shared<LoggingListener<int, int>>();
cache.addListener(logger);

// Используем кэш
cache.put(1, 100);
cache.get(1);   // Hit!
cache.get(999); // Miss!

// Получаем статистику
std::cout << "Hit rate: " << (stats->hitRate() * 100) << "%" << std::endl;
std::cout << "Total hits: " << stats->hits() << std::endl;
std::cout << "Total misses: " << stats->misses() << std::endl;
std::cout << "Total evictions: " << stats->evictions() << std::endl;
```

### 🔗 С многопоточностью

```cpp
#include <cache/concurrency/ThreadSafeCache.hpp>
#include <thread>

Cache<int, int> cache(1000, std::make_unique<LRUPolicy<int>>());
ThreadSafeCache<int, int> threadSafeCache(std::move(cache));

// Теперь можно безопасно использовать из нескольких потоков
std::thread t1([&]() {
    for (int i = 0; i < 1000; ++i) {
        threadSafeCache.put(i, i * 10);
    }
});

std::thread t2([&]() {
    for (int i = 0; i < 1000; ++i) {
        auto val = threadSafeCache.get(i);
    }
});

t1.join();
t2.join();
```

### 💿 С асинхронным сохранением (Persistence)

```cpp
#include <cache/persistence/SnapshotPersistence.hpp>
#include <cache/listeners/PersistenceListener.hpp>
#include <cache/listeners/ThreadPerListenerComposite.hpp>

Cache<std::string, int> cache(10000, std::make_unique<LRUPolicy<std::string>>());

// Создаём persistence
auto persistence = std::make_shared<SnapshotPersistence<std::string, int>>(
    "./cache.bin"
);

// Создаём listener для сохранения (асинхронно)
auto persistenceListener = std::make_shared<PersistenceListener<std::string, int>>(
    persistence
);

// Оборачиваем в асинхронный composite
auto asyncComposite = std::make_shared<ThreadPerListenerComposite<std::string, int>>();
asyncComposite->addListener(persistenceListener);
cache.addListener(asyncComposite);

// Теперь каждая операция с кэшем будет асинхронно сохраняться на диск
cache.put("key1", 100);  // Вернёт мгновенно, сохранение в фоне
cache.put("key2", 200);

// Перед выходом убедитесь что все операции завершены
asyncComposite->stop();  // Ждёт завершения всех фоновых задач
```

### 🔀 Сравнение LRU vs LFU

```cpp
#include <cache/eviction/LRUPolicy.hpp>
#include <cache/eviction/LFUPolicy.hpp>

// LRU — хороший выбор для временной локальности
// Например, новостная лента, сессионное хранилище
Cache<int, int> lruCache(1000, std::make_unique<LRUPolicy<int>>());

// LFU — лучше для стабильного рабочего набора
// Например, CDN кэш, часто используемые конфиги
Cache<int, int> lfuCache(1000, std::make_unique<LFUPolicy<int>>());
```

---

## 📂 Структура проекта

```
include/cache/
├── Cache.hpp                    # Основной класс кэша
├── ICache.hpp                   # Интерфейс кэша
├── concurrency/
│   ├── ThreadSafeCache.hpp      # Wrapper с мьютексом
│   └── ShardedCache.hpp         # Распределённый кэш
├── eviction/
│   ├── IEvictionPolicy.hpp      # Интерфейс политики вытеснения
│   ├── LRUPolicy.hpp            # LRU реализация
│   └── LFUPolicy.hpp            # LFU реализация
├── expiration/
│   ├── IExpirationPolicy.hpp    # Интерфейс TTL политики
│   ├── GlobalTTL.hpp            # Глобальный TTL
│   ├── PerKeyTTL.hpp            # Индивидуальный TTL по ключам
│   └── NoExpiration.hpp         # Без истечения
├── listeners/
│   ├── ICacheListener.hpp       # Интерфейс listener'а
│   ├── StatsListener.hpp        # Сбор статистики
│   ├── LoggingListener.hpp      # Логирование
│   ├── PersistenceListener.hpp  # Сохранение на диск
│   └── ThreadPerListenerComposite.hpp  # Асинхронное выполнение
├── persistence/
│   ├── IPersistence.hpp         # Интерфейс персистентности
│   └── SnapshotPersistence.hpp  # Snapshot-based сохранение
├── serialization/
│   ├── ISerializer.hpp          # Интерфейс сериализатора
│   └── BinarySerializer.hpp     # Бинарная сериализация
└── utils/
    └── ThreadSafeQueue.hpp      # Потокобезопасная очередь
```

---

## ✅ Тестирование

Проект содержит comprehensive unit tests:

```bash
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make
ctest --output-on-failure
```

**Покрытие тестами:**
- ✅ Базовые операции (put, get, remove, clear)
- ✅ Обе политики вытеснения (LRU, LFU)
- ✅ TTL функциональность (GlobalTTL, PerKeyTTL, NoExpiration)
- ✅ Многопоточность (ThreadSafeCache, ShardedCache)
- ✅ Listener'ы и события
- ✅ Persistence и сериализация
- ✅ Конкурентные сценарии

---

## 💡 Рекомендации по использованию

| Сценарий | Рекомендация |
|----------|--------------| 
| Веб-кэш, CDN | LRU + ThreadSafeCache |
| Session хранилище | LRU + ThreadSafeCache + GlobalTTL |
| Часто используемые конфиги | LFU + ThreadSafeCache |
| Высокая конкурентность (8+ потоков) | ShardedCache |
| С асинхронным сохранением | ThreadPerListenerComposite |

---

## ⚠️ Известные ограничения

- TTL проверяется только при доступе к элементу (нет фонового eviction)
- Нет встроенной поддержки FIFO, ARC и других политик (планируется v2.0)

---

## 🔒 Гарантии безопасности

### Memory Safety
- Вся библиотека использует `std::unique_ptr` и `std::shared_ptr`
- Нет утечек памяти благодаря RAII
- Потокобезопасные операции защищены мьютексом

### Exception Safety
- Strong exception guarantee для put/get/remove
- Listener'ы изолированы (исключение в одном не влияет на другие)

---

## 📌 Performance Tips

1. Используйте LRU если приложение имеет временную локальность (большинство случаев)
2. Используйте асинхронные listener'ы для I/O операций (persistence, logging)
3. Используйте ShardedCache для высокой конкурентности (8+ потоков)
4. Выбирайте ёмкость кэша согласно вашему working set size

---

## 🏭 Примеры использования в production

### 📈 Кэш цен на фондовом рынке

```cpp
struct PriceData { 
    double price; 
    std::string currency; 
};

// С TTL 10 минут для всех цен
Cache<std::string, PriceData> priceCache(
    100000,
    std::make_unique<LRUPolicy<std::string>>(),
    std::make_unique<GlobalTTL<std::string>>(std::chrono::minutes(10))
);

auto stats = std::make_shared<StatsListener<std::string, PriceData>>();
priceCache.addListener(stats);

// Usage:
PriceData price = fetchFromAPI("BBG004730N88");
priceCache.put("BBG004730N88", price);

auto cachedPrice = priceCache.get("BBG004730N88");
if (cachedPrice && cachedPrice.value().price > 0) {
    std::cout << "Price: " << cachedPrice.value().price << std::endl;
}
```

### 🌐 Session хранилище в веб-сервере

```cpp
struct Session {
    std::string userId;
    std::string token;
};

// Thread-safe кэш с TTL 24 часа
ThreadSafeCache<std::string, Session> sessionCache(
    Cache<std::string, Session>(
        10000,
        std::make_unique<LRUPolicy<std::string>>(),
        std::make_unique<GlobalTTL<std::string>>(std::chrono::hours(24))
    )
);

// Thread-safe операции из разных потоков обработки запросов
void handleRequest(const std::string& sessionId) {
    auto session = sessionCache.get(sessionId);
    if (session.has_value()) {
        std::cout << "Welcome back, " << session.value().userId << std::endl;
    }
}
```

---

## 📜 Лицензия

MIT License. See LICENSE file for details.

Copyright (c) 2025 Tobolkin Anton

---

## 📧 Поддержка

Для вопросов, Issues, и Pull Requests используйте GitHub issues.
