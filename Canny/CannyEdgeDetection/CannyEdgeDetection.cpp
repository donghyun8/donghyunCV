#include <opencv2/opencv.hpp>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <queue>

using namespace cv;
using namespace std;


struct pos {
    int y;
    int x;
};

int main()
{
    const bool DEBUG_VIEW = false;

    using Clock = chrono::steady_clock;

    VideoCapture cap(0);

    if (!cap.isOpened())
    {
        cerr << "Camera open failed\n";
        return -1;
    }

    cap.set(CAP_PROP_FRAME_WIDTH, 640);
    cap.set(CAP_PROP_FRAME_HEIGHT, 480);
    cap.set(CAP_PROP_FPS, 30);

    Mat frame;

    int frameCount = 0;

    while (true)
    {
        cap >> frame;

        if (frame.empty())
            break;

        auto t0 = Clock::now();

        Mat gray(
            frame.rows,
            frame.cols,
            CV_8UC1
        );

        for (int y = 0; y < frame.rows; y++)
        {
            const Vec3b* src = frame.ptr<Vec3b>(y);
            
            const Vec3b* src_end = src + frame.cols;

            uchar* dst = gray.ptr<uchar>(y);

            for (; src < src_end;) {

                *dst++ = static_cast<uchar>(
                    0.114f * (*src)[0] +
                    0.587f * (*src)[1] +
                    0.299f * (*src)[2]
                    );

                src++;
            }
        }

        auto t1 = Clock::now();

        Mat blur(
            gray.rows,
            gray.cols,
            CV_8UC1
        );

        int kernel[3][3] =
        {
            {1, 2, 1},
            {2, 4, 2},
            {1, 2, 1}
        };

        for (int y = 1; y < gray.rows - 1; y++)
        {
            const uchar* rows[3] = {
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
                        /*int pixelValue =
                            gray.at<unsigned char>(
                                y + ky,
                                x + kx
                            );*/

                        int pixelValue = rows[ky + 1][x + kx];

                        int weight =
                            kernel[ky + 1][kx + 1];

                        sum += pixelValue * weight;
                    }
                }

                blur.at<unsigned char>(y, x) =
                    static_cast<unsigned char>(
                        sum / 16
                        );
            }
        }

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
            {0, 0, 0},
            {1, 2, 1}
        };

        for (int y = 1; y < blur.rows - 1; y++)
        {
            const uchar* rows[3] = {
                blur.ptr<uchar>(y - 1),
                blur.ptr<uchar>(y),
                blur.ptr<uchar>(y + 1),
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

                        int wx =
                            sobelX[ky + 1][kx + 1];

                        int wy =
                            sobelY[ky + 1][kx + 1];

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
            for (int x = 1; x < gray.cols - 1; x++)
            {
                float current =
                    magnitude.at<float>(y, x);

                float theta =
                    angle.at<float>(y, x);

                float neighbor1 = 0.0f;
                float neighbor2 = 0.0f;

                if (
                    (theta >= 0.0f && theta < 22.5f) ||
                    (theta >= 157.5f && theta <= 180.0f)
                    )
                {
                    neighbor1 =
                        magnitude.at<float>(
                            y,
                            x - 1
                        );

                    neighbor2 =
                        magnitude.at<float>(
                            y,
                            x + 1
                        );
                }
                else if (
                    theta >= 22.5f &&
                    theta < 67.5f
                    )
                {
                    neighbor1 =
                        magnitude.at<float>(
                            y - 1,
                            x - 1
                        );

                    neighbor2 =
                        magnitude.at<float>(
                            y + 1,
                            x + 1
                        );
                }
                else if (
                    theta >= 67.5f &&
                    theta < 112.5f
                    )
                {
                    neighbor1 =
                        magnitude.at<float>(
                            y - 1,
                            x
                        );

                    neighbor2 =
                        magnitude.at<float>(
                            y + 1,
                            x
                        );
                }
                else
                {
                    neighbor1 =
                        magnitude.at<float>(
                            y - 1,
                            x + 1
                        );

                    neighbor2 =
                        magnitude.at<float>(
                            y + 1,
                            x - 1
                        );
                }

                if (
                    current >= neighbor1 &&
                    current >= neighbor2
                    )
                {
                    nms.at<float>(y, x) = current;
                }
                else
                {
                    nms.at<float>(y, x) = 0.0f;
                }
            }
        }

        auto t5 = Clock::now();

        double maxMagnitude = 0;

        for (int y = 0; y < magnitude.rows; y++)
        {
            for (int x = 0; x < magnitude.cols; x++)
            {
                float value =
                    magnitude.at<float>(y, x);

                if (value > maxMagnitude)
                    maxMagnitude = value;
            }
        }

        auto t6 = Clock::now();

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
                float value =
                    nms.at<float>(y, x);

                if (value >= ht)
                {
                    thresholdResult.at<unsigned char>(y, x) = STRONG;
                    q.push({ y, x });

                }
                else if (value >= lt)
                {
                    thresholdResult.at<unsigned char>(
                        y,
                        x
                    ) = WEAK;
                }
                else
                {
                    thresholdResult.at<unsigned char>(
                        y,
                        x
                    ) = 0;
                }
            }
        }

        auto t7 = Clock::now();

        Mat edges = thresholdResult.clone();

        bool changed = true;

        int processedNodes = 0;

        // BFS 로 전환
        int dx[8] = { -1,-1,-1,0,0,1,1,1 };
        int dy[8] = { -1, 0, 1,-1,1, -1, 0,1 };

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

        auto t8 = Clock::now();

        for (int y = 0; y < edges.rows; y++)
        {
            for (int x = 0; x < edges.cols; x++)
            {
                if (
                    edges.at<unsigned char>(
                        y,
                        x
                    ) != STRONG
                    )
                {
                    edges.at<unsigned char>(
                        y,
                        x
                    ) = 0;
                }
            }
        }

        auto t9 = Clock::now();

        if (DEBUG_VIEW)
        {
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

            for (int y = 0; y < magnitude.rows; y++)
            {
                for (int x = 0; x < magnitude.cols; x++)
                {
                    if (maxMagnitude > 0)
                    {
                        magnitudeDisplay.at<unsigned char>(y, x) =
                            static_cast<unsigned char>(
                                magnitude.at<float>(y, x) /
                                maxMagnitude *
                                255.0
                                );

                        nmsDisplay.at<unsigned char>(y, x) =
                            static_cast<unsigned char>(
                                nms.at<float>(y, x) /
                                maxMagnitude *
                                255.0
                                );
                    }
                }
            }

            imshow(
                "Sobel Magnitude",
                magnitudeDisplay
            );

            imshow(
                "NMS",
                nmsDisplay
            );
        }
        auto t10 = Clock::now();


        auto getMs = [](auto start, auto end)
            {
                return chrono::duration<double, milli>(
                    end - start
                ).count();
            };

        frameCount++;

        if (frameCount % 30 == 0)
        {
            double total =
                getMs(t0, t10);

            cout
                << fixed
                << setprecision(2)
                << "Gray: "
                << getMs(t0, t1)
                << " ms | Gaussian: "
                << getMs(t1, t2)
                << " ms | Sobel: "
                << getMs(t2, t3)
                << " ms | Gradient: "
                << getMs(t3, t4)
                << " ms | NMS: "
                << getMs(t4, t5)
                << " ms | CannyDisplayPrep: "
                << getMs(t5, t6)
                << " ms | Threshold: "
                << getMs(t6, t7)
                << " ms | Hysteresis: "
                << getMs(t7, t8)
                << " ms | Cleanup: "
                << getMs(t8, t9)
                << " ms | Debug: "
                << getMs(t9, t10)
                << " ms | TOTAL: "
                << total
                << " ms | FPS: "
                << (1000.0 / total)
                << " | processedNodes: "
                << processedNodes
                << endl;
        }


        imshow(
            "Canny Final",
            edges
        );

        if (waitKey(1) == 'q')
            break;
    }

    return 0;
}