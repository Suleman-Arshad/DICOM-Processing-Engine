# Architecture Design Document

## High-Performance Medical Imaging Pipeline (DICOM Processor)

**Version:** 0.3 (Week 7 — complete)
**Status:** Living document — updated at the end of each week as layers are implemented.

---

## 1. Overview & Goals

This project is a multi-threaded C++20 pipeline that ingests raw DICOM files (CT,
MRI, X-Ray), reconstructs them into a 3D voxel volume, applies SIMD-accelerated
filters, detects density anomalies via region growing, and (in a future week)
exports annotated results.

**Design goals, in priority order:**

1. **Correctness first.** Every layer's parallel variant is verified to
   produce identical (or equivalent-as-a-set) output to its serial
   reference before any performance claim is trusted.
2. **Throughput.** Multi-core parallelization (Week 7) and AVX2 SIMD
   (Week 6) target real wall-clock speedup on realistic workloads, not
   synthetic toy benchmarks.
3. **Memory discipline.** Avoid gratuitous copies between layers; prefer
   views/spans and in-place resampling where possible.
4. **Extensibility.** Each layer is a swappable unit with a clear
   serial-vs-parallel API split, so future layers can adopt the same
   `ThreadPool` without redesigning it.

---

## 2. System-Level Data Flow

```bash
 Raw .dcm files
      │
      ▼
 1. INGESTION LAYER (Week 5)           — custom tag/VR/endianness parser
      │  Slice { metadata, pixels, spatial info }
      ▼
 2. RECONSTRUCTION LAYER (Week 6-7)    — HU normalization, trilinear interpolation
      │  VoxelVolume (dense 3D grid)     serial (reconstruct) + parallel (reconstructParallel)
      ▼
 3. PROCESSING LAYER (Week 6)          — AVX2 Gaussian blur / Sobel edge / histogram eq
      │  Filtered VoxelVolume
      ▼
 4. DETECTION LAYER (Week 7)           — 3D region growing + connected component labeling
      │  vector<Anomaly>                 serial (detect) + parallel (detectParallel)
      ▼
 5. OUTPUT LAYER (planned)             — PNG / annotated DICOM / JSON reports
```

---

## 3. Layer 1 — Ingestion Layer

**Status: implemented (Week 5).**

Custom binary DICOM Part 10 parser (`src/parser.cpp`) — no DCMTK
dependency for the core parsing path. Handles Explicit/Implicit VR,
Little/Big Endian, sequence skipping, and both signed and unsigned pixel
representations (a real bug was caught here during Week 5 testing: a
CT file's signed `-2000` background-padding sentinel was decoding as
unsigned `63536` before the fix).

```cpp
struct Slice {
    Dataset metadata;
    std::vector<int32_t> pixels;   // sign-corrected per PixelRepresentation
    int rows{}, columns{};
    bool pixelRepresentationSigned{false};
    double rescaleSlope{1.0}, rescaleIntercept{0.0};
    double imagePositionZ{0.0};        // for Reconstruction Layer slice ordering
    double pixelSpacingRowMM{1.0}, pixelSpacingColMM{1.0}, sliceThicknessMM{1.0};
};
```

Unsupported: compressed pixel data (JPEG/JPEG2000/RLE transfer syntaxes)
— detected and rejected with a clear `ParseError`, by design.

---

## 4. Layer 2 — Reconstruction Layer

**Status: implemented (Week 6); parallelized (Week 7 Day 1).**

`VolumeReconstructor::reconstruct()` (serial) and `reconstructParallel()`
(thread-pool-parallelized) in `src/reconstruction.cpp`. Both share
identical setup logic (`prepare()`) and only differ in whether the final
trilinear resampling loop runs on one thread or is split into per-Z-slice
tasks across the pool.

- Sorts slices by `ImagePositionPatient`'s Z component.
- Converts each slice to HU using *its own* rescale parameters.
- Resamples to isotropic spacing via trilinear interpolation.

**Verified correct** by reconstructing a synthetic volume with a known
linear HU gradient and confirming the output matches the analytic answer
exactly — trilinear interpolation is mathematically exact on linear data.

**Verified parallel-safe**: `reconstructParallel()` produces **bit-identical**
output to `reconstruct()` across 1/2/4/8 threads (`tests/reconstruct_parallel_test.cpp`),
and is race-free under ThreadSanitizer. Each per-Z-slice task only writes
to its own disjoint region of `volume.data` and only reads the shared,
already-built input grid — no locking needed on the hot path.

---

## 5. Layer 3 — Processing Layer

**Status: SIMD filters implemented (Week 6); thread-pool parallelization
of filters not yet wired up (straightforward future work, same pattern as
Reconstruction/Detection).**

### 5.1 SIMD filters (AVX2)

| Filter | Approach | Measured speedup* |
| --- | --- | --- |
| Gaussian blur | Separable convolution; interior vectorized 8-wide via `__m256`+FMA, border pixels scalar. | ~6.1–6.4x |
| Sobel edge detection | 3×3 gradient kernels; interior vectorized, border columns scalar. | ~13–15x |
| Histogram equalization | Histogram/CDF construction is scalar (data-dependent); the remapping pass is vectorized via `_mm256_i32gather_ps` against the CDF lookup table. | ~1.05–1.09x |

\* Measured on a 256×256×32 synthetic volume at `-O2` (CMake's
`RelWithDebInfo` default). **Always benchmark the actual CMake-built
binary** — an earlier pass at this benchmark, run via ad-hoc `g++`
missing `-O2`, showed histogram equalization's AVX2 path as *16x slower*
than scalar, which looked like a hardware limitation until rebuilding
with proper optimization resolved it to the ~1.06x shown above. Documented
here as a caution, not hidden.

**Build-level AVX2 isolation:** `-mavx2 -mfma` is scoped via
`set_source_files_properties()` to exactly one file, `src/filters_avx2.cpp`
— verified by disassembling the compiled objects and confirming zero AVX2
(`ymm`-register) instructions appear anywhere outside that file. This
followed a real incident: an earlier build applied `-mavx2` at the
library level, which let the compiler auto-vectorize ordinary Ingestion
Layer loops with AVX2 instructions and crashed with `SIGILL` on non-AVX2
hardware.

**Runtime dispatch:** every filter has `<name>Scalar`, `<name>AVX2`, and
an auto-dispatching `<name>()` gated by `cpuSupportsAVX2()`
(`__builtin_cpu_supports("avx2")`). `filter_benchmark` falls back to
reporting scalar-only throughput (still genuine, useful data) on CPUs
without AVX2 rather than exiting with nothing.

---

## 6. Layer 4 — Detection Layer

**Status: implemented, both serial and parallel (Week 7 Day 2-3).**

**Design note:** "region growing from a seed within Hounsfield thresholds"
and "connected component labeling of a thresholded volume" are the same
underlying graph traversal here. A voxel within `[huMin, huMax]` is a
valid seed; flood-filling from it via 26-connectivity and marking every
visited voxel is exactly what a connected-component labeler does.
Scanning the whole volume and flood-filling from every not-yet-visited
in-range voxel therefore performs region growing from every implicit seed
*and* produces full connected-component labels in one pass.

### 6.1 Serial: `AnomalyDetector::detect()`

Single flood-fill pass over the whole volume (`src/detection.cpp`),
producing per-component running statistics (voxel count, centroid, bbox,
mean/stddev HU) accumulated during the flood fill itself — no second pass
over the volume needed.

```cpp
struct Anomaly {
    std::array<int,3> centroid, bboxMin, bboxMax;
    size_t voxelCount;
    double meanHU, stddevHU;
};

struct Thresholds {
    double huMin, huMax;
    size_t minVoxelCount = 10;   // density thresholding: reject noise
    size_t maxVoxelCount = SIZE_MAX;  // ... and implausibly large regions
};
```

**Verified correct** against synthetic volumes with planted spheres of
known HU/size/location (`tests/detection_test.cpp`): centroid matches the
planted center exactly, voxel count is within 4% of the analytic sphere
volume `(4/3)πr³` (discretization is the only source of error), mean HU
and stddev HU match exactly for a uniform-density sphere. Also verified:
uniform background produces zero anomalies; a single hot voxel is
correctly rejected by density thresholding; two well-separated spheres
are detected as two independent anomalies.

**Verified end-to-end** through the full pipeline (`anomaly_detector`
CLI): raw synthetic DICOM files → custom parser → volume reconstruction
→ detection, with a planted nodule detected at the exact expected
centroid and correct voxel count.

### 6.2 Parallel: `AnomalyDetector::detectParallel()`

Partitions the volume into contiguous Z-slabs (up to `pool.threadCount()`
slabs), flood-fills each slab independently and concurrently (each task
writes only to its own disjoint `LocalLabeling` allocation — no locking
needed), then performs a cheap serial merge: union-find across each pair
of adjacent slab boundaries, touching only the two boundary planes
(`O(sliceArea)`) rather than re-scanning the volume. This is the standard
scalable connected-component-labeling strategy (local labeling + boundary
merge), not an ad hoc simplification.

**Verified correct** (`tests/detection_parallel_test.cpp`): a volume with
a sphere deliberately placed to straddle likely slab boundaries produces
the *exact same* anomaly set as the serial reference across thread counts
1, 2, 3, 4, 7, 8, and 16 — including 16 threads on a 50-voxel-deep volume,
where slabs are only ~3 voxels thick, heavily stressing the boundary-merge
logic. Also verified race-free under ThreadSanitizer.

---

## 7. Layer 5 — Output Layer

**Status: planned**, not yet implemented. PNG slice export (libpng),
annotated DICOM re-encoding, and JSON findings reports — see the original
design in prior versions of this document for the planned approach;
unchanged by Week 7's additions.

---

## 8. Concurrency: The Thread Pool

**Status: implemented (Week 7 Day 1).** `include/dicom_processor/thread_pool.hpp`
/ `src/thread_pool.cpp` — built from `std::thread`, `std::mutex`,
`std::condition_variable`, and `std::future`/`std::packaged_task`. No
`std::async`, no third-party library.

```cpp
class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads = 0);  // 0 = auto (hardware_concurrency)
    ~ThreadPool();  // drains queued tasks, then joins

    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>;

    size_t threadCount() const;
};
```

### 8.1 Correctness verification

`tests/thread_pool_test.cpp` (8 tests, all passing):

- 100 tasks return correct individual results
- 10,000 concurrent tasks each execute exactly once under contention
- Exceptions thrown inside a task propagate correctly through `future.get()`
- One task throwing doesn't stop other tasks or the pool
- The destructor drains all queued tasks before joining, even with no
  caller ever calling `.get()`
- `threadCount()` reporting, including the auto-detect (`numThreads=0`) path

**Verified under ThreadSanitizer.** The queue/mutex/condition_variable
logic itself shows zero data races, including under the 10,000-task
stress test. TSan does flag something in the exception-propagation path
specifically — investigated via minimal isolated repros and confirmed to
be a [known, documented false positive](https://bugzilla.redhat.com/show_bug.cgi?id=1512587)
inside libstdc++'s internal `std::future`/`exception_ptr` teardown code (a
pre-built system library not compiled with `-fsanitize=thread`, so TSan
can't see the synchronization it performs internally) — not a bug in this
project's own code. Suppressed explicitly and documented in
`tests/tsan.supp`, not silently ignored.

### 8.2 Applied to real workloads (not synthetic benchmarks)

- **Reconstruction:** `reconstructParallel()` — one task per output
  Z-slice.
- **Detection:** `detectParallel()` — one task per Z-slab, with a serial
  union-find merge step afterward.

Both are verified to produce output equivalent to their serial
counterparts (bit-identical for reconstruction; identical anomaly set for
detection) across a range of thread counts, and both are race-free under
ThreadSanitizer.

### 8.3 Speedup measurement — honest status

`threadpool_benchmark` measures wall-clock speedup vs. thread count for
both Reconstruction and Detection, gated by a correctness check before
printing any number. **The development sandbox this was built in reports
only 1 CPU core** (`nproc` = 1), so no real parallel speedup could be
measured there — the tool correctly reports ~1.0x (flat, occasionally
slightly below 1.0x from thread-creation overhead) in that environment,
which is the *correct* result for zero-parallelism hardware, not a bug.
**Near-linear speedup validation on real multi-core hardware should be
run by whoever has access to such a machine** — the tool is built,
correctness-verified, and ready; only the actual multi-core measurement
is outstanding.

---

## 9. Build & Dependency Management

- **CMake ≥ 3.20**, C++20, `CMAKE_EXPORT_COMPILE_COMMANDS` on.
- **Conan 2.x** resolves `dcmtk` (optional cross-check tool only) and
  `libpng` (future Output Layer). None of the four main pipeline binaries
  (`dicom_processor`, `volume_reconstructor`, `anomaly_detector`,
  `filter_benchmark`, `threadpool_benchmark` — five, actually) require
  Conan or DCMTK at all; build with `-DBUILD_DCMTK_VERIFY_TOOL=OFF` to
  skip that dependency entirely.
- `Threads::Threads` (via `find_package(Threads REQUIRED)`) links
  `dicom_reconstruction` for the thread pool.
- AVX2 flags (`-mavx2 -mfma`) are scoped via
  `set_source_files_properties()` to exactly `src/filters_avx2.cpp` — see
  §5.1.

---

## 10. Testing Strategy

| Layer | Test approach | Status |
| --- | --- | --- |
| Ingestion | Unit tests per VR type; Explicit/Implicit, Little/Big Endian fixtures; DCMTK cross-check. | ✅ Done |
| Reconstruction | Linear-gradient analytic correctness check; bit-identical serial-vs-parallel across thread counts; TSan-verified. | ✅ Done |
| Processing | AVX2-vs-scalar diff on non-multiple-of-8 volumes (stresses boundary code); `-O2`-verified benchmark. | ✅ Done |
| Detection | Planted-sphere ground-truth tests (centroid/volume/HU accuracy); density-thresholding rejection test; boundary-straddling serial-vs-parallel equivalence across 7 thread counts; TSan-verified; full pipeline (parse→reconstruct→detect) end-to-end test. | ✅ Done |
| Concurrency (ThreadPool) | 8 correctness tests including a 10,000-task stress test; TSan-verified with documented, justified suppression for one known libstdc++ false positive. | ✅ Done |
| Output | JSON schema validation; PNG round-trip. | ⏳ Planned |

---

## 11. Known Limitations & Risks

- **Compressed pixel data** (JPEG/JPEG2000/RLE) is out of scope for the
  custom parser; detected and rejected with a clear error.
- **DCMTK licensing:** permissive, non-copyleft custom license —
  compatible with this project's use.
- **Patient data privacy:** all sample/test files must be synthetic or
  properly de-identified.
- **Slice ordering fallback:** a series missing `ImagePositionPatient` on
  every slice sorts arbitrarily (stable relative to input order) rather
  than failing.
- **Filters aren't thread-pool-parallelized yet** — only AVX2-lane
  parallel, not across cores. Straightforward future work following the
  Reconstruction/Detection pattern.
- **Region growing sensitivity:** HU tolerance and voxel-count thresholds
  are fixed constants passed by the caller; no automatic per-anatomy
  tuning.
- **Speedup numbers require multi-core hardware to measure** — see §8.3.
  The benchmark tooling is complete and correctness-verified; only the
  actual multi-core run is outstanding, pending access to such a machine.
- **Single-machine scope:** no distributed processing.

---

## 12. Week-by-Week Traceability

| Week | Layer(s) touched | Deliverable | Status |
| --- | --- | --- | --- |
| 5 | Ingestion | Custom binary parser, CT/MRI/X-Ray metadata + pixel extraction | ✅ Done |
| 6 | Reconstruction + Processing (SIMD) | Trilinear-interpolated volume reconstruction, HU normalization, AVX2 filters + benchmark | ✅ Done |
| 7 | Reconstruction (parallel) + Detection (serial + parallel) + Concurrency | Custom thread pool; region growing / connected component labeling anomaly detector; both parallelized; speedup benchmark tooling | ✅ Done (built & correctness-verified; multi-core speedup measurement pending real hardware) |
| 8 | Processing (thread-pool parallel) | Parallelize AVX2 filters across cores using the existing thread pool | ⏳ Planned |
| 9 | Output | PNG/DICOM/JSON export, end-to-end pipeline integration | ⏳ Planned |
