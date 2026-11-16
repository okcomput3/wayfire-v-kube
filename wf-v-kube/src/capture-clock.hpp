#pragma once
#include <cmath>
#include <algorithm>

// Capture cadence is independent of camera redraws. Keep fractional time so
// a 30 Hz capture limit on a 144 Hz output does not drop to 28.8 Hz.
struct cube_capture_clock_t
{
    double next = -1;
    bool refresh(double now, int fps, bool force)
    {
        if (force || fps <= 0 || next < 0 || now < next - 1.0)
        {
            next = fps > 0 ? now + 1.0 / fps : now;
            return true;
        }
        if (now + 1e-6 < next) return false;
        const double step = 1.0 / fps;
        next += (std::floor(std::max(0.0, now - next) / step) + 1) * step;
        return true;
    }
};
