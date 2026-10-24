#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

template <typename T, size_t N> struct RingBuffer
{
    std::array<T, N> data {};
    size_t head { 0 };
    size_t count { N };
    double sum { 0.0 };

    RingBuffer() { data.fill(static_cast<T>(0)); }

    void Push(T v)
    {
        if (count == N)
        {
            sum -= data[head];
        }
        else
        {
            ++count;
        }
        data[head] = v;
        sum += v;
        head = (head + 1) % N;
    }

    size_t Size() const { return N; }

    T At(size_t i) const
    {
        size_t start = head;
        return data[(start + i) % N];
    }

    float Average() const { return static_cast<float>(sum / static_cast<double>(N)); }
};

constexpr int plotWidth = 360;

// Frames of the 1% low (the last 1000) and the 0.1% low (all of them), recalculated twice a second
constexpr int statsFrames = 5000;
constexpr int onePercentFrames = 1000;

// The frame times the overlay shows, measured between the presents of the game's swapchain
struct FrameStats
{
    RingBuffer<float, plotWidth> frameTimes;
    RingBuffer<float, statsFrames> statsFrameTimes;
    RingBuffer<float, 100> recentFrameTimes;

    double lastPresentMs = 0.0;
    double statsLastUpdate = 0.0;
    float onePercentLowFps = 0.0f;
    float pointOneLowFps = 0.0f;
    float lastFrameTime = 0.0f;

    // The average of the last 100 frames
    double frameTime = 0.0;
    double frameRate = 0.0;
    float averageFrameTime = 0.0f;

    void Reset() { *this = FrameStats(); }

    // frameMs is the time since the previous present
    void OnFrame(double nowMs, float frameMs, bool lows, bool pointOneLow)
    {
        lastFrameTime = frameMs;
        frameTimes.Push(frameMs);
        recentFrameTimes.Push(frameMs);

        double sum = 0.0;
        int frames = 0;

        for (size_t i = 0; i < recentFrameTimes.Size(); i++)
        {
            const float ft = recentFrameTimes.At(i);

            if (ft > 0.0f)
            {
                sum += ft;
                frames++;
            }
        }

        frameTime = frames > 0 ? sum / frames : 0.0;
        frameRate = frameTime > 0.0 ? 1000.0 / frameTime : 0.0;
        averageFrameTime = frameTimes.Average();

        // Like FrameView: 1% low is the 99th percentile frame time
        if (!lows && !pointOneLow)
            return;

        statsFrameTimes.Push(frameMs);

        if (nowMs - statsLastUpdate < 500.0)
            return;

        statsLastUpdate = nowMs;

        // The frame time at the percentile of the last frames, oldest first in the buffer
        auto percentileFps = [this](size_t frames, double percentile) -> float
        {
            std::vector<float> times;
            times.reserve(frames);

            for (size_t i = statsFrames - frames; i < statsFrames; i++)
            {
                if (auto ft = statsFrameTimes.At(i); ft > 0.0f)
                    times.push_back(ft);
            }

            if (times.empty())
                return 0.0f;

            auto p = times.begin() + std::min((size_t) (times.size() * percentile), times.size() - 1);
            std::nth_element(times.begin(), p, times.end());
            return 1000.0f / *p;
        };

        onePercentLowFps = percentileFps(onePercentFrames, 0.99);

        if (pointOneLow)
            pointOneLowFps = percentileFps(statsFrames, 0.999);
    }
};
