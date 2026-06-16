cmake_minimum_required(VERSION 3.20)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# ==============================================================================
# 1. НАСТРОЙКА КРОСС-КОМПИЛЯЦИИ (Через системный PATH)
# ==============================================================================
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR ARM)

# CMake сам найдет эти файлы в PATH
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)

# А вот для утилит (objcopy, size) лучше явно вызвать поиск CMake,
# так как сам по себе он их автоматически не ищет
find_program(CMAKE_OBJCOPY arm-none-eabi-objcopy REQUIRED)
find_program(CMAKE_OBJDUMP arm-none-eabi-objdump REQUIRED)
find_program(CMAKE_SIZE    arm-none-eabi-size    REQUIRED)

# ==============================================================================
# 2. ОБЪЯВЛЕНИЕ ПРОЕКТА
# ==============================================================================
project(Handle C ASM)

# Тип сборки по умолчанию
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Тип сборки" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release")
endif()

# Включение SEGGER SYSVIEW в DEBUG конфигурации
option(ENABLE_SEGGER_SYSVIEW "Включить SEGGER SYSVIEW в DEBUG конфигурации" OFF)

set(CPU_FLAGS
    -mcpu=cortex-m4
    -mthumb
    -mfpu=fpv4-sp-d16
    -mfloat-abi=hard
)

set(CMSIS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/CMSIS)
set(STD_PERIPH_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/n32g430_std_periph_driver)
set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/N32G430G8.ld)

add_library(m4_architecture INTERFACE)
target_compile_options(m4_architecture INTERFACE
    ${CPU_FLAGS}
    -specs=nano.specs
    $<$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>: -ffunction-sections -fdata-sections -Wall -fstack-usage>
    $<$<COMPILE_LANGUAGE:CXX>: -fno-exceptions -fno-rtti -fno-use-cxa-atexit>
    $<$<COMPILE_LANGUAGE:ASM>: -x assembler-with-cpp>
    $<$<OR:$<CONFIG:Debug>,$<BOOL:${BUILD_BLUEPILL}>>: -Og -g3 -fno-omit-frame-pointer>
    $<$<CONFIG:Release>: -Os -g0>
)

target_link_options(m4_architecture INTERFACE
    ${CPU_FLAGS}
    -specs=nano.specs
    -Wl,--print-memory-usage
    $<$<CONFIG:Debug>:-Wl,--undefined=xQueueRegistry>
)

target_compile_definitions(m4_architecture INTERFACE
  STM32_CMAKE
  MB_TX_COMPLETE_EMPTY
  SYSCLK_FREQ=128000000
  SYSCLK_SRC=SYSCLK_USE_HSE_PLL
  N32G430
  USE_STDPERIPH_DRIVER
  $<$<CONFIG:Debug>:DEBUG>
  # $<$<CONFIG:Debug>:USE_FULL_ASSERT>
  STM32_THREAD_SAFE_STRATEGY=5

  configUSE_NEWLIB_REENTRANT=1
  $<$<NOT:$<AND:$<CONFIG:Debug>,$<BOOL:${ENABLE_SEGGER_SYSVIEW}>>>:SEGGER_SYSVIEW_DISABLE>
)

target_include_directories(m4_architecture INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMSIS_DIR}
  ${CMSIS_DIR}/device
  ${CMSIS_DIR}/core
  ${STD_PERIPH_DIR}/inc
  ${CMAKE_CURRENT_SOURCE_DIR}/lib/SEGGER/RTT/RTT
  $<$<AND:$<CONFIG:Debug>,$<BOOL:${ENABLE_SEGGER_SYSVIEW}>>:${CMAKE_CURRENT_SOURCE_DIR}/lib/SEGGER/SystemView/SEGGER>
  $<$<AND:$<CONFIG:Debug>,$<BOOL:${ENABLE_SEGGER_SYSVIEW}>>:${CMAKE_CURRENT_SOURCE_DIR}/lib/SEGGER/SystemView/SYSVIEW>
  $<$<AND:$<CONFIG:Debug>,$<BOOL:${ENABLE_SEGGER_SYSVIEW}>>:${CMAKE_CURRENT_SOURCE_DIR}/lib/SEGGER/SystemView/Config>
)

# ==============================================================================
# 3. СОЗДАНИЕ ТАРГЕТА (Исполняемого файла)
# ==============================================================================
# Имя таргета делаем чистым, а расширение .elf зададим через свойства ниже
add_executable(${PROJECT_NAME})

set_target_properties(${PROJECT_NAME} PROPERTIES
    C_STANDARD 17
    C_EXTENSIONS ON
    C_STANDARD_REQUIRED ON
    SUFFIX ".elf" # Теперь на выходе железно будет N32G430G8_Project.elf
)

# ==============================================================================
# 4. ИСХОДНЫЕ ФАЙЛЫ (Очищено от дубликатов)
# ==============================================================================
file(GLOB_RECURSE CMSIS_SOURCES ${CMSIS_DIR}/*.c)
file(GLOB_RECURSE STD_PERIPH_SOURCES ${STD_PERIPH_DIR}/*.c)
file(GLOB_RECURSE PROJECT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)

# Удаляем system_n32g430.c из списка GLOB, чтобы не было дублирования при сборке
list(REMOVE_ITEM CMSIS_SOURCES "${CMSIS_DIR}/device/system_n32g430.c")

# Исключаем SEGGER_SYSVIEW_Config_FreeRTOS.c если SYSVIEW отключён
if(NOT ENABLE_SEGGER_SYSVIEW)
  list(REMOVE_ITEM PROJECT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/SEGGER_SYSVIEW_Config_FreeRTOS.c")
  list(REMOVE_ITEM PROJECT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/SEGGER_SYSVIEW_FreeRTOS.c")
endif()

target_sources(${PROJECT_NAME} PRIVATE
    ${PROJECT_SOURCES}
    ${CMSIS_SOURCES}
    ${STD_PERIPH_SOURCES}
    ${CMSIS_DIR}/device/system_n32g430.c
    ${CMAKE_CURRENT_SOURCE_DIR}/startup_n32g430_gnu.s
    ${CMAKE_CURRENT_SOURCE_DIR}/ThreadSafe/newlib_lock_glue.c

    lib/freeModBUS/modbus/mb.c
    lib/freeModBUS/modbus/rtu/mbcrc.c
    lib/freeModBUS/modbus/rtu/mbrtu.c
    lib/freeModBUS/modbus/ascii/mbascii.c
    lib/freeModBUS/modbus/functions/mbfunccoils.c
    lib/freeModBUS/modbus/functions/mbfuncdiag.c
    lib/freeModBUS/modbus/functions/mbfuncdisc.c
    lib/freeModBUS/modbus/functions/mbfuncfile.c
    lib/freeModBUS/modbus/functions/mbfuncinput.c
    lib/freeModBUS/modbus/functions/mbfuncholding.c
    lib/freeModBUS/modbus/functions/mbfuncother.c
    lib/freeModBUS/modbus/functions/mbutils.c

    src/sfud_port/drv_spi_flash.c
    src/sfud_port/sfud_port.c
    src/fal_port/fal_flash_sfud_port.c

    lib/SFUD/sfud/src/sfud_sfdp.c
    lib/SFUD/sfud/src/sfud.c
    lib/FlashDB/src/fdb_file.c
    lib/FlashDB/src/fdb_kvdb.c
    lib/FlashDB/src/fdb_tsdb.c
    lib/FlashDB/src/fdb_utils.c
    lib/FlashDB/src/fdb.c
    lib/FlashDB/port/fal/src/fal_flash.c
    lib/FlashDB/port/fal/src/fal_partition.c
    lib/FlashDB/port/fal/src/fal_rtt.c
    lib/FlashDB/port/fal/src/fal.c

    $<$<CONFIG:Debug>: lib/SEGGER/RTT/RTT/SEGGER_RTT.c >
    $<$<CONFIG:Debug>: lib/SEGGER/RTT/RTT/SEGGER_RTT_printf.c >
    $<$<CONFIG:Debug>: lib/SEGGER/RTT/RTT/SEGGER_RTT_ASM_ARMv7M.S >
    $<$<AND:$<CONFIG:Debug>,$<BOOL:${ENABLE_SEGGER_SYSVIEW}>>: lib/SEGGER/SystemView/SYSVIEW/SEGGER_SYSVIEW.c >
)

target_link_libraries(${PROJECT_NAME}
    m4_architecture
    m
)

target_include_directories(${PROJECT_NAME} PRIVATE
  # User defined include directories
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/ThreadSafe
  ${CMAKE_CURRENT_SOURCE_DIR}/src/mb_port
  ${CMAKE_CURRENT_SOURCE_DIR}/lib/FlashDB/inc
  ${CMAKE_CURRENT_SOURCE_DIR}/lib/FlashDB/port/fal/inc
  ${CMAKE_CURRENT_SOURCE_DIR}/lib/freeModBUS/modbus/include
  ${CMAKE_CURRENT_SOURCE_DIR}/lib/freeModBUS/modbus/rtu
  ${CMAKE_CURRENT_SOURCE_DIR}/lib/SFUD/sfud/inc
)

configure_file(${LINKER_SCRIPT} ${CMAKE_BINARY_DIR} COPYONLY)

target_link_options(${PROJECT_NAME} PRIVATE
  # User defined linker options
  "LINKER:-Map=${PROJECT_DIR_NAME}.map"
  "LINKER:-T,${LINKER_SCRIPT}"
  -Wl,--gc-sections
  -Wl,--no-warn-rwx-segments
)

set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES
    LINK_DEPENDS "${LINKER_SCRIPT}"
)

# ==============================================================================
# 5. ФЛАГИ КОМПИЛЯЦИИ И ДЕФАЙНЫ
# ==============================================================================
set_source_files_properties(${STD_PERIPH_SOURCES}
    PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-parentheses;-Wno-unused-but-set-variable;-Wno-unused-variable"
)

# ==============================================================================
# 6. ДИРЕКТОРИИ ИНКЛУДОВ
# ==============================================================================
target_include_directories(${PROJECT_NAME} PRIVATE

)

set(FREERTOS_KERNEL_PATH "lib/FreeRTOS-Kernel")
# # Доступные варианты портов можно найти в репозитории FreeRTOS в папке portable/GCC/
add_library(freertos_config INTERFACE)

set(FREERTOS_PORT "GCC_ARM_CM4F" CACHE STRING "FreeRTOS port name")
# set(FREERTOS_HEAP "4" CACHE STRING "FreeRTOS heap scheme")
add_subdirectory(${FREERTOS_KERNEL_PATH} FreeRTOS-Kernel)

target_link_libraries(freertos_kernel PRIVATE m4_architecture)
target_link_libraries(freertos_kernel_port PRIVATE m4_architecture)
target_link_libraries(freertos_config INTERFACE m4_architecture)

set_property(TARGET freertos_kernel PROPERTY C_STANDARD 99)

target_link_libraries(${PROJECT_NAME}
    freertos_config
    freertos_kernel
)

# ==============================================================================
# 8. ПОСТ-СБОРКА (Генерация прошивок)
# ==============================================================================
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${PROJECT_NAME}> ${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}> ${PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}> # Исправлена опечатка с }
    COMMENT "Generating .hex, .bin and printing memory usage..."
)

# ==============================================================================
# 9. ИНФО
# ==============================================================================
message(STATUS "Проект: ${PROJECT_NAME}")
message(STATUS "Тип сборки: ${CMAKE_BUILD_TYPE}")
message(STATUS "Компилятор: ${CMAKE_C_COMPILER}")
