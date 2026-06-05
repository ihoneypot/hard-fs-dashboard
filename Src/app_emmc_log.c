#include "app_emmc_log.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "stm32h745i_discovery.h"
#include "stm32h745i_discovery_mmc.h"

#define APP_EMMC_LOG_INSTANCE 0U
#define APP_EMMC_LOG_BLOCK_SIZE 512U
#define APP_EMMC_LOG_START_BLOCK 4096U
#define APP_EMMC_LOG_RESERVED_BLOCKS 262144U
#define APP_EMMC_LOG_HEADER_MAGIC 0x48464C47UL
#define APP_EMMC_LOG_HEADER_VERSION 1U
#define APP_EMMC_LOG_FLAG_ACTIVE 0x00000001UL
#define APP_EMMC_LOG_FLAG_OVERFLOW 0x00000002UL
#define APP_EMMC_LOG_DUMP_HEADER_SIZE 24U
#define APP_EMMC_LOG_DUMP_RECORD_SIZE 42U
#define APP_EMMC_LOG_DUMP_FOOTER_SIZE 16U

static const uint8_t app_emmc_log_dump_header_magic[4] = {'H', 'F', 'D', 'B'};
static const uint8_t app_emmc_log_dump_footer_magic[4] = {'H', 'F', 'D', 'E'};
static const uint8_t app_emmc_log_dump_record_crc_valid_flag = 0x01U;

typedef struct {
  uint32_t timestamp_ms;
  app_can_dashboard_data_t dashboard_data;
  uint32_t crc32;
} app_emmc_log_record_t;

typedef struct {
  uint32_t magic;
  uint32_t version;
  uint32_t session_id;
  uint32_t record_size;
  uint32_t start_block;
  uint32_t reserved_blocks;
  uint32_t written_blocks;
  uint32_t record_count;
  uint32_t flags;
  uint32_t last_timestamp_ms;
  uint8_t reserved[APP_EMMC_LOG_BLOCK_SIZE - (10U * sizeof(uint32_t))];
} app_emmc_log_header_t;

typedef union {
  uint32_t words[APP_EMMC_LOG_BLOCK_SIZE / sizeof(uint32_t)];
  uint8_t bytes[APP_EMMC_LOG_BLOCK_SIZE];
  app_emmc_log_header_t header;
} app_emmc_log_block_t;

static SemaphoreHandle_t app_emmc_log_mutex = NULL;
static bool app_emmc_log_ready = false;
static uint32_t app_emmc_log_session_id = 0U;
static uint32_t app_emmc_log_written_blocks = 0U;
static uint32_t app_emmc_log_record_count = 0U;
static uint32_t app_emmc_log_last_timestamp_ms = 0U;
static uint32_t app_emmc_log_reserved_blocks = APP_EMMC_LOG_RESERVED_BLOCKS;
static uint32_t app_emmc_log_block_fill = 0U;
static uint32_t app_emmc_log_record_size = sizeof(app_emmc_log_record_t);
static bool app_emmc_log_overflow = false;
static app_emmc_log_block_t app_emmc_log_block_buffer = {0};

static bool app_emmc_log_lock(void) {
  if (app_emmc_log_mutex == NULL) {
    return false;
  }

  return xSemaphoreTake(app_emmc_log_mutex, portMAX_DELAY) == pdTRUE;
}

static void app_emmc_log_unlock(void) {
  if (app_emmc_log_mutex != NULL) {
    (void)xSemaphoreGive(app_emmc_log_mutex);
  }
}

static uint32_t app_emmc_log_crc32_update(uint32_t crc, const uint8_t *data, uint32_t size) {
  uint32_t index = 0U;
  uint32_t bit = 0U;

  if (data == NULL) {
    return crc;
  }

  for (index = 0U; index < size; index++) {
    crc ^= (uint32_t)data[index];
    for (bit = 0U; bit < 8U; bit++) {
      if ((crc & 1U) != 0U) {
        crc = (crc >> 1U) ^ 0xEDB88320UL;
      } else {
        crc >>= 1U;
      }
    }
  }

  return crc;
}

static uint32_t app_emmc_log_crc32_compute(const uint8_t *data, uint32_t size) {
  return app_emmc_log_crc32_update(0xFFFFFFFFUL, data, size) ^ 0xFFFFFFFFUL;
}

static uint32_t app_emmc_log_record_crc32(const app_emmc_log_record_t *record) {
  if (record == NULL) {
    return 0U;
  }

  return app_emmc_log_crc32_compute((const uint8_t *)record,
                                    (uint32_t)offsetof(app_emmc_log_record_t, crc32));
}

static bool app_emmc_log_record_crc_is_valid(const app_emmc_log_record_t *record) {
  if (record == NULL) {
    return false;
  }

  return record->crc32 == app_emmc_log_record_crc32(record);
}

static void app_emmc_log_write_le16(uint8_t *buffer, uint16_t value) {
  buffer[0] = (uint8_t)(value & 0xFFU);
  buffer[1] = (uint8_t)((value >> 8U) & 0xFFU);
}

static void app_emmc_log_write_le32(uint8_t *buffer, uint32_t value) {
  buffer[0] = (uint8_t)(value & 0xFFU);
  buffer[1] = (uint8_t)((value >> 8U) & 0xFFU);
  buffer[2] = (uint8_t)((value >> 16U) & 0xFFU);
  buffer[3] = (uint8_t)((value >> 24U) & 0xFFU);
}

static bool app_emmc_log_uart_write_bytes(const uint8_t *data, uint16_t size) {
  if ((data == NULL) || (size == 0U)) {
    return false;
  }

  return HAL_UART_Transmit(&hcom_uart[COM1], (uint8_t *)data, size, HAL_MAX_DELAY) == HAL_OK;
}

static void app_emmc_log_build_header(app_emmc_log_header_t *header) {
  if (header == NULL) {
    return;
  }

  memset(header, 0, sizeof(*header));
  header->magic = APP_EMMC_LOG_HEADER_MAGIC;
  header->version = APP_EMMC_LOG_HEADER_VERSION;
  header->session_id = app_emmc_log_session_id;
  header->record_size = app_emmc_log_record_size;
  header->start_block = APP_EMMC_LOG_START_BLOCK;
  header->reserved_blocks = app_emmc_log_reserved_blocks;
  header->written_blocks = app_emmc_log_written_blocks;
  header->record_count = app_emmc_log_record_count;
  header->flags = APP_EMMC_LOG_FLAG_ACTIVE;
  if (app_emmc_log_overflow) {
    header->flags |= APP_EMMC_LOG_FLAG_OVERFLOW;
  }
  header->last_timestamp_ms = app_emmc_log_last_timestamp_ms;
}

static bool app_emmc_log_write_header_locked(void) {
  app_emmc_log_block_t header_block = {0};

  memset(&header_block, 0xFF, sizeof(header_block));
  app_emmc_log_build_header(&header_block.header);

  return BSP_MMC_WriteBlocks(
             APP_EMMC_LOG_INSTANCE, header_block.words, APP_EMMC_LOG_START_BLOCK, 1U) ==
         BSP_ERROR_NONE;
}

static bool app_emmc_log_flush_locked(void) {
  uint32_t target_block = 0U;
  app_emmc_log_block_t block = {0};

  if (!app_emmc_log_ready) {
    return false;
  }

  if (app_emmc_log_block_fill == 0U) {
    return true;
  }

  if ((app_emmc_log_written_blocks + 1U) >= app_emmc_log_reserved_blocks) {
    app_emmc_log_overflow = true;
    (void)app_emmc_log_write_header_locked();
    return false;
  }

  memset(&block, 0xFF, sizeof(block));
  memcpy(block.bytes, app_emmc_log_block_buffer.bytes, app_emmc_log_block_fill);

  target_block = APP_EMMC_LOG_START_BLOCK + 1U + app_emmc_log_written_blocks;
  if (BSP_MMC_WriteBlocks(APP_EMMC_LOG_INSTANCE, block.words, target_block, 1U) != BSP_ERROR_NONE) {
    return false;
  }

  app_emmc_log_written_blocks++;
  app_emmc_log_block_fill = 0U;
  memset(&app_emmc_log_block_buffer, 0, sizeof(app_emmc_log_block_buffer));
  return app_emmc_log_write_header_locked();
}

static bool app_emmc_log_start_new_session_locked(uint32_t next_session_id) {
  app_emmc_log_session_id = next_session_id;
  app_emmc_log_written_blocks = 0U;
  app_emmc_log_record_count = 0U;
  app_emmc_log_last_timestamp_ms = 0U;
  app_emmc_log_block_fill = 0U;
  app_emmc_log_overflow = false;
  memset(&app_emmc_log_block_buffer, 0, sizeof(app_emmc_log_block_buffer));
  return app_emmc_log_write_header_locked();
}

static uint32_t app_emmc_log_get_records_per_block(void) {
  if (app_emmc_log_record_size == 0U) {
    return 0U;
  }

  return APP_EMMC_LOG_BLOCK_SIZE / app_emmc_log_record_size;
}

static uint32_t app_emmc_log_pack_dump_record(uint8_t *buffer,
                                              const app_emmc_log_record_t *record,
                                              bool stored_crc_valid) {
  uint32_t offset = 0U;
  uint32_t crc32 = 0U;

  if ((buffer == NULL) || (record == NULL)) {
    return 0U;
  }

  app_emmc_log_write_le32(&buffer[offset], record->timestamp_ms);
  offset += 4U;

  app_emmc_log_write_le16(&buffer[offset], record->dashboard_data.pack_voltage_deci_volts);
  offset += 2U;
  app_emmc_log_write_le16(&buffer[offset], record->dashboard_data.pack_summed_voltage_deci_volts);
  offset += 2U;
  app_emmc_log_write_le16(&buffer[offset], record->dashboard_data.lowest_cell_voltage_100uv);
  offset += 2U;
  app_emmc_log_write_le16(&buffer[offset], record->dashboard_data.average_cell_voltage_100uv);
  offset += 2U;
  app_emmc_log_write_le16(&buffer[offset], record->dashboard_data.highest_cell_voltage_100uv);
  offset += 2U;

  app_emmc_log_write_le16(&buffer[offset], (uint16_t)record->dashboard_data.pack_current_deci_amps);
  offset += 2U;
  app_emmc_log_write_le16(&buffer[offset],
                          (uint16_t)record->dashboard_data.lowest_pack_current_deci_amps);
  offset += 2U;
  app_emmc_log_write_le16(&buffer[offset],
                          (uint16_t)record->dashboard_data.average_pack_current_deci_amps);
  offset += 2U;
  app_emmc_log_write_le16(&buffer[offset],
                          (uint16_t)record->dashboard_data.highest_pack_current_deci_amps);
  offset += 2U;

  app_emmc_log_write_le16(&buffer[offset], (uint16_t)record->dashboard_data.lowest_pack_power_kw);
  offset += 2U;
  app_emmc_log_write_le16(&buffer[offset], (uint16_t)record->dashboard_data.average_pack_power_kw);
  offset += 2U;
  app_emmc_log_write_le16(&buffer[offset], (uint16_t)record->dashboard_data.highest_pack_power_kw);
  offset += 2U;

  buffer[offset++] = record->dashboard_data.pack_state_of_charge;
  buffer[offset++] = (uint8_t)record->dashboard_data.highest_temperature_c;
  buffer[offset++] = (uint8_t)record->dashboard_data.average_temperature_c;
  buffer[offset++] = (uint8_t)record->dashboard_data.lowest_temperature_c;
  buffer[offset++] = record->dashboard_data.fault_flags;
  buffer[offset++] = stored_crc_valid ? app_emmc_log_dump_record_crc_valid_flag : 0U;

  app_emmc_log_write_le32(&buffer[offset], record->crc32);
  offset += 4U;

  crc32 = app_emmc_log_crc32_compute(buffer, offset);
  app_emmc_log_write_le32(&buffer[offset], crc32);
  offset += 4U;

  return offset;
}

bool app_emmc_log_init(void) {
  BSP_MMC_CardInfo card_info = {0};
  app_emmc_log_block_t header_block = {0};
  uint32_t next_session_id = 1U;

  if (app_emmc_log_mutex == NULL) {
    app_emmc_log_mutex = xSemaphoreCreateMutex();
    if (app_emmc_log_mutex == NULL) {
      return false;
    }
  }

  if (!app_emmc_log_lock()) {
    return false;
  }

  if (BSP_MMC_Init(APP_EMMC_LOG_INSTANCE) != BSP_ERROR_NONE) {
    app_emmc_log_ready = false;
    app_emmc_log_unlock();
    return false;
  }

  if (BSP_MMC_GetCardInfo(APP_EMMC_LOG_INSTANCE, &card_info) != BSP_ERROR_NONE) {
    app_emmc_log_ready = false;
    app_emmc_log_unlock();
    return false;
  }

  if (card_info.LogBlockNbr <= (APP_EMMC_LOG_START_BLOCK + 1U)) {
    app_emmc_log_ready = false;
    app_emmc_log_unlock();
    return false;
  }

  app_emmc_log_reserved_blocks = APP_EMMC_LOG_RESERVED_BLOCKS;
  if (APP_EMMC_LOG_START_BLOCK + app_emmc_log_reserved_blocks > card_info.LogBlockNbr) {
    app_emmc_log_reserved_blocks = card_info.LogBlockNbr - APP_EMMC_LOG_START_BLOCK;
  }

  if (BSP_MMC_ReadBlocks(APP_EMMC_LOG_INSTANCE, header_block.words, APP_EMMC_LOG_START_BLOCK, 1U) ==
      BSP_ERROR_NONE) {
    if ((header_block.header.magic == APP_EMMC_LOG_HEADER_MAGIC) &&
        (header_block.header.version == APP_EMMC_LOG_HEADER_VERSION)) {
      next_session_id = header_block.header.session_id + 1U;
    }
  }

  app_emmc_log_ready = app_emmc_log_start_new_session_locked(next_session_id);
  app_emmc_log_unlock();
  return app_emmc_log_ready;
}

bool app_emmc_log_append(uint32_t timestamp_ms, const app_can_dashboard_data_t *data) {
  app_emmc_log_record_t record = {0};

  if ((data == NULL) || !app_emmc_log_ready) {
    return false;
  }

  if (app_emmc_log_record_size > APP_EMMC_LOG_BLOCK_SIZE) {
    return false;
  }

  if (!app_emmc_log_lock()) {
    return false;
  }

  if ((app_emmc_log_block_fill + app_emmc_log_record_size) > APP_EMMC_LOG_BLOCK_SIZE) {
    if (!app_emmc_log_flush_locked()) {
      app_emmc_log_unlock();
      return false;
    }
  }

  record.timestamp_ms = timestamp_ms;
  record.dashboard_data = *data;
  record.crc32 = app_emmc_log_record_crc32(&record);

  memcpy(&app_emmc_log_block_buffer.bytes[app_emmc_log_block_fill], &record, sizeof(record));
  app_emmc_log_block_fill += app_emmc_log_record_size;
  app_emmc_log_record_count++;
  app_emmc_log_last_timestamp_ms = timestamp_ms;

  if (app_emmc_log_block_fill == APP_EMMC_LOG_BLOCK_SIZE) {
    if (!app_emmc_log_flush_locked()) {
      app_emmc_log_unlock();
      return false;
    }
  }

  app_emmc_log_unlock();
  return true;
}

bool app_emmc_log_flush(void) {
  bool result = false;

  if (!app_emmc_log_lock()) {
    return false;
  }

  result = app_emmc_log_flush_locked();
  app_emmc_log_unlock();
  return result;
}

bool app_emmc_log_reset_session(void) {
  bool result = false;
  uint32_t next_session_id = 1U;

  if (!app_emmc_log_lock()) {
    return false;
  }

  next_session_id = app_emmc_log_session_id + 1U;
  result = app_emmc_log_start_new_session_locked(next_session_id);
  app_emmc_log_ready = result;
  app_emmc_log_unlock();
  return result;
}

void app_emmc_log_get_status(app_emmc_log_status_t *status) {
  if (status == NULL) {
    return;
  }

  memset(status, 0, sizeof(*status));
  if (!app_emmc_log_lock()) {
    return;
  }

  status->ready = app_emmc_log_ready;
  status->overflow = app_emmc_log_overflow;
  status->session_id = app_emmc_log_session_id;
  status->record_count = app_emmc_log_record_count;
  status->written_blocks = app_emmc_log_written_blocks;
  status->reserved_blocks = app_emmc_log_reserved_blocks;
  status->staged_bytes = app_emmc_log_block_fill;
  app_emmc_log_unlock();
}

void app_emmc_log_dump_csv(void) {
  app_emmc_log_block_t block = {0};
  app_emmc_log_record_t record = {0};
  bool record_crc_valid = false;
  uint32_t records_per_block = 0U;
  uint32_t record_index = 0U;
  uint32_t cached_block_index = UINT32_MAX;
  uint32_t block_index = 0U;
  uint32_t offset_in_block = 0U;

  if (!app_emmc_log_lock()) {
    return;
  }

  if (!app_emmc_log_ready) {
    app_emmc_log_unlock();
    return;
  }

  if (!app_emmc_log_flush_locked()) {
    app_emmc_log_unlock();
    return;
  }

  records_per_block = app_emmc_log_get_records_per_block();
  if (records_per_block == 0U) {
    app_emmc_log_unlock();
    return;
  }

  printf("timestamp_ms,pack_voltage_deci_v,pack_summed_voltage_deci_v,"
         "lowest_cell_voltage_100uv,average_cell_voltage_100uv,highest_cell_voltage_100uv,"
         "pack_current_deci_a,lowest_pack_current_deci_a,average_pack_current_deci_a,"
         "highest_pack_current_deci_a,lowest_pack_power_kw,average_pack_power_kw,"
         "highest_pack_power_kw,pack_soc,highest_temp_c,average_temp_c,lowest_temp_c,"
         "fault_flags,record_crc_valid\r\n");

  for (record_index = 0U; record_index < app_emmc_log_record_count; record_index++) {
    block_index = record_index / records_per_block;
    offset_in_block = (record_index % records_per_block) * app_emmc_log_record_size;

    if (block_index != cached_block_index) {
      memset(&block, 0, sizeof(block));
      if (BSP_MMC_ReadBlocks(APP_EMMC_LOG_INSTANCE,
                             block.words,
                             APP_EMMC_LOG_START_BLOCK + 1U + block_index,
                             1U) != BSP_ERROR_NONE) {
        break;
      }
      cached_block_index = block_index;
    }

    memcpy(&record, &block.bytes[offset_in_block], sizeof(record));
    record_crc_valid = app_emmc_log_record_crc_is_valid(&record);
    printf("%lu,%u,%u,%u,%u,%u,%d,%d,%d,%d,%d,%d,%d,%u,%d,%d,%d,%u,%u\r\n",
           (unsigned long)record.timestamp_ms,
           (unsigned int)record.dashboard_data.pack_voltage_deci_volts,
           (unsigned int)record.dashboard_data.pack_summed_voltage_deci_volts,
           (unsigned int)record.dashboard_data.lowest_cell_voltage_100uv,
           (unsigned int)record.dashboard_data.average_cell_voltage_100uv,
           (unsigned int)record.dashboard_data.highest_cell_voltage_100uv,
           (int)record.dashboard_data.pack_current_deci_amps,
           (int)record.dashboard_data.lowest_pack_current_deci_amps,
           (int)record.dashboard_data.average_pack_current_deci_amps,
           (int)record.dashboard_data.highest_pack_current_deci_amps,
           (int)record.dashboard_data.lowest_pack_power_kw,
           (int)record.dashboard_data.average_pack_power_kw,
           (int)record.dashboard_data.highest_pack_power_kw,
           (unsigned int)record.dashboard_data.pack_state_of_charge,
           (int)record.dashboard_data.highest_temperature_c,
           (int)record.dashboard_data.average_temperature_c,
           (int)record.dashboard_data.lowest_temperature_c,
           (unsigned int)record.dashboard_data.fault_flags,
           record_crc_valid ? 1U : 0U);
  }

  app_emmc_log_unlock();
}

bool app_emmc_log_dump_binary(void) {
  app_emmc_log_block_t block = {0};
  app_emmc_log_record_t record = {0};
  uint8_t header[APP_EMMC_LOG_DUMP_HEADER_SIZE] = {0};
  uint8_t footer[APP_EMMC_LOG_DUMP_FOOTER_SIZE] = {0};
  uint8_t packed_record[APP_EMMC_LOG_DUMP_RECORD_SIZE] = {0};
  uint32_t records_per_block = 0U;
  uint32_t record_index = 0U;
  uint32_t cached_block_index = UINT32_MAX;
  uint32_t block_index = 0U;
  uint32_t offset_in_block = 0U;
  uint32_t stream_crc32 = 0xFFFFFFFFUL;
  uint32_t footer_crc32 = 0U;
  bool record_crc_valid = false;

  if (!app_emmc_log_lock()) {
    return false;
  }

  if (!app_emmc_log_ready) {
    app_emmc_log_unlock();
    return false;
  }

  if (!app_emmc_log_flush_locked()) {
    app_emmc_log_unlock();
    return false;
  }

  records_per_block = app_emmc_log_get_records_per_block();
  if (records_per_block == 0U) {
    app_emmc_log_unlock();
    return false;
  }

  memcpy(&header[0], app_emmc_log_dump_header_magic, sizeof(app_emmc_log_dump_header_magic));
  app_emmc_log_write_le16(&header[4], APP_EMMC_LOG_HEADER_VERSION);
  app_emmc_log_write_le16(&header[6], APP_EMMC_LOG_DUMP_HEADER_SIZE);
  app_emmc_log_write_le32(&header[8], app_emmc_log_session_id);
  app_emmc_log_write_le32(&header[12], app_emmc_log_record_count);
  app_emmc_log_write_le16(&header[16], APP_EMMC_LOG_DUMP_RECORD_SIZE);
  app_emmc_log_write_le16(&header[18], app_emmc_log_overflow ? 1U : 0U);
  app_emmc_log_write_le32(&header[20], app_emmc_log_crc32_compute(header, 20U));

  if (!app_emmc_log_uart_write_bytes(header, sizeof(header))) {
    app_emmc_log_unlock();
    return false;
  }

  for (record_index = 0U; record_index < app_emmc_log_record_count; record_index++) {
    block_index = record_index / records_per_block;
    offset_in_block = (record_index % records_per_block) * app_emmc_log_record_size;

    if (block_index != cached_block_index) {
      memset(&block, 0, sizeof(block));
      if (BSP_MMC_ReadBlocks(APP_EMMC_LOG_INSTANCE,
                             block.words,
                             APP_EMMC_LOG_START_BLOCK + 1U + block_index,
                             1U) != BSP_ERROR_NONE) {
        app_emmc_log_unlock();
        return false;
      }
      cached_block_index = block_index;
    }

    memcpy(&record, &block.bytes[offset_in_block], sizeof(record));
    record_crc_valid = app_emmc_log_record_crc_is_valid(&record);
    if (app_emmc_log_pack_dump_record(packed_record, &record, record_crc_valid) !=
        APP_EMMC_LOG_DUMP_RECORD_SIZE) {
      app_emmc_log_unlock();
      return false;
    }

    stream_crc32 =
        app_emmc_log_crc32_update(stream_crc32, packed_record, APP_EMMC_LOG_DUMP_RECORD_SIZE);
    if (!app_emmc_log_uart_write_bytes(packed_record, APP_EMMC_LOG_DUMP_RECORD_SIZE)) {
      app_emmc_log_unlock();
      return false;
    }
  }

  stream_crc32 ^= 0xFFFFFFFFUL;
  memcpy(&footer[0], app_emmc_log_dump_footer_magic, sizeof(app_emmc_log_dump_footer_magic));
  app_emmc_log_write_le32(&footer[4], app_emmc_log_record_count);
  app_emmc_log_write_le32(&footer[8], stream_crc32);
  footer_crc32 = app_emmc_log_crc32_compute(footer, 12U);
  app_emmc_log_write_le32(&footer[12], footer_crc32);

  if (!app_emmc_log_uart_write_bytes(footer, sizeof(footer))) {
    app_emmc_log_unlock();
    return false;
  }

  app_emmc_log_unlock();
  return true;
}
