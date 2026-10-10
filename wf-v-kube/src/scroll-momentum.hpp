#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Estimate a short continuation when a two-finger scroll ends. The cube's
// existing eased zoom animation interpolates to that destination, so momentum
// needs no extra render loop and cannot continue indefinitely.
class scroll_momentum_t
{
    bool tracking = false;
    uint32_t last_time = 0;
    double velocity = 0.0;

  public:
    void reset()
    {
        tracking = false;
        velocity = 0.0;
    }

    double update(double delta, uint32_t time)
    {
        if (!std::isfinite(delta))
        {
            reset();
            return 0.0;
        }
        const uint32_t elapsed = time - last_time;
        if (delta == 0.0)
        {
            // Pausing before lifting the fingers should stop the view, not
            // replay an old fling. Ignore duplicate stop notifications too.
            const double continuation = tracking && elapsed <= 100 ?
                std::clamp(velocity * 120.0, -30.0, 30.0) : 0.0;
            reset();
            return continuation;
        }

        if (!tracking || elapsed > 100)
        {
            velocity = 0.0;
        } else
        {
            const double sample = delta / std::max(1u, elapsed);
            // A direction change immediately replaces the old momentum.
            velocity = velocity * sample < 0.0 ? sample :
                0.5 * velocity + 0.5 * sample;
        }
        tracking = true;
        last_time = time;
        return delta;
    }
};
