# DICOM Processor

A multi-threaded C++20 DICOM image processing pipeline: custom binary
ingestion, 3D volume reconstruction with HU normalization, AVX2-accelerated
filtering, region-growing anomaly detection, and PNG/DICOM/JSON export.

**Current status: Week 6 — 3D Volume Reconstruction and SIMD Processing.**
Ingestion, Reconstruction, and the SIMD half of Processing are implemented.
The thread-pool/concurrency half of Processing, Detection, and Output are
still ahead. See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the
full 5-layer design and the week-by-week build plan.

## Project layout

```
dicom-processor/
├── CMakeLists.txt
├── conanfile.txt
├── docs/
│   └── ARCHITECTURE.md         # full pipeline design (all 5 layers, all weeks)
├── include/dicom_processor/
│   ├── tag.hpp                 # Ingestion
│   ├── vr.hpp                  # Ingestion
│   ├── dictionary.hpp          # Ingestion
│   ├── dataset.hpp             # Ingestion
│   ├── parser.hpp              # Ingestion
│   ├── voxel_volume.hpp        # Reconstruction / Processing shared type
│   ├── reconstruction.hpp      # Reconstruction
│   └── filters.hpp             # Processing (SIMD filters)
├── src/
│   ├── vr.cpp                  # Ingestion
│   ├── dictionary.cpp          # Ingestion
│   ├── dataset.cpp             # Ingestion
│   ├── parser.cpp              # Ingestion
│   ├── main.cpp                # Ingestion CLI -> dicom_processor
│   ├── voxel_volume.cpp        # Reconstruction
│   ├── reconstruction.cpp      # Reconstruction
│   ├── reconstruct_main.cpp    # Reconstruction CLI -> volume_reconstructor
│   ├── filters_scalar.cpp      # Processing: portable reference filters
│   ├── filters_avx2.cpp        # Processing: AVX2 filters (flags scoped here only)
│   ├── filters_dispatch.cpp    # Processing: runtime AVX2 capability check
│   └── benchmark_main.cpp      # Processing CLI -> filter_benchmark
├── tools/
│   └── dcmtk_verify.cpp        # optional DCMTK-based cross-check tool
└── data/samples/                # put your test .dcm files here (not committed)
```

## Dependencies

- CMake ≥ 3.20
- A C++20 compiler with AVX2 support (GCC ≥ 11, Clang ≥ 14, or MSVC ≥ 19.29)
- [Conan](https://conan.io/) 2.x, for `DCMTK` and `libpng`
  - `DCMTK` is used **only** by the optional `dcmtk_verify` cross-check tool
    — none of the four main binaries below depend on it.
  - `libpng` is a Week 9 (Output Layer) dependency, pulled in now so it's
    already available when that layer is built.
- An AVX2-capable CPU is **not** required to build or run this project —
  see the runtime dispatch note below — but you won't see any SIMD speedup
  in `filter_benchmark` without one.

## Build

```bash
# One-time Conan profile setup, if you don't already have one
conan profile detect --force

# Resolve dependencies and generate the CMake toolchain
conan install . --output-folder=build --build=missing -s build_type=RelWithDebInfo

# Configure and build
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake
cmake --build build
```

Don't have Conan set up, or don't want the DCMTK dependency? Skip it —
none of the four main binaries need it:

```bash
cmake -S . -B build -DBUILD_DCMTK_VERIFY_TOOL=OFF
cmake --build build
```

This produces four executables in `build/`:

| Binary | Purpose | Depends on DCMTK? |
|---|---|---|
| `dicom_processor` | Week 5: parses a DICOM file, prints metadata + pixel stats | No |
| `volume_reconstructor` | Week 6: stacks a series of 2D slices into a 3D voxel volume | No |
| `filter_benchmark` | Week 6: scalar-vs-AVX2 correctness check + throughput benchmark | No |
| `dcmtk_verify` | Optional: independent cross-check of `dicom_processor`'s output against DCMTK's own reader | Yes |

## Usage

### Parse a single slice (Week 5)

```bash
./build/dicom_processor data/samples/ct_sample.dcm
```

Prints extracted metadata (Modality, Rows/Columns, BitsAllocated, Pixel
Representation, RescaleSlope/Intercept, spatial positioning) and
pixel-data statistics (raw range, HU range for CT, a small pixel-value
patch). Multiple files can be passed at once:

```bash
./build/dicom_processor data/samples/*.dcm
```

Cross-check against DCMTK on the same file:

```bash
./build/dcmtk_verify data/samples/ct_sample.dcm
```

### Reconstruct a 3D volume from a slice series (Week 6)

```bash
./build/volume_reconstructor data/samples/series/*.dcm
```

Sorts the given slices by Z position, converts every slice to Hounsfield
Units using its own rescale parameters, and trilinearly resamples to
isotropic voxel spacing. Prints the resulting volume's dimensions, voxel
spacing, and HU value range.

### Benchmark scalar vs. AVX2 filtering (Week 6)

```bash
./build/filter_benchmark
```

No DICOM files needed — this builds a synthetic 256×256×32 volume, first
verifies every AVX2 filter's output matches its scalar reference within a
small epsilon (refusing to print timings if it doesn't), then reports a
scalar-vs-AVX2 timing table for Gaussian blur, Sobel edge detection, and
histogram equalization. Exits early with a clear message on CPUs without
AVX2, rather than reporting a meaningless comparison.

### Supported vs. unsupported input

| Feature | Status |
|---|---|
| Explicit VR Little Endian | ✅ |
| Implicit VR Little Endian | ✅ |
| Explicit VR Big Endian | ✅ |
| 8-bit and 16-bit pixel data (signed and unsigned) | ✅ |
| Compressed pixel data (JPEG / JPEG2000 / RLE transfer syntaxes) | ❌ rejected with a clear `ParseError`, by design — see `ARCHITECTURE.md` §11 |
| Nested DICOM sequences (SQ) | Cursor advances past them correctly; sequence-internal field extraction not yet implemented |
| Volume reconstruction (any slice count, HU normalization, trilinear resampling) | ✅ |
| AVX2 Gaussian blur / Sobel edge / histogram equalization | ✅, with scalar fallback for non-AVX2 CPUs |
| Multi-core parallelization (thread pool) | ⏳ Week 7 |

## A note on AVX2 safety

An earlier build applied `-mavx2` at the whole-library level, which let
the compiler auto-vectorize ordinary loops with AVX2 instructions even in
code that never used an intrinsic — this crashed with `Illegal instruction
(core dumped)` on any CPU without AVX2. That's fixed now: AVX2 compile
flags are scoped via CMake's `set_source_files_properties()` to exactly
one file, `src/filters_avx2.cpp`, and every call into that file is gated
by a runtime `cpuSupportsAVX2()` check first. This was verified by
disassembling the built objects and confirming zero AVX2 instructions
appear anywhere outside that one file. If `filter_benchmark` reports "This
CPU does not support AVX2," that's the dispatch working correctly, not a
bug — you'll still get correct (scalar) results from every other binary.

## Testing without real sample data

The parser and reconstructor were validated during development against
hand-built synthetic DICOM files (single slices and multi-slice series)
covering all three supported transfer syntaxes, signed and unsigned pixel
representations, and known-correct interpolation results (a linear HU
gradient, which trilinear interpolation must reproduce exactly). If you
need to regenerate similar fixtures, construct a file with:

1. A 128-byte preamble + `"DICM"` magic
2. A File Meta Info group `(0002,xxxx)` (always Explicit VR Little Endian)
   containing at least `TransferSyntaxUID (0002,0010)`
3. A main dataset encoded per that transfer syntax, including `Modality
   (0008,0060)`, `Rows (0028,0010)`, `Columns (0028,0011)`, `BitsAllocated
   (0028,0100)`, `PixelData (7FE0,0010)`, and — for multi-slice
   reconstruction — `ImagePositionPatient (0020,0032)`, `PixelSpacing
   (0028,0030)`, and `SliceThickness (0018,0050)`

## Roadmap

See the traceability table in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md#12-week-by-week-traceability)
for what each remaining week (thread-pool concurrency, Detection, Output)
delivers.