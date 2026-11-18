/**
 * @file model_manager.h
 * @brief Generic ESPM scanner, validator, and active-model registry.
 */

#pragma once

#include "esp_err.h"
#include "esp_model_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MODEL_MANAGER_MAX_MODELS 16
#define MODEL_MANAGER_PATH_MAX   256

typedef struct {
    char name[32];
    char file_path[MODEL_MANAGER_PATH_MAX];
    esp_model_type_t type;
    esp_model_quant_t quant;
    uint64_t file_size;
    uint32_t psram_required_kb;
    bool is_espm;
    bool is_active;
} model_entry_t;

esp_err_t model_manager_init(const char *base_scan_dir);

/**
 * @brief Scan the configured directory.
 * @return Number of valid models, or a negative ESP_ERR_* value on failure.
 */
int model_manager_scan(void);

size_t model_manager_get_count(void);
const model_entry_t *model_manager_get_by_index(size_t index);
const model_entry_t *model_manager_find_by_name(const char *name);
const model_entry_t *model_manager_find_by_path(const char *path);
const model_entry_t *model_manager_get_active(esp_model_type_t type);
esp_err_t model_manager_get_active_path(esp_model_type_t type,
                                        char *out_path, size_t max_len);
esp_err_t model_manager_set_active(esp_model_type_t type, const char *model_name);
void model_manager_print_list(void);

#ifdef __cplusplus
}
#endif
