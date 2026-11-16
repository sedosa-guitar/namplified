#include "PluginProcessor.h"

#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{
constexpr int sampleRate = 48000;
constexpr int blockSize = 128;

void require (bool condition, const char* message)
{
    if (! condition)
        throw std::runtime_error (message);
}

bool near (double actual, double expected, double tolerance)
{
    return std::abs (actual - expected) <= tolerance;
}

void set (NAMplifiedAudioProcessor& processor, const char* id, float value)
{
    auto* parameter = processor.parameters.getParameter (id);
    require (parameter != nullptr, id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

void looperTests()
{
    using Action = TrackLooper::Action;
    using State = TrackLooper::TrackState;
    TrackLooper looper;
    constexpr int rate = 1000;
    constexpr int beat = rate / 2;
    juce::AudioBuffer<float> buffer (1, beat);
    const auto advance = [&] (int samples, int meter = 4, float tempo = 120.0f)
    {
        while (samples > 0)
        {
            const auto count = juce::jmin (samples, buffer.getNumSamples());
            buffer.clear();
            looper.process (buffer, 1, count, true, false, tempo, meter);
            samples -= count;
        }
    };

    looper.prepare (rate);
    looper.request (0, Action::record);
    advance (1);
    require (looper.getState (0) == State::queued, "Track must be queued during count-in");
    for (int number = 1; number <= 4; ++number)
    {
        require (looper.getCountInBeat() == number, "4/4 should show four beats even with click disabled");
        advance (number == 4 ? beat - 1 : beat);
    }
    require (looper.getState (0) == State::queued, "Recording must wait for the full count-in");
    advance (3 * beat);
    require (looper.getState (0) == State::recording, "Recording should begin after count-in");
    looper.request (0, Action::record);
    advance (1);
    require (looper.isFinishingAtBarEnd(), "An early finish should wait for the bar line");
    advance (beat - 1);
    require (looper.getState (0) == State::playing && near (looper.getLengthSeconds(), 2.0, 0.001),
             "The early finish should produce one full 4/4 bar");

    looper.requestReset();
    advance (1, 3);
    looper.request (0, Action::record);
    advance (6 * beat + 100, 3);
    looper.request (0, Action::record);
    advance (1, 3);
    require (looper.getState (0) == State::playing && near (looper.getLengthSeconds(), 1.5, 0.001),
             "A late finish within half a beat should snap back to the previous 3/4 bar");

    looper.requestReset();
    advance (1, 7, 60.0f);
    looper.request (0, Action::record);
    advance (63000, 7, 60.0f);
    require (looper.getState (0) == State::playing && near (looper.getLengthSeconds(), 56.0, 0.001),
             "The time limit should stop at the last full 7/8 bar under 60 seconds");
}

void presetNameTests (NAMplifiedAudioProcessor& processor)
{
    for (const auto* name : { "Lead + Boost (A2)", "Clean_01", "Rhythm-2.0" })
        require (processor.canSavePresetName (name), "A supported name was rejected");
    for (const auto* name : { "", " ", " Lead", "Lead ", ".Lead", "../Lead", "Artist/Sedosa/Lead",
                              "Sub\\Lead", "Lead\nBreak", "Lead;command" })
        require (! processor.canSavePresetName (name), "An invalid preset name was accepted");
    require (processor.canSavePresetName (juce::String::repeatedString ("A", 32)), "32 characters should be allowed");
    require (! processor.canSavePresetName (juce::String::repeatedString ("A", 33)), "New names over 32 characters should be rejected");
}

void routingTests (NAMplifiedAudioProcessor& processor)
{
    auto layout = processor.getBusesLayout();
    layout.getChannelSet (true, 0) = juce::AudioChannelSet::mono();
    layout.getChannelSet (false, 0) = juce::AudioChannelSet::stereo();
    require (processor.setBusesLayout (layout), "Mono input must support stereo output");
    processor.prepareToPlay (sampleRate, blockSize);
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    const auto renderLevel = [&]
    {
        for (int block = 0; block < 12; ++block)
        {
            buffer.clear();
            for (int i = 0; i < blockSize; ++i)
                buffer.setSample (0, i, 0.025f);
            processor.processBlock (buffer, midi);
        }
        require (near (buffer.getSample (0, 64), buffer.getSample (1, 64), 0.00001),
                 "A mono input must reach both stereo outputs equally");
        return buffer.getSample (0, 64);
    };

    set (processor, "ampRouting", 1);
    set (processor, "amp1In", 0);
    set (processor, "amp2In", 6);
    set (processor, "amp1On", 1);
    set (processor, "amp2On", 0);
    const auto ampOne = renderLevel();
    set (processor, "amp1On", 0);
    set (processor, "amp2On", 1);
    const auto ampTwo = renderLevel();
    set (processor, "amp1On", 1);
    const auto blended = renderLevel();
    require (ampOne > 0.005f && near (ampTwo / ampOne, 2.0, 0.025),
             "With one amp enabled its full signal should reach the output");
    require (near (blended, (ampOne + ampTwo) * 0.5, 0.0005), "Two active amps should be mixed equally");

    set (processor, "amp1On", 0);
    set (processor, "amp2On", 0);
    set (processor, "delayOn", 1);
    set (processor, "delayMode", 2);
    set (processor, "delayType", 0);
    set (processor, "delayTime", 20);
    set (processor, "delayFeedback", 0.7f);
    set (processor, "delayMix", 0.5f);
    std::array<std::array<float, 2>, 4> peaks {};
    for (int offset = 0; offset < 3072; offset += blockSize)
    {
        buffer.clear();
        if (offset == 0)
            buffer.setSample (0, 0, 0.3f);
        processor.processBlock (buffer, midi);
        for (int i = 0; i < blockSize; ++i)
        {
            const auto position = offset + i;
            const auto window = position >= 2850 ? 3u : position >= 1890 ? 2u : position >= 930 ? 1u : 0u;
            for (size_t channel = 0; channel < 2; ++channel)
                peaks[window][channel] = juce::jmax (peaks[window][channel],
                    std::abs (buffer.getSample (static_cast<int> (channel), i)));
        }
    }
    require (peaks[0][0] > 0.01f && near (peaks[0][0], peaks[0][1], 0.00001), "Ping Pong dry signal must remain centered");
    for (size_t echo = 1; echo <= 3; ++echo)
    {
        const auto channel = (echo - 1) % 2;
        require (peaks[echo][channel] > 0.003f && peaks[echo][1 - channel] < 0.001f,
                 "Ping Pong echoes should alternate left, right, left");
    }
}

void pitchTests (NAMplifiedAudioProcessor& processor)
{
    processor.prepareToPlay (sampleRate, blockSize);
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    constexpr double lowE = 82.4069;
    constexpr auto twoPi = juce::MathConstants<double>::twoPi;
    for (const auto shift : { 0.0f, -12.0f, 0.0f })
    {
        set (processor, "pitchSemitones", shift);
        std::complex<double> original {}, octave {};
        int measured = 0;
        for (int offset = 0; offset < 2 * sampleRate; offset += blockSize)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto phase = twoPi * lowE * (offset + i) / sampleRate;
                const auto signal = static_cast<float> (0.1 * std::sin (phase));
                buffer.setSample (0, i, signal);
                buffer.setSample (1, i, signal);
            }
            processor.processBlock (buffer, midi);
            if (offset < sampleRate)
                continue;
            measured += blockSize;
            for (int i = 0; i < blockSize; ++i)
            {
                const auto phase = twoPi * lowE * (offset + i) / sampleRate;
                const auto signal = static_cast<double> (buffer.getSample (0, i));
                original += signal * std::polar (1.0, -phase);
                octave += signal * std::polar (1.0, -phase / 2.0);
            }
        }
        const auto dry = 2.0 * std::abs (original) / measured;
        const auto shifted = 2.0 * std::abs (octave) / measured;
        require (shift == 0 ? dry > 0.08 && shifted < 0.005 : shifted > 0.08 && dry < 0.01,
                 "Low E must retain its fundamental at 0 semitones and shift cleanly by one octave at -12");
    }
}
}

int main (int argc, char** argv)
{
    try
    {
        require (argc >= 2, "A test suite name is required");
        const std::string_view suite = argv[1];
        if (suite == "looper")
        {
            looperTests();
        }
        else
        {
            require (argc == 3, "Processor tests require an isolated test home");
            const juce::File testHome (argv[2]);
            const auto actualHome = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
            require (actualHome == testHome, "Refusing to initialise the processor outside the test home");
            juce::ScopedJuceInitialiser_GUI gui;
            auto processor = std::make_unique<NAMplifiedAudioProcessor>();
            set (*processor, "gateOn", 0);
            set (*processor, "reverbOn", 0);
            set (*processor, "ir1On", 0);
            if (suite == "preset_names")
                presetNameTests (*processor);
            else if (suite == "routing")
                routingTests (*processor);
            else if (suite == "pitch")
                pitchTests (*processor);
            else
                require (false, "Unknown test suite");
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
