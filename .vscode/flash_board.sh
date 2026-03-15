#!/bin/sh

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)

TARGET_NAME=${TARGET_NAME:-hard-fs-dashboard}
BUILD_DIR=${BUILD_DIR:-"$PROJECT_DIR/build/Debug"}
FLASH_BIN=${FLASH_BIN:-"$BUILD_DIR/$TARGET_NAME.bin"}
QSPI_BIN=${QSPI_BIN:-"$BUILD_DIR/${TARGET_NAME}_qspi.bin"}
FLASH_ADDR=${FLASH_ADDR:-0x08000000}
QSPI_ADDR=${QSPI_ADDR:-0x90000000}
CONNECT_PORT=${CONNECT_PORT:-SWD}
CONNECT_MODE=${CONNECT_MODE:-UR}
LOADER_NAME=${LOADER_NAME:-MT25TL01G_STM32H750B-DISCO.stldr}

find_programmer() {
    if [ -n "${STM32_PROGRAMMER_CLI:-}" ] && [ -x "${STM32_PROGRAMMER_CLI}" ]; then
        printf '%s\n' "${STM32_PROGRAMMER_CLI}"
        return 0
    fi

    if command -v STM32_Programmer_CLI >/dev/null 2>&1; then
        command -v STM32_Programmer_CLI
        return 0
    fi

    for candidate in \
        "${CUBE_BUNDLE_PATH:-}/programmer"/*/bin/STM32_Programmer_CLI \
        "$HOME/Library/Application Support/stm32cube/bundles/programmer"/*/bin/STM32_Programmer_CLI \
        "$HOME/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI"
    do
        if [ -x "$candidate" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done

    return 1
}

find_loader() {
    if [ -n "${STM32_EXTERNAL_LOADER:-}" ] && [ -f "${STM32_EXTERNAL_LOADER}" ]; then
        printf '%s\n' "${STM32_EXTERNAL_LOADER}"
        return 0
    fi

    programmer_dir=$(dirname "$1")
    for candidate in \
        "$programmer_dir/ExternalLoader/$LOADER_NAME" \
        "${CUBE_BUNDLE_PATH:-}/programmer"/*/bin/ExternalLoader/$LOADER_NAME \
        "$HOME/Library/Application Support/stm32cube/bundles/programmer"/*/bin/ExternalLoader/$LOADER_NAME
    do
        if [ -f "$candidate" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done

    return 1
}

if [ ! -f "$QSPI_BIN" ]; then
    printf 'Missing QSPI image: %s\n' "$QSPI_BIN" >&2
    exit 1
fi

if [ ! -f "$FLASH_BIN" ]; then
    printf 'Missing flash image: %s\n' "$FLASH_BIN" >&2
    exit 1
fi

PROGRAMMER_CLI=$(find_programmer) || {
    printf 'STM32_Programmer_CLI not found. Set STM32_PROGRAMMER_CLI or CUBE_BUNDLE_PATH.\n' >&2
    exit 1
}

EXTERNAL_LOADER=$(find_loader "$PROGRAMMER_CLI") || {
    printf 'External loader %s not found. Set STM32_EXTERNAL_LOADER if needed.\n' "$LOADER_NAME" >&2
    exit 1
}

printf 'Using programmer: %s\n' "$PROGRAMMER_CLI"
printf 'Using loader: %s\n' "$EXTERNAL_LOADER"
printf 'Flashing QSPI: %s -> %s\n' "$QSPI_BIN" "$QSPI_ADDR"
"$PROGRAMMER_CLI" -c "port=$CONNECT_PORT" "mode=$CONNECT_MODE" -el "$EXTERNAL_LOADER" -w "$QSPI_BIN" "$QSPI_ADDR" -v

printf 'Flashing internal flash: %s -> %s\n' "$FLASH_BIN" "$FLASH_ADDR"
"$PROGRAMMER_CLI" -c "port=$CONNECT_PORT" "mode=$CONNECT_MODE" -w "$FLASH_BIN" "$FLASH_ADDR" -v -rst
