# Architecture Design Document

## High-Performance Medical Imaging Pipeline (DICOM Processor)

**Version:** 1.0 (Week 8 — complete)

---

## 1. Overview

A multi-threaded C++20 pipeline: ingest DICOM files, reconstruct a 3D
volume, filter it with AVX2 SIMD, detect density anomalies via region
growing, and export annotated PNG/DICOM/JSON/FHIR results. Built
incrementally over 8 weeks; every layer is independently correctness-
verified before being trusted by the next.

## 2. Data Flow

```bash
Raw .dcm files
     │
     ▼
1. INGESTION        custom tag/VR/endianness parser         Slice
     ▼
2. RECONSTRUCTION   HU normalization, trilinear interp.      VoxelVolume
     ▼                (serial + thread-pool-parallel)
3. PROCESSING       AVX2 Gaussian/Sobel/histogram eq.        Filtered VoxelVolume
     ▼
4. DETECTION        region growing + connected components    vector<Anomaly>
     ▼                (serial + thread-pool-parallel)
5. OUTPUT           PNG / annotated DICOM / JSON / FHIR       files on disk
```

---

## 3. Layer 1 — Ingestion

Custom binary DICOM Part 10 parser (`src/parser.cpp`), no DCMTK
dependency. Handles Explicit/Implicit VR, Little/Big Endian, sequence
skipping, signed and unsigned pixel representations.

```cpp
struct Slice {
    Dataset metadata;
    std::vector<int32_t> pixels;   // sign-corrected
    int rows{}, columns{};
    double rescaleSlope{1.0}, rescaleIntercept{0.0};
    double imagePositionZ{0.0};
    double pixelSpacingRowMM{1.0}, pixelSpacingColMM{1.0}, sliceThicknessMM{1.0};
};
```

Unsupported: compressed pixel data (JPEG/JPEG2000/RLE) — rejected with a
clear `ParseError`, by design.

**Real bug caught and fixed during testing:** a CT file's signed `-2000`
background-padding sentinel decoded as unsigned `63536` before
`PixelRepresentation` handling was added — a physically impossible HU
value that flagged the bug immediately.

---

## 4. Layer 2 — Reconstruction

`VolumeReconstructor::reconstruct()` / `reconstructParallel()`
(`src/reconstruction.cpp`). Sorts slices by Z position, converts each to
HU using its own rescale parameters, resamples to isotropic spacing via
trilinear interpolation. The parallel path chunks resampling by output
Z-slice across the thread pool.

**Verified:** reconstructing a synthetic linear HU gradient reproduces
the analytic answer exactly. Parallel output is bit-identical to serial
across 1/2/4/8 threads and race-free under ThreadSanitizer.

---

## 5. Layer 3 — Processing

AVX2-accelerated Gaussian blur, Sobel edge detection, and histogram
equalization (`src/filters_*.cpp`), each with a scalar fallback and
runtime dispatch via `cpuSupportsAVX2()`.

| Filter | Speedup* | Notes |
| --- | --- | --- |
| Gaussian blur | ~6.1-6.4x | Separable; interior vectorized, border scalar |
| Sobel edge | ~13-15x | Interior vectorized 8-wide, border scalar |
| Histogram equalization | ~1.05-1.09x | Histogram/CDF is inherently scalar; only the remap pass (via `_mm256_i32gather_ps`) is vectorized |

\* At `-O2`. An earlier unoptimized (`g++` with no `-O` flag) benchmark
showed histogram equalization as *16x slower* under AVX2 — a real
measurement mistake, not a hardware limit, resolved by always benchmarking
the actual optimized build.

**AVX2 safety:** `-mavx2 -mfma` is scoped via
`set_source_files_properties()` to exactly `src/filters_avx2.cpp`, never
a whole library — verified by disassembling objects and confirming zero
AVX2 instructions leak elsewhere. (An earlier library-wide application of
this flag caused a `SIGILL` crash on non-AVX2 hardware.)

---

## 6. Layer 4 — Detection

`AnomalyDetector::detect()` / `detectParallel()` (`src/detection.cpp`):
26-connectivity flood fill over voxels within `[huMin, huMax]`, which
simultaneously performs region growing (every in-range voxel is an
implicit seed) and connected component labeling (one flood fill = one
component). Density thresholding rejects components outside
`[minVoxelCount, maxVoxelCount]`.

```cpp
struct Anomaly {
    std::array<int,3> centroid, bboxMin, bboxMax;
    size_t voxelCount;
    double meanHU, stddevHU;
};
```

**Parallel strategy:** partition the volume into Z-slabs, flood-fill each
independently (no locking — disjoint memory), then merge components
crossing slab boundaries via union-find, touching only the boundary
planes rather than re-scanning the volume.

**Verified:** planted-sphere tests give exact centroid match and ~96%
voxel-count accuracy vs. the analytic sphere volume. Serial and parallel
results are identical across 7 thread counts, including a sphere
deliberately placed to straddle slab boundaries at every tested thread
count. Race-free under ThreadSanitizer. Full pipeline (parse → reconstruct
→ detect) verified end-to-end on synthetic DICOM files with a planted
nodule, detected at the exact expected location.

---

## 7. Layer 5 — Output (Week 8)

Four export formats, `src/json_writer.cpp`, `src/findings_report.cpp`,
`src/png_writer.cpp`, `src/dicom_writer.cpp`:

### 7.1 PNG slice export

`PngWriter::writeSlice()` (grayscale, HU-windowed) and
`writeSliceAnnotated()` (RGB, findings drawn as red bounding-box
outlines on slices their Z-range covers). Uses libpng directly.

### 7.2 JSON findings report

Hand-rolled minimal JSON writer (`json::Writer`, proper nested comma
tracking) rather than a dependency — small enough not to warrant one.
Structured object: series info, volume dimensions, one entry per finding
(centroid, bounding box, voxel count, mean/stddev HU).

### 7.3 HL7 FHIR export (optional)

A `Bundle` of one `DiagnosticReport` plus one `Observation` per finding.
Structurally valid FHIR JSON shape; not a certified/validated clinical
profile — sufficient for EHR-integration prototyping, not for production
clinical use without further conformance work.

### 7.4 Annotated DICOM export

`DicomWriter::writeAnnotated()` re-encodes a fresh Explicit VR Little
Endian file: core geometry/identity tags rewritten from the already-
parsed (and therefore already endian/sign-corrected) `Slice` fields,
pixel data re-encoded from `slice.pixels`, plus a private tag block
(`(0009,0010)` creator, `(0009,1001)` UT) holding the findings as JSON
text. Only the tags this project parses are carried through — arbitrary
other elements from the source file are not preserved, to avoid
re-emitting raw bytes whose endianness wasn't independently re-verified.

**Verified two ways:** round-tripped through this project's own parser
(all fields match exactly, pixel data identical), and independently
validated with **pydicom** — a third-party DICOM library confirms the
file is standards-conformant, correctly decodes pixel data to the right
shape/dtype, and the center-of-nodule pixel decodes to exactly the
planted HU value.

---

## 8. Concurrency: The Thread Pool

Built from `std::thread`, `std::mutex`, `std::condition_variable`,
`std::future`/`std::packaged_task` — no `std::async`, no third-party
library (`src/thread_pool.cpp`).

**Correctness:** 8-test suite including a 10,000-task concurrency stress
test; verified race-free under ThreadSanitizer (one documented, justified
suppression for a known libstdc++ `std::future`/exception_ptr false
positive — see `tests/tsan.supp`).

**Applied to real workloads:** Reconstruction (per-Z-slice tasks) and
Detection (per-Z-slab tasks + union-find merge), both verified to produce
output equivalent to their serial counterparts.

### 8.1 Measured speedup (real hardware, Dell Latitude E6510, 8 threads)

**Reconstruction** (60 slices, 256×256), 5-iteration average:

| Threads | Time (ms) | Speedup |
| --- | --- | --- |
| 1 | 214.62 | 1.00x |
| 2 | 192.27 | 1.12x |
| 4 | 151.97 | 1.41x |
| 8 | 111.44 | 1.93x |
| 16 | 102.34 | 2.10x |

**Detection** (128³ volume, 27 planted nodules), 5-iteration average:

| Threads | Time (ms) | Speedup |
| --- | --- | --- |
| 1 | 19.57 | 1.00x |
| 2 | 18.01 | 1.09x |
| 4 | 13.13 | 1.49x |
| 8 | 10.93 | 1.79x |
| 16 | 10.26 | 1.91x |

**Interpretation:** both workloads are memory-bandwidth-bound (scattered
reads dominate arithmetic), so speedup plateaus around 2x rather than
scaling linearly with thread count — expected for this workload class on
this hardware, not a defect. Efficiency (speedup / thread count) drops
steadily past 2 threads, which is the standard signature of a
memory-bound rather than compute-bound workload. Correctness of the
parallel results was verified independently of these timing numbers —
sub-linear speedup with verified-correct output is a legitimate,
reportable engineering result, not a failure to reach a target number.

**Methodology note:** these numbers use averaged timing (5 iterations per
data point). An earlier single-shot measurement on the same hardware
showed noisier, less monotonic results (e.g. 8→16 threads improving more
than the trend suggested) — averaging is what surfaced the real,
reproducible trend shown above.

---

## 9. Build & Dependencies

CMake ≥ 3.20, C++20, Conan 2.x for `libpng` only (the sole external
dependency across the whole project — the earlier optional DCMTK
cross-check tool has been removed, since none of the actual pipeline
binaries ever depended on it). AVX2 flags scoped to exactly
`src/filters_avx2.cpp` via `set_source_files_properties()`.

---

## 10. Testing Summary

| Layer | Approach | Status |
| --- | --- | --- |
| Ingestion | VR/endianness fixtures, sign-representation bug caught and fixed | ✅ |
| Reconstruction | Analytic linear-gradient check; bit-identical parallel/serial | ✅ |
| Processing | AVX2-vs-scalar diff on non-multiple-of-8 volumes; `-O2`-verified benchmark | ✅ |
| Detection | Planted-sphere ground truth; boundary-straddling parallel equivalence; TSan | ✅ |
| Concurrency | 8-test suite incl. 10K-task stress test; TSan-verified | ✅ |
| Output | JSON/FHIR well-formedness; PNG signature; DICOM round-trip incl. third-party (pydicom) validation | ✅ |

---

## 11. Known Limitations

- Compressed pixel data (JPEG/JPEG2000/RLE) unsupported by design.
- Filters are AVX2-parallel but not yet thread-pool-parallel across cores
  (straightforward future work, same pattern as Reconstruction/Detection).
- FHIR export is structurally valid but not a certified clinical profile.
- Annotated DICOM export carries only the tags this project parses, not
  arbitrary passthrough of every original element.
- Speedup is hardware- and workload-dependent; the numbers in §8.1 are
  specific to the measured machine and will differ elsewhere, especially
  on hardware with more memory bandwidth per core.

---

## 12. Week-by-Week Summary

| Week | Delivered |
| --- | --- |
| 5 | Ingestion: custom binary parser |
| 6 | Reconstruction (trilinear interp.) + Processing (AVX2 filters) |
| 7 | Thread pool; parallel Reconstruction + Detection; region growing / CCL |
| 8 | Output: PNG, annotated DICOM, JSON, FHIR; full pipeline integration; final docs |

All eight weeks complete. Full pipeline runs end-to-end via
`pipeline_export`, verified against synthetic test data with known ground
truth at every stage.
