// Adapted from Airwindows StereoChorus.
// Copyright (c) 2016 airwindows. MIT license.
#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>

class StereoChorus
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        cycleLength = juce::jlimit (1, 4, static_cast<int> (spec.sampleRate / 44100.0));
        coreRate = spec.sampleRate / cycleLength;
        channels = {};
        channels[0].phase = juce::MathConstants<double>::pi / 2.7;
        channels[1].phase = juce::MathConstants<double>::pi;
        cycle = 0;
        flip = false;
        initialized = false;
        for (auto* parameter : { &rate, &depth, &mix })
            parameter->reset (spec.sampleRate, 0.03);
    }

    void process (juce::AudioBuffer<float>& buffer, int numChannels, int samples,
                  float rateHz, float modulationDepth, float wetMix)
    {
        setParameter (rate, rateHz);
        setParameter (depth, modulationDepth);
        setParameter (mix, wetMix);
        initialized = true;

        for (int i = 0; i < samples; ++i)
        {
            const auto wet = mix.getNextValue();
            const auto speed = juce::MathConstants<double>::twoPi * rate.getNextValue() / coreRate;
            const auto excursion = juce::jmin ((depth.getNextValue() / 60.0) / speed,
                                                (delayLength - 3.0) / 2.0);
            const auto update = ++cycle == cycleLength;
            if (update)
                cycle = 0;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& voice = channels[static_cast<size_t> (ch)];
                auto* data = buffer.getWritePointer (ch);
                const auto dry = data[i];
                if (update)
                {
                    voice.previousWet = voice.currentWet;
                    voice.currentWet = voice.process (dry, speed, excursion, flip);
                }
                const auto effected = cycleLength == 1 ? voice.currentWet
                    : voice.previousWet + (voice.currentWet - voice.previousWet) * cycle / cycleLength;
                data[i] = static_cast<float> (dry + wet * (effected - dry));
            }
            if (update)
                flip = ! flip;
        }
    }

private:
    static constexpr int delayLength = 32760;

    struct Voice
    {
        double process (double input, double speed, double depth, bool flip)
        {
            // Airwindows' alternating air compensation before the delay tap.
            const auto difference = previousInput - input;
            auto& active = flip ? even : odd;
            auto& inactive = flip ? odd : even;
            active += difference;
            inactive -= difference;
            const auto air = active;
            odd = (odd - (odd - even) / 256.0) / 1.0001;
            even = (even - (even - odd) / 256.0) / 1.0001;
            previousInput = input;
            delay[static_cast<size_t> (head)] = input + air;

            const auto offset = depth * (1.0 + std::sin (phase));
            const auto whole = static_cast<int> (offset);
            const auto fraction = offset - whole;
            const auto tap = [this, whole] (int n)
            {
                return delay[static_cast<size_t> ((head + whole + n) % delayLength)];
            };
            const auto a = tap (0);
            const auto b = tap (1);
            const auto c = tap (2);
            const auto result = 0.5 * (a * (1.0 - fraction) + b + c * fraction - (a - 2.0 * b + c) / 50.0);

            head = head == 0 ? delayLength - 1 : head - 1;
            phase += speed;
            if (phase >= juce::MathConstants<double>::twoPi)
                phase -= juce::MathConstants<double>::twoPi;
            return result;
        }

        std::array<double, delayLength> delay {};
        int head = 0;
        double phase = 0.0, previousInput = 0.0, even = 0.0, odd = 0.0;
        double previousWet = 0.0, currentWet = 0.0;
    };

    void setParameter (juce::SmoothedValue<float>& parameter, float value)
    {
        if (initialized)
            parameter.setTargetValue (value);
        else
            parameter.setCurrentAndTargetValue (value);
    }

    std::array<Voice, 2> channels;
    juce::SmoothedValue<float> rate, depth, mix;
    double coreRate = 44100.0;
    int cycleLength = 1, cycle = 0;
    bool flip = false, initialized = false;
};
