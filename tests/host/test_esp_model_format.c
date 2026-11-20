#include "esp_model_format.h"

#include <stdio.h>
#include <string.h>

#define TEST_FILE_SIZE 2048U
#define TEST_INDEX_OFFSET 256U
#define TEST_INDEX_SIZE 8U
#define TEST_DATA_OFFSET 320U
#define TEST_DATA_SIZE 1025U
#define TEST_METADATA_OFFSET 1408U
#define TEST_METADATA_SIZE 16U
#define TEST_FILE_END (TEST_METADATA_OFFSET + TEST_METADATA_SIZE)

typedef struct {
    const uint8_t *data;
    size_t size;
} memory_file_t;

static int check(bool condition, const char *expression, int line)
{
    if (condition) {
        return 0;
    }
    fprintf(stderr, "line %d: check failed: %s\n", line, expression);
    return 1;
}

#define CHECK(expression) \
    do { \
        if (check((expression), #expression, __LINE__) != 0) return 1; \
    } while (0)

static esp_err_t memory_read_at(void *context, uint64_t offset,
                                void *buffer, size_t size)
{
    const memory_file_t *file = context;
    if (offset > file->size || size > file->size - (size_t)offset) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(buffer, file->data + (size_t)offset, size);
    return ESP_OK;
}

static void set_fixed_string(char *destination, size_t capacity,
                             const char *value)
{
    const size_t length = strlen(value);
    if (length < capacity) {
        memcpy(destination, value, length);
    }
}

static void build_container(uint8_t *data, uint32_t psram_required_kb,
                            uint16_t index_type, uint16_t index_flags)
{
    memset(data, 0, TEST_FILE_SIZE);
    memset(data + TEST_INDEX_OFFSET, 0x31, TEST_INDEX_SIZE);
    memset(data + TEST_DATA_OFFSET, 0x52, TEST_DATA_SIZE);
    memset(data + TEST_METADATA_OFFSET, 0x73, TEST_METADATA_SIZE);

    esp_model_section_t sections[3] = {0};
    sections[0].section_type = index_type;
    sections[0].flags = index_flags;
    sections[0].offset = TEST_INDEX_OFFSET;
    sections[0].size = TEST_INDEX_SIZE;
    sections[0].crc32 = esp_model_crc32(data + TEST_INDEX_OFFSET, TEST_INDEX_SIZE);
    set_fixed_string(sections[0].name, sizeof(sections[0].name),
                     index_type == SECTION_TYPE_TENSOR_INDEX
                         ? "tensor_index" : "future");

    sections[1].section_type = SECTION_TYPE_TENSOR_DATA;
    sections[1].offset = TEST_DATA_OFFSET;
    sections[1].size = TEST_DATA_SIZE;
    sections[1].crc32 = esp_model_crc32(data + TEST_DATA_OFFSET, TEST_DATA_SIZE);
    set_fixed_string(sections[1].name, sizeof(sections[1].name), "tensor_data");

    sections[2].section_type = SECTION_TYPE_METADATA;
    sections[2].offset = TEST_METADATA_OFFSET;
    sections[2].size = TEST_METADATA_SIZE;
    sections[2].crc32 = esp_model_crc32(
        data + TEST_METADATA_OFFSET, TEST_METADATA_SIZE);
    set_fixed_string(sections[2].name, sizeof(sections[2].name), "metadata");

    const size_t table_size = sizeof(sections);
    memcpy(data + ESPM_HEADER_SIZE, sections, table_size);

    esp_model_header_t header = {0};
    header.magic = ESPM_MAGIC_LE;
    header.format_version = ESPM_CURRENT_VERSION;
    header.model_type = MODEL_TYPE_ASR_SEQ2SEQ;
    header.quant_type = MODEL_QUANT_INT8;
    header.num_sections = 3;
    set_fixed_string(header.model_name, sizeof(header.model_name), "host-test");
    set_fixed_string(header.version_str, sizeof(header.version_str), "1");
    header.psram_required_kb = psram_required_kb;
    header.file_size = TEST_FILE_END;
    header.section_table_offset = ESPM_HEADER_SIZE;
    header.header_size = ESPM_HEADER_SIZE;
    header.section_entry_size = ESPM_SECTION_ENTRY_SIZE;
    header.alignment = ESPM_ALIGNMENT;
    header.section_table_crc32 = esp_model_crc32(data + ESPM_HEADER_SIZE, table_size);
    header.header_crc32 = 0;
    header.header_crc32 = esp_model_crc32(
        (const uint8_t *)&header, sizeof(header));
    memcpy(data, &header, sizeof(header));
}

static void set_container_version(uint8_t *data, uint16_t version)
{
    esp_model_header_t header;
    memcpy(&header, data, sizeof(header));
    header.format_version = version;
    header.header_crc32 = 0;
    header.header_crc32 = esp_model_crc32(
        (const uint8_t *)&header, sizeof(header));
    memcpy(data, &header, sizeof(header));
}

static esp_err_t open_container(const uint8_t *data, size_t size)
{
    const memory_file_t file = {.data = data, .size = size};
    const esp_model_io_t io = {
        .context = (void *)&file,
        .read_at = memory_read_at,
        .file_size = size,
    };
    esp_model_container_t container;
    return esp_model_container_open(&io, &container);
}

static esp_err_t validate_two_scope_index(uint32_t second_scope)
{
    uint8_t index[sizeof(esp_model_tensor_index_header_v2_t) +
                  2 * sizeof(esp_model_tensor_record_v2_t)] = {0};
    esp_model_tensor_index_header_v2_t header = {
        .magic = ESPM_TENSOR_INDEX_MAGIC,
        .version = ESPM_TENSOR_INDEX_VERSION,
        .header_size = ESPM_TENSOR_INDEX_HEADER_SIZE,
        .record_size = ESPM_TENSOR_RECORD_SIZE,
        .tensor_count = 2,
        .scope_count = 2,
        .total_elements = 2,
        .total_data_bytes = 2,
    };
    esp_model_tensor_record_v2_t records[2] = {0};
    const char *names[] = {"encoder.weight", "decoder.weight"};
    const uint32_t scopes[] = {0, second_scope};
    for (size_t i = 0; i < 2; ++i) {
        records[i].scope_id = scopes[i];
        records[i].dtype = ESPM_TENSOR_DTYPE_INT8;
        records[i].rank = 1;
        records[i].quant_type = ESPM_TENSOR_QUANT_SYMMETRIC;
        records[i].num_elements = 1;
        records[i].shape[0] = 1;
        records[i].num_channels = 1;
        records[i].channel_axis = 0xFF;
        records[i].scales_offset = i * 4;
        records[i].scales_size = sizeof(float);
        records[i].data_offset = 16 + i * 16;
        records[i].data_size = 1;
        set_fixed_string(records[i].name, sizeof(records[i].name), names[i]);
        records[i].name_hash = esp_model_crc32(
            (const uint8_t *)names[i], strlen(names[i]));
    }
    memcpy(index, &header, sizeof(header));
    memcpy(index + sizeof(header), records, sizeof(records));

    esp_model_tensor_index_header_v2_t parsed_header;
    const esp_model_tensor_record_v2_t *parsed_records;
    return esp_model_tensor_index_v2_validate(
        index, sizeof(index), 33, &parsed_header, &parsed_records);
}

int main(void)
{
    uint8_t data[TEST_FILE_SIZE];

    build_container(data, 2, SECTION_TYPE_TENSOR_INDEX, 0);
    CHECK(open_container(data, TEST_FILE_END) == ESP_OK);

    set_container_version(data, 2U);
    CHECK(open_container(data, TEST_FILE_END) == ESP_ERR_NOT_SUPPORTED);

    build_container(data, 2, SECTION_TYPE_TENSOR_INDEX,
                    ESPM_SECTION_FLAG_OPTIONAL);
    CHECK(open_container(data, TEST_FILE_END) == ESP_ERR_INVALID_ARG);

    build_container(data, 1, SECTION_TYPE_TENSOR_INDEX, 0);
    CHECK(open_container(data, TEST_FILE_END) == ESP_ERR_INVALID_SIZE);

    build_container(data, 2, 9, ESPM_SECTION_FLAG_OPTIONAL);
    CHECK(open_container(data, TEST_FILE_END) == ESP_ERR_INVALID_SIZE);

    build_container(data, 2, 9, 0);
    CHECK(open_container(data, TEST_FILE_END) == ESP_ERR_NOT_SUPPORTED);

    CHECK(validate_two_scope_index(1) == ESP_OK);
    CHECK(validate_two_scope_index(0) == ESP_ERR_INVALID_ARG);

    puts("ESP model format host tests passed");
    return 0;
}
