# Спецификация проекта: Модульная библиотека кэширования

> **Курс**: Алгоритмы и структуры данных  
> **Технологический стек**: C++17  
> **Дата**: Ноябрь 2025

---

## 1. Обзор проекта

### 1.1 Цель

Разработка высокопроизводительной модульной библиотеки кэширования на C++17 с гибкой архитектурой, позволяющей:

- Выбирать и динамически менять политики вытеснения (LRU, LFU, FIFO)
- Настраивать персистентность, TTL, многопоточность
- Интегрировать в микросервисы как готовую библиотеку

### 1.2 Ключевые характеристики

| Характеристика | Описание |
|----------------|----------|
| Тип | Header-only / статическая библиотека |
| Интеграция | CMake, pkg-config |
| API | Шаблонный C++17 |
| Расширяемость | Модульная архитектура на интерфейсах |

---

## 2. Технологический стек

### 2.1 Основные технологии

- **Язык программирования**: C++17
- **Стандартная библиотека**: STL
- **Система сборки**: CMake
- **Тестирование**: GoogleTest
- **Система контроля версий**: GitHub

### 2.2 Среда разработки

- **IDE**: Visual Studio Code
- **Операционная система**: Pop!_OS (Ubuntu)

### 2.3 Соглашения

- Методы: `camelCase` с малой буквы
- Документация: стиль Doxygen
- Разделение на `.hpp` и `.cpp` файлы
- Следование принципам SOLID и паттернам GOF

---

## 3. Архитектура

### 3.1 Паттерны проектирования

| Паттерн | Применение | Описание |
|---------|------------|----------|
| **Strategy** | Политики вытеснения | Клиент явно выбирает алгоритм (LRU/LFU/FIFO) |
| **State** | ARC (адаптивный кэш) | Объект сам переключается между режимами |
| **Decorator** | TTL, Thread-safety | Обёртки добавляют функциональность |
| **Builder** | Конфигурация кэша | Удобное создание сложных конфигураций |
| **Dependency Injection** | Все компоненты | Инъекция зависимостей через конструктор |

### 3.2 Точки вариативности (4 оси расширения)

```
┌─────────────────────────────────────────────────────────────┐
│                        Cache<K, V>                          │
├─────────────────────────────────────────────────────────────┤
│  1. IEvictionPolicy      — политика вытеснения              │
│     ├─ LRUPolicy                                            │
│     ├─ LFUPolicy                                            │
│     ├─ FIFOPolicy                                           │
│     └─ ARCPolicy (адаптивная)                               │
├─────────────────────────────────────────────────────────────┤
│  2. IPersistence         — стратегия сохранения             │
│     ├─ NoPersistence                                        │
│     ├─ SnapshotPersistence                                  │
│     └─ WALPersistence                                       │
├─────────────────────────────────────────────────────────────┤
│  3. IExpirationPolicy    — политика истечения (TTL)         │
│     ├─ NoExpiration                                         │
│     ├─ GlobalTTL                                            │
│     └─ PerKeyTTL                                            │
├─────────────────────────────────────────────────────────────┤
│  4. IConcurrencyPolicy   — политика многопоточности         │
│     ├─ SingleThreaded                                       │
│     ├─ MutexBased                                           │
│     └─ ShardedLock                                          │
└─────────────────────────────────────────────────────────────┘
```

### 3.3 Динамическая смена политики

API позволяет менять политику вытеснения в runtime:

```cpp
auto cache = Cache<std::string, int>(1000, 
    std::make_unique<LRUPolicy<std::string>>());

// Позже — переключение на LFU
cache.setEvictionPolicy(std::make_unique<LFUPolicy<std::string>>());
```

**Поведение при смене**: статистика сбрасывается (простой подход для MVP).

---

## 4. Структура проекта

```
cache-lib/
├── include/
│   └── cache/
│       ├── ICache.hpp                 # Базовый интерфейс кэша
│       ├── IEvictionPolicy.hpp        # Интерфейс политики вытеснения
│       ├── Cache.hpp                  # Основной класс
│       ├── CacheBuilder.hpp           # Builder для конфигурации
│       │
│       ├── policies/                  # Политики вытеснения
│       │   ├── LRUPolicy.hpp
│       │   ├── LFUPolicy.hpp
│       │   ├── FIFOPolicy.hpp
│       │   └── ARCPolicy.hpp
│       │
│       ├── persistence/               # Персистентность
│       │   ├── IPersistence.hpp
│       │   ├── SnapshotPersistence.hpp
│       │   └── WALPersistence.hpp
│       │
│       ├── expiration/                # TTL
│       │   ├── IExpirationPolicy.hpp
│       │   ├── GlobalTTL.hpp
│       │   └── PerKeyTTL.hpp
│       │
│       ├── concurrency/               # Многопоточность
│       │   ├── IConcurrencyPolicy.hpp
│       │   ├── MutexWrapper.hpp
│       │   └── ShardedCache.hpp
│       │
│       ├── stats/                     # Статистика
│       │   └── CacheStats.hpp
│       │
│       └── serialization/             # Сериализация
│           ├── ISerializer.hpp
│           ├── BinarySerializer.hpp
│           └── JsonSerializer.hpp
│
├── src/                               # Реализации (если не header-only)
├── tests/                             # GoogleTest unit-тесты
├── benchmarks/                        # Бенчмарки производительности
├── demo/                              # Примеры использования
├── docs/                              # Документация
└── CMakeLists.txt
```

---

## 5. Базовые интерфейсы (MVP)

### 5.1 ICache

```cpp
/**
 * @brief Базовый интерфейс кэша
 * @tparam K Тип ключа
 * @tparam V Тип значения
 */
template<typename K, typename V>
class ICache {
public:
    virtual ~ICache() = default;
    
    /**
     * @brief Получить значение по ключу
     * @param key Ключ
     * @return Значение или std::nullopt если не найдено
     */
    virtual std::optional<V> get(const K& key) = 0;
    
    /**
     * @brief Добавить или обновить значение
     * @param key Ключ
     * @param value Значение
     */
    virtual void put(const K& key, const V& value) = 0;
    
    /**
     * @brief Удалить значение по ключу
     * @param key Ключ
     * @return true если элемент был удалён
     */
    virtual bool remove(const K& key) = 0;
    
    /**
     * @brief Очистить весь кэш
     */
    virtual void clear() = 0;
    
    /**
     * @brief Получить текущий размер кэша
     */
    virtual size_t size() const = 0;
    
    /**
     * @brief Проверить наличие ключа
     */
    virtual bool contains(const K& key) const = 0;
};
```

### 5.2 IEvictionPolicy

```cpp
/**
 * @brief Интерфейс политики вытеснения
 * @tparam K Тип ключа
 */
template<typename K>
class IEvictionPolicy {
public:
    virtual ~IEvictionPolicy() = default;
    
    /**
     * @brief Уведомление о доступе к ключу (get/put существующего)
     */
    virtual void onAccess(const K& key) = 0;
    
    /**
     * @brief Уведомление о добавлении нового ключа
     */
    virtual void onInsert(const K& key) = 0;
    
    /**
     * @brief Уведомление об удалении ключа
     */
    virtual void onRemove(const K& key) = 0;
    
    /**
     * @brief Выбрать ключ для вытеснения
     * @return Ключ элемента, который следует удалить
     */
    virtual K selectVictim() = 0;
    
    /**
     * @brief Проверить, пуста ли политика
     */
    virtual bool empty() const = 0;
};
```

### 5.3 Cache (основной класс)

```cpp
/**
 * @brief Основной класс кэша с инъекцией политики вытеснения
 */
template<typename K, typename V>
class Cache : public ICache<K, V> {
public:
    /**
     * @brief Конструктор
     * @param capacity Максимальная ёмкость кэша
     * @param policy Политика вытеснения
     */
    explicit Cache(size_t capacity, 
                   std::unique_ptr<IEvictionPolicy<K>> policy);
    
    /**
     * @brief Динамическая смена политики вытеснения
     * @param policy Новая политика
     * @note Статистика предыдущей политики сбрасывается
     */
    void setEvictionPolicy(std::unique_ptr<IEvictionPolicy<K>> policy);
    
    // ICache implementation...
    
private:
    size_t capacity_;
    std::unordered_map<K, V> data_;
    std::unique_ptr<IEvictionPolicy<K>> policy_;
};
```

---

## 6. Алгоритмы и структуры данных

### 6.1 LRU (Least Recently Used)

**Структуры данных**:
- `std::unordered_map<K, Node*>` — O(1) доступ по ключу
- Двусвязный список — O(1) перемещение в начало/удаление с конца

**Сложность операций**: O(1) для всех операций

### 6.2 LFU (Least Frequently Used)

**Структуры данных**:
- `std::unordered_map<K, Node*>` — доступ по ключу
- `std::map<uint32_t, std::list<K>>` — частота → список ключей

**Сложность операций**: O(1) амортизированная

### 6.3 FIFO (First In First Out)

**Структуры данных**:
- `std::unordered_map<K, iterator>` — доступ по ключу
- `std::queue<K>` или `std::list<K>` — порядок добавления

**Сложность операций**: O(1)

### 6.4 ARC (Adaptive Replacement Cache)

**Описание**: Адаптивный алгоритм, балансирующий между LRU и LFU на основе рабочей нагрузки.

**Структуры данных**: 4 списка (T1, T2, B1, B2) + параметр адаптации p.

---

## 7. Модули и трудозатраты

### 7.1 MVP (обязательная часть)

| Компонент | Описание | Часы |
|-----------|----------|------|
| Интерфейсы | ICache, IEvictionPolicy | 2–3 |
| Cache | Основной класс с DI | 4–5 |
| LRUPolicy | Двусвязный список + hashmap | 5–7 |
| Unit-тесты | GoogleTest для MVP | 3–4 |
| Бенчмарк | Базовый бенчмарк | 2–3 |

**Итого MVP**: 16–22 часа

### 7.2 Расширения (независимые модули)

| ID | Модуль | Зависит от | Часы | Приоритет |
|----|--------|------------|------|-----------|
| A1 | LFUPolicy | MVP | 6–8 | 🟡 Should |
| A2 | FIFOPolicy | MVP | 2–3 | 🟢 Could |
| A3 | ARCPolicy | A1 | 10–12 | 🟢 Could |
| B1 | SnapshotPersistence | MVP | 4–6 | 🟡 Should |
| B2 | WALPersistence | MVP | 6–8 | 🟢 Could |
| C1 | GlobalTTL | MVP | 3–4 | 🟡 Should |
| C2 | PerKeyTTL | C1 | 4–6 | 🟢 Could |
| D1 | MutexWrapper | MVP | 3–4 | 🟡 Should |
| D2 | ShardedCache | D1 | 8–10 | 🟢 Could |
| E1 | CacheStats | MVP | 3–4 | 🟡 Should |
| F1 | BinarySerializer | MVP | 3–4 | 🟢 Could |
| F2 | JsonSerializer | MVP | 4–5 | 🟢 Could |

**Приоритеты**:
- 🔴 **Must** — обязательно (MVP)
- 🟡 **Should** — желательно
- 🟢 **Could** — при наличии времени

---

## 8. Сценарии реализации

### 8.1 Минимальный (25–32 часа)

```
MVP + A1 (LFU) + E1 (Stats) + расширенные бенчмарки
```

**Результат**: кэш с двумя политиками вытеснения, статистикой hit/miss, сравнительными бенчмарками LRU vs LFU.

### 8.2 Рекомендуемый (40–50 часов)

```
MVP + A1 + A2 + C1 (TTL) + D1 (Thread-safe) + E1 + B1 (Snapshot)
```

**Результат**: production-ready библиотека, готовая для интеграции в микросервис.

### 8.3 Полный (70–85 часов)

```
Все модули
```

**Результат**: полноценная enterprise-библиотека.

---

## 9. Бенчмарки

### 9.1 Метрики

| Метрика | Описание |
|---------|----------|
| Throughput (ops/sec) | Операций в секунду |
| Latency (p50, p99) | Задержка |
| Hit Rate | Процент попаданий |
| Memory Overhead | Накладные расходы памяти |

### 9.2 Сценарии тестирования

```cpp
class CacheBenchmark {
public:
    void benchmarkPutPerformance();      // writes/sec
    void benchmarkGetPerformance();      // reads/sec
    void benchmarkMixedWorkload();       // 80% read, 20% write
    void benchmarkEvictionOverhead();    // сравнение политик
    void benchmarkConcurrency();         // масштабируемость
    
    void exportToCSV(const std::string& path);
};
```

### 9.3 Ожидаемые результаты

| Операция | LRU | LFU | Примечание |
|----------|-----|-----|------------|
| get() | ~50M ops/sec | ~40M ops/sec | Single-threaded |
| put() | ~30M ops/sec | ~25M ops/sec | Single-threaded |
| Hit rate* | ~85% | ~90% | Zipf distribution |

*При типичном распределении запросов (Zipf)

---

## 10. Пример использования (целевой API)

### 10.1 Базовое использование

```cpp
#include <cache/Cache.hpp>
#include <cache/eviction/LRUPolicy.hpp>

int main() {
    auto cache = Cache<std::string, UserData>(
        10000,  // capacity
        std::make_unique<LRUPolicy<std::string>>()
    );
    
    cache.put("user:123", userData);
    
    if (auto user = cache.get("user:123")) {
        // использование user.value()
    }
    
    return 0;
}
```

### 10.2 Builder API (расширенная конфигурация)

```cpp
#include <cache/CacheBuilder.hpp>

auto cache = CacheBuilder<std::string, UserData>()
    .withCapacity(10000)
    .withEvictionPolicy<LFUPolicy>()
    .withTTL(std::chrono::minutes(30))
    .withPersistence<SnapshotPersistence>("./cache.bin")
    .threadSafe()
    .withStats()
    .build();
```

---

## 11. Принципы разработки

### 11.1 Open-Closed Principle

- Написанные и протестированные классы **не переписываем**
- Новая функциональность добавляется через **новые классы** и **интерфейсы**
- Изменения существующего кода — только при критической необходимости

### 11.2 Минимизация изменений

- При исправлении ошибок правим **только** необходимый функционал
- Не переписываем весь проект для каждого исправления
- Точечные, локальные изменения

### 11.3 Коммуникация

- **Не делаем лишнюю работу** — придерживаемся спецификации
- **При неясности** — спрашиваем, не фантазируем
- **Предложения по улучшению** — обсуждаем перед реализацией

---

## 12. Чек-лист готовности

### MVP
- [ ] Интерфейс ICache
- [ ] Интерфейс IEvictionPolicy
- [ ] Класс Cache с DI
- [ ] LRUPolicy (двусвязный список)
- [ ] Unit-тесты (GoogleTest)
- [ ] Базовый бенчмарк
- [ ] CMake конфигурация
- [ ] README с примерами

### Расширения (по мере реализации)
- [ ] LFUPolicy
- [ ] FIFOPolicy
- [ ] CacheStats
- [ ] GlobalTTL
- [ ] MutexWrapper
- [ ] SnapshotPersistence
- [ ] CacheBuilder

---

## 13. Риски и как снизить

| Риск | Вероятность | Влияние | Как снизить |
|------|-------------|---------|-----------|
| Сложность LFU выше ожидаемой | Средняя | Средняя | Начать с упрощённой версии |
| Проблемы с thread-safety | Высокая | Высокая | Тщательное тестирование, TSan |
| Недостаток времени | Средняя | Высокая | Приоритизация модулей |

---

## 14. Ссылки и материалы

- [LRU Cache — Wikipedia](https://en.wikipedia.org/wiki/Cache_replacement_policies#LRU)
- [LFU Cache — Wikipedia](https://en.wikipedia.org/wiki/Least_frequently_used)
- [ARC — IBM Research Paper](https://www.usenix.org/conference/fast-03/arc-self-tuning-low-overhead-replacement-cache)
- [GoogleTest Documentation](https://google.github.io/googletest/)
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/)

---

