# ESP-IDF Model Manager and ESPM

ESP-IDF component for validating ESPM model containers and scanning a
filesystem directory into a small model registry. The format standardizes
container layout, typed metadata, and tensor storage; it does not define an
inference graph or replace model-specific runtime validation.

## Status

- Current writer format: ESPM v3.
- Readers retain ESPM v2 compatibility for migration.
- Initial runtime profiles: `moonshine.seq2seq` and `pulsevad.cnn`.
- ESPM v3 uses bounded, typed binary metadata and uncompressed tensor payloads.
- ESP32-P4 PIE can accelerate runtime numeric operations; it is not a container
  feature or a general-purpose decompressor.

The normative wire format, bounds, profile requirements, and compatibility
rules are in [ESPM_FORMAT.md](ESPM_FORMAT.md).

## Use in an ESP-IDF project

Add this component as `esp-idf-model-manager` and declare it in the application
component requirements:

```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES esp-idf-model-manager
)
```

Include `model_manager.h` to scan and query registered model files, or
`esp_model_format.h` to use the container, metadata, and tensor-index
validation APIs directly. Applications remain responsible for choosing a
supported model profile and validating runtime-specific tensor names, shapes,
and capabilities before inference.

The component manifest declares ESP-IDF 5.3 or later. The ESP32-P4
implementation and both consumer examples have been built with ESP-IDF 6.0.2;
the declared 5.3 minimum has not yet been independently verified.

## Host tests

The C parser tests use CMake and CTest and do not require ESP-IDF:

```powershell
cmake -S tests\host -B build\espm-host
cmake --build build\espm-host --config Debug
ctest --test-dir build\espm-host -C Debug --output-on-failure
```

Coverage includes container section requirements and flags, PSRAM lower
bounds, unknown section handling, and dense tensor-scope IDs.

## Release status

This checkout is a local development component and has not been published to
GitHub or the ESP Component Registry. Consumer manifests and lockfiles must
resolve the component from a published source before a distributable release.
