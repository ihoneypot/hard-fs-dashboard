cmake_minimum_required(VERSION 3.20)

project("hard-fs-dashboard")
enable_language(C CXX ASM)
message("Build type: " ${CMAKE_BUILD_TYPE})

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set(cpu_PARAMS ${CPU_FLAGS})

set(linker_script_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/stm32h750xb_flash.ld
)

set(sources_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/stm32h7xx_hal_msp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/stm32h7xx_it.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/syscall.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/sysmem.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/system_stm32h7xx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/startup_stm32h750xx.S
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Lvgl_Porting/lvgl_port_lcd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/Lvgl_Porting/lvgl_port_touchpad.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_cortex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_exti.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_flash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_flash_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_gpio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_hsem.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_i2c.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_i2c_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_ltdc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_ltdc_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_mdma.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_qspi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_sdram.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_uart.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_uart_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_fmc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/BSP/Components/mt25tl01g/mt25tl01g.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/BSP/Components/mt48lc4m32b2/mt48lc4m32b2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/BSP/Components/ft5336/ft5336.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/BSP/Components/ft5336/ft5336_reg.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/BSP/STM32H745I-DISCO/stm32h745i_discovery.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/BSP/STM32H745I-DISCO/stm32h745i_discovery_bus.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/BSP/STM32H745I-DISCO/stm32h745i_discovery_qspi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/BSP/STM32H745I-DISCO/stm32h745i_discovery_sdram.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/BSP/STM32H745I-DISCO/stm32h745i_discovery_ts.c
)

set(include_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32H7xx/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Inc/Legacy
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/BSP/STM32H745I-DISCO
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/BSP/Components/Common
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/BSP/Components/ft5336
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/BSP/Components/mt25tl01g
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/BSP/Components/mt48lc4m32b2
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/BSP/Components/rk043fn48h
)

set(symbols_COMMON
    USE_HAL_DRIVER
    STM32H750xx
    CORE_CM7
    LV_DRV_NO_CONF
)

set(common_compile_options
    ${cpu_PARAMS}
    -ffunction-sections
    -fdata-sections
    -fno-unwind-tables
    -fno-asynchronous-unwind-tables
)

set(LV_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE PATH "LVGL configuration" FORCE)
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE PATH "LVGL configuration" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "Build LVGL demos" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "Build LVGL examples" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "Disable ThorVG in embedded builds" FORCE)

add_subdirectory("ui")

file(GLOB_RECURSE lvgl_sources CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/lvgl/src/*.c
)
list(FILTER lvgl_sources EXCLUDE REGEX "/demos/")
list(FILTER lvgl_sources EXCLUDE REGEX "/examples/")
list(FILTER lvgl_sources EXCLUDE REGEX "/tests/")
add_library(lvgl STATIC ${lvgl_sources})
target_include_directories(lvgl SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/lvgl
    ${include_DIRS}
)
target_compile_definitions(lvgl PUBLIC
    LV_LVGL_H_INCLUDE_SIMPLE
    LV_CONF_INCLUDE_SIMPLE
    ${symbols_COMMON}
)

function(configure_embedded_library target_name)
    if(TARGET ${target_name})
        target_compile_options(${target_name} PRIVATE
            ${common_compile_options}
            $<$<COMPILE_LANGUAGE:ASM>:-x assembler-with-cpp>
        )
    endif()
endfunction()

configure_embedded_library(lvgl)
configure_embedded_library(ui)

target_include_directories(ui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${include_DIRS}
)
target_link_libraries(ui PUBLIC lvgl)

add_executable(${CMAKE_PROJECT_NAME})

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${sources_SRCS})

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${include_DIRS})

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    ${symbols_COMMON}
    $<$<CONFIG:Debug>:DEBUG>
)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    lvgl
    ui
)

target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE
    ${common_compile_options}
    -Wall
    -Wextra
    -Wpedantic
    -Wno-unused-parameter
    $<$<COMPILE_LANGUAGE:ASM>:-x assembler-with-cpp -MMD -MP>
    $<$<CONFIG:Debug>:-Os -g3 -ggdb>
    $<$<CONFIG:Release>:-Os>
)

target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
    -T${linker_script_SRC}
    ${cpu_PARAMS}
    -Wl,-Map=${CMAKE_PROJECT_NAME}.map
    -Wl,--gc-sections
    --specs=nano.specs
    --specs=nosys.specs
    -Wl,--start-group
    -lc
    -lm
    -Wl,--end-group
    -Wl,--print-memory-usage
)

set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES LINK_DEPENDS ${linker_script_SRC})

add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY}
            --remove-section=.extflash
            --remove-section=.ui_assets
            -O binary
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.bin
    COMMAND ${CMAKE_OBJCOPY}
            -j .extflash
            -j .ui_assets
            --change-addresses -0x90000000
            -O binary
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}_qspi.bin
)
