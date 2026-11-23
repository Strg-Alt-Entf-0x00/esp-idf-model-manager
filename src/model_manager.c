/**
 * @file model_manager.c
 * @brief Generic ESPM scanner, validator, and active-model registry.
 */

#include "model_manager.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "model_manager";
static const char *NVS_NAMESPACE = "esp_models";
static char s_base_dir[MODEL_MANAGER_PATH_MAX] = "/sd/models";
static model_entry_t s_registry[MODEL_MANAGER_MAX_MODELS];
static size_t s_registry_count;
static bool s_initialized;

static esp_err_t read_file_at(void *context, uint64_t offset,
                              void *buffer, size_t size)
{
    FILE *file = context;
    if (!file || offset > (uint64_t)LONG_MAX ||
        fseek(file, (long)offset, SEEK_SET) != 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    return fread(buffer, 1, size, file) == size ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static esp_err_t validate_espm_file(const char *path, uint64_t *out_file_size,
                                    esp_model_header_t *out_header)
{
    struct stat status;
    if (stat(path, &status) != 0 || !S_ISREG(status.st_mode) ||
        status.st_size < (off_t)ESPM_HEADER_SIZE ||
        (uint64_t)status.st_size > (uint64_t)LONG_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    FILE *file = fopen(path, "rb");
    if (!file) {
        return ESP_FAIL;
    }

    const esp_model_io_t io = {
        .context = file,
        .read_at = read_file_at,
        .file_size = (uint64_t)status.st_size,
    };
    esp_model_container_t container;
    esp_err_t error = esp_model_container_open(&io, &container);
    if (error == ESP_OK) {
        *out_file_size = container.header.file_size;
        *out_header = container.header;
    }
    if (fclose(file) != 0 && error == ESP_OK) {
        error = ESP_FAIL;
    }
    return error;
}

static void nvs_key_for_type(esp_model_type_t type, char key[8])
{
    snprintf(key, 8, "t%04x", (unsigned)type);
}

static esp_err_t register_entry(const model_entry_t *entry, bool *out_registered)
{
    if (!entry || !out_registered) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_registered = false;

    for (size_t i = 0; i < s_registry_count; ++i) {
        if (strcmp(s_registry[i].name, entry->name) == 0) {
            ESP_LOGW(TAG,
                     "Skipping %s: model name %s is already registered from %s",
                     entry->file_path, entry->name, s_registry[i].file_path);
            return ESP_OK;
        }
    }
    if (s_registry_count >= MODEL_MANAGER_MAX_MODELS) {
        ESP_LOGE(TAG, "Model registry capacity exceeded (%u)",
                 (unsigned)MODEL_MANAGER_MAX_MODELS);
        return ESP_ERR_NO_MEM;
    }

    s_registry[s_registry_count] = *entry;
    nvs_handle_t handle;
    esp_err_t error = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (error == ESP_OK) {
        char saved_name[sizeof(entry->name)] = {0};
        size_t length = sizeof(saved_name);
        char key[8];
        nvs_key_for_type(entry->type, key);
        error = nvs_get_str(handle, key, saved_name, &length);
        if (error == ESP_OK && strcmp(saved_name, entry->name) == 0) {
            s_registry[s_registry_count].is_active = true;
        }
        nvs_close(handle);
    }
    ++s_registry_count;
    *out_registered = true;
    return ESP_OK;
}

static esp_err_t scan_directory(const char *directory)
{
    DIR *dir = opendir(directory);
    if (!dir) {
        ESP_LOGE(TAG, "Cannot open model directory %s: errno=%d", directory, errno);
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t result = ESP_OK;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const size_t name_length = strlen(entry->d_name);
        if (name_length <= 5 ||
            strcmp(entry->d_name + name_length - 5, ".espm") != 0) {
            continue;
        }
        char path[MODEL_MANAGER_PATH_MAX];
        const int path_length = snprintf(path, sizeof(path), "%s/%s",
                                         directory, entry->d_name);
        if (path_length <= 0 || (size_t)path_length >= sizeof(path)) {
            ESP_LOGW(TAG, "Skipping model path that exceeds the path limit");
            continue;
        }

        model_entry_t model = {0};
        esp_model_header_t header;
        const esp_err_t error = validate_espm_file(path, &model.file_size, &header);
        if (error != ESP_OK) {
            ESP_LOGW(TAG, "Skipping invalid ESPM file %s: %s",
                     path, esp_err_to_name(error));
            continue;
        }
        snprintf(model.name, sizeof(model.name), "%s", header.model_name);
        snprintf(model.file_path, sizeof(model.file_path), "%s", path);
        model.type = (esp_model_type_t)header.model_type;
        model.quant = (esp_model_quant_t)header.quant_type;
        model.psram_required_kb = header.psram_required_kb;
        model.is_espm = true;
        bool registered = false;
        result = register_entry(&model, &registered);
        if (result != ESP_OK) {
            break;
        }
        if (registered) {
            ESP_LOGI(TAG,
                     "Registered %s from %s (type=%u quant=%u size=%llu bytes)",
                     model.name, model.file_path, (unsigned)model.type,
                     (unsigned)model.quant, (unsigned long long)model.file_size);
        }
    }
    closedir(dir);
    return result;
}

static void ensure_active_fallbacks(void)
{
    for (size_t i = 0; i < s_registry_count; ++i) {
        bool type_has_active = false;
        bool earlier_same_type = false;
        for (size_t j = 0; j < s_registry_count; ++j) {
            if (s_registry[j].type != s_registry[i].type) {
                continue;
            }
            if (s_registry[j].is_active) {
                type_has_active = true;
            }
            if (j < i) {
                earlier_same_type = true;
            }
        }
        if (!type_has_active && !earlier_same_type) {
            s_registry[i].is_active = true;
        }
    }
}

esp_err_t model_manager_init(const char *base_scan_dir)
{
    if (base_scan_dir && base_scan_dir[0] != '\0') {
        const int length = snprintf(s_base_dir, sizeof(s_base_dir), "%s", base_scan_dir);
        if (length <= 0 || (size_t)length >= sizeof(s_base_dir)) {
            return ESP_ERR_INVALID_SIZE;
        }
    }
    s_registry_count = 0;
    memset(s_registry, 0, sizeof(s_registry));
    s_initialized = true;
    return ESP_OK;
}

int model_manager_scan(void)
{
    if (!s_initialized) {
        const esp_err_t init_error = model_manager_init(NULL);
        if (init_error != ESP_OK) {
            return -(int)init_error;
        }
    }
    s_registry_count = 0;
    memset(s_registry, 0, sizeof(s_registry));
    const esp_err_t error = scan_directory(s_base_dir);
    if (error != ESP_OK) {
        return -(int)error;
    }
    ensure_active_fallbacks();
    return (int)s_registry_count;
}

size_t model_manager_get_count(void)
{
    return s_registry_count;
}

const model_entry_t *model_manager_get_by_index(size_t index)
{
    return index < s_registry_count ? &s_registry[index] : NULL;
}

const model_entry_t *model_manager_find_by_name(const char *name)
{
    if (!name) {
        return NULL;
    }
    for (size_t i = 0; i < s_registry_count; ++i) {
        if (strcmp(s_registry[i].name, name) == 0) {
            return &s_registry[i];
        }
    }
    return NULL;
}

const model_entry_t *model_manager_find_by_path(const char *path)
{
    if (!path) {
        return NULL;
    }
    for (size_t i = 0; i < s_registry_count; ++i) {
        if (strcmp(s_registry[i].file_path, path) == 0) {
            return &s_registry[i];
        }
    }
    return NULL;
}

const model_entry_t *model_manager_get_active(esp_model_type_t type)
{
    const model_entry_t *first = NULL;
    for (size_t i = 0; i < s_registry_count; ++i) {
        if (s_registry[i].type != type) {
            continue;
        }
        if (s_registry[i].is_active) {
            return &s_registry[i];
        }
        if (!first) {
            first = &s_registry[i];
        }
    }
    return first;
}

esp_err_t model_manager_get_active_path(esp_model_type_t type,
                                        char *out_path, size_t max_len)
{
    if (!out_path || max_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const model_entry_t *model = model_manager_get_active(type);
    if (!model) {
        return ESP_ERR_NOT_FOUND;
    }
    const int length = snprintf(out_path, max_len, "%s", model->file_path);
    return length > 0 && (size_t)length < max_len ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_err_t model_manager_set_active(esp_model_type_t type, const char *model_name)
{
    if (!model_name) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t selected = s_registry_count;
    for (size_t i = 0; i < s_registry_count; ++i) {
        if (s_registry[i].type == type &&
            strcmp(s_registry[i].name, model_name) == 0) {
            selected = i;
            break;
        }
    }
    if (selected == s_registry_count) {
        return ESP_ERR_NOT_FOUND;
    }

    nvs_handle_t handle;
    esp_err_t error = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (error != ESP_OK) {
        return error;
    }
    char key[8];
    nvs_key_for_type(type, key);
    error = nvs_set_str(handle, key, model_name);
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    if (error != ESP_OK) {
        return error;
    }
    for (size_t i = 0; i < s_registry_count; ++i) {
        if (s_registry[i].type == type) {
            s_registry[i].is_active = i == selected;
        }
    }
    return ESP_OK;
}

void model_manager_print_list(void)
{
    printf("\n=== ESPM MODEL REGISTRY ===\n");
    if (s_registry_count == 0) {
        printf("  No valid models registered in %s\n", s_base_dir);
        printf("===========================\n\n");
        return;
    }
    printf("  %-3s | %-4s | %-5s | %-9s | %s\n",
           "ACT", "TYPE", "QUANT", "SIZE", "MODEL NAME");
    for (size_t i = 0; i < s_registry_count; ++i) {
        const model_entry_t *model = &s_registry[i];
        printf("  [%c] | %4u | %5u | %7llu KB | %s\n",
               model->is_active ? '*' : ' ',
               (unsigned)model->type, (unsigned)model->quant,
               (unsigned long long)(model->file_size / 1024U), model->name);
    }
    printf("===========================\n\n");
}
