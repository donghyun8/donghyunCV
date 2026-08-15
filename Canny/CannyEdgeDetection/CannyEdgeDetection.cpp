#include <opencv2/opencv.hpp>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <queue>
#include <vector>
#include <algorithm>
#include <numeric>
#include <string>

using namespace cv;
using namespace std;

struct pos
{
    int y;
    int x;
};

struct FrameTiming
{
    double gray = 0.0;
    double gaussian = 0.0;
    double sobel = 0.0;
    double gradient = 0.0;
    double nms = 0.0;
    double threshold = 0.0;
    double hysteresis = 0.0;
    double cleanup = 0.0;
    double total = 0.0;
    int processedNodes = 0;
};

struct BenchmarkData
{
    vector<double> gray;
    vector<double> gaussian;
    vector<double> sobel;
    vector<double> gradient;
    vector<double> nms;
    vector<double> threshold;
    vector<double> hysteresis;
    vector<double> cleanup;
    vector<double> total;
};

double mean(const vector<double>& values)
{
    if (values.empty())
        return 0.0;

    return accumulate(values.begin(), values.end(), 0.0) / values.size();
}

double percentile(const vector<double>& values, double p)
{
    if (values.empty())
        return 0.0;

    vector<double> sorted = values;
    sort(sorted.begin(), sorted.end());

    size_t index = static_cast<size_t>(p * (sorted.size() - 1));
    return sorted[index];
}

void printBenchmarkRow(const string& name, const vector<double>& values)
{
    cout
        << left << setw(14) << name
        << right << setw(12) << fixed << setprecision(2) << mean(values)
        << setw(12) << percentile(values, 0.50)
        << setw(12) << percentile(values, 0.95)
        << '\n';
}

void reserveBenchmark(BenchmarkData& benchmark, size_t sampleCount)
{
    benchmark.gray.reserve(sampleCount);
    benchmark.gaussian.reserve(sampleCount);
    benchmark.sobel.reserve(sampleCount);
    benchmark.gradient.reserve(sampleCount);
    benchmark.nms.reserve(sampleCount);
    benchmark.threshold.reserve(sampleCount);
    benchmark.hysteresis.reserve(sampleCount);
    benchmark.cleanup.reserve(sampleCount);
    benchmark.total.reserve(sampleCount);
}

void appendBenchmark(BenchmarkData& benchmark, const FrameTiming& t)
{
    benchmark.gray.push_back(t.gray);
    benchmark.gaussian.push_back(t.gaussian);
    benchmark.sobel.push_back(t.sobel);
    benchmark.gradient.push_back(t.gradient);
    benchmark.nms.push_back(t.nms);
    benchmark.threshold.push_back(t.threshold);
    benchmark.hysteresis.push_back(t.hysteresis);
    benchmark.cleanup.push_back(t.cleanup);
    benchmark.total.push_back(t.total);
}

bool loadBenchmarkVideo(
    const string& path,
    vector<Mat>& frames,
    int expectedWidth,
    int expectedHeight,
    int maxFrames
)
{
    VideoCapture cap(path);

    if (!cap.isOpened())
    {
        cerr << "Benchmark video open failed: " << path << '\n';
        return false;
    }

    Mat frame;

    while (cap.read(frame))
    {
        if (frame.empty())
            break;

        if (frame.cols != expectedWidth || frame.rows != expectedHeight)
        {
            cerr
                << "Benchmark video resolution mismatch.\n"
                << "Expected: " << expectedWidth << "x" << expectedHeight << '\n'
                << "Actual  : " << frame.cols << "x" << frame.rows << '\n';
            return false;
        }

        frames.push_back(frame.clone());

        if (maxFrames > 0 && static_cast<int>(frames.size()) >= maxFrames)
            break;
    }

    if (frames.empty())
    {
        cerr << "No frame loaded from benchmark video.\n";
        return false;
    }

    return true;
}

FrameTiming processCanny(
    const Mat& frame,
    bool debugView,
    Mat* outputEdges = nullptr
)
{
    using Clock = chrono::steady_clock;

    auto getMs = [](auto start, auto end)
        {
            return chrono::duration<double, milli>(end - start).count();
        };

    FrameTiming timing;

    auto t0 = Clock::now();

    Mat gray(
        frame.rows,
        frame.cols,
        CV_8UC1
    );

    for (int y = 0; y < frame.rows; y++)
    {
        const Vec3b* src = frame.ptr<Vec3b>(y);
        const Vec3b* srcEnd = src + frame.cols;
        uchar* dst = gray.ptr<uchar>(y);

        for (; src < srcEnd; ++src)
        {
            *dst++ = static_cast<uchar>(
                0.114f * (*src)[0] +
                0.587f * (*src)[1] +
                0.299f * (*src)[2]
                );
        }
    }

    auto t1 = Clock::now();

    Mat temp(
        gray.rows,
        gray.cols,
        CV_16UC1,
        Scalar(0)
    );

    Mat blur = Mat::zeros(
        gray.rows,
        gray.cols,
        CV_8UC1
    );

    //int kernel[3][3] =
    //{
    //    {1, 2, 1},
    //    {2, 4, 2},
    //    {1, 2, 1}
    //};

    for (int y = 0; y < gray.rows; ++y)
    {
        const uchar* src = gray.ptr<uchar>(y);
        ushort* tmp = temp.ptr<ushort>(y);

        for (int x = 1; x < gray.cols - 1; ++x)
        {
            tmp[x] =
                src[x - 1] +
                2 * src[x] +
                src[x + 1];
        }
    }

    for (int y = 1; y < gray.rows - 1; ++y)
    {
        const ushort* prev = temp.ptr<ushort>(y - 1);
        const ushort* curr = temp.ptr<ushort>(y);
        const ushort* next = temp.ptr<ushort>(y + 1);

        uchar* dst = blur.ptr<uchar>(y);

        for (int x = 1; x < gray.cols - 1; ++x)
        {
            int sum =
                prev[x] +
                2 * curr[x] +
                next[x];

            dst[x] =
                static_cast<uchar>(sum / 16);
        }
    }

    /*for (int y = 1; y < gray.rows - 1; y++)
    {
        const uchar* rows[3] =
        {
            gray.ptr<uchar>(y - 1),
            gray.ptr<uchar>(y),
            gray.ptr<uchar>(y + 1)
        };

        uchar* dst = blur.ptr<uchar>(y);

        for (int x = 1; x < gray.cols - 1; x++)
        {
            int sum = 0;

            for (int ky = -1; ky <= 1; ky++)
            {
                for (int kx = -1; kx <= 1; kx++)
                {
                    int pixelValue = rows[ky + 1][x + kx];
                    int weight = kernel[ky + 1][kx + 1];
                    sum += pixelValue * weight;
                }
            }

            dst[x] = static_cast<uchar>(sum / 16);
        }
    }*/

    auto t2 = Clock::now();

    Mat gx(
        gray.rows,
        gray.cols,
        CV_16SC1,
        Scalar(0)
    );

    Mat gy(
        gray.rows,
        gray.cols,
        CV_16SC1,
        Scalar(0)
    );

    int sobelX[3][3] =
    {
        {-1, 0, 1},
        {-2, 0, 2},
        {-1, 0, 1}
    };

    int sobelY[3][3] =
    {
        {-1, -2, -1},
        { 0,  0,  0},
        { 1,  2,  1}
    };

    for (int y = 1; y < blur.rows - 1; y++)
    {
        const uchar* rows[3] =
        {
            blur.ptr<uchar>(y - 1),
            blur.ptr<uchar>(y),
            blur.ptr<uchar>(y + 1)
        };

        short* gxRow = gx.ptr<short>(y);
        short* gyRow = gy.ptr<short>(y);

        for (int x = 1; x < blur.cols - 1; x++)
        {
            int sumX = 0;
            int sumY = 0;

            for (int ky = -1; ky <= 1; ky++)
            {
                for (int kx = -1; kx <= 1; kx++)
                {
                    int pixel = rows[ky + 1][x + kx];
                    int wx = sobelX[ky + 1][kx + 1];
                    int wy = sobelY[ky + 1][kx + 1];

                    sumX += pixel * wx;
                    sumY += pixel * wy;
                }
            }

            gxRow[x] = static_cast<short>(sumX);
            gyRow[x] = static_cast<short>(sumY);
        }
    }

    auto t3 = Clock::now();

    Mat magnitude(
        gray.rows,
        gray.cols,
        CV_32FC1,
        Scalar(0)
    );

    Mat angle(
        gray.rows,
        gray.cols,
        CV_32FC1,
        Scalar(0)
    );

    for (int y = 0; y < gray.rows; y++)
    {
        const short* gxRow = gx.ptr<short>(y);
        const short* gyRow = gy.ptr<short>(y);

        float* magRow = magnitude.ptr<float>(y);
        float* angleRow = angle.ptr<float>(y);

        for (int x = 0; x < gray.cols; x++)
        {
            float sx = static_cast<float>(gxRow[x]);
            float sy = static_cast<float>(gyRow[x]);

            float mag = sqrt(
                sx * sx +
                sy * sy
            );

            float theta =
                atan2(sy, sx) *
                180.0f /
                CV_PI;

            if (theta < 0)
                theta += 180.0f;

            magRow[x] = mag;
            angleRow[x] = theta;
        }
    }

    auto t4 = Clock::now();

    Mat nms(
        gray.rows,
        gray.cols,
        CV_32FC1,
        Scalar(0)
    );

    for (int y = 1; y < gray.rows - 1; y++)
    {
        const float* prev = magnitude.ptr<float>(y - 1);
        const float* current = magnitude.ptr<float>(y);
        const float* next = magnitude.ptr<float>(y + 1);
        float* dst = nms.ptr<float>(y);
        const float* theta = angle.ptr<float>(y);

        for (int x = 1; x < gray.cols - 1; x++)
        {
            //float current = magnitude.at<float>(y, x);
            //float theta = angle.at<float>(y, x);

            float neighbor1 = 0.0f;
            float neighbor2 = 0.0f;

            if (
                (theta[x] >= 0.0f && theta[x] < 22.5f) ||
                (theta[x] >= 157.5f && theta[x] <= 180.0f)
                )
            {
                neighbor1 = current[x - 1];
                neighbor2 = current[x + 1];
                //neighbor1 = magnitude.at<float>(y, x - 1);
                //neighbor2 = magnitude.at<float>(y, x + 1);
            }
            else if (
                theta[x] >= 22.5f &&
                theta[x] < 67.5f
                )
            {
                neighbor1 = prev[x - 1];
                neighbor2 = next[x + 1];
                //neighbor1 = magnitude.at<float>(y - 1, x - 1);
                //neighbor2 = magnitude.at<float>(y + 1, x + 1);
            }
            else if (
                theta[x] >= 67.5f &&
                theta[x] < 112.5f
                )
            {
                neighbor1 = prev[x];
                neighbor2 = next[x];
                //neighbor1 = magnitude.at<float>(y - 1, x);
                //neighbor2 = magnitude.at<float>(y + 1, x);
            }
            else
            {
                neighbor1 = prev[x + 1];
                neighbor2 = next[x - 1];
                //neighbor1 = magnitude.at<float>(y - 1, x + 1);
                //neighbor2 = magnitude.at<float>(y + 1, x - 1);
            }

            if (
                current[x] >= neighbor1 &&
                current[x] >= neighbor2
                )
            {
                dst[x] = current[x];
            }
            else
            {
                dst[x] = 0.0f;
            }
        }
    }

    auto t5 = Clock::now();
    float lt = 50.0f;
    float ht = 100.0f;

    Mat thresholdResult(
        nms.rows,
        nms.cols,
        CV_8UC1,
        Scalar(0)
    );

    const unsigned char STRONG = 255;
    const unsigned char WEAK = 75;

    queue<pos> q;

    for (int y = 1; y < nms.rows - 1; y++)
    {
        for (int x = 1; x < nms.cols - 1; x++)
        {
            float value = nms.at<float>(y, x);

            if (value >= ht)
            {
                thresholdResult.at<unsigned char>(y, x) = STRONG;
                q.push({ y, x });
            }
            else if (value >= lt)
            {
                thresholdResult.at<unsigned char>(y, x) = WEAK;
            }
            else
            {
                thresholdResult.at<unsigned char>(y, x) = 0;
            }
        }
    }

    auto t6 = Clock::now();

    Mat edges = thresholdResult.clone();

    int processedNodes = 0;

    int dx[8] = { -1, -1, -1,  0, 0, 1, 1, 1 };
    int dy[8] = { -1,  0,  1, -1, 1,-1, 0, 1 };

    while (!q.empty())
    {
        pos now = q.front();
        q.pop();

        processedNodes++;

        for (int i = 0; i < 8; i++)
        {
            int ny = now.y + dy[i];
            int nx = now.x + dx[i];

            if (edges.at<unsigned char>(ny, nx) != WEAK)
                continue;

            edges.at<unsigned char>(ny, nx) = STRONG;
            q.push({ ny, nx });
        }
    }

    auto t7 = Clock::now();

    for (int y = 0; y < edges.rows; y++)
    {
        for (int x = 0; x < edges.cols; x++)
        {
            if (edges.at<unsigned char>(y, x) != STRONG)
                edges.at<unsigned char>(y, x) = 0;
        }
    }

    auto t8 = Clock::now();

    if (debugView)
    {
        double maxMagnitude = 0.0;

        for (int y = 0; y < magnitude.rows; y++)
        {
            const float* magRow = magnitude.ptr<float>(y);

            for (int x = 0; x < magnitude.cols; x++)
            {
                if (magRow[x] > maxMagnitude)
                    maxMagnitude = magRow[x];
            }
        }

        Mat magnitudeDisplay(
            gray.rows,
            gray.cols,
            CV_8UC1,
            Scalar(0)
        );

        Mat nmsDisplay(
            gray.rows,
            gray.cols,
            CV_8UC1,
            Scalar(0)
        );

        if (maxMagnitude > 0.0)
        {
            for (int y = 0; y < magnitude.rows; y++)
            {
                const float* magRow = magnitude.ptr<float>(y);
                const float* nmsRow = nms.ptr<float>(y);
                uchar* magDst = magnitudeDisplay.ptr<uchar>(y);
                uchar* nmsDst = nmsDisplay.ptr<uchar>(y);

                for (int x = 0; x < magnitude.cols; x++)
                {
                    magDst[x] = static_cast<uchar>(
                        magRow[x] / maxMagnitude * 255.0
                        );

                    nmsDst[x] = static_cast<uchar>(
                        nmsRow[x] / maxMagnitude * 255.0
                        );
                }
            }
        }

        imshow("Sobel Magnitude", magnitudeDisplay);
        imshow("NMS", nmsDisplay);
    }

    timing.gray = getMs(t0, t1);
    timing.gaussian = getMs(t1, t2);
    timing.sobel = getMs(t2, t3);
    timing.gradient = getMs(t3, t4);
    timing.nms = getMs(t4, t5);
    timing.threshold = getMs(t5, t6);
    timing.hysteresis = getMs(t6, t7);
    timing.cleanup = getMs(t7, t8);
    timing.total = getMs(t0, t8);
    timing.processedNodes = processedNodes;

    if (outputEdges != nullptr)
        *outputEdges = edges;

    return timing;
}

int main()
{
    constexpr bool BENCHMARK_MODE = false;
    constexpr bool DEBUG_VIEW = false;

    constexpr int INPUT_WIDTH = 640;
    constexpr int INPUT_HEIGHT = 480;

    const string BENCHMARK_VIDEO_PATH = "../benchmark_640x480.mp4";

    constexpr int BENCHMARK_MAX_FRAMES = 300;

    constexpr int WARMUP_PASSES = 1;
    constexpr int BENCHMARK_PASSES = 5;

    if (BENCHMARK_MODE && DEBUG_VIEW)
    {
        cerr << "DEBUG_VIEW must be false in BENCHMARK_MODE.\n";
        return -1;
    }

    if (BENCHMARK_MODE)
    {
        vector<Mat> benchmarkFrames;

        if (!loadBenchmarkVideo(
            BENCHMARK_VIDEO_PATH,
            benchmarkFrames,
            INPUT_WIDTH,
            INPUT_HEIGHT,
            BENCHMARK_MAX_FRAMES
        ))
        {
            return -1;
        }

        cout
            << "Loaded benchmark frames: "
            << benchmarkFrames.size()
            << "\n";

        cout
            << "Warm-up passes: "
            << WARMUP_PASSES
            << "\n";

        cout
            << "Measured passes: "
            << BENCHMARK_PASSES
            << "\n\n";

        for (int pass = 0; pass < WARMUP_PASSES; pass++)
        {
            for (const Mat& frame : benchmarkFrames)
            {
                processCanny(frame, false, nullptr);
            }
        }

        BenchmarkData benchmark;

        const size_t sampleCount =
            benchmarkFrames.size() *
            static_cast<size_t>(BENCHMARK_PASSES);

        reserveBenchmark(benchmark, sampleCount);

        for (int pass = 0; pass < BENCHMARK_PASSES; pass++)
        {
            for (const Mat& frame : benchmarkFrames)
            {
                FrameTiming timing = processCanny(
                    frame,
                    false,
                    nullptr
                );

                appendBenchmark(benchmark, timing);
            }
        }

        cout << "===== Benchmark =====\n";
        cout << "Input       : " << INPUT_WIDTH << "x" << INPUT_HEIGHT << " fixed video frames\n";
        cout << "Frames      : " << benchmarkFrames.size() << "\n";
        cout << "Warm-up     : " << WARMUP_PASSES << " full pass(es)\n";
        cout << "Measured    : " << BENCHMARK_PASSES << " full pass(es)\n";
        cout << "Samples     : " << benchmark.total.size() << " frames\n";
        cout << "Decode / I/O: excluded\n";
        cout << "Debug / GUI : excluded\n\n";

        cout
            << left << setw(14) << "Stage"
            << right << setw(12) << "Mean(ms)"
            << setw(12) << "P50(ms)"
            << setw(12) << "P95(ms)"
            << '\n';

        cout << string(50, '-') << '\n';

        printBenchmarkRow("Gray", benchmark.gray);
        printBenchmarkRow("Gaussian", benchmark.gaussian);
        printBenchmarkRow("Sobel", benchmark.sobel);
        printBenchmarkRow("Gradient", benchmark.gradient);
        printBenchmarkRow("NMS", benchmark.nms);
        printBenchmarkRow("Threshold", benchmark.threshold);
        printBenchmarkRow("Hysteresis", benchmark.hysteresis);
        printBenchmarkRow("Cleanup", benchmark.cleanup);
        printBenchmarkRow("TOTAL", benchmark.total);

        const double meanTotal = mean(benchmark.total);
        const double p95Total = percentile(benchmark.total, 0.95);

        cout
            << "\nMean FPS    : "
            << fixed << setprecision(2)
            << (meanTotal > 0.0 ? 1000.0 / meanTotal : 0.0)
            << '\n';

        cout
            << "30 FPS budget: 33.33 ms / frame\n"
            << "P95 status   : "
            << (p95Total <= 33.33 ? "PASS" : "FAIL")
            << " ("
            << fixed << setprecision(2)
            << p95Total
            << " ms)\n";

        return 0;
    }

    VideoCapture cap(0);

    if (!cap.isOpened())
    {
        cerr << "Camera open failed\n";
        return -1;
    }

    cap.set(CAP_PROP_FRAME_WIDTH, INPUT_WIDTH);
    cap.set(CAP_PROP_FRAME_HEIGHT, INPUT_HEIGHT);
    cap.set(CAP_PROP_FPS, 30);

    Mat frame;
    int frameCount = 0;

    while (true)
    {
        cap >> frame;

        if (frame.empty())
            break;

        Mat edges;

        FrameTiming timing = processCanny(
            frame,
            DEBUG_VIEW,
            &edges
        );

        frameCount++;

        if (frameCount % 30 == 0)
        {
            cout
                << fixed
                << setprecision(2)
                << "Gray: " << timing.gray
                << " ms | Gaussian: " << timing.gaussian
                << " ms | Sobel: " << timing.sobel
                << " ms | Gradient: " << timing.gradient
                << " ms | NMS: " << timing.nms
                << " ms | Threshold: " << timing.threshold
                << " ms | Hysteresis: " << timing.hysteresis
                << " ms | Cleanup: " << timing.cleanup
                << " ms | TOTAL: " << timing.total
                << " ms | FPS: " << (1000.0 / timing.total)
                << " | processedNodes: " << timing.processedNodes
                << '\n';
        }

        imshow("Canny", edges);

        if (waitKey(1) == 'q')
            break;
    }

    return 0;
}
