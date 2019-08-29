#pragma once

template <typename T>
struct FrameTimer
{
    uint32_t m_totalFrames = 0;
    winrt::Windows::Foundation::TimeSpan m_totalTimeBetweenFrames = winrt::Windows::Foundation::TimeSpan::zero();
    T m_lastTimestamp;

    void RecordTimestamp(T const& timestamp)
    {
        if (m_totalFrames > 0)
        {
            auto timeBetweenFrames = std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(timestamp - m_lastTimestamp);
            m_totalTimeBetweenFrames += timeBetweenFrames;
        }

        m_totalFrames++;
        m_lastTimestamp = timestamp;
    }

    std::chrono::duration<double, std::milli> ComputeAverageFrameTime()
    {
        return m_totalTimeBetweenFrames / (double)m_totalFrames;
    }
};