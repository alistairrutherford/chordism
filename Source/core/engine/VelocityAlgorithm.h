// Velocity Algorithm (PLAN.md §4.5): replaces a note's velocity with a curve value at its
// position in the bar.
//
// Each curve is 16 values (0..1), one per sixteenth of the bar (a bar of another length is
// divided into 16 equal parts). A note-on at position x of the bar gets
//
//   velocity = Peak · (1 − Range · (1 − curve[⌊16x⌋]))
//
// so Range 0% plays everything at Peak, and Range 100% spans 0..Peak (at least 1). The linear
// curves are Up, Down (across the bar), Up/Down ½ and ¼ (across each half or quarter bar),
// Up/Down (a peak mid-bar) and Down/Up (a dip mid-bar). Afrobeat, Breakbeat, Dancehall, House
// and Moombahton are Chordism's own accent patterns. In the chord zone the curve applies to
// every note-on: plain chords, strums, arps, repeats and sequencer steps. Plain C++.
#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace chordism
{
    // The order of the Velocity parameter's choices: only ever append.
    enum class VelocityCurve : uint8_t
    {
        Off,
        Up, Down, UpHalf, DownHalf, UpQuarter, DownQuarter, UpDown, DownUp,
        Afrobeat, Breakbeat, Dancehall, House, Moombahton,
        Count
    };
    constexpr int numVelocityCurves = (int) VelocityCurve::Count;   // Off + 13

    inline const char* velocityCurveName (VelocityCurve c) noexcept
    {
        static constexpr const char* names[numVelocityCurves] = {
            "Off", "Up", "Down", "Up 1/2", "Down 1/2", "Up 1/4", "Down 1/4", "Up/Down", "Down/Up",
            "Afrobeat", "Breakbeat", "Dancehall", "House", "Moombahton"
        };
        const int i = (int) c;
        return names[i >= 0 && i < numVelocityCurves ? i : 0];
    }

    using VelocityTable = std::array<float, 16>;

    // Curve value (0..1) for sixteenth `step` (0..15) of the bar.
    inline double velocityCurveValue (VelocityCurve c, int step) noexcept
    {
        static constexpr VelocityTable afrobeat   { 1.0f, .30f, .55f, .85f, .35f, .55f, .90f, .30f, .60f, .35f, .85f, .30f, .95f, .35f, .55f, .40f };
        static constexpr VelocityTable breakbeat  { 1.0f, .25f, .45f, .25f, .90f, .25f, .45f, .55f, .40f, .25f, 1.0f, .25f, .90f, .25f, .45f, .60f };
        static constexpr VelocityTable dancehall  { 1.0f, .30f, .35f, .90f, .30f, .35f, .90f, .30f, 1.0f, .30f, .35f, .90f, .30f, .35f, .90f, .30f };
        static constexpr VelocityTable house      { .60f, .25f, 1.0f, .35f, .60f, .25f, 1.0f, .35f, .60f, .25f, 1.0f, .35f, .60f, .25f, 1.0f, .45f };
        static constexpr VelocityTable moombahton { 1.0f, .30f, .40f, .90f, .50f, .30f, .90f, .30f, 1.0f, .30f, .40f, .90f, .50f, .30f, .90f, .35f };

        const int i = ((step % 16) + 16) % 16;
        switch (c)
        {
            case VelocityCurve::Up:          return i / 15.0;
            case VelocityCurve::Down:        return 1.0 - i / 15.0;
            case VelocityCurve::UpHalf:      return (i % 8) / 7.0;
            case VelocityCurve::DownHalf:    return 1.0 - (i % 8) / 7.0;
            case VelocityCurve::UpQuarter:   return (i % 4) / 3.0;
            case VelocityCurve::DownQuarter: return 1.0 - (i % 4) / 3.0;
            case VelocityCurve::UpDown:      return 1.0 - std::abs (i - 8) / 8.0;
            case VelocityCurve::DownUp:      return std::abs (i - 8) / 8.0;
            case VelocityCurve::Afrobeat:    return afrobeat[(size_t) i];
            case VelocityCurve::Breakbeat:   return breakbeat[(size_t) i];
            case VelocityCurve::Dancehall:   return dancehall[(size_t) i];
            case VelocityCurve::House:       return house[(size_t) i];
            case VelocityCurve::Moombahton:  return moombahton[(size_t) i];
            case VelocityCurve::Off: case VelocityCurve::Count:
            default:                         return 1.0;
        }
    }

    struct VelocitySettings
    {
        VelocityCurve curve = VelocityCurve::Off;
        int peak = 127;                   // 1..127
        double range = 0.5;               // 0..1

        // The velocity for a note-on at `barFraction` (0 ≤ x < 1) of the bar; `input` when Off.
        int apply (int input, double barFraction) const noexcept
        {
            if (curve == VelocityCurve::Off)
                return input;
            int step = (int) std::floor (barFraction * 16.0 + 1.0e-9);
            step = step < 0 ? 0 : (step > 15 ? 15 : step);
            const double r = range < 0.0 ? 0.0 : (range > 1.0 ? 1.0 : range);
            const int p = peak < 1 ? 1 : (peak > 127 ? 127 : peak);
            const int v = (int) std::lround (p * (1.0 - r * (1.0 - velocityCurveValue (curve, step))));
            return v < 1 ? 1 : (v > 127 ? 127 : v);
        }
    };
}
