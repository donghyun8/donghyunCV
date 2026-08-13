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

<img width="2874" height="1016" alt="sobel_nms_canny" src="https://github.com/user-attachments/assets/b553ea45-e24c-4deb-8b24-fd246b750e3b" />
이미지는 왼쪽부터 **Sobel Gradient Magnitude → Non-Maximum Suppression(NMS) → 최종 Canny Edge Map** 순서입니다. 

### Sobel Gradient Magnitude

Sobel 단계에서는 영상의 밝기 변화율을 계산하여 경계 후보를 검출하기 때문인데, 
한 프레임에서도 여러 픽셀이 동시에 큰 Gradient 값을 가지면서 선이 두껍거나 여러 겹으로 보일 수 있습니다. 또한, Sobel -> NMS -> Canny 과정에서 Sobel 가장 다양한 밝기 값을 가진 채로 표현됩니다.

### Non-Maximum Suppression

NMS에서는 Gradient 방향을 기준으로 주변 픽셀과 Magnitude를 비교하고, 해당 방향에서 Local Maximum인 픽셀만 남깁니다. 이를 통해 Sobel에서 두껍게 나타났던 Gradient 영역이 얇은 선으로 정리됩니다.
이미지에서도 Sobel 결과에 비해 중복된 선이 크게 감소하고, 실제 경계 위치가 상대적으로 명확하게 표현되는 것을 확인할 수 있습니다.

다만 이 단계에서도 픽셀 값은 여전히 Gradient Magnitude를 유지합니다.
즉 밝은 선은 상대적으로 강한 Edge이고, 어두운 선은 상대적으로 약한 Edge를 나타냅니다.

### After Double Threshold + Hystersis -> Canny

Canny 결과에서는 Double Threshold를 통해 Edge를 Strong / Weak / Non-edge로 분류한 뒤, Hysteresis를 이용해 Strong Edge와 연결된 Weak Edge만 최종 Edge로 유지합니다.

따라서 NMS 결과와 비교하면 약한 Texture나 독립적인 Gradient 반응이 제거되고, 구조적으로 연결된 주요 경계가 중심적으로 남습니다.

이 단계부터는 Edge의 강도 차이를 표현하는 것이 목적이 아니기 때문에,
최종 출력은 Binary Edge Map으로 변환되어 일반적으로 다음과 같이 표현됩니다

Edge  =  255
Non-edge = 0

따라서 원래 Gradient Magnitude가 서로 달랐던 Edge라도 최종적으로 Edge로 판정되면 
모두 동일한 흰색(255)으로 표시됩니다.

이 때문에 Canny 영상이 Sobel이나 NMS보다 더 밝고 대비가 강하게 보입니다.

Canny에서 Gradient Magnitude 값을 통일하는 이유는, 이후 Edge Detection 결과를 사용하는 입장에서 필요한 정보가 **해당 Edge가 얼마나 강한가**가 아니라 **해당 픽셀이 Edge인가 아닌가**이기 때문입니다.

즉, NMS까지는 **Edge의 위치와 강도에 대한 측정값**을 가지고 있지만, 최종 Canny 결과는 **Edge 여부에 대한 판단값**이라고 볼 수 있습니다.

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

## 6-1. Hysteresis Optimization

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

## 6-2. After Optimization

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

## 7. Optimization Roadmap

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

## 8. Git History Strategy

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

## 9. Project Goal

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
