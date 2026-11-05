#pragma once

#include <chowdsp_wdf/chowdsp_wdf.h>

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
