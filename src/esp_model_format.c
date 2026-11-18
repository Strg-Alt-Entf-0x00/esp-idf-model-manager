#include "esp_model_format.h"

#include <math.h>
#include <string.h>

#define ESPM_METADATA_HEADER_SIZE 16U
#define ESPM_METADATA_RECORD_PREFIX_SIZE 8U

typedef struct {
    uint16_t key_size;
    uint8_t value_type;
    uint8_t flags;
    uint32_t value_size;
} metadata_record_prefix_t;

_Static_assert(sizeof(metadata_record_prefix_t) == ESPM_METADATA_RECORD_PREFIX_SIZE,
               "Unexpected ESPM metadata record prefix size");

static bool all_zero(const uint8_t *data, size_t size)
{
    for (size_t i = 0; i < size; ++i) {
        if (data[i] != 0) {
            return false;
        }
    }
    return true;
}

static bool utf8_valid(const uint8_t *data, size_t size, bool allow_nul)
{
    if (!data && size != 0) {
        return false;
    }
    for (size_t i = 0; i < size;) {
        const uint8_t first = data[i];
        if (first == 0 && !allow_nul) {
            return false;
        }
        if (first < 0x80) {
            ++i;
            continue;
        }

        size_t continuation_count;
        uint32_t codepoint;
        if (first >= 0xC2 && first <= 0xDF) {
            continuation_count = 1;
            codepoint = first & 0x1FU;
        } else if (first >= 0xE0 && first <= 0xEF) {
            continuation_count = 2;
            codepoint = first & 0x0FU;
        } else if (first >= 0xF0 && first <= 0xF4) {
            continuation_count = 3;
            codepoint = first & 0x07U;
        } else {
            return false;
        }
        if (continuation_count > size - i - 1) {
            return false;
        }
        for (size_t j = 1; j <= continuation_count; ++j) {
            const uint8_t continuation = data[i + j];
            if ((continuation & 0xC0U) != 0x80U) {
                return false;
            }
            codepoint = (codepoint << 6) | (continuation & 0x3FU);
        }
        if ((continuation_count == 1 && codepoint < 0x80U) ||
            (continuation_count == 2 && codepoint < 0x800U) ||
            (continuation_count == 3 && codepoint < 0x10000U) ||
            (codepoint >= 0xD800U && codepoint <= 0xDFFFU) ||
            codepoint > 0x10FFFFU) {
            return false;
        }
        i += continuation_count + 1;
    }
    return true;
}

static bool fixed_string_valid(const char *text, size_t capacity, bool allow_empty)
{
    const char *end = (const char *)memchr(text, '\0', capacity);
    if (!end || (!allow_empty && end == text)) {
        return false;
    }
    const size_t length = (size_t)(end - text);
    if (!utf8_valid((const uint8_t *)text, length, false)) {
        return false;
    }
    for (const char *p = end + 1; p < text + capacity; ++p) {
        if (*p != '\0') {
            return false;
        }
    }
    return true;
}

static bool valid_model_type(uint16_t type)
{
    return type == MODEL_TYPE_VAD || type == MODEL_TYPE_KWS ||
           type == MODEL_TYPE_ASR_SEQ2SEQ || type == MODEL_TYPE_TTS_VOICE ||
           type == MODEL_TYPE_CUSTOM;
}

static bool valid_quant_type(uint16_t quant, uint16_t version)
{
    if (version == ESPM_LEGACY_VERSION) {
        return quant <= MODEL_QUANT_FP32;
    }
    return quant == MODEL_QUANT_INT8 || quant == MODEL_QUANT_INT8_PER_CH ||
           quant == MODEL_QUANT_FP32 || quant == MODEL_QUANT_MIXED;
}

static bool known_section_type(uint16_t section_type, uint16_t version)
{
    if (version == ESPM_LEGACY_VERSION) {
        return section_type >= SECTION_TYPE_VOCAB &&
               section_type <= SECTION_TYPE_TENSOR_DATA;
    }
    return section_type == SECTION_TYPE_VOCAB ||
           section_type == SECTION_TYPE_CONFIG_JSON ||
           section_type == SECTION_TYPE_TENSOR_INDEX ||
           section_type == SECTION_TYPE_TENSOR_DATA ||
           section_type == SECTION_TYPE_METADATA;
}

uint32_t esp_model_crc32_update(uint32_t crc, const uint8_t *data, size_t size)
{
    if (!data && size != 0) {
        return crc;
    }
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) ? (crc >> 1) ^ 0xEDB88320U : crc >> 1;
        }
    }
    return crc;
}

uint32_t esp_model_crc32(const uint8_t *data, size_t size)
{
    return esp_model_crc32_update(0xFFFFFFFFU, data, size) ^ 0xFFFFFFFFU;
}

esp_err_t esp_model_container_open(const esp_model_io_t *io,
                                   esp_model_container_t *out_container)
{
    if (!io || !io->read_at || !out_container) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_container, 0, sizeof(*out_container));
    if (io->file_size < ESPM_HEADER_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_model_header_t header;
    esp_err_t error = io->read_at(io->context, 0, &header, sizeof(header));
    if (error != ESP_OK) {
        return error;
    }
    if (header.magic != ESPM_MAGIC_LE ||
        (header.format_version != ESPM_LEGACY_VERSION &&
         header.format_version != ESPM_CURRENT_VERSION) ||
        !valid_model_type(header.model_type) ||
        !valid_quant_type(header.quant_type, header.format_version) ||
        header.num_sections == 0 ||
        (header.format_version == ESPM_LEGACY_VERSION &&
         header.num_sections > ESPM_LEGACY_MAX_SECTIONS) ||
        (header.format_version == ESPM_CURRENT_VERSION &&
         header.num_sections > ESPM_MAX_SECTIONS) ||
        !fixed_string_valid(header.model_name, sizeof(header.model_name), false) ||
        !fixed_string_valid(header.version_str, sizeof(header.version_str),
                            header.format_version == ESPM_LEGACY_VERSION) ||
        header.file_size != io->file_size ||
        header.section_table_offset != ESPM_HEADER_SIZE ||
        header.header_size != ESPM_HEADER_SIZE ||
        header.section_entry_size != ESPM_SECTION_ENTRY_SIZE ||
        header.alignment != ESPM_ALIGNMENT ||
        (header.format_version == ESPM_CURRENT_VERSION &&
         header.psram_required_kb == 0) ||
        header.flags != 0 ||
        !all_zero(header.reserved, sizeof(header.reserved))) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_model_header_t crc_header = header;
    crc_header.header_crc32 = 0;
    if (esp_model_crc32((const uint8_t *)&crc_header, sizeof(crc_header)) !=
        header.header_crc32) {
        return ESP_ERR_INVALID_CRC;
    }

    const uint64_t table_size =
        (uint64_t)header.num_sections * ESPM_SECTION_ENTRY_SIZE;
    if (table_size > SIZE_MAX ||
        header.section_table_offset > header.file_size ||
        table_size > header.file_size - header.section_table_offset) {
        return ESP_ERR_INVALID_SIZE;
    }
    error = io->read_at(io->context, header.section_table_offset,
                        out_container->sections, (size_t)table_size);
    if (error != ESP_OK) {
        return error;
    }
    if (esp_model_crc32((const uint8_t *)out_container->sections,
                        (size_t)table_size) != header.section_table_crc32) {
        return ESP_ERR_INVALID_CRC;
    }

    const uint64_t table_end = header.section_table_offset + table_size;
    if (table_end > UINT64_MAX - (ESPM_ALIGNMENT - 1U)) {
        return ESP_ERR_INVALID_SIZE;
    }
    const uint64_t first_payload =
        (table_end + ESPM_ALIGNMENT - 1U) & ~((uint64_t)ESPM_ALIGNMENT - 1U);
    uint64_t last_section_end = first_payload;
    bool has_tensor_index = false;
    bool has_tensor_data = false;
    bool has_metadata = false;
    for (uint16_t i = 0; i < header.num_sections; ++i) {
        const esp_model_section_t *section = &out_container->sections[i];
        if (!known_section_type(section->section_type, header.format_version) &&
            (header.format_version != ESPM_CURRENT_VERSION ||
             (section->flags & ESPM_SECTION_FLAG_OPTIONAL) == 0)) {
            return ESP_ERR_NOT_SUPPORTED;
        }
        if ((header.format_version == ESPM_LEGACY_VERSION && section->flags != 0) ||
            (header.format_version == ESPM_CURRENT_VERSION &&
             (section->flags & ~ESPM_SECTION_FLAG_OPTIONAL) != 0) ||
            !fixed_string_valid(section->name, sizeof(section->name), false) ||
            section->size == 0 ||
            section->offset % ESPM_ALIGNMENT != 0 ||
            section->offset < first_payload ||
            section->offset > header.file_size ||
            section->size > header.file_size - section->offset) {
            return ESP_ERR_INVALID_SIZE;
        }
        if (header.format_version == ESPM_CURRENT_VERSION &&
            ((section->section_type == SECTION_TYPE_CONFIG_JSON &&
              (section->flags & ESPM_SECTION_FLAG_OPTIONAL) == 0) ||
             ((section->section_type == SECTION_TYPE_TENSOR_INDEX ||
               section->section_type == SECTION_TYPE_TENSOR_DATA ||
               section->section_type == SECTION_TYPE_METADATA) &&
              (section->flags & ESPM_SECTION_FLAG_OPTIONAL) != 0))) {
            return ESP_ERR_INVALID_ARG;
        }
        has_tensor_index |= section->section_type == SECTION_TYPE_TENSOR_INDEX;
        has_tensor_data |= section->section_type == SECTION_TYPE_TENSOR_DATA;
        has_metadata |= section->section_type == SECTION_TYPE_METADATA;
        const uint64_t section_end = section->offset + section->size;
        if (section_end > last_section_end) {
            last_section_end = section_end;
        }
        for (uint16_t previous = 0; previous < i; ++previous) {
            const esp_model_section_t *other = &out_container->sections[previous];
            const uint64_t other_end = other->offset + other->size;
            if (section->section_type == other->section_type ||
                strcmp(section->name, other->name) == 0 ||
                (section->offset < other_end && other->offset < section_end)) {
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    if (header.format_version == ESPM_CURRENT_VERSION &&
        (!has_tensor_index || !has_tensor_data || !has_metadata)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (last_section_end != header.file_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (header.format_version == ESPM_CURRENT_VERSION) {
        const uint64_t header_psram_bytes =
            (uint64_t)header.psram_required_kb * 1024U;
        for (uint16_t i = 0; i < header.num_sections; ++i) {
            const esp_model_section_t *section = &out_container->sections[i];
            if (section->section_type == SECTION_TYPE_TENSOR_DATA &&
                header_psram_bytes < section->size) {
                return ESP_ERR_INVALID_SIZE;
            }
        }
    }

    out_container->io = *io;
    out_container->header = header;
    return ESP_OK;
}

const esp_model_section_t *esp_model_container_find_section(
    const esp_model_container_t *container, uint16_t section_type)
{
    if (!container) {
        return NULL;
    }
    for (uint16_t i = 0; i < container->header.num_sections; ++i) {
        if (container->sections[i].section_type == section_type) {
            return &container->sections[i];
        }
    }
    return NULL;
}

esp_err_t esp_model_container_verify_section(
    const esp_model_container_t *container,
    const esp_model_section_t *section)
{
    if (!container || !section || !container->io.read_at) {
        return ESP_ERR_INVALID_ARG;
    }
    bool belongs_to_container = false;
    for (uint16_t i = 0; i < container->header.num_sections; ++i) {
        if (section == &container->sections[i]) {
            belongs_to_container = true;
            break;
        }
    }
    if (!belongs_to_container) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buffer[512];
    uint64_t position = 0;
    uint32_t crc = 0xFFFFFFFFU;
    while (position < section->size) {
        const uint64_t remaining = section->size - position;
        const size_t count = remaining < sizeof(buffer)
            ? (size_t)remaining : sizeof(buffer);
        const esp_err_t error = container->io.read_at(
            container->io.context, section->offset + position, buffer, count);
        if (error != ESP_OK) {
            return error;
        }
        crc = esp_model_crc32_update(crc, buffer, count);
        position += count;
    }
    return (crc ^ 0xFFFFFFFFU) == section->crc32
        ? ESP_OK : ESP_ERR_INVALID_CRC;
}

esp_err_t esp_model_container_verify_section_buffer(
    const esp_model_section_t *section,
    const uint8_t *payload,
    size_t payload_size)
{
    if (!section || (!payload && payload_size != 0) ||
        section->size != payload_size) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_model_crc32(payload, payload_size) == section->crc32
        ? ESP_OK : ESP_ERR_INVALID_CRC;
}

static esp_err_t metadata_record_at(const esp_model_metadata_view_t *view,
                                    uint32_t wanted_index,
                                    size_t *out_record_offset,
                                    size_t *out_next_offset,
                                    const uint8_t **out_key,
                                    const metadata_record_prefix_t **out_prefix,
                                    const uint8_t **out_value)
{
    if (!view || !view->data || wanted_index >= view->record_count) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t offset = ESPM_METADATA_HEADER_SIZE;
    for (uint32_t index = 0; index <= wanted_index; ++index) {
        if (offset > view->size ||
            sizeof(metadata_record_prefix_t) > view->size - offset) {
            return ESP_ERR_INVALID_SIZE;
        }
        const size_t record_offset = offset;
        const metadata_record_prefix_t *prefix =
            (const metadata_record_prefix_t *)(view->data + offset);
        offset += sizeof(*prefix);
        const size_t key_size = prefix->key_size;
        const size_t value_size = prefix->value_size;
        if (key_size == 0 || key_size > view->size - offset) {
            return ESP_ERR_INVALID_SIZE;
        }
        const uint8_t *key = view->data + offset;
        offset += key_size;
        if (value_size > view->size - offset) {
            return ESP_ERR_INVALID_SIZE;
        }
        const uint8_t *value = view->data + offset;
        offset += value_size;
        if (index == wanted_index) {
            if (out_record_offset) *out_record_offset = record_offset;
            if (out_next_offset) *out_next_offset = offset;
            if (out_key) *out_key = key;
            if (out_prefix) *out_prefix = prefix;
            if (out_value) *out_value = value;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

static bool metadata_type_length_valid(uint8_t type, uint32_t size)
{
    switch (type) {
    case ESPM_METADATA_UINT32:
    case ESPM_METADATA_INT32:
    case ESPM_METADATA_FLOAT32:
        return size == sizeof(uint32_t);
    case ESPM_METADATA_UINT64:
        return size == sizeof(uint64_t);
    case ESPM_METADATA_BOOL:
        return size == 1;
    case ESPM_METADATA_UTF8:
    case ESPM_METADATA_BYTES:
        return true;
    default:
        return false;
    }
}

esp_err_t esp_model_metadata_open(const uint8_t *data, size_t size,
                                  esp_model_metadata_view_t *out_view)
{
    if (!data || !out_view || size < ESPM_METADATA_HEADER_SIZE ||
        size > ESPM_MAX_METADATA_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_view, 0, sizeof(*out_view));
    const uint8_t *magic = data;
    uint16_t version;
    uint16_t header_size;
    uint32_t record_count;
    uint32_t reserved;
    memcpy(&version, data + 4, sizeof(version));
    memcpy(&header_size, data + 6, sizeof(header_size));
    memcpy(&record_count, data + 8, sizeof(record_count));
    memcpy(&reserved, data + 12, sizeof(reserved));
    if (memcmp(magic, "EMET", 4) != 0 ||
        version != 1 ||
        header_size != ESPM_METADATA_HEADER_SIZE ||
        record_count > ESPM_MAX_METADATA_RECORDS ||
        reserved != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    out_view->data = data;
    out_view->size = size;
    out_view->record_count = record_count;
    size_t end_offset = ESPM_METADATA_HEADER_SIZE;
    for (uint32_t i = 0; i < record_count; ++i) {
        size_t record_offset;
        size_t next_offset;
        const uint8_t *key;
        const metadata_record_prefix_t *prefix;
        const uint8_t *value;
        const esp_err_t error = metadata_record_at(
            out_view, i, &record_offset, &next_offset, &key, &prefix, &value);
        if (error != ESP_OK || record_offset != end_offset ||
            (prefix->flags & ~ESPM_METADATA_FLAG_REQUIRED) != 0 ||
            !utf8_valid(key, prefix->key_size, false)) {
            return ESP_ERR_INVALID_ARG;
        }
        const bool known_value_type = prefix->value_type >= ESPM_METADATA_UINT32 &&
            prefix->value_type <= ESPM_METADATA_BYTES;
        if (known_value_type &&
            !metadata_type_length_valid(prefix->value_type, prefix->value_size)) {
            return ESP_ERR_INVALID_SIZE;
        }
        if (!known_value_type) {
            if (prefix->flags & ESPM_METADATA_FLAG_REQUIRED) {
                return ESP_ERR_NOT_SUPPORTED;
            }
        } else {
            switch (prefix->value_type) {
            case ESPM_METADATA_BOOL:
                if (value[0] > 1) return ESP_ERR_INVALID_ARG;
                break;
            case ESPM_METADATA_UTF8:
                if (!utf8_valid(value, prefix->value_size, false)) {
                    return ESP_ERR_INVALID_ARG;
                }
                break;
            case ESPM_METADATA_FLOAT32: {
                float number;
                memcpy(&number, value, sizeof(number));
                if (!isfinite(number)) return ESP_ERR_INVALID_ARG;
                break;
            }
            default:
                break;
            }
        }

        for (uint32_t previous = 0; previous < i; ++previous) {
            const uint8_t *previous_key;
            const metadata_record_prefix_t *previous_prefix;
            if (metadata_record_at(out_view, previous, NULL, NULL,
                                   &previous_key, &previous_prefix, NULL) != ESP_OK) {
                return ESP_ERR_INVALID_ARG;
            }
            if (previous_prefix->key_size == prefix->key_size &&
                memcmp(previous_key, key, prefix->key_size) == 0) {
                return ESP_ERR_INVALID_ARG;
            }
        }
        end_offset = next_offset;
    }
    if (end_offset != size) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

esp_err_t esp_model_metadata_find(const esp_model_metadata_view_t *view,
                                  const char *key,
                                  esp_model_metadata_value_t *out_value)
{
    if (!view || !view->data || !key || !out_value) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t key_size = strlen(key);
    if (key_size == 0 || key_size > UINT16_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    for (uint32_t i = 0; i < view->record_count; ++i) {
        const uint8_t *record_key;
        const metadata_record_prefix_t *prefix;
        const uint8_t *value;
        if (metadata_record_at(view, i, NULL, NULL, &record_key,
                               &prefix, &value) != ESP_OK) {
            return ESP_ERR_INVALID_ARG;
        }
        if (prefix->key_size != key_size ||
            memcmp(record_key, key, key_size) != 0) {
            continue;
        }
        if (!metadata_type_length_valid(prefix->value_type, prefix->value_size)) {
            return ESP_ERR_NOT_SUPPORTED;
        }
        out_value->type = (esp_model_metadata_type_t)prefix->value_type;
        out_value->value = value;
        out_value->value_size = prefix->value_size;
        out_value->flags = prefix->flags;
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t esp_model_metadata_validate_required_keys(
    const esp_model_metadata_view_t *view,
    const char *const *known_keys,
    size_t known_key_count)
{
    if (!view || !view->data || (!known_keys && known_key_count != 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    for (uint32_t i = 0; i < view->record_count; ++i) {
        const uint8_t *record_key;
        const metadata_record_prefix_t *prefix;
        if (metadata_record_at(view, i, NULL, NULL, &record_key,
                               &prefix, NULL) != ESP_OK) {
            return ESP_ERR_INVALID_ARG;
        }
        if ((prefix->flags & ESPM_METADATA_FLAG_REQUIRED) == 0) {
            continue;
        }
        bool known = false;
        for (size_t key_index = 0; key_index < known_key_count; ++key_index) {
            if (!known_keys[key_index]) {
                return ESP_ERR_INVALID_ARG;
            }
            const size_t known_size = strlen(known_keys[key_index]);
            if (known_size == prefix->key_size &&
                memcmp(record_key, known_keys[key_index], known_size) == 0) {
                known = true;
                break;
            }
        }
        if (!known) {
            return ESP_ERR_NOT_SUPPORTED;
        }
    }
    return ESP_OK;
}

esp_err_t esp_model_metadata_get_u32(const esp_model_metadata_view_t *view,
                                     const char *key, uint32_t *out_value)
{
    if (!out_value) return ESP_ERR_INVALID_ARG;
    esp_model_metadata_value_t value;
    const esp_err_t error = esp_model_metadata_find(view, key, &value);
    if (error != ESP_OK) return error;
    if (value.type != ESPM_METADATA_UINT32) return ESP_ERR_INVALID_ARG;
    memcpy(out_value, value.value, sizeof(*out_value));
    return ESP_OK;
}

esp_err_t esp_model_metadata_get_i32(const esp_model_metadata_view_t *view,
                                     const char *key, int32_t *out_value)
{
    if (!out_value) return ESP_ERR_INVALID_ARG;
    esp_model_metadata_value_t value;
    const esp_err_t error = esp_model_metadata_find(view, key, &value);
    if (error != ESP_OK) return error;
    if (value.type != ESPM_METADATA_INT32) return ESP_ERR_INVALID_ARG;
    memcpy(out_value, value.value, sizeof(*out_value));
    return ESP_OK;
}

esp_err_t esp_model_metadata_get_f32(const esp_model_metadata_view_t *view,
                                     const char *key, float *out_value)
{
    if (!out_value) return ESP_ERR_INVALID_ARG;
    esp_model_metadata_value_t value;
    const esp_err_t error = esp_model_metadata_find(view, key, &value);
    if (error != ESP_OK) return error;
    if (value.type != ESPM_METADATA_FLOAT32) return ESP_ERR_INVALID_ARG;
    memcpy(out_value, value.value, sizeof(*out_value));
    return isfinite(*out_value) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t esp_model_metadata_get_bool(const esp_model_metadata_view_t *view,
                                      const char *key, bool *out_value)
{
    if (!out_value) return ESP_ERR_INVALID_ARG;
    esp_model_metadata_value_t value;
    const esp_err_t error = esp_model_metadata_find(view, key, &value);
    if (error != ESP_OK) return error;
    if (value.type != ESPM_METADATA_BOOL || value.value[0] > 1) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_value = value.value[0] != 0;
    return ESP_OK;
}

esp_err_t esp_model_metadata_get_utf8(const esp_model_metadata_view_t *view,
                                      const char *key, const char **out_value,
                                      size_t *out_size)
{
    if (!out_value || !out_size) return ESP_ERR_INVALID_ARG;
    esp_model_metadata_value_t value;
    const esp_err_t error = esp_model_metadata_find(view, key, &value);
    if (error != ESP_OK) return error;
    if (value.type != ESPM_METADATA_UTF8) return ESP_ERR_INVALID_ARG;
    *out_value = (const char *)value.value;
    *out_size = value.value_size;
    return ESP_OK;
}

static bool ranges_overlap(uint64_t first_offset, uint64_t first_size,
                           uint64_t second_offset, uint64_t second_size)
{
    return first_offset < second_offset + second_size &&
           second_offset < first_offset + first_size;
}

esp_err_t esp_model_tensor_index_v2_validate(
    const uint8_t *index_data, size_t index_size,
    uint64_t tensor_data_size,
    esp_model_tensor_index_header_v2_t *out_header,
    const esp_model_tensor_record_v2_t **out_records)
{
    if (!index_data || !out_header || !out_records ||
        index_size < sizeof(*out_header)) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(out_header, index_data, sizeof(*out_header));
    if (memcmp(out_header->magic, ESPM_TENSOR_INDEX_MAGIC, 4) != 0 ||
        out_header->version != ESPM_TENSOR_INDEX_VERSION ||
        out_header->header_size != sizeof(*out_header) ||
        out_header->record_size != sizeof(esp_model_tensor_record_v2_t) ||
        out_header->flags != 0 ||
        out_header->tensor_count == 0 ||
        out_header->tensor_count > ESPM_MAX_TENSORS ||
        out_header->scope_count == 0 ||
        out_header->scope_count > out_header->tensor_count) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint64_t expected_size = sizeof(*out_header) +
        (uint64_t)out_header->tensor_count * sizeof(esp_model_tensor_record_v2_t);
    if (expected_size != index_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    const esp_model_tensor_record_v2_t *records =
        (const esp_model_tensor_record_v2_t *)(index_data + sizeof(*out_header));
    uint64_t total_elements = 0;
    uint64_t total_data_bytes = 0;
    bool scope_seen[ESPM_MAX_TENSORS] = {false};
    for (uint32_t i = 0; i < out_header->tensor_count; ++i) {
        const esp_model_tensor_record_v2_t *record = &records[i];
        if (!fixed_string_valid(record->name, sizeof(record->name), false) ||
            !all_zero(record->reserved, sizeof(record->reserved)) ||
            !all_zero(record->reserved_v2, sizeof(record->reserved_v2)) ||
            record->scope_id >= out_header->scope_count ||
            record->rank == 0 || record->rank > 8 ||
            record->layout != 0 ||
            (record->dtype != ESPM_TENSOR_DTYPE_INT8 &&
             record->dtype != ESPM_TENSOR_DTYPE_FP32)) {
            return ESP_ERR_INVALID_ARG;
        }
        const char *name_end = (const char *)memchr(
            record->name, '\0', sizeof(record->name));
        if (esp_model_crc32((const uint8_t *)record->name,
                            (size_t)(name_end - record->name)) != record->name_hash) {
            return ESP_ERR_INVALID_ARG;
        }
        scope_seen[record->scope_id] = true;

        uint64_t elements = 1;
        for (uint8_t axis = 0; axis < 8; ++axis) {
            if (axis < record->rank) {
                if (record->shape[axis] == 0 ||
                    elements > UINT32_MAX / record->shape[axis]) {
                    return ESP_ERR_INVALID_SIZE;
                }
                elements *= record->shape[axis];
            } else if (record->shape[axis] != 0) {
                return ESP_ERR_INVALID_ARG;
            }
        }
        if (elements != record->num_elements ||
            record->data_offset % 16U != 0 ||
            record->data_offset > tensor_data_size ||
            record->data_size > tensor_data_size - record->data_offset) {
            return ESP_ERR_INVALID_SIZE;
        }
        const uint64_t expected_data_size = elements *
            (record->dtype == ESPM_TENSOR_DTYPE_FP32 ? sizeof(float) : 1U);
        if (record->data_size != expected_data_size) {
            return ESP_ERR_INVALID_SIZE;
        }

        if (record->dtype == ESPM_TENSOR_DTYPE_FP32) {
            if (record->quant_type != ESPM_TENSOR_QUANT_NONE ||
                record->num_channels != 0 || record->channel_axis != 0xFF ||
                record->scales_offset != 0 || record->scales_size != 0) {
                return ESP_ERR_INVALID_ARG;
            }
        } else {
            if (record->quant_type != ESPM_TENSOR_QUANT_SYMMETRIC &&
                record->quant_type != ESPM_TENSOR_QUANT_SYMMETRIC_AXIS) {
                return ESP_ERR_INVALID_ARG;
            }
            if (record->num_channels == 0 ||
                record->num_channels > UINT32_MAX / sizeof(float) ||
                record->scales_size != record->num_channels * sizeof(float) ||
                record->scales_offset % sizeof(float) != 0 ||
                record->scales_offset > tensor_data_size ||
                record->scales_size > tensor_data_size - record->scales_offset) {
                return ESP_ERR_INVALID_SIZE;
            }
            if ((record->quant_type == ESPM_TENSOR_QUANT_SYMMETRIC &&
                 (record->num_channels != 1 || record->channel_axis != 0xFF)) ||
                (record->quant_type == ESPM_TENSOR_QUANT_SYMMETRIC_AXIS &&
                 (record->channel_axis >= record->rank ||
                  record->shape[record->channel_axis] != record->num_channels))) {
                return ESP_ERR_INVALID_ARG;
            }
            if (ranges_overlap(record->scales_offset, record->scales_size,
                               record->data_offset, record->data_size)) {
                return ESP_ERR_INVALID_ARG;
            }
        }

        if (total_elements > UINT64_MAX - elements ||
            total_data_bytes > UINT64_MAX - record->data_size) {
            return ESP_ERR_INVALID_SIZE;
        }
        total_elements += elements;
        total_data_bytes += record->data_size;
        for (uint32_t previous = 0; previous < i; ++previous) {
            const esp_model_tensor_record_v2_t *other = &records[previous];
            if (record->scope_id == other->scope_id &&
                strcmp(record->name, other->name) == 0) {
                return ESP_ERR_INVALID_ARG;
            }
            if (ranges_overlap(record->data_offset, record->data_size,
                               other->data_offset, other->data_size) ||
                (record->quant_type != ESPM_TENSOR_QUANT_NONE &&
                 ranges_overlap(record->scales_offset, record->scales_size,
                                other->data_offset, other->data_size)) ||
                (other->quant_type != ESPM_TENSOR_QUANT_NONE &&
                 ranges_overlap(record->data_offset, record->data_size,
                                other->scales_offset, other->scales_size)) ||
                (record->quant_type != ESPM_TENSOR_QUANT_NONE &&
                 other->quant_type != ESPM_TENSOR_QUANT_NONE &&
                 ranges_overlap(record->scales_offset, record->scales_size,
                                other->scales_offset, other->scales_size))) {
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    for (uint32_t scope = 0; scope < out_header->scope_count; ++scope) {
        if (!scope_seen[scope]) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (total_elements != out_header->total_elements ||
        total_data_bytes != out_header->total_data_bytes) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_records = records;
    return ESP_OK;
}
