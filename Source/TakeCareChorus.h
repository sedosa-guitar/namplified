// TakeCare DSP adapted from Airwindows. Copyright (c) airwindows. MIT license.
#pragma once

#include <JuceHeader.h>

class TakeCareChorus
{
public:
    struct Advanced
    {
        float random, regen, derez, buffer, output;
    };

    TakeCareChorus();
    ~TakeCareChorus();
    void prepare (const juce::dsp::ProcessSpec&);
    void process (juce::AudioBuffer<float>&, int channels, int samples,
                  float rateHz, float depth, float mix, const Advanced&);

private:
    struct State;
    std::unique_ptr<State> state;
    double sampleRate = 44100.0;
};
