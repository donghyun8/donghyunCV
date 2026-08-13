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
| Camera | Built-in Laptop Webcam |
| Input Resolution | 640 × 480 |
| Target Input FPS | 30 FPS |
| Processing | Single-thread CPU |
| Power | AC adapter connected |
| Windows Power Mode | Balanced |
| Debug Visualization | Disabled during benchmark |
| Camera Capture Latency | Excluded from Canny compute latency |

> 성능 측정값은 Debug가 아닌 **Release / x64** 빌드를 기준으로 기록합니다.

> 노트북의 전원 상태에 따라 CPU 동작 클럭과 성능이 달라질 수 있으므로, 모든 성능 비교는 **AC 어댑터를 연결한 상태**에서 수행합니다. Windows 전원 모드는 **Balanced**로 고정하여 실험 간 조건을 동일하게 유지합니다.

### Camera Input

입력 조건을 코드에서 명시적으로 고정합니다.

```cpp
cap.set(CAP_PROP_FRAME_WIDTH, 640);
cap.set(CAP_PROP_FRAME_HEIGHT, 480);
cap.set(CAP_PROP_FPS, 30);
```

Benchmark 기준 입력은 다음과 같습니다.

```text
Resolution   : 640 × 480
Frame Rate   : 30 FPS
Pixel Count  : 307,200 pixels/frame
Frame Budget : 33.3 ms/frame
```

30 FPS 실시간 처리를 목표로 하므로, Canny 연산 자체의 최종 목표 latency는 **33.3 ms/frame 이하**로 설정합니다.

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

- Camera frame acquisition
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

---

## 5. Hysteresis Optimization

### Initial Implementation - Repeated Full Scan

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

### BFS Hysteresis

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

실측 결과:

```text
Before : ~200-300 ms
After  : ~5-7 ms
```

Hysteresis 병목이 크게 감소했습니다.

---

## 6. Current Performance

현재 BFS Hysteresis 적용 이후 전체 Single-thread Canny 처리 시간은 약:

```text
~135 ms / frame
```

수준입니다.

30 FPS 실시간 처리를 목표로 할 경우 한 Frame의 처리 시간은 약:

```text
33.3 ms
```

이하여야 합니다.

따라서 현재 구현은 추가 최적화가 필요합니다.

> Camera capture와 Debug visualization은 알고리즘 자체의 처리 시간과 분리하여 측정합니다.

---

## 7. Benchmark Policy

최적화 전후 비교 시 다음 조건을 최대한 동일하게 유지합니다.

- Release / x64
- 동일한 입력 Camera
- 동일한 입력 Resolution
- 동일한 Threshold
- Single-thread 기준부터 비교
- Camera capture 시간 별도
- `imshow()` 및 Debug visualization 시간 별도
- 동일한 장면 또는 동일한 영상 입력 사용 권장

성능 측정은 각 Stage별로 기록합니다.

```text
Gray
Gaussian
Sobel
Gradient
NMS
Threshold
Hysteresis
Cleanup
TOTAL
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

### Single-thread Optimization

- [x] Separate Debug visualization from algorithm benchmark
- [ ] `Mat::at()` → row pointer access
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
Canny:Baseline

Replace hysteresis with BFS

Pinned Camera Setting
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
