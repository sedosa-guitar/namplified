#pragma once

#include "freeverb/earlyref.hpp"
#include "freeverb/progenitor2.hpp"

#include <algorithm>
#include <array>
#include <cmath>

class DragonflyRoom
{
public:
    void prepare (double sampleRate)
    {
        early.loadPresetReflection (FV3_EARLYREF_PRESET_1);
        early.setdryr (0);
        early.setwet (0);
        early.setSampleRate (sampleRate);
        early.setRSFactor (0.8f);
        early.setLRDelay (0.3f);
        early.setLRCrossApFreq (750, 4);
        early.setDiffusionApFreq (150, 4);
        late.setdryr (0);
        late.setwet (0);
        late.setSampleRate (sampleRate);
        late.setRSFactor (0.8f);
        early.mute();
        late.mute();
        previousSize = previousDamping = -1.0f;
    }

    void process (float* left, float* right, int samples, float size, float damping, float width,
                  float roomSize, float earlyMix, float spin, float wander)
    {
        if (size != previousSize)
        {
            late.setrt60 (0.25f + size * 2.75f);
            previousSize = size;
        }
        if (damping != previousDamping)
        {
            const auto cutoff = 14000.0f * std::pow (0.1f, damping);
            early.setoutputlpf (cutoff);
            late.setdamp (cutoff);
            late.setoutputdamp (cutoff);
            previousDamping = damping;
        }
        early.setTapScale (roomSize);
        if (spin != late.getspin())
            late.setspin (spin);
        late.setwander (wander);
        early.setwidth (width * 0.83f);
        late.setwidth (width);

        for (int offset = 0; offset < samples; offset += chunkSize)
        {
            const auto count = std::min (chunkSize, samples - offset);
            auto* inL = left + offset;
            auto* inR = right + offset;
            early.processreplace (inL, inR, earlyL.data(), earlyR.data(), count);
            for (int i = 0; i < count; ++i)
            {
                lateL[i] = inL[i] + 0.2f * earlyL[i];
                lateR[i] = inR[i] + 0.2f * earlyR[i];
            }
            late.processreplace (lateL.data(), lateR.data(), inL, inR, count);
            for (int i = 0; i < count; ++i)
            {
                inL[i] = earlyMix * earlyL[i] + (1.0f - earlyMix) * inL[i];
                inR[i] = earlyMix * earlyR[i] + (1.0f - earlyMix) * inR[i];
            }
        }
    }

private:
    static constexpr int chunkSize = 256;
    std::array<float, chunkSize> earlyL {}, earlyR {}, lateL {}, lateR {};
    fv3::earlyref_f early;
    fv3::progenitor2_f late;
    float previousSize = -1.0f, previousDamping = -1.0f;
};
