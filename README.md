# Проектная работа: Модульная библиотека кэширования

> **Курс**: Алгоритмы и структуры данных  
> **Технологический стек**: C++17, CMake, GoogleTest 
>
> **Студент**: Тоболкин Антон

---

## 1. Описание проекта

**Цель**: разработка модульной библиотеки кэширования с гибкой архитектурой, позволяющей динамически выбирать и менять политики вытеснения данных.

**Результат**: header-only библиотека на C++17, готовая для интеграции в другие проекты.

---

## 2. Архитектура

### 2.1 Применяемые паттерны

| Паттерн | Применение |
|---------|------------|
| **Strategy** | Политики вытеснения (LRU/LFU/FIFO) — клиент выбирает алгоритм |
| **State** | ARC (адаптивный кэш) — автоматическое переключение режимов |
| **Decorator** | TTL, Thread-safety — обёртки добавляют функциональность |
| **Builder** | Удобная конфигурация кэша |
| **Dependency Injection** | Инъекция зависимостей через конструктор |

### 2.2 Иерархия классов

```
Cache<K, V>
│
├── IEvictionPolicy<K>           — политика вытеснения
│   ├── LRUPolicy                  (Least Recently Used)
│   ├── LFUPolicy                  (Least Frequently Used)
│   ├── FIFOPolicy                 (First In First Out)
│   └── ARCPolicy                  (Adaptive Replacement Cache)
│
├── IPersistence<K, V>           — стратегия сохранения
│   ├── NoPersistence
│   ├── SnapshotPersistence
│   └── WALPersistence
│
├── IExpirationPolicy<K>         — политика истечения (TTL)
│   ├── NoExpiration
│   ├── GlobalTTL
│   └── PerKeyTTL
│
└── IConcurrencyPolicy           — политика многопоточности
    ├── SingleThreaded
    ├── MutexBased
    └── ShardedLock
```

---

## 3. Алгоритмы и структуры данных

| Алгоритм | Структуры данных | Сложность |
|----------|------------------|-----------|
| **LRU** | Двусвязный список + хеш-таблица | O(1) |
| **LFU** | Хеш-таблица + map частот со списками | O(1) амортиз. |
| **FIFO** | Очередь + хеш-таблица | O(1) |
| **ARC** | 4 списка (T1, T2, B1, B2) + параметр адаптации | O(1) |

---

## 4. Объём работ

### 4.1 Обязательная часть (MVP)

| Компонент | Описание | Часы |
|-----------|----------|------|
| Интерфейсы | ICache, IEvictionPolicy | 2–3 |
| Cache | Основной класс с инъекцией зависимостей | 4–5 |
| LRUPolicy | Двусвязный список + hashmap | 5–7 |
| Unit-тесты | GoogleTest | 3–4 |
| Бенчмарк | Измерение производительности | 2–3 |

**Итого MVP**: 16–22 часа

### 4.2 Дополнительные модули

| Модуль | Зависимости | Часы |
|--------|-------------|------|
| LFUPolicy | MVP | 6–8 |
| FIFOPolicy | MVP | 2–3 |
| ARCPolicy | LFUPolicy | 10–12 |
| SnapshotPersistence | MVP | 4–6 |
| WALPersistence | MVP | 6–8 |
| GlobalTTL | MVP | 3–4 |
| PerKeyTTL | GlobalTTL | 4–6 |
| MutexWrapper | MVP | 3–4 |
| ShardedCache | MutexWrapper | 8–10 |
| CacheStats | MVP | 3–4 |

### 4.3 Планируемый сценарий

**Минимальный план (25–32 часа)**:
```
MVP + LFUPolicy + CacheStats + бенчмарки
```

Результат: кэш с двумя политиками вытеснения (LRU, LFU), статистикой hit/miss, сравнительными бенчмарками.

---

## 5. Пример использования

```cpp
#include <cache/Cache.hpp>
#include <cache/policies/LRUPolicy.hpp>

// Создание кэша с LRU политикой
auto cache = Cache<std::string, int>(
    1000,  // ёмкость
    std::make_unique<LRUPolicy<std::string>>()
);

cache.put("key1", 100);

if (auto value = cache.get("key1")) {
    std::cout << value.value() << std::endl;
}

// Динамическая смена политики на LFU
cache.setEvictionPolicy(std::make_unique<LFUPolicy<std::string>>());
```

---

## 6. Бенчмарки

| Метрика | Описание |
|---------|----------|
| Throughput | Операций в секунду (ops/sec) |
| Hit Rate | Процент попаданий в кэш |
| Сравнение политик | LRU vs LFU при разных нагрузках |

---

## 7. Структура проекта

```bash
cache-lib/
├── include/cache/          # Заголовочные файлы
│   ├── ICache.hpp
│   ├── IEvictionPolicy.hpp
│   ├── Cache.hpp
│   └── policies/
│       ├── LRUPolicy.hpp
│       └── LFUPolicy.hpp
├── src/                    # Реализации
├── tests/                  # Unit-тесты
├── benchmarks/             # Бенчмарки
├── demo/                   # Примеры
└── CMakeLists.txt
```
