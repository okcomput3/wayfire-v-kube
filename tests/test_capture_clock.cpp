#include "../wf-v-kube/src/capture-clock.hpp"
#include <cassert>
#include <iostream>
int main()
{
    for (int hz : {60, 120, 144, 165})
    {
        cube_capture_clock_t clock;
        int captures = 0;
        for (int frame = 0; frame < hz * 10; ++frame)
            captures += clock.refresh(double(frame) / hz, 30, false);
        assert(captures == 300);
    }
    cube_capture_clock_t clock;
    assert(clock.refresh(1, 30, false));
    assert(!clock.refresh(1.001, 30, false));
    assert(clock.refresh(1.002, 30, true)); // closing, resize or new visible face
    assert(!clock.refresh(1.003, 30, false));
    assert(clock.refresh(1.004, 0, false));
    assert(clock.refresh(1.005, 0, false)); // unrestricted
    assert(clock.refresh(0, 30, true)); // reopening
    std::cout << "PASS: 30 Hz captures on 60/120/144/165 Hz cameras; immediate invalidation and unlimited mode\n";
}
