#pragma once

#include <chowdsp_wdf/chowdsp_wdf.h>
#include <algorithm>
#include <cmath>

namespace lead_wdft = chowdsp::wdft;

/** A small diode-pair boost stage built from chowdsp_wdf components.
    Each channel owns a separate circuit instance, so the WDF state never
    crosses between left and right audio. */
class WdfBoostCircuit
{
public:
    using Float = float;
    WdfBoostCircuit()
        : inverter (source), diodePair (inverter, 2.52e-9f)
    {
    }

    float processSample (float input, float linearGain) noexcept
    {
        // Map digital full scale to a nominal guitar-pedal signal around 0.7 V.
        constexpr float fullScaleVolts = 0.7f;
        const auto drivenInput = input * linearGain * fullScaleVolts;
        source.setVoltage (drivenInput);
        diodePair.incident (inverter.reflected());
        inverter.incident (diodePair.reflected());
        return lead_wdft::voltage<Float> (diodePair) / fullScaleVolts;
    }

private:
    lead_wdft::ResistiveVoltageSourceT<Float> source { 1000.0f };
    lead_wdft::PolarityInverterT<Float, decltype (source)> inverter;
    lead_wdft::DiodePairT<Float, decltype (inverter)> diodePair;
};

/** Independent Tube Screamer-inspired stage. This is an original, compact
    circuit model: input high-pass, WDF diode clipping, and a variable tone
    low-pass. It does not copy third-party pedal source code. */
class TubeScreamerCircuit
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = std::max (1000.0, newSampleRate);
        inputLowState = 0.0f;
        toneState = 0.0f;
    }

    void setToneFrequency (float frequencyHz) noexcept
    {
        const auto frequency = std::clamp (frequencyHz, 500.0f, 7500.0f);
        toneCoefficient = static_cast<float> (1.0 - std::exp (-2.0 * 3.141592653589793
                                                           * frequency / sampleRate));
    }

    float processSample (float input, float driveGain, float levelGain) noexcept
    {
        // A first-order 720 Hz input high-pass gives the familiar tight low end.
        constexpr float inputHighPassHz = 720.0f;
        const auto hpCoefficient = static_cast<float> (1.0 - std::exp (
            -2.0 * 3.141592653589793 * inputHighPassHz / sampleRate));
        inputLowState += hpCoefficient * (input - inputLowState);
        const auto highPassed = input - inputLowState;
        auto clipped = clipper.processSample (highPassed, driveGain);
        toneState += toneCoefficient * (clipped - toneState);
        return toneState * levelGain;
    }

private:
    WdfBoostCircuit clipper;
    double sampleRate = 44100.0;
    float inputLowState = 0.0f;
    float toneState = 0.0f;
    float toneCoefficient = 0.24f;
};
