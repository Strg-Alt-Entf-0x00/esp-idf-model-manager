cmake_minimum_required(VERSION 3.20)
project(esp_model_format_host_tests C)

enable_testing()

set(MODEL_MANAGER_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

add_executable(esp_model_format_host_tests
    test_esp_model_format.c
    "${MODEL_MANAGER_ROOT}/src/esp_model_format.c"
)

target_include_directories(esp_model_format_host_tests BEFORE PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/include"
    "${MODEL_MANAGER_ROOT}/include"
)
target_compile_features(esp_model_format_host_tests PRIVATE c_std_11)

add_test(NAME esp_model_format_host_tests COMMAND esp_model_format_host_tests)
