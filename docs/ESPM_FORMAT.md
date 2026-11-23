# ESPM model container

## Status and scope

This document specifies ESPM version 3, a self-describing, single-file
container for neural-network model metadata, tensors, vocabularies, and
optional diagnostic assets. It is designed for bounded parsing and direct use
of the stored tensor representation on resource-constrained embedded devices.

ESPM standardizes storage and tensor description. It does not imply that one
inference runtime can execute every architecture or operator. A runtime must
identify the model architecture and reject unsupported architecture versions,
tensor types, layouts, or required features before activating a model.

The current format revision is ESPM version 3. Writers MUST emit version 3.
Readers MUST validate and accept only revisions they explicitly support; this
specification describes revision 3.

The words MUST, MUST NOT, SHOULD, and MAY are used as requirements.

## Design rationale (non-normative)

ESPM adopts useful ideas from existing formats without inheriting their
runtime-specific wire conventions:

- GGUF demonstrates the value of typed metadata, alignment, and extensibility;
  ESPM does not reuse GGML tensor IDs or require a GGML runtime.
- SafeTensors demonstrates explicit tensor offsets and a non-executable
  payload. ESPM uses typed binary metadata with a fixed section-size limit
  rather than requiring embedded readers to parse a JSON tensor header.
- Hardware accelerators and filesystem behavior are runtime concerns, not
  container features. ESPM describes portable tensor types and layouts, while
  implementations choose suitable target-specific kernels.
- Version 3 deliberately leaves tensor payloads uncompressed. Any later
  tensor-compression extension must specify bounded decoding, chunking,
  random-access behavior, integrity coverage, and a fallback path.

References: [GGUF specification](https://github.com/ggml-org/ggml/blob/master/docs/gguf.md),
[SafeTensors format](https://github.com/huggingface/safetensors#format).

## File structure

All integer and floating-point values are little-endian. Packed structures
have no compiler padding. A file contains:

1. A fixed 128-byte header.
2. A section table at byte offset 128, with one 40-byte record per section.
3. Zero padding through the next 64-byte boundary.
4. Non-empty section payloads, each starting at a 64-byte-aligned file offset.

There are at most 16 sections. Sections MUST be in bounds, non-overlapping,
and unique by type and name. The last section MUST end at the declared file
size. A writer MUST zero alignment padding and reserved fields. Readers MUST
not interpret padding as payload.

### Header

| Offset | Size | Field | Version 3 meaning |
|---:|---:|---|---|
| 0 | 4 | Magic | ASCII `ESPM` |
| 4 | 2 | Format version | `3` |
| 6 | 2 | Model type | Stable model-type identifier |
| 8 | 2 | Primary quantization | `1` INT8, `2` INT8-per-axis, `4` FP32, `5` mixed |
| 10 | 2 | Section count | 1 through 16 |
| 12 | 32 | Model name | Non-empty UTF-8, NUL-terminated, zero-padded |
| 44 | 16 | Model version | Non-empty UTF-8, NUL-terminated, zero-padded |
| 60 | 4 | Sample rate | Zero if not applicable |
| 64 | 4 | Required PSRAM | Rounded-up lower bound for the uncompressed `tensor_data` payload, in KiB; non-zero |
| 68 | 4 | Header CRC-32 | CRC of all 128 bytes with this field zero |
| 72 | 8 | File size | Exact file length in bytes |
| 80 | 4 | Section-table offset | `128` |
| 84 | 2 | Header size | `128` |
| 86 | 2 | Section-entry size | `40` |
| 88 | 4 | Section alignment | `64` |
| 92 | 4 | Flags | Zero in version 3 |
| 96 | 4 | Section-table CRC-32 | CRC of all serialized section records |
| 100 | 28 | Reserved | All zero |

CRC-32 uses the reflected IEEE polynomial `0xEDB88320`, initial value
`0xFFFFFFFF`, and final XOR `0xFFFFFFFF`. CRC detects accidental corruption;
it does not authenticate a model.

The PSRAM field covers only the mandatory uncompressed tensor payload. Writers
MUST encode `ceil(tensor_data_size / 1024)`. Readers MUST reject values below
that lower bound, but MUST accept larger values for compatibility. The field
does not include indexes, vocabularies, allocator overhead, or runtime
workspaces, and MUST NOT be treated as the model's total runtime memory
requirement. A runtime MUST calculate its own additional memory needs.

The identifiers below are normative wire values. Public C enums and other
implementations MUST mirror this table; implementation headers are not the
source of wire-format assignments.

| Model type ID | Meaning |
|---:|---|
| 1 | Voice activity detection (VAD) |
| 2 | Keyword spotting (KWS) |
| 3 | ASR sequence-to-sequence |
| 4 | Text-to-speech voice |
| 99 | Custom architecture, identified by required architecture metadata |

Model type ID `0` is unknown and MUST NOT be written. Unassigned model type
IDs are reserved and MUST be rejected unless assigned by a future revision of
this specification.

| Header quantization ID | Meaning in version 3 |
|---:|---|
| 1 | INT8 per-tensor |
| 2 | INT8 per-axis |
| 4 | FP32 |
| 5 | Mixed tensor quantization or data types |

Quantization IDs `0` and `3` are not valid version 3 summaries; all other
unassigned IDs are reserved. Readers MUST reject unsupported values. The
header quantization is a summary; the tensor index is authoritative for each
tensor. A model whose tensors use more than one quantization or data type MUST
use the `mixed` summary value.

### Section records

Each 40-byte record contains:

| Field | Type |
|---|---|
| Type | `uint16` |
| Flags | `uint16` |
| File offset | `uint64` |
| Payload size | `uint64` |
| Payload CRC-32 | `uint32` |
| Name | 16-byte NUL-terminated, zero-padded UTF-8 |

The following section types are assigned in version 3:

| Type | Name | Version 3 use |
|---:|---|---|
| 1 | `vocab` | Optional tokenizer vocabulary |
| 5 | `config_json` | Optional human-readable diagnostic metadata only |
| 6 | `tensor_index` | Required generic tensor index (ETIX schema revision `2`) |
| 7 | `tensor_data` | Required tensor bytes and quantization scales |
| 8 | `metadata` | Required typed model metadata (`EMET` version 1) |

Every version 3 container MUST contain exactly one section of each required
type: `tensor_index`, `tensor_data`, and `metadata`.

Unassigned section type IDs are reserved. Readers MUST skip an unassigned
section only when it is marked optional; an unassigned required section MUST
be rejected.

Section flag bit 0 means optional. The `config_json` section MUST be marked
optional; the required `tensor_index`, `tensor_data`, and `metadata` sections
MUST NOT be marked optional. Unknown optional sections MUST be skipped.
Unknown required sections MUST be rejected. All other section flag bits are
reserved and MUST be zero. Version 3 does not define compression; a reader
MUST reject a section that requests an unsupported feature rather than
silently interpreting its bytes.

All sections MUST have a CRC covering exactly their payload. Padding is not
included. A loader MUST verify the CRC before activating a model. It SHOULD
verify large payloads during the read already needed to load them, avoiding a
second full read from SD or flash.

## Typed metadata

The metadata section begins with a 16-byte header:

| Field | Type | Value |
|---|---|---|
| Magic | 4 bytes | ASCII `EMET` |
| Metadata schema version | `uint16` | `1` |
| Header size | `uint16` | `16` |
| Record count | `uint32` | Number of following records |
| Reserved | `uint32` | Zero |

Each record starts with an 8-byte prefix followed by its key and value:

| Field | Type |
|---|---|
| Key length | `uint16` |
| Value type | `uint8` |
| Flags | `uint8` |
| Value length | `uint32` |
| Key | `key_length` UTF-8 bytes, without a terminator |
| Value | `value_length` bytes in the selected type |

Keys SHOULD be stable, dotted names such as `general.architecture` or
`general.architecture_version`. Keys MUST be non-empty, valid UTF-8, and unique
within a metadata section. Metadata values MUST fit within the section.
The complete metadata section MUST NOT exceed 64 KiB.

| Value type | ID | Encoding |
|---|---:|---|
| Unsigned 32-bit integer | 1 | Four little-endian bytes |
| Unsigned 64-bit integer | 2 | Eight little-endian bytes |
| Signed 32-bit integer | 3 | Four little-endian bytes |
| IEEE-754 float32 | 4 | Four little-endian bytes |
| Boolean | 5 | One byte, exactly 0 or 1 |
| UTF-8 string | 6 | Exact UTF-8 byte sequence, no NUL |
| Opaque bytes | 7 | Exact byte sequence |

Metadata flag bit 0 marks a record as required. Readers MUST reject unknown
required keys or value types and MAY skip unknown optional records. Other
metadata flag bits are reserved and MUST be zero. Readers MUST reject duplicate
keys, malformed UTF-8, invalid lengths, non-finite values where a model
requires a finite number, and trailing bytes after the declared records.

JSON is not required by the runtime. `config_json`, when present, is only for
inspection and diagnostics; it MUST NOT override typed metadata or tensor
records.

## Tensor index

The tensor-index section starts with a 36-byte `ETIX` header whose independent
index-version field is `2`:

| Field | Type |
|---|---|
| Magic | 4 bytes, ASCII `ETIX` |
| Index schema revision | `uint16`, value `2` |
| Header size | `uint16`, value `36` |
| Record size | `uint16`, value `160` |
| Flags | `uint16`, zero |
| Tensor count | `uint32`, non-zero and at most 512 |
| Scope count | `uint32`, non-zero and no greater than `tensor_count` |
| Total elements | `uint64`, sum of record element counts |
| Total tensor data bytes | `uint64`, sum of record `data_size` fields |

The header is followed by exactly `tensor_count` fixed 160-byte records:

| Field | Type |
|---|---|
| Name hash | `uint32`, CRC-32 of the tensor name bytes |
| Scope ID | `uint32`, less than `scope_count` |
| Data type | `uint8` |
| Rank | `uint8`, 1 through 8 |
| Quantization | `uint8` |
| Layout | `uint8`, zero for contiguous row-major |
| Element count | `uint32` |
| Shape | Eight `uint32` dimensions |
| Quantization channel count | `uint32` |
| Quantization channel axis | `uint8`, 255 for per-tensor or unquantized data |
| Reserved | Three zero bytes |
| Scales offset | `uint64`, relative to `tensor_data` |
| Scales size | `uint32` |
| Data offset | `uint64`, relative to `tensor_data` |
| Data size | `uint64` |
| Name | 64-byte NUL-terminated, zero-padded UTF-8 |
| Reserved | 12 zero bytes |

Active shape dimensions MUST be positive; unused dimensions MUST be zero; the
dimension product MUST equal `num_elements`. Records MUST be unique by
`(scope_id, name)`. Name hashes are a lookup aid only; readers MUST compare
the full name when resolving a hash collision. Every scope ID from zero to
`scope_count - 1` MUST be used by at least one tensor record.

| Data type | ID | Representation |
|---|---:|---|
| Signed INT8 | 1 | One signed byte per element |
| IEEE-754 float32 | 2 | Four little-endian bytes per element |

| Quantization | ID | Rules |
|---|---:|---|
| None | 0 | No scales; channel count zero; axis 255 |
| Symmetric INT8 per tensor | 1 | One positive finite float32 scale; axis 255 |
| Symmetric INT8 per axis | 2 | One positive finite float32 scale per element of the selected axis |

INT8 records MUST use one of the defined INT8 quantization modes. Float32
records MUST be unquantized. `data_size` MUST equal the element count times
the data type size. Scale offsets MUST be 4-byte aligned; tensor data offsets
MUST be 16-byte aligned. Scale and tensor data ranges MUST be in bounds and
MUST NOT overlap each other or any other tensor range. Gaps for alignment are
allowed. Unsupported data types, quantization modes, or layouts MUST be
rejected before the model is activated.

## Vocabulary

The optional `vocab` section is a sequence of tokens in tokenizer-ID order.
Each token is an unsigned little-endian base-128 length followed by that many
UTF-8 bytes. Zero-length tokens are allowed for unused IDs. Tokens MUST NOT
contain NUL bytes. An architecture that requires a vocabulary MUST verify that
the entry count matches its typed metadata.

## Architecture profiles

Every model MUST provide `general.architecture` (UTF-8) and
`general.architecture_version` (uint32) metadata. A runtime profile defines
the additional required metadata, tensor names, shapes, and capabilities for
its architecture. Those profile requirements are independent of this
container specification and MUST be validated by the runtime before model
activation.

This separation allows one container and tensor representation to support
different model architectures without prescribing execution semantics or
requiring a generic graph interpreter.

## Validation and compatibility requirements

Readers MUST validate all counts, arithmetic, offsets, ranges, names, flags,
versions, and checksums before publishing a model handle. Arithmetic used for
sizes and offsets MUST be overflow-checked. A failed model load MUST leave the
currently active model unchanged. Unknown required features and malformed
files MUST fail closed; they MUST NOT be partially loaded.

Writers MUST emit ESPM format version 3. Readers MUST reject unsupported
container revisions, malformed fields, and unsupported required features.
Model-directory scans may validate the bounded container header and section
table without reading every large tensor payload; the model loader remains
responsible for verifying payload checksums before activation.

CRC-32 protects against accidental corruption, not malicious replacement.
Authenticity, signing, compression, sharding, and execution-graph semantics
are outside this revision and require separately specified features.
