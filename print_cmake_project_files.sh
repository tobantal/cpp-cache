#!/bin/bash

# Скрипт для вывода структуры проекта C++ с содержимым файлов .h, .hpp, .cpp
# Принимает один параметр - путь к проекту CMake

if [ "$#" -ne 1 ]; then
    echo "Использование: $0 <путь_к_проекту>"
    exit 1
fi

PROJECT_DIR="$1"

# Проверяем, что указанный путь существует и это каталог
if [ ! -d "$PROJECT_DIR" ]; then
    echo "Ошибка: '$PROJECT_DIR' не является директорией"
    exit 2
fi

cd "$PROJECT_DIR" || exit 3

# Вывод структуры проекта (список всех файлов и папок относительно PROJECT_DIR)
echo "Структура проекта:"
tree
echo

# Поиск и вывод содержимого файлов .h, .hpp, .cpp с заголовками
find . -type f \( -name "*.h" -o -name "*.hpp" -o -name "*.cpp" \) | sort | while read -r file; do
    # Убираем префикс ./ для вывода относительно проекта
    relative_path="${file#./}"
    echo "===== $relative_path ===="
    cat "$file"
    echo
done
