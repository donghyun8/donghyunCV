# Canny Edge Detection

C++로 Canny Edge Detection을 직접 구현하고, 각 처리 단계의 동작 원리와 성능 병목을 확인하면서 단계적으로 최적화를 시도하기 위한 레포

OpenCV의 `cv::Canny()`를 사용하지 않고 Grayscale, Gaussian Blur, Sobel Gradient, Non-Maximum Suppression, Double Threshold, Hysteresis를 직접 구현합니다.

---

## 1. Development Environment

본 프로젝트의 성능 측정은 아래 환경을 기준으로 수행합니다.

| Item | Environment |
|---|---|
| OS | Windows 11 64-bit |
| IDE | Visual Studio 2022 Community |
| Language | C++ |
| Build | Release / x64 |
| OpenCV | 4.12.0 |
| Package Manager | vcpkg |
| CPU | Intel(R) Core(TM) Ultra 9 185H @ 2.50 GHz |
| Memory | 64 GB RAM |
| Live Input | Built-in Laptop Webcam |
| Benchmark Input | Fixed 640 × 480 / 30 FPS video |
| Benchmark Frames | 300 frames (~10 s) |
| Benchmark Repeats | Warm-up 1 pass + Measured 5 passes |
| Benchmark Samples | 1,500 frames |
| Processing | Single-thread CPU |
| Power | AC adapter connected |
| Windows Power Mode | Balanced |
| Debug Visualization | Disabled during benchmark |
| Video Decode / Frame I/O | Excluded from Canny compute latency |

> 성능 측정값은 Debug가 아닌 **Release / x64** 빌드를 기준으로 기록합니다.

> 노트북의 전원 상태에 따라 CPU 동작 클럭과 성능이 달라질 수 있으므로, 모든 성능 비교는 **AC 어댑터를 연결한 상태**에서 수행합니다. Windows 전원 모드는 **Balanced**로 고정하여 실험 간 조건을 동일하게 유지합니다.

### Input & Benchmark Conditions

실시간 동작 확인 시에는 노트북 웹캠을 사용하며 입력 조건을 다음과 같이 고정합니다.

```cpp
cap.set(CAP_PROP_FRAME_WIDTH, 640);
cap.set(CAP_PROP_FRAME_HEIGHT, 480);
cap.set(CAP_PROP_FPS, 30);
```

```text
Resolution   : 640 × 480
Frame Rate   : 30 FPS
Pixel Count  : 307,200 pixels/frame
Frame Budget : 33.3 ms/frame
```

30 FPS 실시간 처리를 목표로 하므로, Canny 연산 자체의 최종 목표 latency는 **33.3 ms/frame 이하**로 설정합니다.

실시간 웹캠은 동작 확인용으로 사용하고, 최적화 전후의 정량 비교는 **고정된 benchmark video**를 사용합니다.

```text
Benchmark Video : 640 × 480 / 30 FPS
Frames          : 300 frames (~10 s)
Warm-up         : 1 full pass
Measured        : 5 full passes
Samples         : 1,500 frames
Metrics         : Mean / P50 / P95
```

Benchmark 시작 전에 영상 전체를 decode하여 RAM에 저장한 뒤, 동일한 300개 Frame sequence를 반복 처리합니다.

따라서 Video decode / File I/O / Camera capture는 Canny compute latency에 포함하지 않으며, 최적화 전후에 항상 동일한 입력을 사용합니다.

### Benchmark Scope

알고리즘 성능 비교 시 다음 Canny 처리 단계만 compute latency에 포함합니다.

```text
Grayscale
→ Gaussian Blur
→ Sobel
→ Gradient
→ Non-Maximum Suppression
→ Double Threshold
→ Hysteresis
→ Cleanup
```

다음 항목은 Canny compute latency와 분리합니다.

- Camera / video frame acquisition and video decoding
- Debug image normalization
- `imshow()`
- GUI processing
- Console logging

---

## 2. Dependencies

### OpenCV

OpenCV는 `vcpkg`를 통해 설치합니다.

```bash
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.bat
./vcpkg install opencv4:x64-windows
```

설치 여부 확인:

```bash
vcpkg list
```

본 프로젝트 개발 환경에서는 다음 패키지를 사용했습니다.

```text
opencv4:x64-windows 4.12.0
```

---

## 3. Visual Studio Configuration

프로젝트는 다음 설정을 기준으로 빌드합니다.

```text
Configuration : Release
Platform      : x64
```

OpenCV 헤더를 찾지 못하는 경우 Visual Studio에서 다음 항목을 확인합니다.

```text
Project
→ Properties
→ C/C++
→ General
→ Additional Include Directories
```

vcpkg 설치 경로에 맞춰 OpenCV include directory를 추가합니다.

예시:

```text
<vcpkg-root>\installed\x64-windows\include\opencv4
```

개발 PC에서는 다음과 같은 형태의 경로를 사용했습니다.

```text
C:\vcpkg\installed\x64-windows\include\opencv4
```

> 절대 경로는 각 PC의 vcpkg 설치 위치에 맞게 변경해야 합니다.

가능하면 Visual Studio 설정을 `All Configurations / x64`에 적용해 Debug와 Release 전환 시 include path가 누락되지 않도록 합니다.

---

## 4. Canny Pipeline

현재까지 구현 된 전체 흐름입니다

```text
Camera Frame
    ↓
Grayscale
    ↓
Gaussian Blur
    ↓
Sobel Gx / Gy
    ↓
Gradient Magnitude / Direction
    ↓
Non-Maximum Suppression
    ↓
Double Threshold
    ↓
Hysteresis (Brute Force -> BFS)
    ↓
Edge Map
```

### Grayscale

RGB/BGR 영상에서 밝기 정보만 추출합니다.

```text
Gray = 0.299R + 0.587G + 0.114B
```

### Gaussian Blur

3×3 Gaussian 근사 커널을 직접 적용합니다.

```text
1 2 1
2 4 2
1 2 1
```

전체 합이 16이므로 마지막 결과를 16으로 나누어 정규화합니다.

### Sobel Gradient

Sobel X / Y kernel을 사용하여 공간 미분을 근사합니다.

```text
Gx

-1  0  1
-2  0  2
-1  0  1
```

```text
Gy

-1 -2 -1
 0  0  0
 1  2  1
```

Gradient magnitude:

```text
sqrt(Gx² + Gy²)
```

Gradient direction:

```text
atan2(Gy, Gx)
```

### Non-Maximum Suppression

Gradient 방향을 기준으로 주변 두 픽셀과 magnitude를 비교하여 local maximum만 남깁니다.

방향은 다음 네 구간으로 제한합니다.

```text
0°
45°
90°
135°
```

### Double Threshold

현재 기준값:

```text
Low Threshold  = 50
High Threshold = 100
```

픽셀은 다음 세 상태로 분류합니다.

```text
Strong Edge = 255
Weak Edge   = 75
Non Edge    = 0
```

### Hysteresis

Weak Edge 중 Strong Edge와 연결된 픽셀만 Strong Edge로 변경 후, 
최종적으로 Strong Edge만 Edge Node로 처리합니다.

## Visual Comparison

<img width="2874" height="1016" alt="sobel_nms_canny" src="https://github.com/user-attachments/assets/b553ea45-e24c-4deb-8b24-fd246b750e3b" />

> 왼쪽부터 **Sobel Gradient Magnitude → Non-Maximum Suppression → Canny Edge Map**

### 1. Sobel Gradient Magnitude

Sobel은 영상의 밝기 변화율을 계산하여 **Edge 후보와 Edge Strength**를 구합니다.

하나의 실제 경계 주변에서도 여러 픽셀이 동시에 큰 Gradient를 가질 수 있기 때문에,
Edge가 두껍거나 여러 겹으로 표현됩니다.

**Result:** `Edge 위치 후보 + Edge Strength`

---

### 2. Non-Maximum Suppression

NMS는 Gradient 방향을 기준으로 주변 Magnitude와 비교하여  
**Local Maximum인 픽셀만 유지**합니다.

이를 통해 Sobel에서 두껍게 형성된 Gradient 영역이 얇아지고,
실제 Edge 위치가 보다 명확하게 정리됩니다.

이 단계에서는 아직 Gradient Magnitude를 유지하기 때문에:

- 밝은 Edge → 상대적으로 강한 Gradient
- 어두운 Edge → 상대적으로 약한 Gradient

를 의미합니다.

**Result:** `정제된 Edge 위치 + Edge Strength`

---

### 3. Double Threshold + Hysteresis

Double Threshold를 통해 픽셀을 `Strong / Weak / Non-edge`로 분류하고,
Hysteresis에서는 **Strong Edge와 연결된 Weak Edge만 최종 Edge로 유지**합니다.

따라서 NMS에 남아 있던 약한 Texture나 독립적인 Gradient 반응은 제거되고,
구조적으로 연결된 주요 경계가 중심적으로 남습니다.

최종 Canny 결과는 Edge Strength를 표현하는 것이 아니라
**Edge인지 아닌지를 판단하는 Binary Edge Map**입니다.

```text
Edge      = 255
Non-edge  = 0
```

---

## 5. Benchmark Baseline

### Baseline 측정

현재까지의 구현이 단순하게 Canny Edge Detection의 결과를 얻기 위해 직접 코드를 구현한 결과입니다.
이를 토대로 각 단계의 지연을 측정하여 Benchmark Baseline을 만들고
병목 지점을 파악하고 간 단계를 최적화 해 나가며 지연 시간을 최적화 하려 합니다.

### 측정 결과

일반적인 프레임 기준 처리 시간은 다음과 같이 나타났습니다.

| 단계 | 처리 시간 |
| --- | --- |
| Gray Scale | 약 10~12 ms |
| Gaussian Blur | 약 26~29 ms |
| Sobel | 약 32~35 ms |
| Gradient Magnitude / Direction | 약 17~18 ms |
| NMS | 약 16~18 ms |
| Display 변환 | 약 14~16 ms |
| Double Threshold | 약 5~7 ms |
| Cleanup | 약 4~6 ms |
| **Hysteresis** | **약 257~405 ms 이상** |

전체 처리 시간은 장면에 따라 약 **300~500 ms 이상**

---

### 주요 병목: Hysteresis

Hysteresis 반복 횟수와 처리 시간 사이에 강한 상관관계가 나타났다.

```
Pass 21  → 약 67 ms
Pass 30  → 약 89 ms
Pass 44  → 약 114 ms
Pass 64  → 약 175~190 ms
Pass 105 → 약 285 ms
Pass 113 → 약 305 ms
Pass 137 → 약 354 ms
Pass 144 → 약 405 ms
```

현재 구현은 한 번의 Pass마다 전체 프레임을 다시 탐색합니다.

예를 들어 640×480 영상이라면 한 번의 Pass마다 약 30만 픽셀을 확인하며, 100회 반복 시 단순 픽셀 위치 기준으로 약 3천만 회 이상을 재 방문하게 됩니다.

---

## 6. 1차 최적화

초기 Baseline에서 가장 큰 병목이었던 Hysteresis를 우선 개선한 뒤, 실제 Canny 연산과 무관한 Debug 경로를 분리하고 반복적인 픽셀 접근이 많은 구간에 row pointer 접근을 적용했습니다.

### 6-1. Hysteresis Optimization

#### Initial Implementation - Repeated Full Scan

초기 구현은 Strong Edge와 연결된 Weak Edge가 더 이상 발견되지 않을 때까지 전체 Frame을 반복 탐색했습니다.

개념적으로는 다음과 같습니다.

```text
while changed
    scan entire image

    if weak pixel is connected to strong pixel
        weak → strong
```

이 방식은 구현은 단순하지만, Edge 연결 구조에 따라 전체 Frame 반복 횟수가 크게 증가했습니다.

실측 결과 Hysteresis만 평균적으로 약:

```text
200 ~ 300 ms
```

가 소요되었으며, 복잡한 장면에서는 더 큰 지연과 jitter가 발생했습니다.

#### BFS Hysteresis

Strong Edge들을 Queue에 넣은 후 주변 8-neighbor Weak Edge만 탐색하도록 변경했습니다.

```text
Initial Strong Edges
        ↓
       Queue
        ↓
Pop Strong Edge
        ↓
Check 8 Neighbors
        ↓
Weak → Strong
        ↓
Push to Queue
```

이를 통해 반복적인 Full-frame Scan을 제거했습니다.

```text
Before : ~200-300 ms
After  : ~5-7 ms
```

Hysteresis가 전체 Pipeline의 절대적인 병목이던 상태에서, 다른 단계들과 비슷한 수준의 비용으로 감소했습니다.

### 6-2. Debug Visualization 분리

Hysteresis 최적화와 함께, Magnitude / NMS 확인을 위해 사용하던 다음 Debug 작업을 `DEBUG_VIEW` 조건 내부로 이동했습니다.

```text
maxMagnitude 탐색
→ Display용 Mat 생성
→ 0~255 범위 변환
→ imshow()
```

이 작업들은 Canny 결과를 계산하는 데 필요한 연산이 아니라 중간 결과를 시각적으로 확인하기 위한 작업입니다.

기존에는 Debug 화면을 사용하지 않는 상황에서도 일부 Display 변환 작업이 실행되어 **대략 10 ms 수준의 불필요한 비용**이 발생했습니다. 이를 Debug 경로로 완전히 분리하여 `DEBUG_VIEW = false`인 Benchmark에서는 실행되지 않도록 변경했습니다.

> 이 변경은 Canny 알고리즘 자체를 빠르게 만든 최적화라기보다, **측정 범위를 실제 알고리즘 compute**만 잡도록 수정한 부분입니다.

### 6-3. `Mat::at()` → Row Pointer Access

다음으로 픽셀 단위 반복 접근이 많은 구간에서 `Mat::at()` 호출을 줄이고, 각 Row의 시작 주소를 한 번 얻은 뒤 Pointer를 통해 연속적으로 접근하도록 변경했습니다.

예를 들어:

```cpp
const uchar* src = gray.ptr<uchar>(y);
uchar* dst = blur.ptr<uchar>(y);

for (int x = 1; x < gray.cols - 1; ++x)
{
    // src[x - 1], src[x], src[x + 1]
}
```

형태로 접근하도록 수정했습니다.

특히 **Gaussian Blur와 Sobel처럼 한 Frame에서 주변 픽셀을 반복해서 읽는 연산에서 예상보다 큰 성능 개선**이 확인되었고, Grayscale 등 연속적인 Row 접근이 가능한 구간에도 같은 방식을 적용했습니다.

현재 코드는 모든 Stage를 Pointer 방식으로 변경한 상태는 아니며, NMS / Threshold / Cleanup 등에는 아직 추가 적용 여지가 남아 있습니다.

#### 추가 Pointer 순차 접근 실험

Row Pointer 적용 이후에는 다음 글을 참고하여, Indexing과 중간 변수를 더 줄이고 Pointer 자체를 증가시키면서 순차 접근하는 형태도 추가로 실험했습니다.

- 참고: https://blog.naver.com/dorergiverny/223037431607

예를 들어 다음과 같이 현재 위치를 가리키는 Pointer를 직접 증가시키는 형태입니다.

```cpp
for (; src < src_end; )
{
    *dst++ = /* operation using *src */;
    ++src;
}
```

하지만 **이미 Row Pointer 접근을 적용한 상태에서 Pointer 변수를 더 단순화하고 순차 접근 형태로 변경한 추가 최적화의 이득은 미미했습니다.**

현재 실험에서는 메모리 접근 방식 자체보다 각 Stage가 수행하는 연산의 성격이 더 큰 영향을 주는 것으로 판단했습니다.

- Gaussian / Sobel: 3×3 convolution 연산
- Gradient: `sqrt()` / `atan2()`
- NMS: 방향 분기와 주변 Magnitude 비교
- Threshold: 전체 Frame 조건 분기

즉 `Mat::at()`에서 Row Pointer로 변경할 때는 반복적인 접근 overhead를 줄이며 의미 있는 개선을 얻었지만, 그 이후 Pointer 표현 자체를 더 단순화하는 것은 현재 Pipeline의 주요 병목을 직접 줄이지 못했습니다.

#### Stage별 Pointer 적용 결과

동일한 알고리즘 구조를 유지한 상태에서 `Mat::at()` 기반 픽셀 접근을 Row Pointer 방식으로 변경하여 각 Stage의 처리 시간을 비교했습니다.

현재까지 Grayscale / Gaussian Blur / Sobel / Gradient 단계에 순차적으로 적용했으며, Live profiling 기준으로 다음과 같은 감소가 확인되었습니다.

| Stage | `Mat::at()` 기반 | Row Pointer 적용 | 감소량 | 감소율 |
|---|---:|---:|---:|---:|
| Grayscale | ~10.2 ms | ~3.0 ms | ~7.2 ms | ~71% |
| Gaussian Blur | ~28.2 ms | ~6.4 ms | ~21.8 ms | ~77% |
| Sobel | ~32.2 ms | ~10~12 ms | ~20~22 ms | ~65% |
| Gradient | ~16.2 ms | ~10 ms | ~6 ms | ~38% |

특히 Gaussian Blur와 Sobel에서 큰 감소가 확인되었습니다.

Gaussian Blur와 Sobel은 각 출력 픽셀을 계산하기 위해 주변 `3 × 3` 영역을 반복적으로 참조하므로, 기존 구현에서는 하나의 출력 픽셀을 계산하는 과정에서도 `Mat::at()`가 여러 번 호출되었습니다.

따라서 Row 시작 주소를 한 번 얻은 뒤 다음과 같이 직접 접근하도록 변경하면서 반복적인 픽셀 접근 비용을 크게 줄일 수 있었습니다.

---

## 7. Reproducible Fixed-Video Benchmark

기존 Live profiling은 Camera 장면이 계속 변하기 때문에 최적화 전후를 완전히 동일한 입력으로 비교하기 어려웠습니다.

특히 Hysteresis는 실제 Strong / Weak Edge 수에 따라 탐색량이 달라질 수 있으므로, 이후 최적화 효과를 재현 가능하게 비교하기 위해 **고정된 640 × 480 Benchmark video**를 도입했습니다.

### 7-1. Benchmark Method

```text
Input           : Fixed 640 × 480 / 30 FPS video
Frames          : 300 frames (~10 s)
Warm-up         : 1 full pass
Measured Passes : 5 full passes
Samples         : 1,500 frames
Build           : Release / x64
Processing      : Single-thread CPU
Decode / I/O    : Excluded
Debug / GUI     : Excluded
Metrics         : Mean / P50 / P95
```

Benchmark 시작 전에 300개 Frame을 모두 decode하여 RAM에 적재합니다.

이후 동일한 Frame sequence를 1회 Warm-up한 뒤 5회 반복 측정하여 총 1,500개의 Stage latency sample을 수집합니다.

`Mean`뿐 아니라 `P50`, `P95`를 함께 기록하여 평균적인 처리 시간과 Tail latency를 같이 확인합니다.

### 7-2. Current Benchmark Baseline

현재 Benchmark에는 다음 변경사항이 이미 반영되어 있습니다.

- BFS 기반 Hysteresis
- Debug visualization compute 경로 분리
- Grayscale / Gaussian / Sobel 등의 Row Pointer 접근
- Fixed-video preload 및 반복 측정

실측 결과:

| Stage | Mean (ms) | P50 (ms) | P95 (ms) |
|---|---:|---:|---:|
| Gray | 3.21 | 2.74 | 6.08 |
| Gaussian | 5.67 | 4.64 | 11.45 |
| Sobel | 8.26 | 6.84 | 16.06 |
| Gradient | 12.30 | 10.73 | 21.48 |
| NMS | **18.84** | **16.21** | **33.83** |
| Threshold | 9.84 | 9.01 | 16.75 |
| Hysteresis | 4.77 | 4.20 | 9.28 |
| Cleanup | 5.87 | 4.94 | 11.28 |
| **TOTAL** | **68.76** | **60.27** | **115.52** |

```text
Mean FPS      : 14.54 FPS
30 FPS Budget : 33.33 ms/frame
P95 Status    : FAIL (115.52 ms)
```

**벤치마크 적용과 관련해서 코드 구조 변경 등에는 AI를 적극 활용하였습니다**

### 7-3. Bottleneck after Benchmark Standardization

초기에는 Hysteresis가 압도적인 병목이었지만, BFS 전환 이후 현재 Benchmark에서 가장 큰 평균 비용은 다음 순서로 나타났습니다.

```text
NMS       : 18.84 ms
Gradient  : 12.30 ms
Threshold :  9.84 ms
Sobel     :  8.26 ms
Gaussian  :  5.67 ms
```

---

## 8. Optimization Roadmap

### Completed

- [x] Manual Grayscale
- [x] Manual 3×3 Gaussian Blur
- [x] Manual Sobel X / Y
- [x] Gradient Magnitude / Direction
- [x] Non-Maximum Suppression
- [x] Double Threshold
- [x] Iterative Full-scan Hysteresis
- [x] Queue/BFS Hysteresis
- [x] Stage-level latency profiling
- [x] Separate Debug visualization from algorithm benchmark
- [x] Row Pointer access in Grayscale / Gaussian / Sobel hot loops
- [x] Sequential Pointer-walking experiment
- [x] Fixed-video Benchmark harness
- [x] Frame preload to exclude Decode / I/O
- [x] Warm-up + repeated measurement
- [x] Mean / P50 / P95 latency reporting

### Single-thread Optimization

- [ ] Extend Row Pointer access to NMS / Threshold / Cleanup
- [ ] Reuse intermediate buffers
- [ ] Separable Gaussian Blur
- [ ] Simplify fixed Sobel convolution
- [ ] Remove / approximate `sqrt()`
- [ ] Remove `atan2()` and directly quantize direction
- [ ] Reduce unnecessary full-frame passes
- [ ] Fuse compatible processing stages

### Parallel Optimization

- [ ] Multithreading
- [ ] SIMD / Auto-vectorization
- [ ] Platform-specific optimization

---

## 9. Git History Strategy

이 Repository는 최종 코드만 저장하는 것이 아니라 **알고리즘 구현과 최적화 과정을 Commit 단위로 기록**합니다.

예시:

```text
Canny: Baseline

Optimize: replace repeated hysteresis scan with BFS

Optimize: separate debug visualization from compute path

Optimize: apply row pointer access to hot loops

Benchmark: add reproducible fixed-video benchmark harness
```

각 버전의 변경사항은 Git diff를 통해 비교할 수 있습니다.

```bash
git log --oneline
```

특정 두 버전 비교:

```bash
git diff <commit-A> <commit-B>
```

특정 Commit의 변경사항 확인:

```bash
git show <commit>
```

---

## 10. Project Goal

이 프로젝트의 목적은 OpenCV의 완성된 Canny API를 호출하는 것이 아니라, Canny Edge Detection의 각 단계를 직접 구현하면서 다음 내용을 이해하는 것입니다.

- Image filtering
- Convolution
- Spatial derivative
- Gradient
- Numerical approximation
- Local neighborhood processing
- Graph traversal
- Memory access pattern
- Algorithmic optimization
- Real-time image processing
