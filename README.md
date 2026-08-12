# DICOM Processor

A multi-threaded C++20 DICOM image processing pipeline: custom binary
ingestion, 3D volume reconstruction, AVX2-accelerated filtering, a custom
thread pool, region-growing anomaly detection, and annotated
PNG/DICOM/JSON/FHIR export.

**Status: complete (Weeks 5-8).** Ingestion → Reconstruction → Processing
→ Detection → Output are all implemented, tested, and wired into one
end-to-end pipeline. See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)
for the full design and measured performance results.

## Project layout

``` bash
dicom-processor/
├── CMakeLists.txt
├── conanfile.txt
├── docs/ARCHITECTURE.md
├── include/dicom_processor/
│   ├── tag.hpp, vr.hpp, dictionary.hpp, dataset.hpp, parser.hpp   # Ingestion
│   ├── voxel_volume.hpp, reconstruction.hpp                       # Reconstruction
│   ├── thread_pool.hpp                                            # Concurrency
│   ├── detection.hpp                                               # Detection
│   ├── filters.hpp                                                 # Processing
│   ├── json_writer.hpp, findings_report.hpp,
│   │   png_writer.hpp, dicom_writer.hpp                            # Output
├── src/                        # one .cpp per header above, plus:
│   ├── main.cpp                     -> dicom_processor
│   ├── reconstruct_main.cpp         -> volume_reconstructor
│   ├── detect_main.cpp              -> anomaly_detector
│   ├── benchmark_main.cpp           -> filter_benchmark
│   ├── threadpool_benchmark_main.cpp -> threadpool_benchmark
│   └── pipeline_export.cpp          -> pipeline_export (full pipeline)
├── tests/          # thread_pool, reconstruct_parallel, detection,
│                    # detection_parallel, output -- all ctest-runnable
└── data/samples/    # your test .dcm files (not committed)
```

## Dependencies

- CMake ≥ 3.20, C++20 compiler with AVX2 support
- [Conan](https://conan.io/) 2.x for `libpng` (Output Layer PNG export)

## Build

```bash
conan profile detect --force
conan install . --output-folder=build --build=missing -s build_type=RelWithDebInfo
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

| Binary | Purpose |
| --- | --- |
| `dicom_processor` | Parse a DICOM file, print metadata + pixel stats |
| `volume_reconstructor` | Stack a slice series into a 3D volume |
| `anomaly_detector` | Full pipeline through detection, print findings |
| `filter_benchmark` | Scalar-vs-AVX2 filter correctness check + throughput |
| `threadpool_benchmark` | Thread pool speedup measurement |
| `pipeline_export` | **Full pipeline**: parse → reconstruct → detect → export PNG/DICOM/JSON/FHIR |

## Usage

```bash
# Full pipeline, one command
./build/pipeline_export --hu-min 100 --hu-max 1500 --min-voxels 10 \
    --fhir --out results data/samples/series/*.dcm
```

Produces:

- `results/slices/slice_NNN.png` — one annotated PNG per axial slice, findings outlined in red
- `results/findings.json` — structured findings report
- `results/findings_fhir.json` — HL7 FHIR Bundle (DiagnosticReport + Observations), if `--fhir` passed
- `results/annotated.dcm` — re-encoded DICOM with findings embedded in a private tag

Individual stages can also be run standalone:

```bash
./build/dicom_processor data/samples/ct.dcm
./build/volume_reconstructor data/samples/series/*.dcm
./build/anomaly_detector --hu-min 100 --hu-max 1500 data/samples/series/*.dcm
./build/filter_benchmark
./build/threadpool_benchmark
```

## Correctness verification

Every layer is checked against a known-correct reference, not just "runs
without crashing":

- **Ingestion:** validated against hand-built fixtures across all 3
  transfer syntaxes and both signed/unsigned pixel representations.
- **Reconstruction:** trilinear interpolation reproduces a known linear HU
  gradient exactly; parallel and serial paths bit-identical across thread
  counts.
- **Processing:** AVX2 output matches scalar reference to float-rounding
  precision on non-multiple-of-8 volumes.
- **Detection:** planted-sphere tests confirm exact centroid and ~96%
  voxel-count accuracy vs. analytic volume; serial and parallel paths
  produce identical results across 7 thread counts, including a sphere
  deliberately straddling slab boundaries; race-free under ThreadSanitizer.
- **Output:** JSON/FHIR validated well-formed and independently
  parseable; PNG signature-verified; annotated DICOM round-trip verified
  both against this project's own parser and against **pydicom**, an
  independent third-party library — pixel data, metadata, and the
  embedded findings all read back correctly.

## Performance: single-threaded vs. parallel throughput

Measured on real hardware (Dell Latitude E6510, 8 logical threads), via
`threadpool_benchmark`, averaged over 5 iterations per data point:

**Reconstruction** (60 slices, 256×256):

| Threads | Time (ms) | Speedup | Efficiency |
| --- | --- | --- | --- |
| 1 (serial) | 214.62 | 1.00x | 100% |
| 2 | 192.27 | 1.12x | 56% |
| 4 | 151.97 | 1.41x | 35% |
| 8 | 111.44 | 1.93x | 24% |
| 16 | 102.34 | 2.10x | 13% |

**Detection** (128³ volume, 27 planted nodules):

| Threads | Time (ms) | Speedup | Efficiency |
| --- | --- | --- | --- |
| 1 (serial) | 19.57 | 1.00x | 100% |
| 2 | 18.01 | 1.09x | 54% |
| 4 | 13.13 | 1.49x | 37% |
| 8 | 10.93 | 1.79x | 22% |
| 16 | 10.26 | 1.91x | 12% |

Speedup plateaus around 2x rather than scaling linearly with thread
count. Both workloads are memory-bandwidth-bound (scattered reads
dominate over arithmetic), so throughput is capped by how fast the CPU
can be fed data rather than by core count — expected behavior for this
class of workload, not a defect. Parallel correctness (identical output
to the serial reference) is verified independently of these numbers; see
`docs/ARCHITECTURE.md` §8 for the full discussion.

## AVX2 filter speedup

Measured via `filter_benchmark` at `-O2` on AVX2-capable hardware:

| Filter | Speedup |
| --- | --- |
| Gaussian blur | ~6.1-6.4x |
| Sobel edge detection | ~13-15x |
| Histogram equalization | ~1.05-1.09x (mostly scalar by design — see ARCHITECTURE.md) |

## Supported vs. unsupported

| Feature | Status |
| --- | --- |
| Explicit/Implicit VR, Little/Big Endian | ✅ |
| Signed and unsigned pixel data | ✅ |
| Compressed pixel data (JPEG/JPEG2000/RLE) | ❌ rejected with a clear error, by design |
| Volume reconstruction, serial + parallel | ✅ |
| AVX2 filters | ✅ |
| Region growing / CCL anomaly detection, serial + parallel | ✅ |
| PNG slice export with finding overlays | ✅ |
| Annotated DICOM export | ✅ |
| JSON findings report | ✅ |
| HL7 FHIR export | ✅ (structurally valid; not a certified clinical profile) |
| Thread-pool parallelization of filters | ⏳ Future work |

## Architecture

Full design document, per-layer implementation notes, and complete
verification history: [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).
