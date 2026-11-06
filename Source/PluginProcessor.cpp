#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "AmpSimBridge.h"
#include <get_dsp.h>

#include <array>
#include <cmath>

struct TheLeadAudioProcessor::AmpModel
{
    mutable juce::SpinLock lock;
    std::unique_ptr<nam::DSP> models[2];
    std::array<std::vector<NAM_SAMPLE>, 2> input;
    std::array<std::vector<NAM_SAMPLE>, 2> output;
    juce::String path;
    juce::String status;
    NAMinatorAmpSim* internalAmp = naminAmpSimCreate (0);

    ~AmpModel() { naminAmpSimDestroy (internalAmp); }

    void prepare (double sampleRate, int maxBlockSize)
    {
        const juce::SpinLock::ScopedLockType guard (lock);
        for (auto channel = 0; channel < 2; ++channel)
        {
            input[static_cast<size_t> (channel)].resize (static_cast<size_t> (maxBlockSize));
            output[static_cast<size_t> (channel)].resize (static_cast<size_t> (maxBlockSize));
            if (models[channel] != nullptr)
            {
                models[channel]->SetPrewarmOnReset (false);
                models[channel]->Reset (sampleRate, maxBlockSize);
                models[channel]->prewarm();
            }
        }
    }

    bool create (const juce::File& file, double sampleRate, int maxBlockSize, juce::String& error)
    {
        try
        {
            nam::DspLoadOptions options;
            options.prewarm = false;
            auto left = nam::get_dsp (std::filesystem::path (file.getFullPathName().toStdString()), options);
            auto right = nam::get_dsp (std::filesystem::path (file.getFullPathName().toStdString()), options);
            if (left->NumInputChannels() != 1 || left->NumOutputChannels() != 1
                || right->NumInputChannels() != 1 || right->NumOutputChannels() != 1)
            {
                error = "This slot currently accepts mono-in/mono-out NAM captures.";
                setStatus (error);
                return false;
            }

            left->SetPrewarmOnReset (false);
            right->SetPrewarmOnReset (false);
            left->Reset (sampleRate, maxBlockSize);
            right->Reset (sampleRate, maxBlockSize);
            left->prewarm();
            right->prewarm();

            const juce::SpinLock::ScopedLockType guard (lock);
            models[0] = std::move (left);
            models[1] = std::move (right);
            path = file.getFullPathName();
            status = "Loaded";
            for (auto channel = 0; channel < 2; ++channel)
            {
                input[static_cast<size_t> (channel)].resize (static_cast<size_t> (maxBlockSize));
                output[static_cast<size_t> (channel)].resize (static_cast<size_t> (maxBlockSize));
            }
            return true;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            setStatus (error);
            return false;
        }
    }

    void setStatus (const juce::String& newStatus)
    {
        const juce::SpinLock::ScopedLockType guard (lock);
        status = newStatus;
    }

    void process (juce::AudioBuffer<float>& buffer, int numChannels, int numSamples)
    {
        const juce::SpinLock::ScopedTryLockType guard (lock);
        if (! guard.isLocked())
            return;

        for (auto channel = 0; channel < juce::jmin (numChannels, 2); ++channel)
        {
            auto& in = input[static_cast<size_t> (channel)];
            auto& out = output[static_cast<size_t> (channel)];
            if (models[channel] == nullptr || numSamples > static_cast<int> (in.size()))
                continue;

            auto* channelData = buffer.getWritePointer (channel);
            for (auto sample = 0; sample < numSamples; ++sample)
                in[static_cast<size_t> (sample)] = static_cast<NAM_SAMPLE> (channelData[sample]);
            NAM_SAMPLE* inputChannels[] { in.data() };
            NAM_SAMPLE* outputChannels[] { out.data() };
            models[channel]->process (inputChannels, outputChannels, numSamples);
            for (auto sample = 0; sample < numSamples; ++sample)
                channelData[sample] = static_cast<float> (out[static_cast<size_t> (sample)]);
        }
    }

    void processInternal (juce::AudioBuffer<float>& buffer, int numChannels, int numSamples,
                          int model, const std::array<float, 12>& controls, float sampleRate)
    {
        const juce::SpinLock::ScopedTryLockType guard (lock);
        if (! guard.isLocked() || internalAmp == nullptr)
            return;
        naminAmpSimSetModel (internalAmp, model);
        for (int parameter = 0; parameter < static_cast<int> (controls.size()); ++parameter)
            naminAmpSimSetParameter (internalAmp, parameter, controls[static_cast<size_t> (parameter)]);
        for (int channel = 0; channel < juce::jmin (numChannels, 2); ++channel)
            naminAmpSimProcess (internalAmp, channel, buffer.getWritePointer (channel), numSamples, sampleRate);
    }

    juce::String getPath() const
    {
        const juce::SpinLock::ScopedLockType guard (lock);
        return path;
    }

    juce::String getStatus() const
    {
        const juce::SpinLock::ScopedLockType guard (lock);
        return status;
    }
};

TheLeadAudioProcessor::TheLeadAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor (BusesProperties()
                    #if ! JucePlugin_IsMidiEffect
                     #if ! JucePlugin_IsSynth
                      .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                     #endif
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                    #endif
                      ),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
#else
    : parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
#endif
{
    for (int slot = 0; slot < numAmpSlots; ++slot)
        ampModels[slot] = std::make_unique<AmpModel>();
}

TheLeadAudioProcessor::IRParameters::ParameterLayout TheLeadAudioProcessor::createParameterLayout()
{
    IRParameters::ParameterLayout layout;
    const auto addToggle = [&layout] (const char* id, const char* name, bool initial = false)
    {
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, initial));
    };
    const auto addKnob = [&layout] (const char* id, const char* name, float min, float max, float initial)
    {
        const auto step = juce::String (id) == "gateThreshold" ? 1.0f : 0.01f;
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (min, max, step), initial));
    };
    const auto addFrequency = [&layout] (const char* id, const char* name, float min, float max, float initial)
    {
        juce::NormalisableRange<float> range (min, max, 1.0f);
        range.setSkewForCentre (std::sqrt (min * max));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, range, initial, juce::AudioParameterFloatAttributes().withLabel ("Hz")));
    };

    addKnob ("inputGain", "Input Gain", -24.0f, 24.0f, 0.0f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "pitchSemitones", 1 }, "Pitch",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("st")));
    addToggle ("gateOn", "Noise Gate", true);
    // The gate threshold is shown and adjusted in whole decibels.
    addKnob ("gateThreshold", "Gate Threshold", -80.0f, -20.0f, -74.0f);
    addToggle ("compOn", "Compressor");
    addKnob ("compSustain", "Comp Input", 0.0f, 1.0f, 0.45f);
    addKnob ("compAttack", "Comp Attack", 0.02f, 0.8f, 0.2f);
    addKnob ("compRelease", "Comp Release", 50.0f, 1100.0f, 250.0f);
    addKnob ("compTone", "Comp Tone", -12.0f, 12.0f, 0.0f);
    addKnob ("compBlend", "Comp Blend", 0.0f, 1.0f, 1.0f);
    addKnob ("compVolume", "Comp Volume", -12.0f, 12.0f, 0.0f);
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "compRatioMode", 1 }, "1176 Ratio",
        juce::StringArray { "4:1", "8:1", "12:1", "20:1", "All Buttons In" }, 0));
    addKnob ("compEQ", "Compressor EQ", 0.0f, 1.0f, 0.0f);
    // Keep the old parameter in saved sessions, but the editor now uses EQ.
    addToggle ("compBright", "Compressor Bright");
    addToggle ("boostOn", "Boost");
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "boostModel", 1 }, "Boost Model",
        juce::StringArray { "Clean Boost", "808 Drive" }, 0));
    // Keep the boostGain ID so existing sessions and presets retain the value;
    // in Clean Boost mode it now controls post-circuit output level.
    addKnob ("boostGain", "Boost Post Volume", -6.0f, 24.0f, 6.0f);
    addKnob ("boostBass", "Boost Bass", -12.0f, 12.0f, 0.0f);
    addKnob ("boostMid", "Boost Mid", -12.0f, 12.0f, 0.0f);
    addKnob ("boostTreble", "Boost Treble", -12.0f, 12.0f, 0.0f);
    addKnob ("boost808Drive", "808 Drive", 0.0f, 30.0f, 12.0f);
    addFrequency ("boost808Tone", "808 Tone", 500.0f, 7500.0f, 4200.0f);
    addKnob ("boost808Level", "808 Level", -24.0f, 12.0f, 0.0f);
    addToggle ("preEqOn", "Pre EQ");
    addKnob ("preLow", "Pre EQ Low", -12.0f, 12.0f, 0.0f);
    addFrequency ("preLowFreq", "Pre EQ Low Frequency", 80.0f, 12000.0f, 120.0f);
    addKnob ("preBand2", "Pre EQ Band 2", -12.0f, 12.0f, 0.0f);
    addKnob ("preMid", "Pre EQ Mid", -12.0f, 12.0f, 0.0f);
    addFrequency ("preMidFreq", "Pre EQ Mid Frequency", 80.0f, 12000.0f, 1000.0f);
    addKnob ("preBand4", "Pre EQ Band 4", -12.0f, 12.0f, 0.0f);
    addKnob ("preHigh", "Pre EQ High", -12.0f, 12.0f, 0.0f);
    addFrequency ("preHighFreq", "Pre EQ High Frequency", 80.0f, 12000.0f, 8000.0f);
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "ampRouting", 1 }, "Amp Routing",
        juce::StringArray { "Series", "Parallel Blend" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "parallelIRMode", 1 }, "Parallel IR Routing",
        juce::StringArray { "Paired IRs", "Shared IR 1" }, 0));
    for (int slot = 0; slot < numAmpSlots; ++slot)
    {
        const auto number = juce::String (slot + 1);
        addToggle (("amp" + juce::String (slot + 1) + "On").toRawUTF8(),
                   ("Amp " + juce::String (slot + 1)).toRawUTF8(), slot == 0);
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ("amp" + number + "Source").toRawUTF8(), 1 },
            ("Amp " + number + " Source").toRawUTF8(),
            juce::StringArray { "NAM Capture", "Amp Sim" }, 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ("amp" + number + "Model").toRawUTF8(), 1 },
            ("Amp " + number + " Model").toRawUTF8(),
            juce::StringArray { "Fullerton Clean", "British Crunch", "Southwest Lead", "Essex Chime",
                                "Tweed Blues", "Meridian High Gain", "Citrus Roar", "Citrus Terror",
                                "Regent 800", "Solar Monolith", "Eclipse Drone", "Emerald Ratrod Deluxe" }, 0));
        const char* ampControlNames[] { "Gain", "Volume", "Bass", "Mid", "Treble", "Presence",
                                        "Sag", "Master", "Bright", "Cut", "Tone", "Feedback" };
        for (int index = 0; index < 12; ++index)
        {
            const auto* controlName = ampControlNames[index];
            const auto defaultValue = index == 6 ? 0.3f : index == 11 ? 0.0f : index == 8 ? 0.0f : 0.5f;
            addKnob (("amp" + number + controlName).toRawUTF8(), ("Amp " + juce::String (controlName)).toRawUTF8(), 0.0f, 1.0f, defaultValue);
        }
        addKnob (("amp" + juce::String (slot + 1) + "In").toRawUTF8(), "Amp Input", -24.0f, 24.0f, 0.0f);
        addKnob (("amp" + juce::String (slot + 1) + "Out").toRawUTF8(), "Amp Output", -24.0f, 24.0f, 0.0f);
    }
    addKnob ("ampParallelBlend", "Parallel Blend", 0.0f, 1.0f, 0.5f);
    addKnob ("irParallelBlend", "Cabinet IR Blend", 0.0f, 1.0f, 0.5f);

    for (int slot = 0; slot < numIRSlots; ++slot)
    {
        const auto prefix = "ir" + juce::String (slot + 1) + "Shape";
        addToggle (("ir" + juce::String (slot + 1) + "On").toRawUTF8(),
                   ("Cabinet IR " + juce::String (slot + 1)).toRawUTF8(), slot == 0);
        addKnob ((prefix + "Low").toRawUTF8(), "IR Shape Low", -12.0f, 12.0f, 0.0f);
        addFrequency ((prefix + "LowFreq").toRawUTF8(), "IR Shape Low Frequency", 50.0f, 16000.0f, 120.0f);
        addKnob ((prefix + "Mid").toRawUTF8(), "IR Shape Mid", -12.0f, 12.0f, 0.0f);
        addFrequency ((prefix + "MidFreq").toRawUTF8(), "IR Shape Mid Frequency", 50.0f, 16000.0f, 1000.0f);
        addKnob ((prefix + "High").toRawUTF8(), "IR Shape High", -12.0f, 12.0f, 0.0f);
        addFrequency ((prefix + "HighFreq").toRawUTF8(), "IR Shape High Frequency", 50.0f, 16000.0f, 8000.0f);
    }

    addToggle ("delayOn", "Delay");
    addKnob ("delayTime", "Delay Time", 1.0f, 1200.0f, 360.0f);
    addKnob ("delayFeedback", "Delay Feedback", 0.0f, 0.9f, 0.32f);
    addKnob ("delayMix", "Delay Mix", 0.0f, 1.0f, 0.25f);
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "delayMode", 1 }, "Delay Mode",
        juce::StringArray { "Mono", "Stereo", "Ping Pong" }, 1));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "delayType", 1 }, "Delay Character",
        juce::StringArray { "Digital", "Analog" }, 0));
    addFrequency ("delayTone", "Delay Tone", 500.0f, 12000.0f, 4500.0f);
    addKnob ("delayDuck", "Delay Ducking", 0.0f, 1.0f, 0.0f);
    addKnob ("delayDuckThreshold", "Delay Duck Threshold", -48.0f, 0.0f, -30.0f);
    addKnob ("delayDuckRelease", "Delay Duck Release", 20.0f, 1000.0f, 250.0f);
    addToggle ("reverbOn", "Reverb", true);
    addKnob ("reverbSize", "Reverb Size", 0.0f, 1.0f, 0.35f);
    addKnob ("reverbMix", "Reverb Mix", 0.0f, 1.0f, 0.12f);
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "reverbMode", 1 }, "Reverb Mode",
        juce::StringArray { "Room", "Plate", "Cloud" }, 0));
    addKnob ("reverbDamping", "Reverb Damping", 0.0f, 1.0f, 0.45f);
    addKnob ("reverbPreDelay", "Reverb Pre-Delay", 0.0f, 100.0f, 0.0f);
    addKnob ("reverbWidth", "Reverb Width", 0.0f, 1.0f, 0.8f);
    addToggle ("chorusOn", "Chorus");
    addKnob ("chorusRate", "Chorus Rate", 0.1f, 5.0f, 0.8f);
    addKnob ("chorusDepth", "Chorus Depth", 0.0f, 1.0f, 0.35f);
    addKnob ("chorusMix", "Chorus Mix", 0.0f, 1.0f, 0.25f);
    addToggle ("postOn", "Post EQ");
    addKnob ("postLow", "Post EQ Low", -12.0f, 12.0f, 0.0f);
    addFrequency ("postLowFreq", "Post EQ Low Frequency", 50.0f, 16000.0f, 120.0f);
    addKnob ("postMid", "Post EQ Mid", -12.0f, 12.0f, 0.0f);
    addFrequency ("postMidFreq", "Post EQ Mid Frequency", 50.0f, 16000.0f, 1000.0f);
    addKnob ("postHigh", "Post EQ High", -12.0f, 12.0f, 0.0f);
    addFrequency ("postHighFreq", "Post EQ High Frequency", 50.0f, 16000.0f, 8000.0f);
    addKnob ("outputGain", "Output Gain", -24.0f, 12.0f, 0.0f);
    return layout;
}

TheLeadAudioProcessor::~TheLeadAudioProcessor() = default;

void TheLeadAudioProcessor::BiquadState::set (float nb0, float nb1, float nb2,
                                               float na0, float na1, float na2)
{
    const auto normaliser = na0 != 0.0f ? 1.0f / na0 : 1.0f;
    b0 = nb0 * normaliser; b1 = nb1 * normaliser; b2 = nb2 * normaliser;
    a1 = na1 * normaliser; a2 = na2 * normaliser;
}

float TheLeadAudioProcessor::BiquadState::process (float input)
{
    const auto output = b0 * input + z1;
    z1 = b1 * input - a1 * output + z2;
    z2 = b2 * input - a2 * output;
    return output;
}

void TheLeadAudioProcessor::setEqCoefficients (BiquadState& filter, int band, float frequency,
                                               float gainDb, double sampleRate)
{
    const auto sr = static_cast<float> (juce::jmax (sampleRate, 1.0));
    const auto f = juce::jlimit (20.0f, sr * 0.45f, frequency);
    const auto w = juce::MathConstants<float>::twoPi * f / sr;
    const auto c = std::cos (w);
    const auto s = std::sin (w);
    const auto A = std::pow (10.0f, gainDb / 40.0f);
    const auto alpha = s * 0.5f;
    float b0, b1, b2, a0, a1, a2;

    if (band == 1)
    {
        b0 = 1.0f + alpha * A; b1 = -2.0f * c; b2 = 1.0f - alpha * A;
        a0 = 1.0f + alpha / A; a1 = -2.0f * c; a2 = 1.0f - alpha / A;
    }
    else
    {
        const auto beta = s * std::sqrt (2.0f * A);
        if (band == 0)
        {
            b0 = A * ((A + 1.0f) - (A - 1.0f) * c + beta);
            b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * c);
            b2 = A * ((A + 1.0f) - (A - 1.0f) * c - beta);
            a0 = (A + 1.0f) + (A - 1.0f) * c + beta;
            a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * c);
            a2 = (A + 1.0f) + (A - 1.0f) * c - beta;
        }
        else
        {
            b0 = A * ((A + 1.0f) + (A - 1.0f) * c + beta);
            b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * c);
            b2 = A * ((A + 1.0f) + (A - 1.0f) * c - beta);
            a0 = (A + 1.0f) - (A - 1.0f) * c + beta;
            a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * c);
            a2 = (A + 1.0f) - (A - 1.0f) * c - beta;
        }
    }
    filter.set (b0, b1, b2, a0, a1, a2);
}

void TheLeadAudioProcessor::processEqualizer (juce::AudioBuffer<float>& buffer, int channels, int samples,
                                               const juce::String& prefix, bool pre)
{
    const auto sampleRate = juce::jmax (1.0, modelSampleRate.load());
    constexpr int bandCount = 5;
    const float preFrequencies[] { 80.0f, 250.0f, 800.0f, 2500.0f, 8000.0f };
    const char* preGainIds[] { "preLow", "preBand2", "preMid", "preBand4", "preHigh" };
    const float postDefaults[] { 120.0f, 1000.0f, 8000.0f };
    const char* postBandNames[] { "Low", "Mid", "High" };
    auto& filters = pre ? preEqFilters : postEqFilters;
    for (int band = 0; band < bandCount; ++band)
    {
        const auto suffix = band < 3 ? juce::String (postBandNames[band]) : juce::String();
        const auto gainId = pre ? juce::String (preGainIds[band]) : prefix + suffix;
        const auto* gain = parameters.getRawParameterValue (gainId);
        const auto* frequency = pre ? nullptr : parameters.getRawParameterValue (prefix + suffix + "Freq");
        const auto gainDb = gain != nullptr ? gain->load() : 0.0f;
        const auto hz = pre ? preFrequencies[band]
                            : (band < 3 && frequency != nullptr ? frequency->load() : postDefaults[band % 3]);
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto filterType = pre ? (band == 0 ? 0 : band == 4 ? 2 : 1) : juce::jmin (band, 2);
            setEqCoefficients (filters[static_cast<size_t> (channel)][static_cast<size_t> (band)],
                               filterType, hz, gainDb, sampleRate);
        }
    }

    for (int channel = 0; channel < channels; ++channel)
    {
        auto* samplesData = buffer.getWritePointer (channel);
        auto& channelFilters = filters[static_cast<size_t> (channel)];
        for (int i = 0; i < samples; ++i)
        {
            auto sample = samplesData[i];
            for (auto& filter : channelFilters)
                sample = filter.process (sample);
            samplesData[i] = sample;
        }
    }
}

void TheLeadAudioProcessor::processIRShapeEqualizer (juce::AudioBuffer<float>& buffer, int channels, int samples, int slot)
{
    const auto sampleRate = juce::jmax (1.0, modelSampleRate.load());
    constexpr const char* bands[] { "Low", "Mid", "High" };
    const auto prefix = "ir" + juce::String (slot + 1) + "Shape";
    for (int band = 0; band < 3; ++band)
    {
        const auto* gain = parameters.getRawParameterValue (prefix + bands[band]);
        const auto* frequency = parameters.getRawParameterValue (prefix + bands[band] + "Freq");
        const auto gainDb = gain != nullptr ? gain->load() : 0.0f;
        const auto hz = frequency != nullptr ? frequency->load() : 1000.0f;
        for (int channel = 0; channel < channels; ++channel)
            setEqCoefficients (irShapeEqFilters[static_cast<size_t> (slot)][static_cast<size_t> (channel)][static_cast<size_t> (band)],
                               band, hz, gainDb, sampleRate);
    }
    for (int channel = 0; channel < channels; ++channel)
    {
        auto* data = buffer.getWritePointer (channel);
        auto& filters = irShapeEqFilters[static_cast<size_t> (slot)][static_cast<size_t> (channel)];
        for (int i = 0; i < samples; ++i)
            for (auto& filter : filters)
                data[i] = filter.process (data[i]);
    }
}

const juce::String TheLeadAudioProcessor::getName() const { return JucePlugin_Name; }
std::array<int, TheLeadAudioProcessor::numPreFxStages> TheLeadAudioProcessor::getPreFxOrder() const
{
    std::array<int, numPreFxStages> order {};
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = preFxOrder[i].load();
    return order;
}

void TheLeadAudioProcessor::setPreFxOrder (const std::array<int, numPreFxStages>& order)
{
    std::array<bool, numPreFxStages> seen {};
    for (const auto stage : order)
    {
        if (stage < 0 || stage >= numPreFxStages || seen[static_cast<size_t> (stage)])
            return;
        seen[static_cast<size_t> (stage)] = true;
    }
    for (size_t i = 0; i < order.size(); ++i)
        preFxOrder[i].store (order[i]);
}

std::array<int, TheLeadAudioProcessor::numAmpSlots> TheLeadAudioProcessor::getAmpSlotOrder() const
{
    std::array<int, numAmpSlots> order {};
    for (size_t i = 0; i < order.size(); ++i) order[i] = ampSlotOrder[i].load();
    return order;
}

void TheLeadAudioProcessor::setAmpSlotOrder (const std::array<int, numAmpSlots>& order)
{
    std::array<bool, numAmpSlots> seen {};
    for (const auto slot : order)
    {
        if (slot < 0 || slot >= numAmpSlots || seen[static_cast<size_t> (slot)]) return;
        seen[static_cast<size_t> (slot)] = true;
    }
    for (size_t i = 0; i < order.size(); ++i) ampSlotOrder[i].store (order[i]);
}

std::array<int, TheLeadAudioProcessor::numIRSlots> TheLeadAudioProcessor::getIRSlotOrder() const
{
    std::array<int, numIRSlots> order {};
    for (size_t i = 0; i < order.size(); ++i) order[i] = irSlotOrder[i].load();
    return order;
}

void TheLeadAudioProcessor::setIRSlotOrder (const std::array<int, numIRSlots>& order)
{
    std::array<bool, numIRSlots> seen {};
    for (const auto slot : order)
    {
        if (slot < 0 || slot >= numIRSlots || seen[static_cast<size_t> (slot)]) return;
        seen[static_cast<size_t> (slot)] = true;
    }
    for (size_t i = 0; i < order.size(); ++i) irSlotOrder[i].store (order[i]);
}

std::array<int, TheLeadAudioProcessor::numPostFxStages> TheLeadAudioProcessor::getPostFxOrder() const
{
    std::array<int, numPostFxStages> order {};
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = postFxOrder[i].load();
    return order;
}

void TheLeadAudioProcessor::setPostFxOrder (const std::array<int, numPostFxStages>& order)
{
    std::array<bool, numPostFxStages> seen {};
    for (const auto stage : order)
    {
        if (stage < 0 || stage >= numPostFxStages || seen[static_cast<size_t> (stage)])
            return;
        seen[static_cast<size_t> (stage)] = true;
    }
    for (size_t i = 0; i < order.size(); ++i)
        postFxOrder[i].store (order[i]);
}

void TheLeadAudioProcessor::resetToDefaults()
{
    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());

    setPreFxOrder ({ 0, 1, 2, 3 });
    setAmpSlotOrder ({ 0, 1 });
    setIRSlotOrder ({ 0, 1 });
    setPostFxOrder ({ 0, 1, 2, 3 });
}

bool TheLeadAudioProcessor::acceptsMidi() const { return JucePlugin_WantsMidiInput != 0; }
bool TheLeadAudioProcessor::producesMidi() const { return JucePlugin_ProducesMidiOutput != 0; }
bool TheLeadAudioProcessor::isMidiEffect() const { return JucePlugin_IsMidiEffect != 0; }
double TheLeadAudioProcessor::getTailLengthSeconds() const { return 10.0; }
int TheLeadAudioProcessor::getNumPrograms() { return 1; }
int TheLeadAudioProcessor::getCurrentProgram() { return 0; }
void TheLeadAudioProcessor::setCurrentProgram (int index) { juce::ignoreUnused (index); }
const juce::String TheLeadAudioProcessor::getProgramName (int index) { juce::ignoreUnused (index); return {}; }
void TheLeadAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

juce::File TheLeadAudioProcessor::getUserPresetDirectory() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("The Lead").getChildFile ("Presets");
}

juce::File TheLeadAudioProcessor::getUserPresetFile (const juce::String& name) const
{
    auto safeName = name.trim().retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-().");
    while (safeName.startsWith (".")) safeName = safeName.substring (1);
    safeName = safeName.trim();
    if (safeName.isEmpty()) return {};
    return getUserPresetDirectory().getChildFile (safeName).withFileExtension (".tlpreset");
}

juce::StringArray TheLeadAudioProcessor::getUserPresetNames() const
{
    juce::StringArray names;
    const auto folder = getUserPresetDirectory();
    if (! folder.isDirectory()) return names;
    for (const auto& file : folder.findChildFiles (juce::File::findFiles, false, "*.tlpreset"))
        names.add (file.getFileNameWithoutExtension());
    names.sort (true);
    return names;
}

bool TheLeadAudioProcessor::saveUserPreset (const juce::String& name)
{
    const auto file = getUserPresetFile (name);
    if (file == juce::File() || ! getUserPresetDirectory().createDirectory().wasOk())
        return false;
    juce::MemoryBlock state;
    getStateInformation (state);
    return file.replaceWithData (state.getData(), state.getSize());
}

bool TheLeadAudioProcessor::loadUserPreset (const juce::String& name)
{
    const auto file = getUserPresetFile (name);
    if (file == juce::File() || ! file.existsAsFile()) return false;
    juce::MemoryBlock state;
    if (! file.loadFileAsData (state)) return false;
    setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    return true;
}

bool TheLeadAudioProcessor::deleteUserPreset (const juce::String& name)
{
    const auto file = getUserPresetFile (name);
    return file != juce::File() && file.existsAsFile() && file.deleteFile();
}

void TheLeadAudioProcessor::capturePresetSnapshot()
{
    presetSnapshot.clear();
    for (auto* parameter : getParameters())
    {
        const auto* parameterWithID = dynamic_cast<const juce::AudioProcessorParameterWithID*> (parameter);
        if (parameterWithID == nullptr)
            continue;
        if (const auto* rawValue = parameters.getRawParameterValue (parameterWithID->paramID))
            presetSnapshot.emplace (parameterWithID->paramID.toStdString(), rawValue->load());
    }
}

bool TheLeadAudioProcessor::isParameterModified (const juce::String& parameterId) const
{
    const auto baseline = presetSnapshot.find (parameterId.toStdString());
    const auto* current = parameters.getRawParameterValue (parameterId);
    return baseline != presetSnapshot.end() && current != nullptr
        && std::abs (current->load() - baseline->second) > 0.0001f;
}

bool TheLeadAudioProcessor::hasUnsavedParameterChanges() const
{
    for (const auto& [parameterId, baseline] : presetSnapshot)
        if (const auto* current = parameters.getRawParameterValue (parameterId); current != nullptr
            && std::abs (current->load() - baseline) > 0.0001f)
            return true;
    return false;
}

void TheLeadAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const auto channels = static_cast<juce::uint32> (juce::jmax (1, getTotalNumOutputChannels()));
    const juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (samplesPerBlock), channels };
    modelSampleRate.store (sampleRate);
    modelMaxBlockSize.store (samplesPerBlock);
    pitchBufferSize = juce::jmax (2, juce::roundToInt (sampleRate * 0.16));
    for (auto& channel : pitchBuffers)
        channel.assign (static_cast<size_t> (pitchBufferSize), 0.0f);
    pitchWritePosition = 0;
    pitchPhase = 0.0f;
    pitchCurrentSemitones = 0.0f;
    for (int slot = 0; slot < numAmpSlots; ++slot)
        ampModels[slot]->prepare (sampleRate, samplesPerBlock);
    for (auto& circuit : tubeScreamerCircuits)
        circuit.prepare (sampleRate);
    for (int slot = 0; slot < numIRSlots; ++slot)
    {
        irConvolvers[slot].prepare (spec);
        irWorkBuffers[slot].setSize (static_cast<int> (channels), samplesPerBlock);
    }
    irInputBuffer.setSize (static_cast<int> (channels), samplesPerBlock);
    ampInputBuffer.setSize (static_cast<int> (channels), samplesPerBlock);
    ampMixBuffer.setSize (static_cast<int> (channels), samplesPerBlock);
    for (auto& ampBuffer : ampWorkBuffers)
        ampBuffer.setSize (static_cast<int> (channels), samplesPerBlock);
    for (auto& delayLine : delayLines)
    {
        delayLine.setMaximumDelayInSamples (static_cast<int> (sampleRate * 1.2) + 2);
        delayLine.prepare (spec);
        delayLine.reset();
    }
    analogDelayToneState.fill (0.0f);
    for (auto& predelayLine : reverbPreDelayLines)
    {
        predelayLine.setMaximumDelayInSamples (static_cast<int> (sampleRate * 0.15) + 2);
        predelayLine.prepare (spec);
        predelayLine.reset();
    }
    reverbWorkBuffer.setSize (static_cast<int> (channels), samplesPerBlock);
    delayDuckEnvelope = 0.0f;
    chorus.prepare (spec);
    outputSafetyLimiter.prepare (spec);
    outputSafetyLimiter.setThreshold (0.0f);
    outputSafetyLimiter.setRelease (80.0f);
    outputSafetyLimiter.reset();
    juce::Reverb::Parameters reverbParameters;
    reverbParameters.roomSize = 0.45f;
    reverb.setParameters (reverbParameters);
}

void TheLeadAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool TheLeadAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    const auto output = layouts.getMainOutputChannelSet();
    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;
   #if ! JucePlugin_IsSynth
    if (output != layouts.getMainInputChannelSet())
        return false;
   #endif
    return true;
  #endif
}
#endif

void TheLeadAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midiMessages);

    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = juce::jmin (getTotalNumOutputChannels(), buffer.getNumChannels());
    const auto sampleRate = juce::jmax (1.0, modelSampleRate.load());
    const auto get = [this] (const char* id, float fallback = 0.0f)
    {
        if (const auto* value = parameters.getRawParameterValue (id))
            return value->load();
        return fallback;
    };
    const auto enabled = [&get] (const char* id) { return get (id) >= 0.5f; };

    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear (channel, 0, numSamples);

    float inputBlockPeak = 0.0f;
    const auto inputMeterChannels = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());
    for (int channel = 0; channel < inputMeterChannels; ++channel)
        inputBlockPeak = juce::jmax (inputBlockPeak, buffer.getMagnitude (channel, 0, numSamples));
    const auto meterFall = std::exp (-static_cast<float> (numSamples) / static_cast<float> (sampleRate * 0.32));
    inputPeak.store (juce::jmax (inputBlockPeak, inputPeak.load (std::memory_order_relaxed) * meterFall),
                     std::memory_order_relaxed);

    // Two crossfaded delay taps provide a low-latency, monophonic-style pitch
    // shift before the rest of the chain. The 20 ms minimum delay prevents the
    // read heads from catching the write head; all memory is allocated in prepareToPlay.
    const auto pitchTarget = juce::jlimit (-24.0f, 24.0f, get ("pitchSemitones"));
    const auto pitchSmoothing = 1.0f - std::exp (-1.0f / static_cast<float> (sampleRate * 0.025));
    const auto pitchMinDelay = static_cast<float> (sampleRate * 0.02);
    const auto pitchRange = juce::jmax (1.0f, static_cast<float> (sampleRate * 0.10));
    const auto wrapReadIndex = [this] (float position)
    {
        while (position < 0.0f) position += static_cast<float> (pitchBufferSize);
        while (position >= static_cast<float> (pitchBufferSize)) position -= static_cast<float> (pitchBufferSize);
        return position;
    };
    for (int sample = 0; sample < numSamples; ++sample)
    {
        pitchCurrentSemitones += (pitchTarget - pitchCurrentSemitones) * pitchSmoothing;
        for (int channel = 0; channel < juce::jmin (numChannels, 2); ++channel)
            pitchBuffers[static_cast<size_t> (channel)][static_cast<size_t> (pitchWritePosition)]
                = buffer.getReadPointer (channel)[sample];

        if (std::abs (pitchCurrentSemitones) > 0.01f)
        {
            const auto ratio = std::exp2 (pitchCurrentSemitones / 12.0f);
            const auto phaseIncrement = std::abs (1.0f - ratio) / pitchRange;
            auto phaseA = pitchPhase;
            auto phaseB = phaseA + 0.5f;
            if (phaseB >= 1.0f) phaseB -= 1.0f;
            const auto delayForPhase = [pitchCurrent = pitchCurrentSemitones, pitchMinDelay, pitchRange] (float phase)
            {
                return pitchCurrent > 0.0f ? pitchMinDelay + (1.0f - phase) * pitchRange
                                           : pitchMinDelay + phase * pitchRange;
            };
            const auto delayA = delayForPhase (phaseA);
            const auto delayB = delayForPhase (phaseB);
            const auto weightA = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * phaseA);
            const auto weightB = 1.0f - weightA;
            for (int channel = 0; channel < juce::jmin (numChannels, 2); ++channel)
            {
                const auto readTap = [this, channel, &wrapReadIndex] (float delay)
                {
                    const auto position = wrapReadIndex (static_cast<float> (pitchWritePosition) - delay);
                    const auto index0 = static_cast<int> (position);
                    const auto index1 = (index0 + 1) % pitchBufferSize;
                    const auto fraction = position - static_cast<float> (index0);
                    const auto& data = pitchBuffers[static_cast<size_t> (channel)];
                    return data[static_cast<size_t> (index0)] * (1.0f - fraction)
                         + data[static_cast<size_t> (index1)] * fraction;
                };
                buffer.getWritePointer (channel)[sample] = readTap (delayA) * weightA + readTap (delayB) * weightB;
            }
            pitchPhase += phaseIncrement;
            if (pitchPhase >= 1.0f)
                pitchPhase -= std::floor (pitchPhase);
        }
        pitchWritePosition = (pitchWritePosition + 1) % pitchBufferSize;
    }

    const auto inputGain = juce::Decibels::decibelsToGain (get ("inputGain"));
    buffer.applyGain (inputGain);
    const auto gateThreshold = juce::Decibels::decibelsToGain (get ("gateThreshold", -55.0f));

    const auto processPreFx = [&] (int stage)
    {
        if (stage == 0 && enabled ("gateOn"))
        {
            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto* samples = buffer.getWritePointer (channel);
                const auto ch = static_cast<size_t> (channel);
                for (int i = 0; i < numSamples; ++i)
                {
                    gateEnvelope[ch] = juce::jmax (std::abs (samples[i]), gateEnvelope[ch] * 0.995f);
                    const auto target = gateEnvelope[ch] >= gateThreshold ? 1.0f : 0.025f;
                    gateGain[ch] += (target - gateGain[ch]) * (target > gateGain[ch] ? 0.025f : 0.0025f);
                    samples[i] *= gateGain[ch];
                }
            }
        }
        else if (stage == 1 && enabled ("compOn"))
        {
            const auto inputGain = juce::Decibels::decibelsToGain (-6.0f + 30.0f * get ("compSustain", 0.45f));
            const auto ratioMode = juce::jlimit (0, 4, juce::roundToInt (get ("compRatioMode", 0.0f)));
            constexpr float ratios[] { 4.0f, 8.0f, 12.0f, 20.0f, 20.0f };
            const auto ratio = ratios[ratioMode];
            constexpr float thresholdDb = -18.0f;
            constexpr float kneeDb = 1.0f;
            const auto attackSeconds = juce::jmax (0.00002f, get ("compAttack", 0.2f) / 1000.0f);
            const auto attackCoefficient = std::exp (-1.0f / static_cast<float> (sampleRate * attackSeconds));
            const auto releaseSeconds = juce::jmax (0.05f, get ("compRelease", 250.0f) / 1000.0f);
            const auto releaseCoefficient = std::exp (-1.0f / static_cast<float> (sampleRate * releaseSeconds));
            const auto mix = juce::jlimit (0.0f, 1.0f, get ("compBlend", 1.0f));
            const auto tone = get ("compTone", 0.0f);
            const auto volume = juce::Decibels::decibelsToGain (get ("compVolume", 0.0f));
            const auto eq = juce::jlimit (0.0f, 1.0f, get ("compEQ", 0.0f));
            const auto brightSmoothingCoefficient = 1.0f - std::exp (-1.0f / static_cast<float> (sampleRate * 0.02));
            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto* samples = buffer.getWritePointer (channel);
                const auto ch = static_cast<size_t> (channel);
                setEqCoefficients (compressorToneFilters[ch], 2, 2500.0f, tone, sampleRate);
                setEqCoefficients (compressorLowEqFilters[ch], 0, 180.0f, -6.0f * eq, sampleRate);
                setEqCoefficients (compressorBrightFilters[ch], 2, 4200.0f, 6.0f * eq, sampleRate);
                for (int i = 0; i < numSamples; ++i)
                {
                    const auto level = std::abs (samples[i] * inputGain);
                    const auto coefficient = level > compressorEnvelope[ch] ? attackCoefficient : releaseCoefficient;
                    compressorEnvelope[ch] = coefficient * compressorEnvelope[ch] + (1.0f - coefficient) * level;
                    const auto detectorDb = juce::Decibels::gainToDecibels (juce::jmax (compressorEnvelope[ch], 1.0e-9f));
                    const auto overThresholdDb = detectorDb - thresholdDb;
                    float gainReductionDb = 0.0f;
                    if (overThresholdDb > -0.5f * kneeDb)
                    {
                        if (overThresholdDb >= 0.5f * kneeDb)
                            gainReductionDb = (thresholdDb + overThresholdDb / ratio) - detectorDb;
                        else
                        {
                            const auto kneePosition = overThresholdDb + 0.5f * kneeDb;
                            gainReductionDb = (1.0f / ratio - 1.0f) * kneePosition * kneePosition / (2.0f * kneeDb);
                        }
                    }

                    const auto targetGain = juce::Decibels::decibelsToGain (gainReductionDb);
                    auto compressed = samples[i] * inputGain * targetGain;
                    if (ratioMode == 4)
                        compressed = std::tanh (compressed * 1.8f) / std::tanh (1.8f);
                    auto output = (samples[i] * (1.0f - mix) + compressed * mix) * volume;
                    compressorBrightMix[ch] += (eq - compressorBrightMix[ch])
                                             * brightSmoothingCoefficient;
                    const auto eqOutput = compressorBrightFilters[ch].process (compressorLowEqFilters[ch].process (output));
                    samples[i] = output + (eqOutput - output) * compressorBrightMix[ch];
                }
                for (int i = 0; i < numSamples; ++i)
                    samples[i] = compressorToneFilters[ch].process (samples[i]);
            }
        }
        else if (stage == 2 && enabled ("boostOn"))
        {
            const auto use808 = get ("boostModel") >= 0.5f;
            if (use808)
            {
                const auto drive = juce::Decibels::decibelsToGain (get ("boost808Drive", 12.0f));
                const auto level = juce::Decibels::decibelsToGain (get ("boost808Level"));
                for (auto& circuit : tubeScreamerCircuits)
                    circuit.setToneFrequency (get ("boost808Tone", 4200.0f));
                for (int channel = 0; channel < numChannels; ++channel)
                {
                    auto* samples = buffer.getWritePointer (channel);
                    auto& circuit = tubeScreamerCircuits[static_cast<size_t> (channel)];
                    for (int i = 0; i < numSamples; ++i)
                        samples[i] = circuit.processSample (samples[i], drive, level);
                }
            }
            else
            {
                const auto postVolume = juce::Decibels::decibelsToGain (get ("boostGain", 6.0f));
                const float frequencies[] { 120.0f, 800.0f, 5000.0f };
                const char* ids[] { "boostBass", "boostMid", "boostTreble" };
                for (int band = 0; band < 3; ++band)
                    for (int channel = 0; channel < numChannels; ++channel)
                        setEqCoefficients (boostEqFilters[static_cast<size_t> (channel)][static_cast<size_t> (band)],
                                           band, frequencies[band], get (ids[band]), sampleRate);
                for (int channel = 0; channel < numChannels; ++channel)
                {
                    auto* samples = buffer.getWritePointer (channel);
                    auto& filters = boostEqFilters[static_cast<size_t> (channel)];
                    for (int i = 0; i < numSamples; ++i)
                    {
                        samples[i] = boostCircuits[static_cast<size_t> (channel)].processSample (samples[i], 1.0f);
                        for (auto& filter : filters)
                            samples[i] = filter.process (samples[i]);
                        samples[i] *= postVolume;
                    }
                }
            }
        }
        else if (stage == 3 && enabled ("preEqOn"))
            processEqualizer (buffer, numChannels, numSamples, "pre", true);
    };
    for (const auto stage : getPreFxOrder())
        processPreFx (stage);

    // In Series mode, active NAM slots process one after another. In Parallel
    // Blend mode, each active slot receives the same input and their outputs mix.
    constexpr const char* ampEnableIds[] { "amp1On", "amp2On" };
    constexpr const char* ampInputIds[] { "amp1In", "amp2In" };
    constexpr const char* ampOutputIds[] { "amp1Out", "amp2Out" };
    constexpr const char* irEnableIds[] { "ir1On", "ir2On" };
    const auto parallelNAMs = get ("ampRouting") >= 0.5f;
    const auto sharedParallelIR = get ("parallelIRMode") >= 0.5f;
    const auto processAmpSlot = [&] (juce::AudioBuffer<float>& target, int slot)
    {
        const auto number = juce::String (slot + 1);
        if (get (("amp" + number + "Source").toRawUTF8()) >= 0.5f)
        {
            std::array<float, 12> controls {};
            const char* suffixes[] { "Gain", "Volume", "Bass", "Mid", "Treble", "Presence",
                                     "Sag", "Master", "Bright", "Cut", "Tone", "Feedback" };
            for (size_t index = 0; index < controls.size(); ++index)
                controls[index] = get (("amp" + number + suffixes[index]).toRawUTF8(), 0.5f);
            const auto model = juce::jlimit (0, 11, juce::roundToInt (get (("amp" + number + "Model").toRawUTF8())));
            ampModels[slot]->processInternal (target, numChannels, numSamples, model, controls,
                                              static_cast<float> (sampleRate));
        }
        else
            ampModels[slot]->process (target, numChannels, numSamples);
    };
    const auto processCabIR = [&] (juce::AudioBuffer<float>& branch, int slot)
    {
        if (! irLoaded[slot].load() || ! enabled (irEnableIds[slot]))
            return;
        auto audioBlock = juce::dsp::AudioBlock<float> (branch).getSubBlock (0, static_cast<size_t> (numSamples));
        irConvolvers[slot].process (juce::dsp::ProcessContextReplacing<float> (audioBlock));
        processIRShapeEqualizer (branch, numChannels, numSamples, slot);
    };
    if (! parallelNAMs)
    {
        for (const auto slot : getAmpSlotOrder())
        {
            if (enabled (ampEnableIds[slot]))
            {
                buffer.applyGain (juce::Decibels::decibelsToGain (get (ampInputIds[slot])));
                processAmpSlot (buffer, slot);
                buffer.applyGain (juce::Decibels::decibelsToGain (get (ampOutputIds[slot])));
            }
        }
    }
    else
    {
        std::array<int, numAmpSlots> activeSlots {};
        int activeCount = 0;
        for (int slot = 0; slot < numAmpSlots; ++slot)
            if (enabled (ampEnableIds[slot]))
                activeSlots[static_cast<size_t> (activeCount++)] = slot;

        if (activeCount > 0)
        {
            const auto primaryWeight = juce::jlimit (0.0f, 1.0f, get ("ampParallelBlend", 0.5f));
            for (int channel = 0; channel < numChannels; ++channel)
                ampInputBuffer.copyFrom (channel, 0, buffer, channel, 0, numSamples);
            ampMixBuffer.clear();
            for (int branchIndex = 0; branchIndex < activeCount; ++branchIndex)
            {
                const auto slot = activeSlots[static_cast<size_t> (branchIndex)];
                const auto weight = activeCount == 1 ? 1.0f
                                  : branchIndex == 0 ? primaryWeight
                                  : (1.0f - primaryWeight) / static_cast<float> (activeCount - 1);
                if (weight <= 0.0f)
                    continue;
                auto& branch = ampWorkBuffers[slot];
                for (int channel = 0; channel < numChannels; ++channel)
                    branch.copyFrom (channel, 0, ampInputBuffer, channel, 0, numSamples);
                branch.applyGain (juce::Decibels::decibelsToGain (get (ampInputIds[slot])));
                processAmpSlot (branch, slot);
                branch.applyGain (juce::Decibels::decibelsToGain (get (ampOutputIds[slot])));
                if (! sharedParallelIR)
                    processCabIR (branch, slot);
                for (int channel = 0; channel < numChannels; ++channel)
                    ampMixBuffer.addFrom (channel, 0, branch, channel, 0, numSamples, weight);
            }
            for (int channel = 0; channel < numChannels; ++channel)
                buffer.copyFrom (channel, 0, ampMixBuffer, channel, 0, numSamples);
            if (sharedParallelIR)
                processCabIR (buffer, 0);
        }
    }

    // In NAM parallel mode, each amp feeds its matching cab before the two
    // amp/cab paths are blended. In NAM series mode, both cabs process the
    // completed amp chain in parallel and are blended here.
    if (! parallelNAMs)
    {
        std::array<int, numIRSlots> activeIRs {};
        int activeIRCount = 0;
        for (int slot = 0; slot < numIRSlots; ++slot)
            if (irLoaded[slot].load() && enabled (irEnableIds[slot]))
                activeIRs[static_cast<size_t> (activeIRCount++)] = slot;

        if (activeIRCount > 0)
        {
            for (int channel = 0; channel < numChannels; ++channel)
                irInputBuffer.copyFrom (channel, 0, buffer, channel, 0, numSamples);
            buffer.clear();

            const auto cab1Weight = juce::jlimit (0.0f, 1.0f, get ("irParallelBlend", 0.5f));
            for (int index = 0; index < activeIRCount; ++index)
            {
                const auto slot = activeIRs[static_cast<size_t> (index)];
                const auto weight = activeIRCount == 1 ? 1.0f : (slot == 0 ? cab1Weight : 1.0f - cab1Weight);
                if (weight <= 0.0f)
                    continue;
                auto& branch = irWorkBuffers[slot];
                for (int channel = 0; channel < numChannels; ++channel)
                    branch.copyFrom (channel, 0, irInputBuffer, channel, 0, numSamples);
                processCabIR (branch, slot);
                for (int channel = 0; channel < numChannels; ++channel)
                    buffer.addFrom (channel, 0, branch, channel, 0, numSamples, weight);
            }
        }
    }

    const auto processPostFx = [&] (int stage)
    {
        if (stage == 0 && enabled ("delayOn"))
        {
            const auto delaySamples = static_cast<float> (juce::jlimit (1.0, sampleRate * 1.2,
                static_cast<double> (get ("delayTime", 360.0f)) * sampleRate / 1000.0));
            const auto feedback = juce::jlimit (0.0f, 0.9f, get ("delayFeedback", 0.32f));
            const auto mix = juce::jlimit (0.0f, 1.0f, get ("delayMix", 0.25f));
            const auto mode = juce::jlimit (0, 2, juce::roundToInt (get ("delayMode", 1.0f)));
            const auto analog = get ("delayType", 0.0f) >= 0.5f;
            const auto tone = juce::jlimit (500.0f, 12000.0f, get ("delayTone", 4500.0f));
            const auto toneCoefficient = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * tone
                                                           / static_cast<float> (sampleRate));
            const auto shapeRepeat = [&] (int channel, float sample)
            {
                if (! analog)
                    return sample;
                auto& state = analogDelayToneState[static_cast<size_t> (channel)];
                state += toneCoefficient * (sample - state);
                return std::tanh (state * 1.12f) / std::tanh (1.12f);
            };
            const auto duckAmount = juce::jlimit (0.0f, 1.0f, get ("delayDuck", 0.0f));
            const auto duckThreshold = get ("delayDuckThreshold", -30.0f);
            const auto duckAttackCoefficient = std::exp (-1.0f / static_cast<float> (sampleRate * 0.01));
            const auto duckReleaseCoefficient = std::exp (-1.0f / static_cast<float> (
                sampleRate * juce::jmax (0.02f, get ("delayDuckRelease", 250.0f) / 1000.0f)));
            auto* left = buffer.getWritePointer (0);
            auto* right = numChannels > 1 ? buffer.getWritePointer (1) : left;
            for (int i = 0; i < numSamples; ++i)
            {
                const auto dryLeft = left[i];
                const auto dryRight = numChannels > 1 ? right[i] : dryLeft;
                const auto monoInput = numChannels > 1 ? 0.5f * (dryLeft + dryRight) : dryLeft;
                const auto detector = juce::jmax (std::abs (dryLeft), std::abs (dryRight));
                const auto detectorCoefficient = detector > delayDuckEnvelope
                    ? duckAttackCoefficient : duckReleaseCoefficient;
                delayDuckEnvelope = detectorCoefficient * delayDuckEnvelope
                                  + (1.0f - detectorCoefficient) * detector;
                const auto detectorDb = juce::Decibels::gainToDecibels (juce::jmax (delayDuckEnvelope, 1.0e-9f));
                const auto duckDb = duckAmount * juce::jlimit (0.0f, 18.0f,
                    (detectorDb - duckThreshold) * 0.75f);
                const auto duckGain = juce::Decibels::decibelsToGain (-duckDb);

                if (mode == 0) // Mono repeats sit in the centre of a stereo output.
                {
                    auto& line = delayLines[0];
                    const auto wet = shapeRepeat (0, line.popSample (0, delaySamples));
                    line.pushSample (0, monoInput + wet * feedback);
                    const auto outputWet = wet * duckGain * mix;
                    left[i] = dryLeft * (1.0f - mix) + outputWet;
                    if (numChannels > 1)
                        right[i] = dryRight * (1.0f - mix) + outputWet;
                }
                else if (mode == 2) // Mono input alternates between the left and right delay lines.
                {
                    auto& leftLine = delayLines[0];
                    auto& rightLine = delayLines[1];
                    const auto wetLeft = shapeRepeat (0, leftLine.popSample (0, delaySamples));
                    const auto wetRight = shapeRepeat (1, rightLine.popSample (0, delaySamples));
                    leftLine.pushSample (0, monoInput + wetRight * feedback);
                    rightLine.pushSample (0, wetLeft * feedback);
                    if (numChannels > 1)
                    {
                        left[i] = dryLeft * (1.0f - mix) + wetLeft * duckGain * mix;
                        right[i] = dryRight * (1.0f - mix) + wetRight * duckGain * mix;
                    }
                    else
                        left[i] = dryLeft * (1.0f - mix) + 0.5f * (wetLeft + wetRight) * duckGain * mix;
                }
                else // Stereo keeps independent repeats on each channel.
                {
                    const auto wetLeft = shapeRepeat (0, delayLines[0].popSample (0, delaySamples));
                    delayLines[0].pushSample (0, dryLeft + wetLeft * feedback);
                    left[i] = dryLeft * (1.0f - mix) + wetLeft * duckGain * mix;
                    if (numChannels > 1)
                    {
                        const auto wetRight = shapeRepeat (1, delayLines[1].popSample (0, delaySamples));
                        delayLines[1].pushSample (0, dryRight + wetRight * feedback);
                        right[i] = dryRight * (1.0f - mix) + wetRight * duckGain * mix;
                    }
                }
            }
        }
        else if (stage == 1 && enabled ("reverbOn"))
        {
            const auto mode = juce::jlimit (0, 2, juce::roundToInt (get ("reverbMode", 0.0f)));
            const auto size = juce::jlimit (0.0f, 1.0f, get ("reverbSize", 0.45f));
            const auto damping = juce::jlimit (0.0f, 1.0f, get ("reverbDamping", 0.45f));
            const auto width = juce::jlimit (0.0f, 1.0f, get ("reverbWidth", 0.8f));
            const auto userPredelayMs = juce::jlimit (0.0f, 100.0f, get ("reverbPreDelay", 0.0f));
            float roomSize = size;
            float modeDamping = damping;
            float modeWidth = width;
            float modePredelayMs = userPredelayMs;
            if (mode == 1) // Plate: denser, brighter response with a short pre-delay.
            {
                roomSize = 0.55f + 0.4f * size;
                modeDamping = damping * 0.55f;
                modeWidth = juce::jmax (0.9f, width);
                modePredelayMs += 12.0f;
            }
            else if (mode == 2) // Cloud: very long, wide, diffuse algorithmic tail.
            {
                roomSize = 0.78f + 0.21f * size;
                modeDamping = 0.2f + 0.45f * damping;
                modeWidth = 1.0f;
                modePredelayMs += 35.0f;
            }

            auto reverbParameters = reverb.getParameters();
            reverbParameters.roomSize = roomSize;
            reverbParameters.damping = modeDamping;
            reverbParameters.width = modeWidth;
            reverbParameters.wetLevel = 1.0f;
            reverbParameters.dryLevel = 0.0f;
            reverb.setParameters (reverbParameters);
            const auto predelaySamples = static_cast<float> (juce::jlimit (1.0, sampleRate * 0.15,
                static_cast<double> (modePredelayMs) * sampleRate / 1000.0));
            for (int channel = 0; channel < numChannels; ++channel)
            {
                const auto* dry = buffer.getReadPointer (channel);
                auto* wet = reverbWorkBuffer.getWritePointer (channel);
                auto& line = reverbPreDelayLines[static_cast<size_t> (channel)];
                for (int i = 0; i < numSamples; ++i)
                {
                    const auto delayedInput = line.popSample (0, predelaySamples);
                    line.pushSample (0, dry[i]);
                    wet[i] = delayedInput;
                }
            }
            if (numChannels >= 2)
                reverb.processStereo (reverbWorkBuffer.getWritePointer (0), reverbWorkBuffer.getWritePointer (1), numSamples);
            else
                reverb.processMono (reverbWorkBuffer.getWritePointer (0), numSamples);
            const auto mix = juce::jlimit (0.0f, 1.0f, get ("reverbMix", 0.2f));
            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto* dry = buffer.getWritePointer (channel);
                const auto* wet = reverbWorkBuffer.getReadPointer (channel);
                for (int i = 0; i < numSamples; ++i)
                    dry[i] = dry[i] * (1.0f - mix) + wet[i] * mix;
            }
        }
        else if (stage == 2 && enabled ("chorusOn"))
        {
            chorus.setRate (get ("chorusRate", 0.8f));
            chorus.setDepth (get ("chorusDepth", 0.35f));
            chorus.setMix (get ("chorusMix", 0.25f));
            auto block = juce::dsp::AudioBlock<float> (buffer).getSubBlock (0, static_cast<size_t> (numSamples));
            chorus.process (juce::dsp::ProcessContextReplacing<float> (block));
        }
        else if (stage == 3 && enabled ("postOn"))
            processEqualizer (buffer, numChannels, numSamples, "post", false);
    };
    for (const auto stage : getPostFxOrder())
        processPostFx (stage);
    const auto outputGainDb = get ("outputGain");
    buffer.applyGain (juce::Decibels::decibelsToGain (outputGainDb));
    if (std::abs (outputGainDb) < 0.001f)
    {
        auto outputBlock = juce::dsp::AudioBlock<float> (buffer).getSubBlock (0, static_cast<size_t> (numSamples));
        outputSafetyLimiter.process (juce::dsp::ProcessContextReplacing<float> (outputBlock));
    }
    float outputBlockPeak = 0.0f;
    for (int channel = 0; channel < numChannels; ++channel)
        outputBlockPeak = juce::jmax (outputBlockPeak, buffer.getMagnitude (channel, 0, numSamples));
    outputPeak.store (juce::jmax (outputBlockPeak, outputPeak.load (std::memory_order_relaxed) * meterFall),
                      std::memory_order_relaxed);
}

bool TheLeadAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* TheLeadAudioProcessor::createEditor() { return new TheLeadAudioProcessorEditor (*this); }

void TheLeadAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    for (int slot = 0; slot < numIRSlots; ++slot)
        state.setProperty ("irFile" + juce::String (slot + 1), irFilePaths[slot], nullptr);
    for (int slot = 0; slot < numAmpSlots; ++slot)
        state.setProperty ("ampFile" + juce::String (slot + 1), namFilePaths[slot], nullptr);
    const auto preOrder = getPreFxOrder();
    for (size_t i = 0; i < preOrder.size(); ++i)
        state.setProperty ("preFxOrder" + juce::String (static_cast<int> (i)), preOrder[i], nullptr);
    const auto postOrder = getPostFxOrder();
    for (size_t i = 0; i < postOrder.size(); ++i)
        state.setProperty ("postFxOrder" + juce::String (static_cast<int> (i)), postOrder[i], nullptr);
    const auto ampOrder = getAmpSlotOrder();
    const auto irOrder = getIRSlotOrder();
    for (size_t i = 0; i < ampOrder.size(); ++i)
        state.setProperty ("ampSlotOrder" + juce::String (static_cast<int> (i)), ampOrder[i], nullptr);
    for (size_t i = 0; i < irOrder.size(); ++i)
        state.setProperty ("irSlotOrder" + juce::String (static_cast<int> (i)), irOrder[i], nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void TheLeadAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (! xml->hasTagName (parameters.state.getType()))
            return;
        auto state = juce::ValueTree::fromXml (*xml);
        parameters.replaceState (state);
        auto preOrder = getPreFxOrder();
        std::array<bool, numPreFxStages> preSeen {};
        size_t preCount = 0;
        const auto storedPreCount = state.hasProperty ("preFxOrder4") ? 5 : 4;
        for (int i = 0; i < storedPreCount; ++i)
        {
            const auto stage = static_cast<int> (state.getProperty ("preFxOrder" + juce::String (i), i));
            if (stage >= 0 && stage < numPreFxStages && ! preSeen[static_cast<size_t> (stage)])
            {
                preSeen[static_cast<size_t> (stage)] = true;
                preOrder[preCount++] = stage;
            }
        }
        for (int stage = 0; stage < numPreFxStages; ++stage)
            if (! preSeen[static_cast<size_t> (stage)]) preOrder[preCount++] = stage;
        setPreFxOrder (preOrder);
        auto postOrder = getPostFxOrder();
        std::array<bool, numPostFxStages> postSeen {};
        size_t postCount = 0;
        for (int i = 0; i < 5; ++i)
        {
            const auto stage = static_cast<int> (state.getProperty ("postFxOrder" + juce::String (i), i));
            if (stage >= 0 && stage < numPostFxStages && ! postSeen[static_cast<size_t> (stage)])
            {
                postSeen[static_cast<size_t> (stage)] = true;
                postOrder[postCount++] = stage;
            }
        }
        for (int stage = 0; stage < numPostFxStages; ++stage)
            if (! postSeen[static_cast<size_t> (stage)]) postOrder[postCount++] = stage;
        setPostFxOrder (postOrder);
        auto ampOrder = getAmpSlotOrder();
        auto irOrder = getIRSlotOrder();
        for (size_t i = 0; i < ampOrder.size(); ++i)
            ampOrder[i] = static_cast<int> (state.getProperty ("ampSlotOrder" + juce::String (static_cast<int> (i)), ampOrder[i]));
        for (size_t i = 0; i < irOrder.size(); ++i)
            irOrder[i] = static_cast<int> (state.getProperty ("irSlotOrder" + juce::String (static_cast<int> (i)), irOrder[i]));
        setAmpSlotOrder (ampOrder);
        setIRSlotOrder (irOrder);
        for (int slot = 0; slot < numIRSlots; ++slot)
        {
            irFilePaths[slot] = state.getProperty ("irFile" + juce::String (slot + 1)).toString();
            const juce::File file (irFilePaths[slot]);
            irLoaded[slot].store (file.existsAsFile());
            if (irLoaded[slot].load())
                loadImpulseResponse (slot, file);
        }
        for (int slot = 0; slot < numAmpSlots; ++slot)
        {
            namFilePaths[slot] = state.getProperty ("ampFile" + juce::String (slot + 1)).toString();
            const juce::File file (namFilePaths[slot]);
            if (file.existsAsFile())
                loadNAMCapture (slot, file);
        }
    }
}

void TheLeadAudioProcessor::loadImpulseResponse (int slot, const juce::File& file)
{
    if (slot < 0 || slot >= numIRSlots || ! file.existsAsFile())
        return;
    irConvolvers[slot].loadImpulseResponse (file, juce::dsp::Convolution::Stereo::yes,
                                            juce::dsp::Convolution::Trim::yes, 0,
                                            juce::dsp::Convolution::Normalise::yes);
    irFilePaths[slot] = file.getFullPathName();
    irLoaded[slot].store (true);
}

juce::String TheLeadAudioProcessor::getImpulseResponseName (int slot) const
{
    if (slot < 0 || slot >= numIRSlots || irFilePaths[slot].isEmpty())
        return "No IR loaded";
    return juce::File (irFilePaths[slot]).getFileName();
}

void TheLeadAudioProcessor::loadNAMCapture (int slot, const juce::File& file)
{
    if (slot < 0 || slot >= numAmpSlots || ! file.existsAsFile())
        return;
    namFilePaths[slot] = file.getFullPathName();
    ampModels[slot]->setStatus ("Loading NAM capture…");
    modelLoader.addJob ([this, slot, file]
    {
        juce::String error;
        ampModels[slot]->create (file, modelSampleRate.load(), modelMaxBlockSize.load(), error);
    });
}

juce::String TheLeadAudioProcessor::getNAMCaptureName (int slot) const
{
    if (slot < 0 || slot >= numAmpSlots || namFilePaths[slot].isEmpty())
        return "No NAM capture loaded";
    return juce::File (namFilePaths[slot]).getFileName();
}

juce::String TheLeadAudioProcessor::getNAMCaptureStatus (int slot) const
{
    if (slot < 0 || slot >= numAmpSlots)
        return "Invalid slot";
    return ampModels[slot]->getStatus();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TheLeadAudioProcessor();
}
