#include "PluginProcessor.h"

#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

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

void looperTrackTests()
{
    using Action = TrackLooper::Action;
    using State = TrackLooper::TrackState;
    constexpr int rate = 8000;
    TrackLooper looper;
    looper.prepare (rate);
    juce::AudioBuffer<float> buffer (1, rate);
    const auto render = [&] (int count, float input = 0.0f, bool click = false)
    {
        juce::FloatVectorOperations::fill (buffer.getWritePointer (0), input, count);
        looper.process (buffer, 1, count, true, click, 120.0f, 2);
    };

    looper.request (0, Action::record);
    render (rate);
    render (rate, 0.1f);
    looper.request (0, Action::record);
    render (rate);
    require (looper.getState (0) == State::playing, "First track should loop after recording");
    const auto first = buffer.getSample (0, 100);
    require (first > 0.05f, "First track should play its recorded input");

    looper.request (1, Action::record);
    render (rate);
    render (rate, 0.2f, true);
    require (looper.getState (1) == State::playing, "Second track should stop at the same loop boundary");
    render (rate);
    for (int i = 80; i < 160; ++i)
        require (near (buffer.getSample (0, i), 3.0f * first, 0.0001),
                 "Second track should record guitar alone, excluding playback and the click");

    looper.setTrackLevel (1, 0.5f);
    render (rate);
    require (near (buffer.getSample (0, rate / 2), 2.0f * first, 0.0001),
             "Track level should change only the selected track");
    looper.request (1, Action::mute);
    render (rate);
    require (near (buffer.getSample (0, 100), first, 0.0001), "Muting should leave the other track audible");
    looper.request (0, Action::clear);
    render (rate);
    require (looper.getState (0) == State::empty && looper.getState (1) == State::muted,
             "Clearing one track should preserve the other");
}

void presetRoundtripTests (std::unique_ptr<NAMplifiedAudioProcessor>& processor)
{
    constexpr auto name = "Regression Roundtrip + FX";
    constexpr std::array order { 3, 1, 0, 2 };
    set (*processor, "delayTime", 320.0f);
    set (*processor, "chorusOn", 1);
    set (*processor, "masterVolume", -6.0f);
    processor->setPreFxOrder (order);
    require (processor->saveUserPreset (name), "Could not save the regression preset");

    processor.reset();
    processor = std::make_unique<NAMplifiedAudioProcessor>();
    set (*processor, "delayTime", 120.0f);
    set (*processor, "chorusOn", 0);
    set (*processor, "masterVolume", -15.0f);
    processor->setPreFxOrder ({ 0, 1, 2, 3 });
    juce::StringArray missingAssets;
    require (processor->loadUserPreset (name, missingAssets) && missingAssets.isEmpty(),
             "Fresh processor should load the saved preset");
    const auto value = [&] (const char* id) { return processor->parameters.getRawParameterValue (id)->load(); };
    require (near (value ("delayTime"), 320.0f, 0.01) && value ("chorusOn") == 1.0f,
             "Loading should restore saved effect parameters");
    require (processor->getPreFxOrder() == order, "Loading should restore the saved effect order");
    require (near (value ("masterVolume"), -15.0f, 0.01), "Loading should preserve the current global master volume");
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

    set (processor, "ampParallelBlend", 0.8f);
    require (near (renderLevel(), ampOne * 0.8f + ampTwo * 0.2f, 0.0005),
             "Parallel blend should weight the two active amps");
    set (processor, "ampParallelBlend", 1.0f);
    require (near (renderLevel(), ampOne, 0.0005), "100/0 should isolate Amp 1");
    set (processor, "ampParallelBlend", 0.0f);
    require (near (renderLevel(), ampTwo, 0.0005), "0/100 should isolate Amp 2");
    set (processor, "amp2On", 0);
    require (near (renderLevel(), ampOne, 0.0005), "The remaining amp should receive 100% regardless of blend");
    set (processor, "ampParallelBlend", 0.5f);
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

juce::File makeIR (const juce::File& testHome, const char* filename, float polarity, bool stereo = false)
{
    const auto file = testHome.getChildFile (filename);
    auto stream = std::unique_ptr<juce::OutputStream> (file.createOutputStream());
    require (stream != nullptr, "Could not open the temporary IR file");
    stream->setPosition (0);
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions {}
        .withSampleRate (sampleRate).withNumChannels (stereo ? 2 : 1).withBitsPerSample (24));
    require (writer != nullptr, "Could not create the IR fixture");
    juce::AudioBuffer<float> ir (stereo ? 2 : 1, 32);
    ir.clear();
    ir.setSample (0, 0, polarity * 0.5f);
    ir.setSample (0, 8, polarity * 0.25f);
    if (stereo)
    {
        ir.setSample (1, 0, polarity);
        ir.setSample (1, 8, polarity * 0.5f);
    }
    require (writer->writeFromAudioSampleBuffer (ir, 0, ir.getNumSamples()), "Could not write the IR fixture");
    return file;
}

void cabTests (NAMplifiedAudioProcessor& processor, const juce::File& testHome)
{
    processor.loadImpulseResponse (0, makeIR (testHome, "cab-positive.wav", 1.0f));
    processor.loadImpulseResponse (1, makeIR (testHome, "cab-negative.wav", -1.0f));
    require (processor.getImpulseResponseName (0) == "cab-positive.wav"
             && processor.getImpulseResponseName (1) == "cab-negative.wav", "Both fixtures should load");

    processor.prepareToPlay (sampleRate, blockSize);
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    const auto render = [&] (bool cabOne, bool cabTwo)
    {
        set (processor, "ir1On", cabOne ? 1.0f : 0.0f);
        set (processor, "ir2On", cabTwo ? 1.0f : 0.0f);
        for (int block = 0; block < 12; ++block)
        {
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                for (int i = 0; i < blockSize; ++i)
                    buffer.setSample (ch, i, 0.02f);
            processor.processBlock (buffer, midi);
        }
        return buffer.getSample (0, 64);
    };

    const auto dry = render (false, false);
    const auto cabOne = render (true, false);
    const auto cabTwo = render (false, true);
    const auto both = render (true, true);
    require (dry > 0.005f, "Both disabled cabs should pass the signal through");
    require (cabOne > 0.005f && near (cabTwo, -cabOne, 0.0005),
             "Each isolated cab should use its own IR and preserve its polarity");
    require (std::abs (both) < 0.0005f, "The two opposite-polarity IRs should cancel at 50/50");
    set (processor, "irParallelBlend", 0.8f);
    require (near (render (true, true), cabOne * 0.8f + cabTwo * 0.2f, 0.0005),
             "Cabinet blend should weight the two active IRs");
    set (processor, "irParallelBlend", 1.0f);
    require (near (render (true, true), cabOne, 0.0005), "100/0 should isolate Cab 1");
    set (processor, "irParallelBlend", 0.0f);
    require (near (render (true, true), cabTwo, 0.0005), "0/100 should isolate Cab 2");
    require (near (render (true, false), cabOne, 0.0005) && near (render (false, true), cabTwo, 0.0005),
             "Turning either cab off should restore full level on the remaining branch");

    juce::MemoryBlock validState;
    processor.getStateInformation (validState);
    auto invalidState = juce::AudioProcessor::getXmlFromBinary (validState.getData(), static_cast<int> (validState.getSize()));
    require (invalidState != nullptr, "Cabinet test state should be readable");
    const auto unreadableIR = testHome.getChildFile ("unreadable.wav");
    require (unreadableIR.replaceWithText ("Invalid WAV"), "The unreadable IR fixture should be created");
    invalidState->setAttribute ("irFile2", unreadableIR.getFullPathName());
    juce::MemoryBlock invalidStateData;
    juce::AudioProcessor::copyXmlToBinary (*invalidState, invalidStateData);
    processor.setStateInformation (invalidStateData.getData(), static_cast<int> (invalidStateData.getSize()));
    require (processor.getImpulseResponseName (1) == "No IR loaded", "An unreadable IR must not be shown as loaded");
    require (near (render (false, true), dry, 0.0005), "An unreadable IR must not reuse the previous IR");
    processor.setStateInformation (validState.getData(), static_cast<int> (validState.getSize()));
    require (near (render (false, true), cabTwo, 0.0005), "A subsequent valid state should restore its IR");
}

void centeredCabTests (NAMplifiedAudioProcessor& processor, const juce::File& testHome)
{
    processor.setCenteredInput (true);
    processor.loadImpulseResponse (0, makeIR (testHome, "unequal-stereo.wav", 1.0f, true));
    set (processor, "ir1On", 1);
    processor.prepareToPlay (sampleRate, blockSize);
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    for (int block = 0; block < 64; ++block)
    {
        buffer.clear();
        juce::FloatVectorOperations::fill (buffer.getWritePointer (1), 0.05f, blockSize);
        processor.processBlock (buffer, midi);
    }
    require (buffer.getMagnitude (0, blockSize) > 0.001f, "The centered cab must remain audible");
    for (int i = 0; i < blockSize; ++i)
        require (near (buffer.getSample (0, i), buffer.getSample (1, i), 0.00001),
                 "A stereo cabinet IR with unequal channels must keep centered guitar equally loud on both outputs");
}

void delayTests (NAMplifiedAudioProcessor& processor)
{
    set (processor, "delayOn", 1);
    set (processor, "delayTime", 20);
    set (processor, "delayFeedback", 0);
    set (processor, "delayMix", 1);
    set (processor, "delayMode", 1);
    set (processor, "delayType", 0);
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    const auto render = [&] (bool monoOutput)
    {
        set (processor, "outputMode", monoOutput ? 1.0f : 0.0f);
        processor.prepareToPlay (sampleRate, blockSize);
        std::array<std::array<float, 2>, 3> peaks {};
        for (int offset = 0; offset < 3072; offset += blockSize)
        {
            buffer.clear();
            if (offset == 0)
                buffer.setSample (1, 0, 0.25f);
            processor.processBlock (buffer, midi);
            for (int i = 0; i < blockSize; ++i)
            {
                const auto position = offset + i;
                const auto window = position >= 1890 ? 2u : position >= 930 ? 1u : 0u;
                for (size_t ch = 0; ch < 2; ++ch)
                    peaks[window][ch] = juce::jmax (peaks[window][ch],
                        std::abs (buffer.getSample (static_cast<int> (ch), i)));
            }
        }
        return peaks;
    };

    const auto stereo = render (false);
    require (stereo[0][1] < 0.001f && stereo[1][1] > 0.02f && stereo[1][0] < 0.001f,
             "Fully wet Standard delay should repeat only on the input side after the selected time");
    require (stereo[2][1] < 0.001f, "Zero feedback should not create a second echo");
    const auto mono = render (true);
    require (near (mono[1][0], stereo[1][1] * 0.5, 0.001) && near (mono[1][0], mono[1][1], 0.00001),
             "Global Mono must fold the Standard delay to both outputs");
}

void chorusTests (NAMplifiedAudioProcessor& processor)
{
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    const auto render = [&] (bool enabled, float mode, float mix)
    {
        set (processor, "chorusOn", enabled ? 1.0f : 0.0f);
        set (processor, "chorusMode", mode);
        set (processor, "chorusMix", mix);
        processor.prepareToPlay (sampleRate, blockSize);
        std::vector<std::array<float, 2>> result;
        for (int offset = 0; offset < sampleRate; offset += blockSize)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto time = static_cast<double> (offset + i) / sampleRate;
                const auto signal = 0.05f * static_cast<float> (
                    std::sin (juce::MathConstants<double>::twoPi * 330.0 * time));
                for (int ch = 0; ch < 2; ++ch)
                    buffer.setSample (ch, i, signal);
            }
            processor.processBlock (buffer, midi);
            if (offset >= sampleRate / 2)
                for (int i = 0; i < blockSize; ++i)
                    result.push_back ({ buffer.getSample (0, i), buffer.getSample (1, i) });
        }
        return result;
    };

    const auto dry = render (false, 0, 0);
    for (const auto mode : { 0.0f, 1.0f })
    {
        const auto zeroMix = render (true, mode, 0);
        const auto wet = render (true, mode, 1);
        double wetEnergy = 0.0, changeEnergy = 0.0, sideEnergy = 0.0;
        for (size_t i = 0; i < dry.size(); ++i)
        {
            require (near (zeroMix[i][0], dry[i][0], 0.0001)
                     && near (zeroMix[i][1], dry[i][1], 0.0001),
                     "Zero chorus mix should match the dry signal on both channels");
            const auto output = wet[i][0];
            const auto change = output - dry[i][0];
            const auto side = output - wet[i][1];
            require (std::isfinite (output) && std::isfinite (wet[i][1]), "Chorus output must remain finite");
            wetEnergy += output * output;
            changeEnergy += change * change;
            sideEnergy += side * side;
        }
        const auto samples = static_cast<double> (wet.size());
        require (std::sqrt (wetEnergy / samples) > 0.005, "Fully wet chorus must produce an audible signal");
        require (std::sqrt (changeEnergy / samples) > 0.005, "Fully wet chorus must change the dry signal");
        require (std::sqrt (sideEnergy / samples) > 0.002, "Chorus must create stereo width from centered input");
    }
}

void auditionLoopTests (NAMplifiedAudioProcessor& processor)
{
    processor.prepareToPlay (sampleRate, blockSize);
    juce::AudioBuffer<float> output (2, 4);
    juce::MidiBuffer midi;
    const auto play = [&] (float first)
    {
        juce::AudioBuffer<float> clip (2, 3);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 3; ++i)
                clip.setSample (ch, i, first + static_cast<float> (i) * 0.1f);
        processor.playAudition (std::move (clip), sampleRate, true);
        processor.processBlock (output, midi);
    };
    play (0.1f);
    require (near (output.getSample (0, 0), 0.1f, 0.00001)
             && near (output.getSample (0, 3), 0.1f, 0.00001),
             "The audition loop should wrap within the audio block");
    play (0.4f);
    require (near (output.getSample (0, 0), 0.5f, 0.00001)
             && near (output.getSample (1, 0), 0.5f, 0.00001),
             "Switching rigs should keep the current position in the DI");
    const auto processLiveInput = [&]
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 4; ++i)
                output.setSample (ch, i, 0.05f);
        processor.processBlock (output, midi);
    };
    const auto measureLiveOutput = [&]
    {
        for (int block = 0; block < 256; ++block)
            processLiveInput();
        return output.getMagnitude (0, 4);
    };
    processor.setCompareVolume (0.0f);
    const auto liveLevel = measureLiveOutput();
    require (liveLevel > 0.001f && liveLevel < 0.1f,
             "Muting the audition should restore live input after the gain ramp");

    processor.setCompareVolume (1.0f);
    play (0.4f);
    processor.stopAudition();
    require (near (measureLiveOutput(), liveLevel, 0.00001), "Stop should restore live input");
}

void comparisonApplyTests (NAMplifiedAudioProcessor& processor, const juce::File& testHome)
{
    using Routing = NAMplifiedAudioProcessor::ComparisonRouting;
    using Rig = NAMplifiedAudioProcessor::ComparisonRig;
    auto captures = testHome.findChildFiles (juce::File::findFiles, true, "*.nam");
    captures.sort();
    require (captures.size() >= 2, "The test home should contain factory captures");
    const auto cabA = makeIR (testHome, "comparison-a.wav", 1.0f);
    const auto cabB = makeIR (testHome, "comparison-b.wav", -1.0f);
    const auto value = [&] (const char* id) { return processor.parameters.getRawParameterValue (id)->load(); };

    Rig a;
    a.routing = Routing::pairedIRs;
    a.captures = { captures[0], captures[1] };
    a.cabinetIRs = { cabA, cabB };
    a.ampsEnabled = { true, true };
    a.cabsEnabled = { true, true };
    set (processor, "parallelIRMode", 1.0f);
    processor.applyComparisonRig (a);
    require (value ("ampRouting") == 1.0f && value ("parallelIRMode") == 0.0f,
             "Applying A should select paired parallel routing");
    require (processor.getNAMCaptureFile (0) == captures[0] && processor.getNAMCaptureFile (1) == captures[1],
             "Applying A should load both captures into their slots");
    require (processor.getImpulseResponseName (0) == cabA.getFileName()
             && processor.getImpulseResponseName (1) == cabB.getFileName(),
             "Applying A should load both cabinet IRs into their slots");

    auto b = a;
    b.routing = Routing::sharedIR;
    b.captures = { captures[1], juce::File() };
    b.cabinetIRs = { cabB, juce::File() };
    b.ampsEnabled = { true, false };
    b.cabsEnabled = { true, false };
    processor.applyComparisonRig (b);
    require (value ("ampRouting") == 1.0f && value ("parallelIRMode") == 1.0f,
             "Applying B should select shared parallel routing");
    require (processor.getNAMCaptureFile (0) == captures[1] && processor.getImpulseResponseName (0) == cabB.getFileName(),
             "Applying B should replace the selected capture and cabinet IR");
    require (value ("amp1On") == 1.0f && value ("amp2On") == 0.0f
             && value ("ir1On") == 1.0f && value ("ir2On") == 0.0f,
             "Applying B should restore the enabled state of each slot");
}

void pitchTests (NAMplifiedAudioProcessor& processor)
{
    processor.prepareToPlay (sampleRate, blockSize);
    constexpr int testBlockSize = 127;
    juce::AudioBuffer<float> buffer (2, testBlockSize);
    juce::MidiBuffer midi;
    constexpr double lowE = 82.4069;
    constexpr double highE = 329.6276;
    constexpr auto twoPi = juce::MathConstants<double>::twoPi;
    struct PitchCase { float shift; double frequency; };
    constexpr PitchCase cases[] { { 0.0f, lowE }, { -12.0f, lowE }, { -2.0f, highE },
                                  { 12.0f, 220.0 }, { 0.0f, lowE } };
    for (const auto [shift, frequency] : cases)
    {
        set (processor, "pitchSemitones", shift);
        const auto ratio = shift == 0.0f ? 0.5 : std::exp2 (static_cast<double> (shift) / 12.0);
        std::complex<double> original {}, transposed {};
        int measured = 0;
        for (int offset = 0; offset < 2 * sampleRate; offset += testBlockSize)
        {
            for (int i = 0; i < testBlockSize; ++i)
            {
                const auto phase = twoPi * frequency * (offset + i) / sampleRate;
                const auto signal = static_cast<float> (0.1 * std::sin (phase));
                buffer.setSample (0, i, signal);
                buffer.setSample (1, i, signal);
            }
            processor.processBlock (buffer, midi);
            if (offset < sampleRate)
                continue;
            measured += testBlockSize;
            for (int i = 0; i < testBlockSize; ++i)
            {
                const auto phase = twoPi * frequency * (offset + i) / sampleRate;
                const auto signal = static_cast<double> (buffer.getSample (0, i));
                original += signal * std::polar (1.0, -phase);
                transposed += signal * std::polar (1.0, -phase * ratio);
            }
        }
        const auto dry = 2.0 * std::abs (original) / measured;
        const auto shifted = 2.0 * std::abs (transposed) / measured;
        require (shift == 0 ? dry > 0.08 && shifted < 0.005 : shifted > 0.08 && dry < 0.01,
                 "Pitch should preserve bypass or produce the requested shift without the original tone");
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
        else if (suite == "looper_tracks")
        {
            looperTrackTests();
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
            else if (suite == "preset_roundtrip")
                presetRoundtripTests (processor);
            else if (suite == "routing")
                routingTests (*processor);
            else if (suite == "centered_cab")
                centeredCabTests (*processor, testHome);
            else if (suite == "cab")
                cabTests (*processor, testHome);
            else if (suite == "delay")
                delayTests (*processor);
            else if (suite == "chorus")
                chorusTests (*processor);
            else if (suite == "pitch")
                pitchTests (*processor);
            else if (suite == "comparison_apply")
                comparisonApplyTests (*processor, testHome);
            else if (suite == "audition_loop")
                auditionLoopTests (*processor);
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
