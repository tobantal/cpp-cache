# Проектная работа: Модульная библиотека кэширования

> **Курс**: Алгоритмы и структуры данных
> **Технологический стек**: C++17, CMake, GoogleTest
>
> **Студент**: Тоболкин Антон

---

## 1. Описание проекта

**Цель**: разработка модульной библиотеки in-memory кэширования с возможностью выбора и комбинирования политик вытеснения, истечения (TTL), наблюдения (listeners) и многопоточности.

**Результат**: header-only библиотека на C++17, предназначенная для использования и экспериментов с алгоритмами кэширования.

---

## 2. Архитектура

### 2.1 Применяемые паттерны

| Паттерн                  | Применение в проекте                                       |
| ------------------------ | ---------------------------------------------------------- |
| **Strategy**             | Политики вытеснения (LRU, LFU) и политики expiration (TTL) |
| **Decorator**            | ThreadSafeCache, ShardedCache, PersistenceListener         |
| **Observer**             | Cache listeners (hit/miss/evict и др.)                     |
| **Composite**            | Объединение listeners с поддержкой sync / async выполнения |
| **Dependency Injection** | Все политики и listeners передаются через конструкторы     |

TODO: добавить.
Command - внутри ThreadPerListenerComposite используется паттерн "команда" в потокобезопасной очереди.

Добавить "строитель" после реализации Builder.

---

### 2.2 Иерархия основных интерфейсов и классов

```
Cache<K, V>
│
├── IEvictionPolicy<K>            — политика вытеснения
│   ├── LRUPolicy                 (Least Recently Used)
│   └── LFUPolicy                 (Least Frequently Used)
│
├── IExpirationPolicy<K>          — политика истечения (TTL)
│   ├── NoExpiration
│   ├── GlobalTTL
│   └── PerKeyTTL
│
├── ICacheListener<K, V>          — наблюдатель событий кэша
│   ├── StatsListener
│   ├── LoggingListener
│   └── PersistenceListener
│
├── ThreadSafeCache<K, V>         — потокобезопасная обёртка (mutex)
│
└── ShardedCache<K, V>            — кэш с шардингом по ключу
```

---

## 3. Алгоритмы и структуры данных

| Алгоритм | Используемые структуры данных                     | Сложность операций |
| -------- | ------------------------------------------------- | ------------------ |
| **LRU**  | `std::list` + `std::unordered_map`                | O(1)               |
| **LFU**  | `std::unordered_map` + таблица частот со списками | O(1) амортиз.      |

---

## 4. Реализованный функционал

### 4.1 Базовый кэш

* `Cache<K, V>` с фиксированной ёмкостью
* Операции: `put`, `get`, `remove`, `contains`, `clear`
* `get` возвращает `std::optional<V>`

---

### 4.2 Политики вытеснения

* LRU — вытеснение по принципу «давно не использовался»
* LFU — вытеснение по частоте обращений с LRU tie-breaker

---

### 4.3 TTL / Expiration

* `NoExpiration`
* `GlobalTTL`
* `PerKeyTTL`

Истёкшие элементы удаляются лениво при обращении к ключу.

---

### 4.4 Listeners

Поддерживаемые события:

* hit / miss
* insert / update
* evict
* remove
* clear
* expire

Listeners могут выполняться:

* синхронно
* асинхронно (отдельный поток на listener)

---

### 4.5 Persistence

* Snapshot persistence
* Полная сериализация состояния кэша
* Бинарный формат

---

### 4.6 Concurrency

* `ThreadSafeCache` — глобальный mutex
* `ShardedCache` — несколько shard'ов с независимыми mutex

---

## 5. Бенчмарки

Реализованы бенчмарки для следующих сценариев:

| Сценарий          | Описание                          |
| ----------------- | --------------------------------- |
| LRU vs LFU        | Сравнение политик вытеснения      |
| Put / Get         | Пропускная способность операций   |
| Uniform access    | Равномерное распределение ключей  |
| Zipf distribution | Нагрузка с перекосом популярности |
| Temporal locality | Временная локальность             |
| Listener overhead | Sync vs Async listeners           |
| Concurrency       | ThreadSafe vs Sharded             |

---

## 6. Сравнение с существующими библиотеками

### Используемые библиотеки для сравнения

* **facebook/CacheLib**
* **tsl::lru_cache**
* **gkiryaziev/lru_cache**

### Feature comparison

| Feature            | This project | CacheLib | tsl::lru_cache | gkiryaziev/lru_cache |
| ------------------ | ------------ | -------- | -------------- | -------------------- |
| LRU                | yes          | yes      | yes            | yes                  |
| LFU                | yes          | limited  | no             | no                   |
| TTL                | yes          | yes      | no             | no                   |
| Pluggable eviction | yes          | no       | no             | no                   |
| Listeners / hooks  | yes          | yes      | no             | no                   |
| Async hooks        | yes          | yes      | no             | no                   |
| Persistence        | snapshot     | yes      | no             | no                   |
| Thread-safe        | wrappers     | yes      | no             | no                   |
| Sharded cache      | yes          | yes      | no             | no                   |
| Header-only        | yes          | no       | yes            | yes                  |

---

## 7. Структура проекта

```text
include/cache/
  Cache.hpp
  IEvictionPolicy.hpp
  eviction/
    LRUPolicy.hpp
    LFUPolicy.hpp
  expiration/
  listeners/
  concurrency/
  persistence/

benchmarks/
tests/
demo/
```

---

## 8. TODO / Открытые вопросы

