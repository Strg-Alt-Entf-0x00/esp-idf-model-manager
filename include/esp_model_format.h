/**
 * @file esp_model_format.h
 * @brief ESPM container and tensor-index wire format.
 *
 * All integer and floating-point fields are little-endian. Header CRC is
 * computed over the 128-byte header with header_crc32 set to zero. The
 * section-table CRC covers the serialized table; each section CRC covers its
 * exact payload bytes, excluding alignment padding.
 */

#pragma once

#include "esp_err.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_MSC_VER)
#pragma pack(push, 1)
#define ESPM_PACKED
#else
#define ESPM_PACKED __attribute__((packed))
#endif

#define ESPM_MAGIC_STR                    "ESPM"
#define ESPM_MAGIC_LE                     0x4D505345U
#define ESPM_CURRENT_VERSION              3U
#define ESPM_HEADER_SIZE                  128U
#define ESPM_MAX_SECTIONS                 16U
#define ESPM_ALIGNMENT                    64U
#define ESPM_SECTION_ENTRY_SIZE           40U
#define ESPM_TENSOR_INDEX_MAGIC           "ETIX"
#define ESPM_TENSOR_INDEX_LEGACY_VERSION  1U
#define ESPM_TENSOR_INDEX_VERSION         2U
#define ESPM_TENSOR_INDEX_LEGACY_HEADER_SIZE 52U
#define ESPM_TENSOR_INDEX_HEADER_SIZE     36U
#define ESPM_TENSOR_RECORD_LEGACY_SIZE    128U
#define ESPM_TENSOR_RECORD_SIZE           160U
#define ESPM_MAX_TENSORS                  512U
#define ESPM_MAX_METADATA_RECORDS         512U
#define ESPM_MAX_METADATA_SIZE            (64U * 1024U)
#define ESPM_SECTION_FLAG_OPTIONAL        0x0001U
#define ESPM_METADATA_FLAG_REQUIRED       0x01U

typedef enum {
    MODEL_TYPE_UNKNOWN     = 0,
    MODEL_TYPE_VAD         = 1,
    MODEL_TYPE_KWS         = 2,
    MODEL_TYPE_ASR_SEQ2SEQ = 3,
    MODEL_TYPE_TTS_VOICE   = 4,
    MODEL_TYPE_CUSTOM      = 99
} esp_model_type_t;

typedef enum {
    MODEL_QUANT_NONE        = 0,
    MODEL_QUANT_INT8        = 1,
    MODEL_QUANT_INT8_PER_CH = 2,
    MODEL_QUANT_FP16        = 3,
    MODEL_QUANT_FP32        = 4,
    MODEL_QUANT_MIXED        = 5
} esp_model_quant_t;

typedef enum {
    SECTION_TYPE_VOCAB        = 1,
    SECTION_TYPE_CONFIG_JSON  = 5,
    SECTION_TYPE_TENSOR_INDEX = 6,
    SECTION_TYPE_TENSOR_DATA  = 7,
    SECTION_TYPE_METADATA    = 8
} esp_model_section_type_t;

typedef enum {
    ESPM_TENSOR_PART_ENCODER = 1,
    ESPM_TENSOR_PART_DECODER = 2
} esp_model_tensor_part_t;

typedef enum {
    ESPM_DTYPE_INT8 = 1
} esp_model_dtype_t;

typedef struct ESPM_PACKED {
    uint16_t section_type;
    uint16_t flags;
    uint64_t offset;
    uint64_t size;
    uint32_t crc32;
    char name[16];
} esp_model_section_t;

typedef struct ESPM_PACKED {
    uint32_t magic;
    uint16_t format_version;
    uint16_t model_type;
    uint16_t quant_type;
    uint16_t num_sections;
    char model_name[32];
    char version_str[16];
    uint32_t sample_rate;
    uint32_t psram_required_kb;
    uint32_t header_crc32;
    uint64_t file_size;
    uint32_t section_table_offset;
    uint16_t header_size;
    uint16_t section_entry_size;
    uint32_t alignment;
    uint32_t flags;
    uint32_t section_table_crc32;
    uint8_t reserved[28];
} esp_model_header_t;

typedef struct ESPM_PACKED {
    char magic[4];
    uint16_t version;
    uint16_t header_size;
    uint16_t record_size;
    uint16_t reserved;
    uint32_t tensor_count;
    uint32_t vocab_size;
    uint32_t hidden_size;
    uint32_t encoder_layers;
    uint32_t decoder_layers;
    uint32_t attention_heads;
    uint32_t ffn_size;
    uint32_t encoder_tensor_count;
    uint32_t decoder_tensor_count;
    uint32_t total_params;
} esp_model_tensor_index_header_t;

typedef struct ESPM_PACKED {
    uint32_t name_hash;
    uint8_t part;
    uint8_t dtype;
    uint8_t rank;
    uint8_t quant_type;
    uint32_t num_elements;
    uint32_t shape[4];
    uint32_t num_channels;
    uint8_t channel_axis;
    uint8_t reserved[3];
    uint64_t scales_offset;
    uint32_t scales_size;
    uint64_t data_offset;
    uint64_t data_size;
    char name[64];
} esp_model_tensor_record_t;

typedef struct ESPM_PACKED {
    char magic[4];
    uint16_t version;
    uint16_t header_size;
    uint16_t record_size;
    uint16_t flags;
    uint32_t tensor_count;
    uint32_t scope_count;
    uint64_t total_elements;
    uint64_t total_data_bytes;
} esp_model_tensor_index_header_v2_t;

typedef struct ESPM_PACKED {
    uint32_t name_hash;
    uint32_t scope_id;
    uint8_t dtype;
    uint8_t rank;
    uint8_t quant_type;
    uint8_t layout;
    uint32_t num_elements;
    uint32_t shape[8];
    uint32_t num_channels;
    uint8_t channel_axis;
    uint8_t reserved[3];
    uint64_t scales_offset;
    uint32_t scales_size;
    uint64_t data_offset;
    uint64_t data_size;
    char name[64];
    uint8_t reserved_v2[12];
} esp_model_tensor_record_v2_t;

#if defined(_MSC_VER)
#pragma pack(pop)
#endif
#undef ESPM_PACKED

typedef enum {
    ESPM_TENSOR_DTYPE_INT8  = 1,
    ESPM_TENSOR_DTYPE_FP32  = 2
} esp_model_tensor_dtype_t;

typedef enum {
    ESPM_TENSOR_QUANT_NONE          = 0,
    ESPM_TENSOR_QUANT_SYMMETRIC     = 1,
    ESPM_TENSOR_QUANT_SYMMETRIC_AXIS = 2
} esp_model_tensor_quant_t;

typedef enum {
    ESPM_METADATA_UINT32 = 1,
    ESPM_METADATA_UINT64 = 2,
    ESPM_METADATA_INT32  = 3,
    ESPM_METADATA_FLOAT32 = 4,
    ESPM_METADATA_BOOL   = 5,
    ESPM_METADATA_UTF8   = 6,
    ESPM_METADATA_BYTES  = 7
} esp_model_metadata_type_t;

typedef struct {
    const uint8_t *data;
    size_t size;
    uint32_t record_count;
} esp_model_metadata_view_t;

typedef struct {
    esp_model_metadata_type_t type;
    const uint8_t *value;
    uint32_t value_size;
    uint8_t flags;
} esp_model_metadata_value_t;

typedef esp_err_t (*esp_model_read_at_fn)(void *context, uint64_t offset,
                                          void *buffer, size_t size);

typedef struct {
    void *context;
    esp_model_read_at_fn read_at;
    uint64_t file_size;
} esp_model_io_t;

typedef struct {
    esp_model_io_t io;
    esp_model_header_t header;
    esp_model_section_t sections[ESPM_MAX_SECTIONS];
} esp_model_container_t;

#if defined(__cplusplus)
static_assert(sizeof(esp_model_section_t) == ESPM_SECTION_ENTRY_SIZE,
              "Unexpected ESPM section entry size");
static_assert(sizeof(esp_model_header_t) == ESPM_HEADER_SIZE,
              "Unexpected ESPM header size");
static_assert(sizeof(esp_model_tensor_index_header_t) ==
                  ESPM_TENSOR_INDEX_LEGACY_HEADER_SIZE,
              "Unexpected ESPM tensor index header size");
static_assert(sizeof(esp_model_tensor_record_t) == ESPM_TENSOR_RECORD_LEGACY_SIZE,
              "Unexpected legacy ESPM tensor record size");
static_assert(sizeof(esp_model_tensor_index_header_v2_t) ==
                  ESPM_TENSOR_INDEX_HEADER_SIZE,
              "Unexpected ESPM v2 tensor index header size");
static_assert(sizeof(esp_model_tensor_record_v2_t) == ESPM_TENSOR_RECORD_SIZE,
              "Unexpected ESPM v2 tensor record size");
#else
_Static_assert(sizeof(esp_model_section_t) == ESPM_SECTION_ENTRY_SIZE,
               "Unexpected ESPM section entry size");
_Static_assert(sizeof(esp_model_header_t) == ESPM_HEADER_SIZE,
               "Unexpected ESPM header size");
_Static_assert(sizeof(esp_model_tensor_index_header_t) ==
                   ESPM_TENSOR_INDEX_LEGACY_HEADER_SIZE,
               "Unexpected ESPM tensor index header size");
_Static_assert(sizeof(esp_model_tensor_record_t) == ESPM_TENSOR_RECORD_LEGACY_SIZE,
               "Unexpected legacy ESPM tensor record size");
_Static_assert(sizeof(esp_model_tensor_index_header_v2_t) ==
                   ESPM_TENSOR_INDEX_HEADER_SIZE,
               "Unexpected ESPM v2 tensor index header size");
_Static_assert(sizeof(esp_model_tensor_record_v2_t) == ESPM_TENSOR_RECORD_SIZE,
               "Unexpected ESPM v2 tensor record size");
#endif

esp_err_t esp_model_container_open(const esp_model_io_t *io,
                                   esp_model_container_t *out_container);
const esp_model_section_t *esp_model_container_find_section(
    const esp_model_container_t *container, uint16_t section_type);
esp_err_t esp_model_container_verify_section(
    const esp_model_container_t *container,
    const esp_model_section_t *section);
esp_err_t esp_model_container_verify_section_buffer(
    const esp_model_section_t *section,
    const uint8_t *payload,
    size_t payload_size);

uint32_t esp_model_crc32(const uint8_t *data, size_t size);
uint32_t esp_model_crc32_update(uint32_t crc, const uint8_t *data, size_t size);

esp_err_t esp_model_metadata_open(const uint8_t *data, size_t size,
                                  esp_model_metadata_view_t *out_view);
esp_err_t esp_model_metadata_find(const esp_model_metadata_view_t *view,
                                  const char *key,
                                  esp_model_metadata_value_t *out_value);
esp_err_t esp_model_metadata_validate_required_keys(
    const esp_model_metadata_view_t *view,
    const char *const *known_keys,
    size_t known_key_count);
esp_err_t esp_model_metadata_get_u32(const esp_model_metadata_view_t *view,
                                     const char *key, uint32_t *out_value);
esp_err_t esp_model_metadata_get_i32(const esp_model_metadata_view_t *view,
                                     const char *key, int32_t *out_value);
esp_err_t esp_model_metadata_get_f32(const esp_model_metadata_view_t *view,
                                     const char *key, float *out_value);
esp_err_t esp_model_metadata_get_bool(const esp_model_metadata_view_t *view,
                                      const char *key, bool *out_value);
esp_err_t esp_model_metadata_get_utf8(const esp_model_metadata_view_t *view,
                                      const char *key, const char **out_value,
                                      size_t *out_size);

esp_err_t esp_model_tensor_index_v2_validate(
    const uint8_t *index_data, size_t index_size,
    uint64_t tensor_data_size,
    esp_model_tensor_index_header_v2_t *out_header,
    const esp_model_tensor_record_v2_t **out_records);

#ifdef __cplusplus
}
#endif
