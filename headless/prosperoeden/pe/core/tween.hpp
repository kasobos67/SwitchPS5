// ProsperoEden - Easing curves and springs for UI motion.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cmath>

namespace pe::tween
{

inline float clamp01(float t)
{
    return std::clamp(t, 0.0f, 1.0f);
}
inline float cubic_out(float t)
{
    t = clamp01(t);
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
}
inline float cubic_in_out(float t)
{
    t = clamp01(t);
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
}
inline float expo_out(float t)
{
    t = clamp01(t);
    return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
}
// Overshoots slightly before settling (card pops, star bursts).
inline float back_out(float t)
{
    t = clamp01(t);
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.0f;
    const float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}
inline float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

// Critically damped spring toward a target; frame-rate independent.
struct Spring
{
    float value = 0.0f;
    float velocity = 0.0f;
    float target = 0.0f;

    void snap(float v)
    {
        value = target = v;
        velocity = 0.0f;
    }
    // omega ~ responsiveness (rad/s): 12 snappy, 6 soft.
    void update(float dt, float omega = 12.0f)
    {
        // Exact solution of the critically damped oscillator over dt.
        const float x = value - target;
        const float e = std::exp(-omega * dt);
        const float next_x = (x + (velocity + omega * x) * dt) * e;
        velocity = (velocity - omega * (velocity + omega * x) * dt) * e;
        value = target + next_x;
        if (std::fabs(value - target) < 1e-4f && std::fabs(velocity) < 1e-3f)
            snap(target);
    }
    bool settled() const
    {
        return value == target && velocity == 0.0f;
    }
};

// A one-shot timeline value: 0 -> 1 over duration seconds.
struct Timer
{
    float elapsed = 0.0f;
    float duration = 0.25f;
    bool running = false;

    void start(float seconds)
    {
        elapsed = 0.0f;
        duration = seconds;
        running = true;
    }
    void update(float dt)
    {
        if (!running)
            return;
        elapsed += dt;
        if (elapsed >= duration)
        {
            elapsed = duration;
            running = false;
        }
    }
    float progress() const
    {
        return duration > 0.0f ? clamp01(elapsed / duration) : 1.0f;
    }
};

} // namespace pe::tween
