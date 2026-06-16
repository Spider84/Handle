# Handle

Проект для микроконтроллера N32G430G8 на базе FreeRTOS с использованием CMake для сборки.

## Клонирование проекта с submodule

Для корректного клонирования проекта со всеми зависимостями используйте следующую команду:

```bash
git clone --recursive https://github.com/Spider84/Handle.git
```

Если вы уже клонировали проект без submodule, выполните:

```bash
git submodule update --init --recursive
```

## Зависимости проекта

Проект использует следующие submodule:
- `lib/FlashDB` - FlashDB (база данных для Flash памяти)
- `lib/freeModBUS` - freeModBUS (реализация протокола Modbus)
- `lib/FreeRTOS-Kernel` - FreeRTOS Kernel
- `lib/SEGGER/RTT` - SEGGER RTT (Real Time Transfer)
- `lib/SEGGER/SystemView` - SEGGER SystemView (профилировщик)
- `lib/SFUD` - SFUD (Serial Flash Universal Driver)

## Требования к системе

Для сборки проекта необходимы:
- **CMake** версии 3.20 или выше
- **ARM GCC Toolchain** (arm-none-eabi-gcc, arm-none-eabi-g++, arm-none-eabi-objcopy, arm-none-eabi-objdump, arm-none-eabi-size)

Убедитесь, что инструменты ARM GCC добавлены в системную переменную PATH.

## Сборка проекта через CMake

### 1. Создание директории для сборки

```bash
mkdir build
cd build
```

### 2. Конфигурация проекта

#### Debug конфигурация (по умолчанию)

Debug версия предназначена для отладки и разработки. Включает:
- Оптимизация `-Og` (оптимизация для отладки)
- Отладочные символы `-g3`
- Флаг `-fno-omit-frame-pointer` для корректного стека вызовов
- Включение SEGGER RTT для вывода отладочной информации

> **⚠️ Внимание**: Debug версия требует подключения SEGGER RTT Viewer. Без подключенного RTT Viewer программа может работать некорректно или зависнуть при попытке вывода отладочной информации.

```bash
cmake ..
```

#### Release конфигурация

Release версия предназначена для продакшена. Включает:
- Оптимизация `-Os` (оптимизация по размеру)
- Отключение отладочных символов `-g0`
- Меньший размер прошивки

```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
```

#### Переключение между конфигурациями

Если вы уже сконфигурировали проект в одной конфигурации и хотите переключиться на другую, необходимо удалить директорию `build` и повторить конфигурацию:

```bash
cd ..
rm -rf build
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
```

### 3. Сборка проекта

```bash
cmake --build .
```

Или с использованием make:
```bash
make
```

### 4. Результаты сборки

После успешной сборки в директории `build` будут созданы следующие файлы:
- `Handle.elf` - исполняемый файл ELF
- `Handle.hex` - прошивка в формате Intel HEX
- `Handle.bin` - прошивка в бинарном формате
- `Handle.map` - карта памяти

## Опции сборки

### Включение SEGGER SystemView

Для включения SEGGER SystemView в Debug конфигурации:

```bash
cmake -DENABLE_SEGGER_SYSVIEW=ON ..
cmake --build .
```

## Описание проекта

- **Микроконтроллер**: N32G430G8 (Cortex-M4F)
- **Ядро**: ARM Cortex-M4 с FPU
- **ОСРВ**: FreeRTOS
- **Частота**: 128 MHz
- **Компилятор**: arm-none-eabi-gcc

## Структура проекта

- `src/` - исходные файлы проекта
- `include/` - заголовочные файлы проекта
- `lib/` - библиотеки и submodule
- `ThreadSafe/` - реализации блокировок для newlib
