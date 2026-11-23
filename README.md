# ESP-IDF Model Manager

ESP-IDF Model Manager validates ESPM model containers and indexes model files
from an application-mounted filesystem. It provides a small model registry and
persists the selected model by model type in NVS.

The format API validates container structure and bounds, checks header and
section-table checksums, parses typed metadata and tensor indexes, and
provides section-payload CRC verification. Directory scans validate the
bounded container structure without reading every tensor payload. The
component does not execute models; applications remain responsible for
architecture-specific runtime support.

## ESPM format

The component supports the ESPM version 3 container format. ESPM is
architecture-neutral: runtimes define and validate the metadata and tensor
requirements for each model architecture.

See the [ESPM format specification](docs/ESPM_FORMAT.md) for the wire format,
field definitions, and validation requirements.

## Requirements and target support

- ESP-IDF 6.0.2 or later

The component manifest intentionally omits a chip-specific target list.
Component Manager interprets this as compatibility with all ESP-IDF targets.
The implementation uses generic ESP-IDF services and does not depend on a
specific inference architecture or accelerator. The full target build and
runtime matrix has not yet been validated; check filesystem and NVS behavior
on the selected target before production use.

## Add the component

Declare the component as a dependency in the application component:

```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES esp-idf-model-manager
)
```

Include `model_manager.h` to scan a mounted directory and query the model
registry. Include `esp_model_format.h` to validate an ESPM container, inspect
typed metadata, or validate its tensor index directly. Mount the filesystem
before initializing a directory scan.

## Licensing

The component is licensed under the MIT License. See [LICENSE](LICENSE).

---

## License

This project is licensed under the [PolyForm Noncommercial License 1.0.0](LICENSE).
- **Free for noncommercial use**: Personal, educational, research, and hobbyist projects are completely free.
- **Commercial use**: Not permitted without prior authorization. For commercial inquiries, please contact the author via GitHub.
