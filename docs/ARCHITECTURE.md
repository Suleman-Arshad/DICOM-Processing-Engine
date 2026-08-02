# Architecture Design Document
## High-Performance Medical Imaging Pipeline (DICOM Processor)

**Version:** 0.2 (Week 6 baseline)
**Status:** Living document — updated at the end of each week as layers are implemented.

---

## 1. Overview & Goals

This project is a multi-threaded C++20 pipeline that ingests raw DICOM files (CT,
MRI, X-Ray), reconstructs them into a 3D voxel volume, applies SIMD-accelerated
filters, detects density anomalies via region growing, and exports annotated
results.

**Design goals, in priority order:**

1. **Correctness first.** Medical pixel data is meaningless if the parser
   mishandles endianness, VR, or HU rescaling — silent corruption is worse
   than a crash.
2. **Throughput.** Target: process a 512×512×300-slice CT series (~150M
   voxels) through the full pipeline (ingest → reconstruct → filter →
   detect → export) in well under a minute on a modern 8-core desktop.
3. **Memory discipline.** A single full-body CT volume at 16-bit depth is
   ~300 MB raw; the pipeline should avoid gratuitous copies (prefer views/
   spans over new allocations between layers).
4. **Extensibility.** Each layer is a swappable unit — e.g., the detection
   layer's region-growing algorithm should be replaceable later without
   touching ingestion or reconstruction code.

**Why C++20 + AVX2 instead of Python/NumPy/SimpleITK:** this is explicitly an
internship exercise in systems-level performance engineering — hand-rolled
parsing, manual SIMD, and custom thread pooling are the point, not just the
means to an end. Production DICOM work in industry would typically lean on
DCMTK/ITK far more heavily than this project does by design.

---

## 2. System-Level Data Flow

```
 ┌─────────────────┐
 │  Raw .dcm files │  (CT / MRI / X-Ray, one file per slice)
 └────────┬────────┘
          │  bytes
          ▼
 ┌─────────────────────────────┐
 │  1. INGESTION LAYER         │
 │  - Tag/VR/endianness parser │
 │  - Metadata extraction      │
 │  - Pixel data extraction    │
 └────────┬────────────────────┘
          │  DicomSlice { metadata, pixel buffer }
          ▼
 ┌─────────────────────────────┐
 │  2. RECONSTRUCTION LAYER    │
 │  - Slice ordering/validation│
 │  - HU normalization         │
 │  - Trilinear interpolation  │
 │    (anisotropic → isotropic)│
 └────────┬────────────────────┘
          │  VoxelVolume (dense 3D float grid)
          ▼
 ┌─────────────────────────────┐
 │  3. PROCESSING LAYER        │
 │  - Thread pool (task queue) │
 │  - AVX2 Gaussian blur       │
 │  - AVX2 edge detection      │
 │  - Histogram equalization   │
 └────────┬────────────────────┘
          │  Filtered VoxelVolume
          ▼
 ┌──────────────────────────────────┐
 │  4. DETECTION LAYER              │
 │  - Seed selection (HU threshold) │
 │  - 3D region growing (26-conn.)  │
 │  - Candidate scoring/filtering   │
 └────────┬─────────────────────────┘
          │  vector<Anomaly> { centroid, bbox, volume, mean HU }
          ▼
 ┌─────────────────────────────┐
 │  5. OUTPUT LAYER            │
 │  - PNG slice export (libpng)│
 │  - Annotated DICOM re-encode│
 │  - JSON findings report     │
 └─────────────────────────────┘
```

Each arrow is a deliberate ownership boundary: a layer consumes the previous
layer's output type and produces the next layer's input type. No layer reaches
"backward" into an earlier layer's internal state.

---

## 3. Layer 1 — Ingestion Layer

**Scope:** custom binary DICOM parser (not a DCMTK passthrough — DCMTK is used
in Week 5 only to *verify* our own parser's output against a trusted
reference).

### 3.1 File structure assumptions

A DICOM file (Part 10 format) consists of:

```
[128-byte preamble] [ "DICM" magic ] [ File Meta Info group (0002,xxxx) ]
[ Data Set: sequence of (Tag, VR, Length, Value) elements ]
```

### 3.2 Core data structures

```cpp
struct DicomTag {
    uint16_t group;
    uint16_t element;
    bool operator==(const DicomTag&) const = default;
};

enum class VR {
    AE, AS, AT, CS, DA, DS, DT, FL, FD, IS, LO, LT,
    OB, OD, OF, OL, OW, PN, SH, SL, SQ, SS, ST, TM,
    UC, UI, UL, UN, UR, US, UT,
    Implicit  // used when transfer syntax is Implicit VR Little Endian
};

struct DicomElement {
    DicomTag tag;
    VR vr;
    uint32_t length;
    std::vector<std::byte> rawValue;   // owns the bytes; typed accessors decode lazily
};

class DicomDataset {
public:
    std::optional<std::string_view> getString(DicomTag tag) const;
    std::optional<int32_t>          getInt(DicomTag tag) const;
    std::optional<double>           getDouble(DicomTag tag) const;
    const DicomElement*             getRaw(DicomTag tag) const;   // for PixelData etc.

private:
    std::unordered_map<DicomTag, DicomElement> elements_;
};

struct DicomSlice {  // as-built: dicom::Slice in include/dicom_processor/parser.hpp
    DicomDataset metadata;
    std::vector<int32_t> pixels;        // decoded, sign-corrected per PixelRepresentation
    int rows{};
    int columns{};
    bool pixelRepresentationSigned{false};
    double rescaleSlope{1.0};
    double rescaleIntercept{0.0};
    // Added in Week 6 for the Reconstruction Layer:
    double imagePositionZ{0.0};         // (0020,0032) 3rd component, mm
    double pixelSpacingRowMM{1.0};      // (0028,0030) 1st value, mm
    double pixelSpacingColMM{1.0};      // (0028,0030) 2nd value, mm
    double sliceThicknessMM{1.0};       // (0018,0050), mm
};
```

### 3.3 Parsing responsibilities

| Concern | Handling strategy |
|---|---|
| Transfer syntax detection | Read `(0002,0010) TransferSyntaxUID` from File Meta Info (always Explicit VR Little Endian) before parsing the main dataset, since it dictates how the rest of the file is read. |
| Explicit vs. Implicit VR | Explicit VR elements carry a 2-byte VR code in the stream; Implicit VR elements do not and require a static tag→VR dictionary lookup instead. |
| Endianness | Little Endian is the overwhelming default; Big Endian Explicit VR is legacy but must be supported per DICOM PS3.5 — byte-swap on read based on the transfer syntax UID. |
| Long-form VR lengths | `OB, OW, OF, SQ, UT, UN` use a 4-byte length field (with 2 reserved bytes first); all others use 2-byte length. Getting this wrong misaligns every subsequent tag in the file. |
| Sequences (SQ) | Recursive: a sequence contains items, each item is itself a nested dataset. Parsed depth-first. |
| Pixel Data (7FE0,0010) | For uncompressed transfer syntaxes, raw pixel bytes; length may be `0xFFFFFFFF` (undefined length) for encapsulated/compressed pixel data using Basic Offset Table + fragments — out of scope for Week 5, detected and reported as "unsupported" rather than silently mishandled. |
| Pixel Representation (0028,0103) | Determines whether pixel bytes are signed (two's complement) or unsigned. Getting this wrong doesn't crash anything — it silently produces wrong numbers: a real-world bug caught during testing had a CT file's signed `-2000` background-padding sentinel decode as unsigned `63536`, propagating into a physically impossible HU value. Fixed by reinterpreting the raw bit pattern per this tag rather than assuming unsigned. |

### 3.4 Validation strategy for Week 5

Every sample file (one CT, one MRI, one X-Ray minimum) is run through both:

1. our custom parser, and
2. DCMTK's `DcmFileFormat::loadFile`,

and the extracted `Modality`, `Rows`, `Columns`, `BitsAllocated`,
`PixelData` length, and pixel checksum are diff'd. Divergence here is a
parser bug, full stop — DCMTK is the ground truth for this comparison only.

---

## 4. Layer 2 — Reconstruction Layer

**Status: implemented (Week 6).** `VolumeReconstructor::reconstruct()` in
`src/reconstruction.cpp`, backed by the `VoxelVolume` type in
`include/dicom_processor/voxel_volume.hpp`.

**Input:** `std::vector<Slice>` (one series, unordered; `Slice` is the
Week 5 parser's output type, extended in Week 6 with `imagePositionZ`,
`pixelSpacingRowMM`/`pixelSpacingColMM`, and `sliceThicknessMM`).
**Output:** `VoxelVolume` (dense 3D grid, isotropic spacing).

### 4.1 Slice ordering

Slices are sorted by `ImagePositionPatient`'s Z component (0020,0032,
third value) — *not* by filename or `InstanceNumber`, which can be
unreliable or absent. Files missing this tag default to Z=0mm; a series
where every slice is missing it will sort arbitrarily (stable relative to
input order), which is a known limitation worth flagging if it ever
matters for a specific dataset — see §11.

### 4.2 Hounsfield Unit normalization

For CT series, each slice's raw pixel values are converted using *that
slice's own* rescale parameters (slices in a series can legitimately carry
different `RescaleSlope`/`RescaleIntercept`):

```
HU = pixelValue * RescaleSlope + RescaleIntercept
```

MRI/X-Ray series lack a standardized HU scale; those modalities pass
through with whatever rescale the file provides (typically slope=1,
intercept=0, i.e. a no-op), with the actual modality tracked in
`VoxelVolume::modality`.

### 4.3 Trilinear interpolation

Slice thickness and pixel spacing are rarely isotropic (e.g. 1mm × 1mm
in-plane, 2mm between slices). To build a volume with uniform voxel
spacing, the reconstructor:

1. Builds an intermediate anisotropic grid at native resolution
   (`rows × columns × sliceCount`), with every voxel already converted to
   HU per §4.2.
2. Picks a target isotropic spacing — by default, the smallest of the
   three native spacing dimensions, so reconstruction never upsamples an
   axis beyond what the source data actually resolves. Callers can
   override this via `reconstruct(slices, targetSpacingMM)`.
3. For each output voxel, maps back to fractional coordinates in the
   native grid and trilinearly blends the 8 nearest native voxels,
   clamping at the grid's edges.

```cpp
struct VoxelVolume {
    int width, height, depth;
    double voxelSpacingMM;         // isotropic, post-interpolation
    Modality modality;
    std::vector<float> data;       // width*height*depth, row-major

    float at(int x, int y, int z) const {
        return data[(z * height + y) * width + x];
    }
};
```

**Correctness check performed:** trilinear interpolation is mathematically
exact on linear data (a fundamental property of linear interpolation), so
the implementation was verified by reconstructing a synthetic volume with
a known linear HU gradient and confirming the output matched the analytic
answer exactly (within float rounding) at every voxel — not just spot-
checked informally.

Interpolation is embarrassingly parallel per output voxel and remains a
strong candidate for thread-pool parallelization once the Week 7
concurrency layer lands (see §12).

---

## 5. Layer 3 — Processing Layer

**Status: SIMD filters implemented (Week 6); thread pool not yet built
(Week 7).** Implementations in `src/filters_scalar.cpp` (portable
reference), `src/filters_avx2.cpp` (AVX2-accelerated), and
`src/filters_dispatch.cpp` (runtime CPU-capability dispatch). Public
interface: `include/dicom_processor/filters.hpp`.

### 5.1 Thread pool — planned, Week 7

Not yet implemented. Filters currently run single-threaded (parallelized
only via AVX2 lanes, not across CPU cores); the thread pool design below
is retained as the Week 7 target:

```cpp
class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads = std::thread::hardware_concurrency());
    ~ThreadPool();

    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>;

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queueMutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_{false};
};
```

Volume-processing tasks will be chunked along the Z axis (one task per N
slices) so each worker operates on a contiguous, cache-friendly memory
region with no false sharing between threads.

### 5.2 SIMD filters (AVX2) — implemented, Week 6

| Filter | Approach | Measured speedup* |
|---|---|---|
| Gaussian blur | Separable convolution (horizontal then vertical pass). Interior columns/rows vectorized 8-wide via `__m256` + `_mm256_fmadd_ps`; border pixels (where clamped neighbors would need per-lane divergent indices) fall back to scalar. | ~6.1–6.4x |
| Sobel edge detection | 3×3 gradient kernels. Interior pixels vectorized (row above/current/below loaded as three `__m256` triples, shifted by -1/0/+1 in X); left/right border columns computed scalar. Magnitude via `_mm256_sqrt_ps`. | ~13–15x |
| Histogram equalization | Histogram bucket accumulation and CDF construction are scalar (data-dependent, would need cross-lane conflict resolution to vectorize safely — not attempted). The remapping pass is vectorized: `_mm256_cvttps_epi32` computes each voxel's bin index, `_mm256_i32gather_ps` looks up the corresponding CDF value, then `_mm256_fmadd_ps` rescales it back to the original value range. | ~1.05–1.09x |

\* From `filter_benchmark` on a 256×256×32 synthetic volume, 5-iteration
average, this development machine, **built at `-O2`** (CMake's default
`RelWithDebInfo` build type). Actual speedup on your hardware will vary —
the histogram equalization figure in particular is expected to stay
modest anywhere, since roughly half its work (histogram + CDF) is
inherently scalar by design.

**A measurement mistake worth documenting, not hiding:** an earlier pass at
this benchmark was run via ad-hoc `g++` invocations that omitted `-O2`
entirely. Under that unoptimized build, histogram equalization's AVX2 path
measured as *16x slower* than scalar (gather-heavy code is disproportionately
punished by missing optimization) — a result that looked like a genuine
hardware limitation until compiling the identical source with `-O2`
resolved it to the ~1.06x shown above, consistent across repeated runs.
The lesson: **always benchmark a binary built the same way it will actually
ship** (i.e. via `cmake --build build`, not a hand-typed `g++` command
missing flags CMake would have supplied) — an unoptimized build doesn't
just run everything uniformly slower, it can distort *relative* comparisons
between code paths unpredictably enough to produce a wrong conclusion.

**Runtime AVX2 dispatch:** every filter has three entry points —
`<name>Scalar`, `<name>AVX2`, and a plain `<name>()` that auto-dispatches
based on `cpuSupportsAVX2()` (via `__builtin_cpu_supports("avx2")`).
Production code should call the auto-dispatching form; the explicit
Scalar/AVX2 variants exist so the benchmark tool can force either path
deliberately and so tests can verify they agree.

**Build-level AVX2 isolation (important — this is a lesson learned, not
just a design choice):** an earlier iteration of this project applied
`-mavx2` at the *library* level to the Ingestion Layer, which let the
compiler auto-vectorize ordinary loops with AVX2 instructions even though
that code never touched an intrinsic — this crashed with `SIGILL` on
non-AVX2 hardware despite having a "runtime check" elsewhere in the
codebase, because the crashing code wasn't gated by that check at all.
The fix, applied here: `-mavx2 -mfma` is scoped via CMake's
`set_source_files_properties()` to exactly one file,
`src/filters_avx2.cpp` — verified by disassembling the compiled objects
and confirming zero AVX2 (`ymm`-register) instructions appear anywhere
outside that one file.

**Correctness gate:** `filter_benchmark` refuses to report any timing
numbers unless AVX2 output first matches the scalar reference within a
small epsilon (`1e-1`, chosen for float32 accumulation-order tolerance
across different summation orders) — a fast-but-wrong implementation is
worse than a slow-but-right one, and it shouldn't be possible to
accidentally ship a benchmark result for the former.

---

## 6. Layer 4 — Detection Layer

**Goal:** flag candidate nodules/calcifications as 3D connected regions of
anomalous density.

### 6.1 Seed selection

Voxels whose HU value falls in a suspicious range (e.g., 100–400 HU for
calcifications, tunable per use case) and that are local maxima within a
small neighborhood become seed candidates.

### 6.2 Region growing

26-connectivity (full 3×3×3 neighborhood minus center) flood-fill from each
seed, accepting neighbors within a tolerance band of the seed's HU value:

```cpp
struct Anomaly {
    std::array<int,3> centroid;
    std::array<int,3> bboxMin, bboxMax;
    size_t voxelCount;
    double meanHU;
    double stddevHU;
};

std::vector<Anomaly> regionGrow(const VoxelVolume& volume,
                                 const std::vector<Seed>& seeds,
                                 double huTolerance);
```

Visited voxels are tracked in a `std::vector<bool>` (or bitset) sized to the
volume to avoid revisiting; each region-growing call from an unvisited seed
runs independently, making this parallelizable across the thread pool with
one caveat: two seeds must not race on the shared visited-mask, so seeds are
partitioned into non-overlapping bounding regions before dispatch, or a
mutex-guarded mask is used for the (expected rare) boundary cases.

### 6.3 Post-filtering

Regions below a minimum voxel count (noise) or exceeding a maximum size
(likely a segmentation error, e.g. leaking into surrounding tissue) are
discarded before being reported as findings.

---

## 7. Layer 5 — Output Layer

| Output | Library/approach |
|---|---|
| PNG slice export | libpng, one 2D slice (or a chosen orthogonal plane) at a time, with detected anomalies overlaid as bounding boxes/contours. |
| Annotated DICOM | Re-encode the original `DicomDataset` with a private tag block or a Structured Report (SR) referencing the findings, preserving original pixel data. |
| JSON findings report | One `Anomaly` → one JSON object: `centroid`, `bbox`, `voxelCount`, `meanHU`, `stddevHU`, plus series-level metadata (`PatientID`, `StudyDate`, `Modality`). |

Example JSON shape:

```json
{
  "patientId": "ANON001",
  "modality": "CT",
  "studyDate": "20240115",
  "findings": [
    {
      "id": 1,
      "centroid": [154, 201, 88],
      "boundingBox": { "min": [148, 195, 84], "max": [160, 207, 92] },
      "voxelCount": 342,
      "meanHU": 187.4,
      "stddevHU": 22.1
    }
  ]
}
```

---

## 8. Concurrency Model Summary

- **Ingestion:** one file parsed per thread-pool task; independent, no shared
  mutable state (each `DicomSlice` is owned by its own task until returned).
- **Reconstruction:** interpolation parallelized by output-voxel-range chunks.
- **Processing:** SIMD filters parallelized by Z-slice range.
- **Detection:** region growing parallelized by seed partition, guarded
  visited-mask for boundary overlap.
- **Output:** export tasks (per-slice PNG, JSON serialization) are
  independent and trivially parallel.

`std::atomic<size_t>` counters track pipeline progress for logging;
`std::condition_variable` coordinates the thread pool's task queue; no
layer holds a lock for longer than a queue push/pop.

---

## 9. Build & Dependency Management

- **CMake ≥ 3.20**, C++20, `CMAKE_EXPORT_COMPILE_COMMANDS` on for tooling.
- **Conan 2.x** resolves `dcmtk`, `libpng`, `zlib` via `CMakeDeps` +
  `CMakeToolchain` generators — see `conanfile.txt`.
- AVX2 flags (`-mavx2 -mfma` / `/arch:AVX2`) applied at the target level, not
  globally, so future non-SIMD targets (e.g. a test runner) aren't forced to
  assume AVX2 hardware.
- DCMTK is used in two distinct roles across the project: (a) Week 5
  ground-truth verification of our custom parser, and (b) optionally, DICOM
  re-encoding in the Output Layer, since re-implementing a spec-correct
  writer is lower value than a spec-correct reader for this project's goals.

---

## 10. Testing Strategy

| Layer | Test approach | Status |
|---|---|---|
| Ingestion | Unit tests per VR type; round-trip byte fixtures for Explicit/Implicit and Little/Big Endian; parser-vs-DCMTK diff on real sample files. | ✅ Done (Week 5) |
| Reconstruction | Known-input synthetic volumes (linear HU gradient) to verify interpolation math analytically — output must equal the analytic answer exactly, not just look reasonable. | ✅ Done (Week 6) |
| Processing | AVX2 filter output compared against scalar reference on a non-multiple-of-8 volume (stresses boundary/tail code paths), within float epsilon. `filter_benchmark` refuses to report timings if this check fails. | ✅ Done (Week 6) |
| Detection | Synthetic volumes with planted spheres of known HU/size to verify recall and bounding-box accuracy. | ⏳ Planned |
| Output | JSON schema validation; PNG round-trip (write then re-read, compare pixels). | ⏳ Planned |

Sample DICOM corpus: synthetic/de-identified test files only (e.g., public
datasets from TCIA or DICOM library test suites) — no real patient data
under any circumstance.

---

## 11. Known Limitations & Risks

- **Compressed pixel data** (JPEG/JPEG2000/RLE transfer syntaxes) is out of
  scope for the custom parser; such files are detected and rejected with a
  clear error rather than silently mis-decoded.
- **DCMTK licensing:** DCMTK uses a permissive, non-copyleft custom license
  (not GPL) — compatible with this project's use, but worth citing explicitly
  in any downstream distribution.
- **Patient data privacy:** all sample/test files must be synthetic or
  properly de-identified; this is a hard project constraint, not a
  nice-to-have.
- **Slice ordering fallback:** a series where every slice is missing
  `ImagePositionPatient` sorts arbitrarily (stable relative to input file
  order) rather than failing outright — acceptable for now since real CT/MR
  series reliably include this tag, but worth revisiting if a sample series
  ever lacks it.
- **Single-threaded filters (for now):** AVX2 gives per-core throughput,
  but nothing yet parallelizes across cores — the Week 7 thread pool is
  what unlocks that; see §5.1.
- **Region growing sensitivity:** HU tolerance and minimum-size thresholds
  are currently fixed constants; tuning per modality/anatomy is a known
  future improvement, not a near-term blocker.
- **Single-machine scope:** no distributed processing; the planned thread
  pool is bounded by `std::thread::hardware_concurrency()` on one machine.

---

## 12. Week-by-Week Traceability

| Week | Layer(s) touched | Deliverable | Status |
|---|---|---|---|
| 5 | Ingestion | Environment setup, custom binary parser, CT/MRI/X-Ray metadata + pixel extraction | ✅ Done |
| 6 | Reconstruction + Processing (SIMD only) | Trilinear-interpolated volume reconstruction, HU normalization, AVX2 Gaussian blur / Sobel edge / histogram equalization, scalar-vs-AVX2 benchmark | ✅ Done |
| 7 | Processing (concurrency) | `std::thread`-based thread pool; parallelize reconstruction and filtering across CPU cores | ⏳ Planned |
| 8 | Detection | 3D region growing for density anomalies | ⏳ Planned |
| 9 | Output | PNG/DICOM/JSON export, end-to-end pipeline integration | ⏳ Planned |

This table is the anchor for internship progress reviews — each row should
be checked off with a link to the corresponding PR/commit once complete.
Note that Weeks 6–9 above reflect the actual delivery schedule as it
evolved (Reconstruction and the AVX2 half of Processing were combined into
Week 6, with the thread-pool/concurrency half of Processing deferred to
Week 7) rather than the original per-layer-per-week split sketched when
this document was first drafted.