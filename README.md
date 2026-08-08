# DICOM Processor

A multi-threaded C++20 DICOM image processing pipeline: custom binary
ingestion, 3D volume reconstruction with HU normalization, AVX2-accelerated
filtering, a custom thread pool, and region-growing anomaly detection.

**Current status: Week 7 complete.** Ingestion, Reconstruction (serial +
parallel), Processing (SIMD), Detection (serial + parallel), and the
custom thread pool are all implemented and correctness-verified. Output
(PNG/DICOM/JSON export) is the remaining layer. See
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the full design.

## Project layout

```
dicom-processor/
├── CMakeLists.txt
├── conanfile.txt
├── docs/ARCHITECTURE.md
├── include/dicom_processor/
│   ├── tag.hpp, vr.hpp, dictionary.hpp, dataset.hpp, parser.hpp   # Ingestion
│   ├── voxel_volume.hpp, reconstruction.hpp                       # Reconstruction
│   ├── thread_pool.hpp                                            # Concurrency (Week 7)
│   ├── detection.hpp                                               # Detection (Week 7)
│   └── filters.hpp                                                 # Processing
├── src/
│   ├── vr.cpp, dictionary.cpp, dataset.cpp, parser.cpp, main.cpp   # Ingestion
│   ├── voxel_volume.cpp, reconstruction.cpp, reconstruct_main.cpp  # Reconstruction
│   ├── thread_pool.cpp                                              # Concurrency
│   ├── detection.cpp, detect_main.cpp                               # Detection
│   ├── filters_scalar.cpp, filters_avx2.cpp, filters_dispatch.cpp,
│   │   benchmark_main.cpp                                           # Processing
│   └── threadpool_benchmark_main.cpp                                # Speedup benchmark
├── tests/
│   ├── thread_pool_test.cpp, reconstruct_parallel_test.cpp,
│   │   detection_test.cpp, detection_parallel_test.cpp
│   ├── tsan.supp
│   └── CMakeLists.txt
├── tools/dcmtk_verify.cpp        # optional DCMTK cross-check
└── data/samples/                  # put your test .dcm files here (not committed)
```

## Dependencies

- CMake ≥ 3.20, C++20 compiler with AVX2 support (GCC ≥ 11, Clang ≥ 14, MSVC ≥ 19.29)
- [Conan](https://conan.io/) 2.x — **only** needed for the optional
  `dcmtk_verify` tool and the future Output Layer's `libpng`. None of the
  five main pipeline binaries need it.

## Build

```bash
# Skip DCMTK entirely (recommended unless you specifically want dcmtk_verify)
cmake -S . -B build -DBUILD_DCMTK_VERIFY_TOOL=OFF
cmake --build build

# With tests
cmake -S . -B build -DBUILD_DCMTK_VERIFY_TOOL=OFF -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

| Binary | Purpose |
|---|---|
| `dicom_processor` | Week 5: parse a DICOM file, print metadata + pixel stats |
| `volume_reconstructor` | Week 6: stack a slice series into a 3D volume |
| `anomaly_detector` | Week 7: full pipeline — parse, reconstruct, detect anomalies |
| `filter_benchmark` | Week 6: scalar-vs-AVX2 filter correctness check + throughput |
| `threadpool_benchmark` | Week 7: near-linear speedup measurement (Reconstruction + Detection) |
| `dcmtk_verify` (optional) | Independent DCMTK-based cross-check |

## Usage

### Parse a single slice
```bash
./build/dicom_processor data/samples/ct_sample.dcm
```

### Reconstruct a volume from a slice series
```bash
./build/volume_reconstructor data/samples/series/*.dcm
```

### Detect anomalies end-to-end
```bash
./build/anomaly_detector --hu-min 100 --hu-max 1500 --min-voxels 10 data/samples/series/*.dcm
```
Defaults to a broad calcification/dense-nodule HU range if flags are
omitted — narrow `--hu-min`/`--hu-max` for your specific data. Prints each
finding's centroid, bounding box, voxel count, and mean/stddev HU.

### Benchmark filters (scalar vs. AVX2)
```bash
./build/filter_benchmark              # auto: AVX2 table if supported, scalar-only report otherwise
./build/filter_benchmark --scalar-only  # force scalar-only reporting on any machine
```

### Measure thread pool speedup
```bash
./build/threadpool_benchmark
```
Correctness-gated (won't print numbers if parallel output doesn't match
serial), covers both Reconstruction and Detection. **Requires a
multi-core machine for a meaningful result** — on a single-core machine
it correctly reports ~1.0x and says so explicitly, rather than faking a
number.

## Correctness verification performed

This isn't "written but untested" — every layer has been checked against
a known-correct reference:

- **Ingestion:** parser output validated against hand-built fixtures
  covering all 3 supported transfer syntaxes and both signed/unsigned
  pixel representations (a real sign-handling bug was caught and fixed
  this way).
- **Reconstruction:** trilinear interpolation reproduces a known linear
  HU gradient exactly; parallel and serial paths verified bit-identical
  across 1/2/4/8 threads.
- **Processing:** AVX2 output matches scalar reference to float-rounding
  precision on a deliberately non-multiple-of-8 volume (stresses
  boundary/tail code).
- **Detection:** planted-sphere tests confirm exact centroid, ~96% voxel-
  count accuracy vs. the analytic sphere volume, and exact mean/stddev HU;
  serial and parallel paths verified to produce identical results across
  7 thread counts on a volume deliberately built to straddle slab
  boundaries; full pipeline (parse → reconstruct → detect) verified
  end-to-end on synthetic DICOM files with a planted nodule.
- **Concurrency:** 8-test correctness suite including a 10,000-task
  stress test; verified race-free under ThreadSanitizer (one documented,
  justified false-positive suppression for a known libstdc++ limitation
  — see `tests/tsan.supp`).

## A note on AVX2 safety

`-mavx2`/`-mfma` are scoped via CMake's `set_source_files_properties()`
to exactly one file, `src/filters_avx2.cpp` — never the target/library
level. An earlier build applied AVX2 flags library-wide, which let the
compiler auto-vectorize ordinary Ingestion Layer loops with AVX2
instructions and crashed with `Illegal instruction (core dumped)` on
non-AVX2 hardware. Verified fixed by disassembling the compiled objects
and confirming zero AVX2 instructions appear anywhere outside that one
file.

## Supported vs. unsupported input

| Feature | Status |
|---|---|
| Explicit/Implicit VR, Little/Big Endian | ✅ |
| 8-bit and 16-bit pixel data (signed and unsigned) | ✅ |
| Compressed pixel data (JPEG/JPEG2000/RLE) | ❌ rejected with a clear error, by design |
| Volume reconstruction (serial + parallel) | ✅ |
| AVX2 filters (Gaussian blur, Sobel edge, histogram equalization) | ✅ |
| Region growing / connected component anomaly detection (serial + parallel) | ✅ |
| Thread-pool parallelization of filters | ⏳ Planned |
| PNG / DICOM / JSON export | ⏳ Planned |

## Roadmap

See [`docs/ARCHITECTURE.md` §12](docs/ARCHITECTURE.md#12-week-by-week-traceability).
