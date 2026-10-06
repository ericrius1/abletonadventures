#pragma once

#include <array>
#include <cmath>
#include <algorithm>

namespace boing
{
constexpr int maxBounces = 24;
constexpr double maxDelaySeconds = 12.0;

enum class Mode { bounce = 0, rise, steady };

/** The schedule of echoes: when each bounce lands (seconds after the input) and how loud it is.
    Shared by the DSP and the UI so the picture always matches the sound. */
struct Schedule
{
    int count = 0;
    std::array<double, maxBounces> time {};      // seconds after the dry hit
    std::array<double, maxBounces> interval {};  // flight time of the arc that ends at this bounce
    std::array<float, maxBounces> gain {};       // linear amplitude
    double totalTime = 0.0;
};

/** firstInterval: seconds before the first landing; restitution: 0..1 (each flight is this
    fraction of the previous); damping: 0..1 (amplitude lost per bounce). */
inline Schedule computeSchedule (double firstInterval, double restitution, int bounces, double damping, Mode mode)
{
    Schedule s;
    bounces = std::clamp (bounces, 1, maxBounces);
    const double r = std::clamp (restitution, 0.05, 0.99);
    const float perBounce = (float) (1.0 - std::clamp (damping, 0.0, 1.0) * 0.55);

    std::array<double, maxBounces> intervals {};
    for (int k = 0; k < bounces; ++k)
    {
        switch (mode)
        {
            case Mode::bounce: intervals[(size_t) k] = firstInterval * std::pow (r, k); break;
            case Mode::rise:   intervals[(size_t) k] = firstInterval * std::pow (r, bounces - 1 - k); break;
            case Mode::steady: intervals[(size_t) k] = firstInterval; break;
        }
    }

    double t = 0.0;
    float g = 1.0f;
    for (int k = 0; k < bounces; ++k)
    {
        const double next = t + intervals[(size_t) k];
        if (next > maxDelaySeconds - 0.01)
            break;
        t = next;

        // Each impact is a bit quieter; in rise mode the echoes swell in instead.
        const float level = mode == Mode::rise ? std::pow (perBounce, (float) (bounces - 1 - k)) : g;
        s.time[(size_t) s.count] = t;
        s.interval[(size_t) s.count] = intervals[(size_t) k];
        s.gain[(size_t) s.count] = level * 0.85f;
        ++s.count;
        g *= perBounce;
    }

    s.totalTime = t;
    return s;
}
} // namespace boing
