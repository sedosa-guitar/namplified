#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <get_dsp.h>

#include <array>
#include <cmath>
#include <limits>

namespace
{
bool copyLibraryFile (const juce::File& source, const juce::File& destination)
{
    if (destination.existsAsFile())
        return true;
    if (! destination.getParentDirectory().createDirectory().wasOk())
        return false;

    juce::TemporaryFile staging (destination);
    return source.copyFileTo (staging.getFile())
        && staging.overwriteTargetFileWithTemporary();
}

juce::File findNamedNAMPeer (const juce::File& file, int slot)
{
    if (! file.existsAsFile())
        return file;

    const auto genericName = "NAM" + juce::String (slot + 1);
    if (! file.getFileNameWithoutExtension().equalsIgnoreCase (genericName)
        && ! file.getFileNameWithoutExtension().equalsIgnoreCase (genericName + "_capture"))
        return file;

    const juce::SHA256 sourceHash (file);
    juce::File bestMatch;
    int bestRepeatedPrefixCount = std::numeric_limits<int>::max();
    for (const auto& candidate : file.getParentDirectory().findChildFiles (
             juce::File::findFiles, false, genericName + "_*.nam"))
    {
        if (candidate.getFileNameWithoutExtension().equalsIgnoreCase (genericName + "_capture"))
            continue;
        if (candidate.getSize() != file.getSize() || juce::SHA256 (candidate) != sourceHash)
            continue;

        auto remainder = candidate.getFileName();
        int repeatedPrefixCount = 0;
        const auto prefix = genericName + "_";
        while (remainder.startsWithIgnoreCase (prefix))
        {
            ++repeatedPrefixCount;
            remainder = remainder.substring (prefix.length());
        }
        if (repeatedPrefixCount < bestRepeatedPrefixCount)
        {
            bestMatch = candidate;
            bestRepeatedPrefixCount = repeatedPrefixCount;
        }
    }
    return bestMatch != juce::File() ? bestMatch : file;
}

juce::String displayNameForNAMFile (const juce::File& file, int slot)
{
    auto name = file.getFileName();
    if (name.equalsIgnoreCase ("NAM" + juce::String (slot + 1) + ".nam")
        || name.equalsIgnoreCase ("NAM" + juce::String (slot + 1) + "_capture.nam"))
        return "NAM capture";
    const auto prefix = "NAM" + juce::String (slot + 1) + "_";
    if (name.startsWithIgnoreCase (prefix))
        name = name.substring (prefix.length());
    return name;
}
}

struct NAMplifiedAudioProcessor::NAMModel
{
    mutable juce::SpinLock lock;
    std::unique_ptr<nam::DSP> models[2];
    std::array<std::vector<NAM_SAMPLE>, 2> input;
    std::array<std::vector<NAM_SAMPLE>, 2> output;
    juce::String path;
    juce::String status;
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

NAMplifiedAudioProcessor::NAMplifiedAudioProcessor()
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
        namModels[slot] = std::make_unique<NAMModel>();
    boostNAMModel = std::make_unique<NAMModel>();
    const auto& processorParameters = getParameters();
    midiParameterIds.reserve (static_cast<size_t> (processorParameters.size()));
    for (auto* parameter : processorParameters)
        if (const auto* withId = dynamic_cast<const juce::AudioProcessorParameterWithID*> (parameter))
            midiParameterIds.push_back (withId->paramID);
    for (auto& mapping : midiCCParameterIndices)
        mapping.store (-1, std::memory_order_relaxed);
    loadMidiMappings();
}

juce::File NAMplifiedAudioProcessor::getMidiMappingsFile() const
{
    const auto appData = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
    const auto mappings = appData.getChildFile ("NAMplified").getChildFile ("MidiMappings.xml");
    const auto legacyMappings = appData.getChildFile ("NAMinator").getChildFile ("MidiMappings.xml");
    if (! mappings.existsAsFile() && legacyMappings.existsAsFile())
    {
        mappings.getParentDirectory().createDirectory();
        legacyMappings.copyFileTo (mappings);
    }
    return mappings;
}

int NAMplifiedAudioProcessor::findMidiParameterIndex (const juce::String& parameterId) const
{
    for (size_t i = 0; i < midiParameterIds.size(); ++i)
        if (midiParameterIds[i] == parameterId)
            return static_cast<int> (i);
    return -1;
}

void NAMplifiedAudioProcessor::loadMidiMappings()
{
    const auto file = getMidiMappingsFile();
    if (! file.existsAsFile())
        return;
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
        return;
    const auto state = juce::ValueTree::fromXml (*xml);
    if (! state.isValid())
        return;
    for (const auto entry : state)
    {
        const auto cc = static_cast<int> (entry.getProperty ("cc", -1));
        const auto index = findMidiParameterIndex (entry.getProperty ("parameter").toString());
        if (juce::isPositiveAndBelow (cc, 128) && index >= 0)
            midiCCParameterIndices[static_cast<size_t> (cc)].store (index, std::memory_order_relaxed);
    }
}

bool NAMplifiedAudioProcessor::saveMidiMappings() const
{
    juce::ValueTree state ("MidiMappings");
    for (int cc = 0; cc < 128; ++cc)
    {
        const auto index = midiCCParameterIndices[static_cast<size_t> (cc)].load (std::memory_order_relaxed);
        if (! juce::isPositiveAndBelow (index, static_cast<int> (midiParameterIds.size())))
            continue;
        juce::ValueTree entry ("Mapping");
        entry.setProperty ("cc", cc, nullptr);
        entry.setProperty ("parameter", midiParameterIds[static_cast<size_t> (index)], nullptr);
        state.addChild (entry, -1, nullptr);
    }
    const auto file = getMidiMappingsFile();
    file.getParentDirectory().createDirectory();
    return state.createXml()->writeTo (file);
}

bool NAMplifiedAudioProcessor::setMidiLearnTarget (const juce::String& parameterId)
{
    const auto index = findMidiParameterIndex (parameterId);
    if (index < 0)
        return false;
    learnedMidiCC.store (-1, std::memory_order_relaxed);
    midiLearnTargetIndex.store (index, std::memory_order_release);
    return true;
}

void NAMplifiedAudioProcessor::armMidiLearn() noexcept
{
    midiLearnTargetIndex.store (-1, std::memory_order_release);
    learnedMidiCC.store (-1, std::memory_order_relaxed);
    midiLearningArmed.store (true, std::memory_order_release);
}

bool NAMplifiedAudioProcessor::isMidiLearnArmed() const noexcept
{
    return midiLearningArmed.load (std::memory_order_acquire);
}

bool NAMplifiedAudioProcessor::hasMidiLearnTarget() const noexcept
{
    return midiLearnTargetIndex.load (std::memory_order_acquire) >= 0;
}

void NAMplifiedAudioProcessor::cancelMidiLearn() noexcept
{
    midiLearnTargetIndex.store (-1, std::memory_order_release);
    midiLearningArmed.store (false, std::memory_order_release);
}

bool NAMplifiedAudioProcessor::takeMidiLearnResult (int& controller, juce::String& parameterId)
{
    controller = learnedMidiCC.exchange (-1, std::memory_order_acq_rel);
    if (! juce::isPositiveAndBelow (controller, 128))
        return false;
    const auto index = learnedMidiParameterIndex.load (std::memory_order_acquire);
    if (! juce::isPositiveAndBelow (index, static_cast<int> (midiParameterIds.size())))
        return false;
    parameterId = midiParameterIds[static_cast<size_t> (index)];
    return true;
}

int NAMplifiedAudioProcessor::getMidiMappingCount() const noexcept
{
    int count = 0;
    for (const auto& mapping : midiCCParameterIndices)
        count += mapping.load (std::memory_order_relaxed) >= 0 ? 1 : 0;
    return count;
}

NAMplifiedAudioProcessor::IRParameters::ParameterLayout NAMplifiedAudioProcessor::createParameterLayout()
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
    addKnob ("masterVolume", "Master Volume", -24.0f, 24.0f, 0.0f);
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
        juce::ParameterID { "boostSource", 1 }, "Boost Source",
        juce::StringArray { "Boost", "NAM Capture" }, 0));
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
    addKnob ("boostNAMIn", "NAM Boost Input", -24.0f, 24.0f, 0.0f);
    addKnob ("boostNAMOut", "NAM Boost Output", -24.0f, 24.0f, 0.0f);
    addToggle ("preEqOn", "Pre EQ");
    addKnob ("preLow", "Pre EQ Low", -12.0f, 12.0f, 0.0f);
    addFrequency ("preLowFreq", "Pre EQ Low Frequency", 50.0f, 12000.0f, 80.0f);
    addKnob ("preBand2", "Pre EQ Band 2", -12.0f, 12.0f, 0.0f);
    addFrequency ("preBand2Freq", "Pre EQ Band 2 Frequency", 50.0f, 12000.0f, 250.0f);
    addKnob ("preMid", "Pre EQ Mid", -12.0f, 12.0f, 0.0f);
    addFrequency ("preMidFreq", "Pre EQ Mid Frequency", 50.0f, 12000.0f, 800.0f);
    addKnob ("preBand4", "Pre EQ Band 4", -12.0f, 12.0f, 0.0f);
    addFrequency ("preBand4Freq", "Pre EQ Band 4 Frequency", 50.0f, 12000.0f, 2500.0f);
    addKnob ("preHigh", "Pre EQ High", -12.0f, 12.0f, 0.0f);
    addFrequency ("preHighFreq", "Pre EQ High Frequency", 50.0f, 12000.0f, 8000.0f);
    for (const auto* id : { "preLowQ", "preBand2Q", "preMidQ", "preBand4Q", "preHighQ" })
        addKnob (id, "Pre EQ Band Q", 0.30f, 4.0f, 0.707f);
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "ampRouting", 1 }, "Amp Routing",
        juce::StringArray { "Series", "Parallel Blend" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "parallelIRMode", 1 }, "Parallel IR Routing",
        juce::StringArray { "Paired IRs", "Shared IR 1" }, 0));
    addToggle ("ampCrossoverOn", "Parallel Frequency Split");
    addFrequency ("ampCrossoverFreq", "Amp Crossover Frequency", 80.0f, 1200.0f, 250.0f);
    for (int slot = 0; slot < numAmpSlots; ++slot)
    {
        const auto number = juce::String (slot + 1);
        addToggle (("amp" + juce::String (slot + 1) + "On").toRawUTF8(),
                   ("Amp " + juce::String (slot + 1)).toRawUTF8(), slot == 0);
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ("amp" + number + "EqPosition").toRawUTF8(), 1 },
            ("NAM " + number + " EQ Position").toRawUTF8(),
            juce::StringArray { "Pre", "Post" }, 1));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ("amp" + number + "Polarity").toRawUTF8(), 1 },
            ("NAM " + number + " Polarity").toRawUTF8(),
            juce::StringArray { "Normal", "Invert" }, 0));
        addKnob (("amp" + number + "EqBass").toRawUTF8(), ("NAM " + number + " EQ Bass").toRawUTF8(), -12.0f, 12.0f, 0.0f);
        addKnob (("amp" + number + "EqMid").toRawUTF8(), ("NAM " + number + " EQ Mid").toRawUTF8(), -12.0f, 12.0f, 0.0f);
        addKnob (("amp" + number + "EqTreble").toRawUTF8(), ("NAM " + number + " EQ Treble").toRawUTF8(), -12.0f, 12.0f, 0.0f);
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
    addFrequency ("delayHighPass", "Delay High-Pass", 20.0f, 1000.0f, 100.0f);
    addFrequency ("delayLowPass", "Delay Low-Pass", 1000.0f, 20000.0f, 8000.0f);
    addKnob ("delayDuck", "Delay Ducking", 0.0f, 1.0f, 0.0f);
    addKnob ("delayDuckThreshold", "Delay Duck Threshold", -48.0f, 0.0f, -30.0f);
    addKnob ("delayDuckRelease", "Delay Duck Release", 20.0f, 1000.0f, 250.0f);
    addToggle ("reverbOn", "Reverb", true);
    addKnob ("reverbSize", "Reverb Size", 0.0f, 1.0f, 0.35f);
    addKnob ("reverbMix", "Reverb Mix", 0.0f, 1.0f, 0.12f);
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "reverbMode", 1 }, "Reverb Mode",
        juce::StringArray { "Room", "Plate", "Cloud", "IR" }, 0));
    addKnob ("reverbDamping", "Reverb Damping", 0.0f, 1.0f, 0.45f);
    addKnob ("reverbPreDelay", "Reverb Pre-Delay", 0.0f, 100.0f, 0.0f);
    addKnob ("roomSize", "Room Size", 0.5f, 1.5f, 1.0f);
    addKnob ("roomEarlyMix", "Early Reflections", 0.0f, 1.0f, 0.35f);
    addKnob ("roomSpin", "Room Spin", 0.1f, 5.0f, 0.5f);
    addKnob ("roomWander", "Room Wander", 0.0f, 1.0f, 0.5f);
    addKnob ("reverbWidth", "Reverb Width", 0.0f, 1.0f, 0.8f);
    addFrequency ("reverbHighPass", "Reverb High-Pass", 20.0f, 1000.0f, 120.0f);
    addFrequency ("reverbLowPass", "Reverb Low-Pass", 1000.0f, 20000.0f, 10000.0f);
    addToggle ("chorusOn", "Chorus");
    addKnob ("chorusRate", "Chorus Rate", 0.1f, 5.0f, 0.8f);
    addKnob ("chorusDepth", "Chorus Depth", 0.0f, 1.0f, 0.35f);
    addKnob ("chorusMix", "Chorus Mix", 0.0f, 1.0f, 0.25f);
    addToggle ("postOn", "Post EQ");
    addKnob ("postLow", "Post EQ Low", -12.0f, 12.0f, 0.0f);
    addFrequency ("postLowFreq", "Post EQ Low Frequency", 50.0f, 16000.0f, 50.0f);
    addKnob ("postMid", "Post EQ Mid", -12.0f, 12.0f, 0.0f);
    addFrequency ("postGraphic2Freq", "Post EQ 100 Hz Frequency", 50.0f, 16000.0f, 100.0f);
    addKnob ("postGraphic2", "Post EQ 100 Hz", -12.0f, 12.0f, 0.0f);
    addFrequency ("postGraphic3Freq", "Post EQ 200 Hz Frequency", 50.0f, 16000.0f, 200.0f);
    addKnob ("postGraphic3", "Post EQ 200 Hz", -12.0f, 12.0f, 0.0f);
    addFrequency ("postGraphic4Freq", "Post EQ 400 Hz Frequency", 50.0f, 16000.0f, 400.0f);
    addKnob ("postGraphic4", "Post EQ 400 Hz", -12.0f, 12.0f, 0.0f);
    addFrequency ("postMidFreq", "Post EQ Mid Frequency", 50.0f, 16000.0f, 800.0f);
    addKnob ("postGraphic6", "Post EQ 1.6 kHz", -12.0f, 12.0f, 0.0f);
    addFrequency ("postGraphic6Freq", "Post EQ 1.6 kHz Frequency", 50.0f, 16000.0f, 1600.0f);
    addKnob ("postGraphic7", "Post EQ 3.2 kHz", -12.0f, 12.0f, 0.0f);
    addFrequency ("postGraphic7Freq", "Post EQ 3.2 kHz Frequency", 50.0f, 16000.0f, 3200.0f);
    addKnob ("postGraphic8", "Post EQ 6.4 kHz", -12.0f, 12.0f, 0.0f);
    addFrequency ("postGraphic8Freq", "Post EQ 6.4 kHz Frequency", 50.0f, 16000.0f, 6400.0f);
    addKnob ("postHigh", "Post EQ High", -12.0f, 12.0f, 0.0f);
    addFrequency ("postHighFreq", "Post EQ High Frequency", 50.0f, 16000.0f, 16000.0f);
    for (const auto* id : { "postLowQ", "postGraphic2Q", "postGraphic3Q", "postGraphic4Q", "postMidQ",
                            "postGraphic6Q", "postGraphic7Q", "postGraphic8Q", "postHighQ" })
        addKnob (id, "Post EQ Band Q", 0.30f, 4.0f, 0.707f);
    // Keep unity gain at the rotary's midpoint so 0 dB points to 12 o'clock.
    addKnob ("outputGain", "Output Gain", -24.0f, 24.0f, 0.0f);
    return layout;
}

NAMplifiedAudioProcessor::~NAMplifiedAudioProcessor() = default;

void NAMplifiedAudioProcessor::BiquadState::set (float nb0, float nb1, float nb2,
                                               float na0, float na1, float na2)
{
    const auto normaliser = na0 != 0.0f ? 1.0f / na0 : 1.0f;
    b0 = nb0 * normaliser; b1 = nb1 * normaliser; b2 = nb2 * normaliser;
    a1 = na1 * normaliser; a2 = na2 * normaliser;
}

float NAMplifiedAudioProcessor::BiquadState::process (float input)
{
    const auto output = b0 * input + z1;
    z1 = b1 * input - a1 * output + z2;
    z2 = b2 * input - a2 * output;
    return output;
}

void NAMplifiedAudioProcessor::setEqCoefficients (BiquadState& filter, int band, float frequency,
                                               float gainDb, double sampleRate, float q)
{
    const auto sr = static_cast<float> (juce::jmax (sampleRate, 1.0));
    const auto f = juce::jlimit (20.0f, sr * 0.45f, frequency);
    const auto w = juce::MathConstants<float>::twoPi * f / sr;
    const auto c = std::cos (w);
    const auto s = std::sin (w);
    const auto A = std::pow (10.0f, gainDb / 40.0f);
    const auto alpha = s / (2.0f * juce::jmax (0.1f, q));
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

void NAMplifiedAudioProcessor::setCutFilterCoefficients (BiquadState& filter, bool highPass,
                                                        float frequency, double sampleRate)
{
    const auto sr = static_cast<float> (juce::jmax (sampleRate, 1.0));
    const auto f = juce::jlimit (20.0f, sr * 0.45f, frequency);
    const auto omega = juce::MathConstants<float>::twoPi * f / sr;
    const auto cosine = std::cos (omega);
    const auto alpha = std::sin (omega) / (2.0f * 0.70710678f);
    const auto inverseA0 = 1.0f / (1.0f + alpha);
    if (highPass)
        filter.set ((1.0f + cosine) * 0.5f * inverseA0, -(1.0f + cosine) * inverseA0,
                    (1.0f + cosine) * 0.5f * inverseA0, 1.0f,
                    -2.0f * cosine * inverseA0, (1.0f - alpha) * inverseA0);
    else
        filter.set ((1.0f - cosine) * 0.5f * inverseA0, (1.0f - cosine) * inverseA0,
                    (1.0f - cosine) * 0.5f * inverseA0, 1.0f,
                    -2.0f * cosine * inverseA0, (1.0f - alpha) * inverseA0);
}

void NAMplifiedAudioProcessor::processEqualizer (juce::AudioBuffer<float>& buffer, int channels, int samples,
                                               const juce::String& prefix, bool pre)
{
    const auto sampleRate = juce::jmax (1.0, modelSampleRate.load());
    juce::ignoreUnused (prefix);
    constexpr int preBandCount = 5;
    constexpr int postBandCount = 9;
    const char* preGainIds[] { "preLow", "preBand2", "preMid", "preBand4", "preHigh" };
    const char* preFrequencyIds[] { "preLowFreq", "preBand2Freq", "preMidFreq", "preBand4Freq", "preHighFreq" };
    const char* postGainIds[] { "postLow", "postGraphic2", "postGraphic3", "postGraphic4", "postMid",
                                "postGraphic6", "postGraphic7", "postGraphic8", "postHigh" };
    const char* postFrequencyIds[] { "postLowFreq", "postGraphic2Freq", "postGraphic3Freq", "postGraphic4Freq",
                                     "postMidFreq", "postGraphic6Freq", "postGraphic7Freq", "postGraphic8Freq",
                                     "postHighFreq" };
    const auto processFilters = [&] (auto& filters, int bandCount, bool isPre)
    {
        for (int band = 0; band < bandCount; ++band)
        {
            const auto gainId = isPre ? juce::String (preGainIds[band]) : juce::String (postGainIds[band]);
            const auto* gain = parameters.getRawParameterValue (gainId);
            const auto gainDb = gain != nullptr ? gain->load() : 0.0f;
            const auto frequencyId = isPre ? juce::String (preFrequencyIds[band])
                                            : juce::String (postFrequencyIds[band]);
            const auto* frequency = parameters.getRawParameterValue (frequencyId);
            const auto hz = frequency != nullptr ? frequency->load() : 1000.0f;
            const auto* qParameter = parameters.getRawParameterValue (gainId + "Q");
            const auto q = qParameter != nullptr ? qParameter->load() : 0.707f;
            for (int channel = 0; channel < channels; ++channel)
            {
                const auto filterType = band == 0 ? 0 : band == bandCount - 1 ? 2 : 1;
                setEqCoefficients (filters[static_cast<size_t> (channel)][static_cast<size_t> (band)],
                                   filterType, hz, gainDb, sampleRate, q);
            }
        }

        for (int channel = 0; channel < channels; ++channel)
        {
            auto* samplesData = buffer.getWritePointer (channel);
            auto& channelFilters = filters[static_cast<size_t> (channel)];
            for (int i = 0; i < samples; ++i)
            {
                auto sample = samplesData[i];
                for (int band = 0; band < bandCount; ++band)
                    sample = channelFilters[static_cast<size_t> (band)].process (sample);
                samplesData[i] = sample;
            }
        }
    };
    if (pre)
        processFilters (preEqFilters, preBandCount, true);
    else
        processFilters (postEqFilters, postBandCount, false);
}

void NAMplifiedAudioProcessor::processIRShapeEqualizer (juce::AudioBuffer<float>& buffer, int channels, int samples, int slot)
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

const juce::String NAMplifiedAudioProcessor::getName() const { return JucePlugin_Name; }
std::array<int, NAMplifiedAudioProcessor::numPreFxStages> NAMplifiedAudioProcessor::getPreFxOrder() const
{
    std::array<int, numPreFxStages> order {};
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = preFxOrder[i].load();
    return order;
}

void NAMplifiedAudioProcessor::setPreFxOrder (const std::array<int, numPreFxStages>& order)
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

std::array<int, NAMplifiedAudioProcessor::numAmpSlots> NAMplifiedAudioProcessor::getAmpSlotOrder() const
{
    std::array<int, numAmpSlots> order {};
    for (size_t i = 0; i < order.size(); ++i) order[i] = ampSlotOrder[i].load();
    return order;
}

void NAMplifiedAudioProcessor::setAmpSlotOrder (const std::array<int, numAmpSlots>& order)
{
    std::array<bool, numAmpSlots> seen {};
    for (const auto slot : order)
    {
        if (slot < 0 || slot >= numAmpSlots || seen[static_cast<size_t> (slot)]) return;
        seen[static_cast<size_t> (slot)] = true;
    }
    for (size_t i = 0; i < order.size(); ++i) ampSlotOrder[i].store (order[i]);
}

std::array<int, NAMplifiedAudioProcessor::numIRSlots> NAMplifiedAudioProcessor::getIRSlotOrder() const
{
    std::array<int, numIRSlots> order {};
    for (size_t i = 0; i < order.size(); ++i) order[i] = irSlotOrder[i].load();
    return order;
}

void NAMplifiedAudioProcessor::setIRSlotOrder (const std::array<int, numIRSlots>& order)
{
    std::array<bool, numIRSlots> seen {};
    for (const auto slot : order)
    {
        if (slot < 0 || slot >= numIRSlots || seen[static_cast<size_t> (slot)]) return;
        seen[static_cast<size_t> (slot)] = true;
    }
    for (size_t i = 0; i < order.size(); ++i) irSlotOrder[i].store (order[i]);
}

std::array<int, NAMplifiedAudioProcessor::numPostFxStages> NAMplifiedAudioProcessor::getPostFxOrder() const
{
    std::array<int, numPostFxStages> order {};
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = postFxOrder[i].load();
    return order;
}

void NAMplifiedAudioProcessor::setPostFxOrder (const std::array<int, numPostFxStages>& order)
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

void NAMplifiedAudioProcessor::resetToDefaults()
{
    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());

    setPreFxOrder ({ 0, 1, 2, 3 });
    setAmpSlotOrder ({ 0, 1 });
    setIRSlotOrder ({ 0, 1 });
    setPostFxOrder ({ 0, 1, 2, 3 });
}

bool NAMplifiedAudioProcessor::acceptsMidi() const { return JucePlugin_WantsMidiInput != 0; }
bool NAMplifiedAudioProcessor::producesMidi() const { return JucePlugin_ProducesMidiOutput != 0; }
bool NAMplifiedAudioProcessor::isMidiEffect() const { return JucePlugin_IsMidiEffect != 0; }
double NAMplifiedAudioProcessor::getTailLengthSeconds() const { return 10.0; }
int NAMplifiedAudioProcessor::getNumPrograms() { return 1; }
int NAMplifiedAudioProcessor::getCurrentProgram() { return 0; }
void NAMplifiedAudioProcessor::setCurrentProgram (int index) { juce::ignoreUnused (index); }
const juce::String NAMplifiedAudioProcessor::getProgramName (int index) { juce::ignoreUnused (index); return {}; }
void NAMplifiedAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

juce::File NAMplifiedAudioProcessor::getUserPresetDirectory() const
{
    const auto appData = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
    const auto presets = appData.getChildFile ("Application Support")
        .getChildFile ("NAMplified").getChildFile ("Presets");
    presets.createDirectory();
    const juce::File legacyLocations[] {
        appData.getChildFile ("The Lead").getChildFile ("Presets"),
        appData.getChildFile ("Application Support").getChildFile ("NAMinator").getChildFile ("Presets"),
        appData.getChildFile ("NAMinator").getChildFile ("Presets"),
        appData.getChildFile ("NAMplified").getChildFile ("Presets")
    };
    const auto migrationMarker = presets.getChildFile (".legacy-presets-migrated");
    if (! migrationMarker.existsAsFile())
    {
        bool migrationSucceeded = true;
        for (const auto& oldPresets : legacyLocations)
            if (oldPresets != presets && oldPresets.isDirectory()
                && ! oldPresets.copyDirectoryTo (presets))
                migrationSucceeded = false;
        if (migrationSucceeded)
            migrationMarker.replaceWithText ("Legacy preset migration completed once.");
    }
    return presets;
}

juce::File NAMplifiedAudioProcessor::getUserPresetFile (const juce::String& name) const
{
    auto safeName = name.trim().retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-().");
    while (safeName.startsWith (".")) safeName = safeName.substring (1);
    safeName = safeName.trim();
    if (safeName.isEmpty()) return {};
    return getUserPresetDirectory().getChildFile (safeName).withFileExtension (".tlpreset");
}

juce::File NAMplifiedAudioProcessor::getUserPresetLibraryDirectory() const
{
    return getUserPresetDirectory().getChildFile ("Library");
}

juce::StringArray NAMplifiedAudioProcessor::getUserPresetNames() const
{
    juce::StringArray names;
    const auto folder = getUserPresetDirectory();
    if (! folder.isDirectory()) return names;
    for (const auto& file : folder.findChildFiles (juce::File::findFiles, false, "*.tlpreset"))
        names.add (file.getFileNameWithoutExtension());
    names.sort (true);
    return names;
}

bool NAMplifiedAudioProcessor::saveUserPreset (const juce::String& name)
{
    const auto file = getUserPresetFile (name);
    if (file == juce::File() || ! getUserPresetDirectory().createDirectory().wasOk())
        return false;

    juce::MemoryBlock savedState;
    getStateInformation (savedState);
    auto xml = getXmlFromBinary (savedState.getData(), static_cast<int> (savedState.getSize()));
    if (xml == nullptr)
        return false;
    auto presetState = juce::ValueTree::fromXml (*xml);
    // Master volume is an application-wide control, not part of a named preset.
    for (int i = presetState.getNumChildren(); --i >= 0;)
        if (presetState.getChild (i).getProperty ("id").toString() == "masterVolume")
            presetState.removeChild (i, nullptr);
    presetState.setProperty ("selectedUserPreset", file.getFileNameWithoutExtension(), nullptr);
    const auto library = file.getSiblingFile ("Library");
    const auto storePath = [&presetState, &library] (const juce::String& property, int captureSlot)
    {
        const auto originalPath = presetState.getProperty (property).toString();
        if (originalPath.isEmpty())
            return true;

        const juce::File source (originalPath);
        if (! source.existsAsFile())
            return true;

        auto destination = source;
        if (! source.isAChildOf (library))
        {
            if (source.hasFileExtension ("nam"))
            {
                const auto selected = captureSlot >= 0 ? findNamedNAMPeer (source, captureSlot) : source;
                auto family = source.getParentDirectory().findChildFiles (juce::File::findFiles, false, "*.nam");
                if (! family.contains (selected))
                    family.add (selected);
                family.sort();

                const auto selectedHash = juce::SHA256 (selected).toHexString();
                juce::StringArray hashes;
                juce::Array<juce::File> captures;
                for (const auto& capture : family)
                {
                    const auto name = capture.getFileName();
                    if (capture != selected && capture.getParentDirectory().getFileName().endsWith ("_Assets"))
                    {
                        // Old flat preset folders can contain NAMs for several blocks.
                        const auto prefix = captureSlot >= 0 ? "NAM" + juce::String (captureSlot + 1)
                                                             : juce::String ("BOOST_NAM");
                        if (! name.startsWithIgnoreCase (prefix + "_")
                            && ! name.equalsIgnoreCase (prefix + ".nam"))
                            continue;
                    }

                    const auto hash = juce::SHA256 (capture).toHexString();
                    if (! hashes.contains (hash))
                    {
                        hashes.add (hash);
                        captures.add (capture);
                    }
                }

                juce::String manifest;
                for (int i = 0; i < captures.size(); ++i)
                    manifest += hashes[i] + " " + captures[i].getFileName() + "\n";
                const auto familyHash = juce::SHA256 (manifest.toRawUTF8(), manifest.getNumBytesAsUTF8()).toHexString();
                const auto folder = library.getChildFile ("Captures")
                    .getChildFile (source.getParentDirectory().getFileName()).getChildFile (familyHash);
                for (const auto& capture : captures)
                {
                    const auto target = folder.getChildFile (capture.getFileName());
                    if (! copyLibraryFile (capture, target))
                        return false;
                    const auto metadata = capture.withFileExtension (".t3k.json");
                    if (metadata.existsAsFile() && ! copyLibraryFile (metadata, target.withFileExtension (".t3k.json")))
                        return false;
                }
                destination = folder.getChildFile (captures[hashes.indexOf (selectedHash)].getFileName());
            }
            else
            {
                const auto hash = juce::SHA256 (source).toHexString();
                const auto folder = library.getChildFile ("IRs").getChildFile (hash);
                auto name = source.getFileName();
                if (source.getParentDirectory().getFileName().endsWith ("_Assets")
                    && (name.startsWithIgnoreCase ("IR1_") || name.startsWithIgnoreCase ("IR2_")))
                    name = name.substring (4);
                destination = folder.getChildFile (name);
                if (! copyLibraryFile (source, destination))
                    return false;
            }
        }

        auto relativePath = destination.getRelativePathFrom (library);
       #if JUCE_WINDOWS
        relativePath = relativePath.replaceCharacter (juce::File::getSeparatorChar(), '/');
       #endif
        presetState.setProperty (property, "@library/" + relativePath, nullptr);
        if (captureSlot >= 0)
            presetState.setProperty ("ampDisplayName" + juce::String (captureSlot + 1),
                                     displayNameForNAMFile (destination, captureSlot), nullptr);
        return true;
    };

    for (int slot = 0; slot < numAmpSlots; ++slot)
        if (! storePath ("ampFile" + juce::String (slot + 1), slot))
            return false;
    if (! storePath ("reverbIRFile", -1) || ! storePath ("boostNAMFile", -1))
        return false;
    for (int slot = 0; slot < numIRSlots; ++slot)
        if (! storePath ("irFile" + juce::String (slot + 1), -1))
            return false;

    auto presetXml = presetState.createXml();
    if (presetXml == nullptr)
        return false;
    juce::MemoryBlock presetData;
    copyXmlToBinary (*presetXml, presetData);
    if (! file.replaceWithData (presetData.getData(), presetData.getSize()))
        return false;

    const auto resolveLibraryPath = [&library, &presetState] (const juce::String& property)
    {
        const auto path = presetState.getProperty (property).toString();
        if (! path.startsWith ("@library/"))
            return juce::String();
        return library.getChildFile (path.fromFirstOccurrenceOf ("/", false, false)).getFullPathName();
    };
    for (int slot = 0; slot < numAmpSlots; ++slot)
    {
        const auto path = resolveLibraryPath ("ampFile" + juce::String (slot + 1));
        if (path.isNotEmpty())
        {
            namFilePaths[slot] = path;
            namDisplayNames[slot] = presetState.getProperty (
                "ampDisplayName" + juce::String (slot + 1)).toString();
        }
    }
    for (int slot = 0; slot < numIRSlots; ++slot)
    {
        const auto path = resolveLibraryPath ("irFile" + juce::String (slot + 1));
        if (path.isNotEmpty())
            irFilePaths[slot] = path;
    }
    if (const auto path = resolveLibraryPath ("reverbIRFile"); path.isNotEmpty())
        reverbIRPath = path;
    const auto resolvedBoostPath = resolveLibraryPath ("boostNAMFile");
    if (resolvedBoostPath.isNotEmpty())
        boostNAMFilePath = resolvedBoostPath;
    return true;
}

bool NAMplifiedAudioProcessor::loadUserPreset (const juce::String& name)
{
    const auto file = getUserPresetFile (name);
    if (file == juce::File() || ! file.existsAsFile()) return false;
    juce::MemoryBlock state;
    if (! file.loadFileAsData (state)) return false;
    auto xml = getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    if (xml == nullptr)
        return false;
    auto presetState = juce::ValueTree::fromXml (*xml);
    const auto* masterParameter = parameters.getRawParameterValue ("masterVolume");
    const auto preservedMasterVolume = masterParameter != nullptr ? masterParameter->load() : 0.0f;
    const auto assetsFolder = file.getSiblingFile (file.getFileNameWithoutExtension() + "_Assets");
    const auto library = file.getSiblingFile ("Library");
    for (const auto& property : juce::StringArray { "ampFile1", "ampFile2", "boostNAMFile", "irFile1", "irFile2", "reverbIRFile" })
    {
        const auto storedPath = presetState.getProperty (property).toString();
        const auto shared = storedPath.startsWith ("@library/");
        if (! shared && ! storedPath.startsWith ("@preset-assets/"))
            continue;
        const auto assetFile = (shared ? library : assetsFolder).getChildFile (storedPath.fromFirstOccurrenceOf ("/", false, false));
        if (! assetFile.existsAsFile())
            return false;
        presetState.setProperty (property, assetFile.getFullPathName(), nullptr);
    }
    auto resolvedXml = presetState.createXml();
    if (resolvedXml == nullptr)
        return false;
    juce::MemoryBlock resolvedState;
    copyXmlToBinary (*resolvedXml, resolvedState);
    setStateInformation (resolvedState.getData(), static_cast<int> (resolvedState.getSize()));
    if (auto* parameter = parameters.getParameter ("masterVolume"))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (preservedMasterVolume));
    return true;
}

bool NAMplifiedAudioProcessor::deleteUserPreset (const juce::String& name)
{
    const auto file = getUserPresetFile (name);
    if (file == juce::File() || ! file.existsAsFile() || ! file.deleteFile())
        return false;
    const auto assetsFolder = file.getSiblingFile (file.getFileNameWithoutExtension() + "_Assets");
    if (assetsFolder.isDirectory())
        assetsFolder.deleteRecursively();
    return true;
}

juce::String NAMplifiedAudioProcessor::getSelectedUserPresetName() const
{
    return parameters.state.getProperty ("selectedUserPreset").toString();
}

void NAMplifiedAudioProcessor::setSelectedUserPresetName (const juce::String& name)
{
    parameters.state.setProperty ("selectedUserPreset", name, nullptr);
}

void NAMplifiedAudioProcessor::capturePresetSnapshot()
{
    getStateInformation (presetSnapshotState);
    presetPreFxOrder = getPreFxOrder();
    presetPostFxOrder = getPostFxOrder();
    presetAmpOrder = getAmpSlotOrder();
    presetIROrder = getIRSlotOrder();
    presetSnapshot.clear();
    presetFileSnapshot.clear();
    for (auto* parameter : getParameters())
    {
        const auto* parameterWithID = dynamic_cast<const juce::AudioProcessorParameterWithID*> (parameter);
        if (parameterWithID == nullptr)
            continue;
        if (parameterWithID->paramID == "masterVolume")
            continue;
        if (const auto* rawValue = parameters.getRawParameterValue (parameterWithID->paramID))
            presetSnapshot.emplace (parameterWithID->paramID.toStdString(), rawValue->load());
    }
    for (int slot = 0; slot < numAmpSlots; ++slot)
        presetFileSnapshot.emplace ("amp" + std::to_string (slot), namFilePaths[slot]);
    presetFileSnapshot.emplace ("reverbIR", reverbIRPath);
    presetFileSnapshot.emplace ("boostNAM", boostNAMFilePath);
    for (int slot = 0; slot < numIRSlots; ++slot)
        presetFileSnapshot.emplace ("ir" + std::to_string (slot), irFilePaths[slot]);
}

void NAMplifiedAudioProcessor::promptToSavePreset (std::function<void()> onSaved)
{
    if (auto* editor = dynamic_cast<NAMplifiedAudioProcessorEditor*> (getActiveEditor()))
        editor->showSavePresetPrompt (std::move (onSaved));
}

void NAMplifiedAudioProcessor::restorePresetSnapshot()
{
    if (presetSnapshotState.isEmpty())
        return;
    auto* master = parameters.getParameter ("masterVolume");
    const auto volume = master->getValue();
    setStateInformation (presetSnapshotState.getData(), static_cast<int> (presetSnapshotState.getSize()));
    master->setValueNotifyingHost (volume);
}

bool NAMplifiedAudioProcessor::isParameterModified (const juce::String& parameterId) const
{
    const auto baseline = presetSnapshot.find (parameterId.toStdString());
    const auto* current = parameters.getRawParameterValue (parameterId);
    return baseline != presetSnapshot.end() && current != nullptr
        && std::abs (current->load() - baseline->second) > 0.0001f;
}

bool NAMplifiedAudioProcessor::hasUnsavedParameterChanges() const
{
    if (! presetSnapshotState.isEmpty()
        && (getPreFxOrder() != presetPreFxOrder || getPostFxOrder() != presetPostFxOrder
            || getAmpSlotOrder() != presetAmpOrder || getIRSlotOrder() != presetIROrder))
        return true;
    for (const auto& [parameterId, baseline] : presetSnapshot)
        if (const auto* current = parameters.getRawParameterValue (parameterId); current != nullptr
            && std::abs (current->load() - baseline) > 0.0001f)
            return true;
    for (const auto& [assetId, baseline] : presetFileSnapshot)
    {
        juce::String current;
        if (assetId == "boostNAM")
            current = boostNAMFilePath;
        else if (assetId == "amp0") current = namFilePaths[0];
        else if (assetId == "amp1") current = namFilePaths[1];
        else if (assetId == "reverbIR") current = reverbIRPath;
        else if (assetId == "ir0") current = irFilePaths[0];
        else if (assetId == "ir1") current = irFilePaths[1];
        if (current != baseline)
            return true;
    }
    return false;
}

void NAMplifiedAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
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
        namModels[slot]->prepare (sampleRate, samplesPerBlock);
    boostNAMModel->prepare (sampleRate, samplesPerBlock);
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
    for (auto& slotFilters : ampToneEqFilters)
        for (auto& channelFilters : slotFilters)
            for (auto& filter : channelFilters)
                filter = {};
    for (auto& slotFilters : ampCrossoverFilters)
        for (auto& channelFilters : slotFilters)
            for (auto& filter : channelFilters)
                filter = {};
    for (auto& delayLine : delayLines)
    {
        delayLine.setMaximumDelayInSamples (static_cast<int> (sampleRate * 1.2) + 2);
        delayLine.prepare (spec);
        delayLine.reset();
    }
    analogDelayInput.fill (0.0f);
    for (auto& line : analogDelay)
        line.prepare (sampleRate);
    for (auto& predelayLine : reverbPreDelayLines)
    {
        predelayLine.setMaximumDelayInSamples (static_cast<int> (sampleRate * 0.15) + 2);
        predelayLine.prepare (spec);
        predelayLine.reset();
    }
    reverbWorkBuffer.setSize (2, samplesPerBlock);
    delayDuckEnvelope = 0.0f;
    chorus.prepare (spec);
    outputSafetyLimiter.prepare (spec);
    outputSafetyLimiter.setThreshold (0.0f);
    outputSafetyLimiter.setRelease (80.0f);
    outputSafetyLimiter.reset();
    {
        const juce::SpinLock::ScopedLockType lock (reverbConvolutionLock);
        reverbConvolver.prepare (spec);
    }
    plateReverb.prepare (sampleRate);
    roomReverb.prepare (sampleRate);
    cloudReverb.prepare (sampleRate);
}

void NAMplifiedAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool NAMplifiedAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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

bool NAMplifiedAudioProcessor::renderAudition (const juce::MemoryBlock& state,
                                               const juce::AudioBuffer<float>& input,
                                               double sampleRate, juce::AudioBuffer<float>& output,
                                               const std::function<bool()>& cancelled)
{
    constexpr int blockSize = 512;
    NAMplifiedAudioProcessor renderer;
    renderer.modelSampleRate.store (sampleRate);
    renderer.modelMaxBlockSize.store (blockSize);
    renderer.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    while (renderer.modelLoader.getNumJobs() > 0)
    {
        if (cancelled())
            return false;
        juce::Thread::sleep (10);
    }
    const auto enabled = [&renderer] (const juce::String& id)
    {
        return renderer.parameters.getRawParameterValue (id)->load() >= 0.5f;
    };
    for (int slot = 0; slot < numAmpSlots; ++slot)
        if (enabled ("amp" + juce::String (slot + 1) + "On")
            && renderer.namFilePaths[slot].isNotEmpty() && renderer.getNAMCaptureStatus (slot) != "Loaded")
            return false;
    if (enabled ("boostOn") && enabled ("boostSource") && renderer.boostNAMFilePath.isNotEmpty()
        && renderer.getNAMCaptureStatus (boostNAMSlot) != "Loaded")
        return false;
    renderer.prepareToPlay (sampleRate, blockSize);
    output.setSize (2, input.getNumSamples() + juce::roundToInt (sampleRate));
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> block (2, blockSize);
    for (int offset = 0; offset < output.getNumSamples(); offset += blockSize)
    {
        if (cancelled())
            return false;
        block.clear();
        const auto available = juce::jlimit (0, blockSize, input.getNumSamples() - offset);
        if (available > 0)
            for (int channel = 0; channel < 2; ++channel)
                block.copyFrom (channel, 0, input, juce::jmin (channel, input.getNumChannels() - 1), offset, available);
        renderer.processBlock (block, midi);
        for (int channel = 0; channel < 2; ++channel)
            output.copyFrom (channel, offset, block, channel, 0,
                             juce::jmin (blockSize, output.getNumSamples() - offset));
    }
    return true;
}

void NAMplifiedAudioProcessor::playAudition (juce::AudioBuffer<float> audio, double sampleRate)
{
    const juce::SpinLock::ScopedLockType lock (auditionLock);
    auditionAudio = std::move (audio);
    auditionRate = sampleRate;
    auditionPosition = 0;
}

void NAMplifiedAudioProcessor::stopAudition()
{
    playAudition ({}, 0.0);
}

bool NAMplifiedAudioProcessor::startDIRecording (double sampleRate)
{
    if (getTotalNumInputChannels() == 0 || sampleRate < 8000.0 || sampleRate > 384000.0
        || ! std::isfinite (sampleRate))
        return false;

    juce::AudioBuffer<float> storage (1, juce::roundToInt (20.0 * sampleRate));
    const juce::SpinLock::ScopedLockType lock (recordingLock);
    recordingAudio = std::move (storage);
    recordingPosition = 0;
    recordingRate = sampleRate;
    recordingActive.store (true);
    return true;
}

int NAMplifiedAudioProcessor::stopDIRecording (juce::AudioBuffer<float>& destination)
{
    const juce::SpinLock::ScopedLockType lock (recordingLock);
    recordingActive.store (false);
    destination = std::move (recordingAudio);
    const auto samples = recordingPosition;
    recordingPosition = 0;
    return samples;
}

void NAMplifiedAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    if (recordingActive.load())
    {
        const juce::SpinLock::ScopedTryLockType lock (recordingLock);
        if (lock.isLocked() && recordingActive.load())
        {
            if (recordingRate != modelSampleRate.load() || getTotalNumInputChannels() == 0
                || buffer.getNumChannels() == 0)
                recordingActive.store (false);
            else
            {
                const auto count = juce::jmin (buffer.getNumSamples(),
                                              recordingAudio.getNumSamples() - recordingPosition);
                recordingAudio.copyFrom (0, recordingPosition, buffer, 0, 0, count);
                recordingPosition += count;
                if (recordingPosition == recordingAudio.getNumSamples())
                    recordingActive.store (false);
            }
        }
    }

    {
        const juce::SpinLock::ScopedTryLockType lock (auditionLock);
        if (lock.isLocked() && auditionRate == modelSampleRate.load()
            && auditionPosition < auditionAudio.getNumSamples())
        {
            buffer.clear();
            const auto count = juce::jmin (buffer.getNumSamples(), auditionAudio.getNumSamples() - auditionPosition);
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                buffer.copyFrom (channel, 0, auditionAudio, juce::jmin (channel, 1), auditionPosition, count);
            auditionPosition += count;
            outputPeak.store (buffer.getNumChannels() > 0 ? buffer.getMagnitude (0, count) : 0.0f);
            return;
        }
    }
    for (const auto metadata : midiMessages)
    {
        const auto message = metadata.getMessage();
        if (! message.isController())
            continue;
        const auto cc = message.getControllerNumber();
        const auto target = midiLearnTargetIndex.exchange (-1, std::memory_order_acq_rel);
        if (target >= 0 && juce::isPositiveAndBelow (target, static_cast<int> (midiParameterIds.size())))
        {
            for (auto& mapping : midiCCParameterIndices)
                if (mapping.load (std::memory_order_relaxed) == target)
                    mapping.store (-1, std::memory_order_relaxed);
            midiCCParameterIndices[static_cast<size_t> (cc)].store (target, std::memory_order_relaxed);
            learnedMidiParameterIndex.store (target, std::memory_order_release);
            learnedMidiCC.store (cc, std::memory_order_release);
            midiLearningArmed.store (false, std::memory_order_release);
            continue;
        }
        const auto index = midiCCParameterIndices[static_cast<size_t> (cc)].load (std::memory_order_relaxed);
        if (juce::isPositiveAndBelow (index, static_cast<int> (midiParameterIds.size())))
            if (auto* parameter = parameters.getParameter (midiParameterIds[static_cast<size_t> (index)]))
                parameter->setValueNotifyingHost (static_cast<float> (message.getControllerValue()) / 127.0f);
    }

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
    if (inputBlockPeak >= 1.0f)
        inputClipped.store (true, std::memory_order_relaxed);
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
            if (get ("boostSource") >= 0.5f)
            {
                buffer.applyGain (juce::Decibels::decibelsToGain (get ("boostNAMIn")));
                boostNAMModel->process (buffer, numChannels, numSamples);
                buffer.applyGain (juce::Decibels::decibelsToGain (get ("boostNAMOut")));
                return;
            }
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
        namModels[slot]->process (target, numChannels, numSamples);
    };
    const auto processAmpToneEQ = [&] (juce::AudioBuffer<float>& target, int slot, bool pre)
    {
        const auto number = juce::String (slot + 1);
        const auto isPre = get (("amp" + number + "EqPosition").toRawUTF8(), 1.0f) < 0.5f;
        if (isPre != pre)
            return;

        constexpr float frequencies[] { 120.0f, 800.0f, 5000.0f };
        const char* suffixes[] { "EqBass", "EqMid", "EqTreble" };
        for (int band = 0; band < 3; ++band)
        {
            const auto gain = get (("amp" + number + suffixes[band]).toRawUTF8());
            for (int channel = 0; channel < numChannels; ++channel)
                setEqCoefficients (ampToneEqFilters[static_cast<size_t> (slot)][static_cast<size_t> (channel)]
                                                             [static_cast<size_t> (band)],
                                   band, frequencies[band], gain, sampleRate);
        }
        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto* samples = target.getWritePointer (channel);
            auto& filters = ampToneEqFilters[static_cast<size_t> (slot)][static_cast<size_t> (channel)];
            for (int sample = 0; sample < numSamples; ++sample)
                for (auto& filter : filters)
                    samples[sample] = filter.process (samples[sample]);
        }
    };
    const auto invertAmpPolarity = [&] (juce::AudioBuffer<float>& target, int slot)
    {
        const auto parameterId = "amp" + juce::String (slot + 1) + "Polarity";
        if (get (parameterId.toRawUTF8()) >= 0.5f)
            for (int channel = 0; channel < numChannels; ++channel)
                target.applyGain (channel, 0, numSamples, -1.0f);
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
                processAmpToneEQ (buffer, slot, true);
                processAmpSlot (buffer, slot);
                processAmpToneEQ (buffer, slot, false);
                invertAmpPolarity (buffer, slot);
                buffer.applyGain (juce::Decibels::decibelsToGain (get (ampOutputIds[slot])));
            }
        }
    }
    else
    {
        std::array<int, numAmpSlots> activeSlots {};
        int activeCount = 0;
        for (int slot = 0; slot < numAmpSlots; ++slot)
        {
            if (enabled (ampEnableIds[slot]))
                activeSlots[static_cast<size_t> (activeCount++)] = slot;
        }

        if (activeCount > 0)
        {
            const auto primaryWeight = juce::jlimit (0.0f, 1.0f, get ("ampParallelBlend", 0.5f));
            const auto crossoverEnabled = activeCount == 2 && enabled ("ampCrossoverOn");
            const auto crossoverFrequency = get ("ampCrossoverFreq", 250.0f);
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
                if (crossoverEnabled)
                {
                    const auto highPass = slot == 1;
                    for (int channel = 0; channel < numChannels; ++channel)
                    {
                        auto& filters = ampCrossoverFilters[static_cast<size_t> (slot)][static_cast<size_t> (channel)];
                        for (auto& filter : filters)
                            setCutFilterCoefficients (filter, highPass, crossoverFrequency, sampleRate);
                        auto* samples = branch.getWritePointer (channel);
                        for (int sample = 0; sample < numSamples; ++sample)
                            for (auto& filter : filters)
                                samples[sample] = filter.process (samples[sample]);
                    }
                }
                processAmpToneEQ (branch, slot, true);
                processAmpSlot (branch, slot);
                processAmpToneEQ (branch, slot, false);
                invertAmpPolarity (branch, slot);
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
            const auto highPass = get ("delayHighPass", 100.0f);
            const auto lowPass = juce::jmax (get ("delayLowPass", 8000.0f), highPass * 1.1f);
            for (int channel = 0; channel < 2; ++channel)
            {
                setCutFilterCoefficients (delayHighPassFilters[static_cast<size_t> (channel)], true,
                                          highPass, sampleRate);
                setCutFilterCoefficients (delayLowPassFilters[static_cast<size_t> (channel)], false,
                                          lowPass, sampleRate);
            }
            if (analog)
            {
                for (auto& line : analogDelay)
                {
                    line.setFilterFreq (tone);
                    line.setDelayTime (delaySamples / static_cast<float> (sampleRate));
                }
            }

            const auto readRepeat = [&] (size_t channel)
            {
                const auto sample = analog ? analogDelay[channel].process (analogDelayInput[channel]) * 0.65f
                                           : delayLines[channel].popSample (0, delaySamples);
                return delayLowPassFilters[channel].process (delayHighPassFilters[channel].process (sample));
            };
            const auto writeRepeat = [&] (size_t channel, float sample)
            {
                if (analog)
                    analogDelayInput[channel] = sample;
                else
                    delayLines[channel].pushSample (0, sample);
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
                    const auto wet = readRepeat (0);
                    writeRepeat (0, monoInput + wet * feedback);
                    const auto outputWet = wet * duckGain * mix;
                    left[i] = dryLeft * (1.0f - mix) + outputWet;
                    if (numChannels > 1)
                        right[i] = dryRight * (1.0f - mix) + outputWet;
                }
                else if (mode == 2) // Mono input alternates between the left and right delay lines.
                {
                    const auto wetLeft = readRepeat (0);
                    const auto wetRight = readRepeat (1);
                    writeRepeat (0, monoInput + wetRight * feedback);
                    writeRepeat (1, wetLeft * feedback);
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
                    const auto wetLeft = readRepeat (0);
                    writeRepeat (0, dryLeft + wetLeft * feedback);
                    left[i] = dryLeft * (1.0f - mix) + wetLeft * duckGain * mix;
                    if (numChannels > 1)
                    {
                        const auto wetRight = readRepeat (1);
                        writeRepeat (1, dryRight + wetRight * feedback);
                        right[i] = dryRight * (1.0f - mix) + wetRight * duckGain * mix;
                    }
                }
            }
        }
        else if (stage == 1 && enabled ("reverbOn"))
        {
            const auto mode = juce::jlimit (0, 3, juce::roundToInt (get ("reverbMode", 0.0f)));
            const juce::SpinLock::ScopedTryLockType irLock (reverbConvolutionLock);
            if (mode == 3 && (! irLock.isLocked() || ! reverbIRLoaded.load()))
                return;
            const auto size = juce::jlimit (0.0f, 1.0f, get ("reverbSize", 0.45f));
            const auto damping = juce::jlimit (0.0f, 1.0f, get ("reverbDamping", 0.45f));
            const auto width = juce::jlimit (0.0f, 1.0f, get ("reverbWidth", 0.8f));
            const auto highPass = get ("reverbHighPass", 120.0f);
            const auto lowPassSetting = get ("reverbLowPass", 10000.0f);
            const auto plateDampingCutoff = 20000.0f * std::pow (0.1f, damping);
            const auto lowPass = juce::jmax (mode == 1 ? juce::jmin (lowPassSetting, plateDampingCutoff) : lowPassSetting,
                                            highPass * 1.1f);
            for (int channel = 0; channel < numChannels; ++channel)
            {
                setCutFilterCoefficients (reverbHighPassFilters[static_cast<size_t> (channel)], true,
                                          highPass, sampleRate);
                setCutFilterCoefficients (reverbLowPassFilters[static_cast<size_t> (channel)], false,
                                          lowPass, sampleRate);
            }
            const auto userPredelayMs = juce::jlimit (0.0f, 100.0f, get ("reverbPreDelay", 0.0f));
            float modePredelayMs = userPredelayMs;
            if (mode == 1)
                modePredelayMs += 12.0f;
            else if (mode == 2)
                modePredelayMs += 35.0f;
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
            if (mode == 3)
            {
                auto wet = juce::dsp::AudioBlock<float> (reverbWorkBuffer)
                    .getSubsetChannelBlock (0, static_cast<size_t> (numChannels))
                    .getSubBlock (0, static_cast<size_t> (numSamples));
                reverbConvolver.process (juce::dsp::ProcessContextReplacing<float> (wet));
            }
            else
            {
                if (numChannels == 1)
                    reverbWorkBuffer.copyFrom (1, 0, reverbWorkBuffer, 0, 0, numSamples);
                auto* left = reverbWorkBuffer.getWritePointer (0);
                auto* right = reverbWorkBuffer.getWritePointer (1);
                if (mode == 0)
                    roomReverb.process (left, right, numSamples, size, damping, width,
                                        get ("roomSize", 1.0f), get ("roomEarlyMix", 0.35f),
                                        get ("roomSpin", 0.5f), get ("roomWander", 0.5f));
                else if (mode == 1)
                    plateReverb.process (left, right, numSamples, size);
                else
                    cloudReverb.process (left, right, numSamples, size, damping);
                for (int i = 0; i < numSamples; ++i)
                {
                    const auto mid = (left[i] + right[i]) * 0.5f;
                    const auto side = numChannels == 2 ? (left[i] - right[i]) * (0.5f * (mode == 0 ? 1.0f : width)) : 0.0f;
                    left[i] = mid + side;
                    right[i] = mid - side;
                }
            }
            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto* wet = reverbWorkBuffer.getWritePointer (channel);
                auto& highPassFilter = reverbHighPassFilters[static_cast<size_t> (channel)];
                auto& lowPassFilter = reverbLowPassFilters[static_cast<size_t> (channel)];
                for (int sample = 0; sample < numSamples; ++sample)
                    wet[sample] = lowPassFilter.process (highPassFilter.process (wet[sample]));
            }
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
    buffer.applyGain (juce::Decibels::decibelsToGain (get ("masterVolume")));
    auto outputBlock = juce::dsp::AudioBlock<float> (buffer).getSubBlock (0, static_cast<size_t> (numSamples));
    outputSafetyLimiter.process (juce::dsp::ProcessContextReplacing<float> (outputBlock));
    float outputBlockPeak = 0.0f;
    for (int channel = 0; channel < numChannels; ++channel)
        outputBlockPeak = juce::jmax (outputBlockPeak, buffer.getMagnitude (channel, 0, numSamples));
    if (outputBlockPeak >= 1.0f)
        outputClipped.store (true, std::memory_order_relaxed);
    outputPeak.store (juce::jmax (outputBlockPeak, outputPeak.load (std::memory_order_relaxed) * meterFall),
                      std::memory_order_relaxed);
}

bool NAMplifiedAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* NAMplifiedAudioProcessor::createEditor() { return new NAMplifiedAudioProcessorEditor (*this); }

void NAMplifiedAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    for (int slot = 0; slot < numIRSlots; ++slot)
        state.setProperty ("irFile" + juce::String (slot + 1), irFilePaths[slot], nullptr);
    for (int slot = 0; slot < numAmpSlots; ++slot)
        state.setProperty ("ampFile" + juce::String (slot + 1), namFilePaths[slot], nullptr);
    for (int slot = 0; slot < numAmpSlots; ++slot)
        state.setProperty ("ampDisplayName" + juce::String (slot + 1), namDisplayNames[slot], nullptr);
    state.setProperty ("reverbIRFile", reverbIRPath, nullptr);
    state.setProperty ("boostNAMFile", boostNAMFilePath, nullptr);
    state.setProperty ("boostNAMDisplayName", boostNAMDisplayName, nullptr);
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

void NAMplifiedAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (! xml->hasTagName (parameters.state.getType()))
            return;
        auto state = juce::ValueTree::fromXml (*xml);
        parameters.replaceState (state);
        reverbIRLoaded.store (false);
        reverbIRPath.clear();
        const auto irPath = state.getProperty ("reverbIRFile").toString();
        if (irPath.isNotEmpty())
            loadReverbIR (juce::File (irPath));
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
            juce::File file (namFilePaths[slot]);
            file = findNamedNAMPeer (file, slot);
            namFilePaths[slot] = file.getFullPathName();
            if (file.existsAsFile())
                loadNAMCapture (slot, file);
            auto displayName = state.getProperty ("ampDisplayName" + juce::String (slot + 1),
                                                  file.getFileName()).toString().trim();
            const auto slotPrefix = "NAM" + juce::String (slot + 1) + "_";
            const auto isGenericName = [&displayName, slot]
            {
                return displayName.isEmpty() || displayName.equalsIgnoreCase ("NAM capture")
                    || displayName.equalsIgnoreCase ("NAM" + juce::String (slot + 1) + ".nam")
                    || displayName.equalsIgnoreCase ("NAM" + juce::String (slot + 1) + "_capture.nam");
            };
            if ((isGenericName() || displayName.equalsIgnoreCase (file.getFileName()))
                && file.getFileName().startsWithIgnoreCase (slotPrefix))
                displayName = displayNameForNAMFile (file, slot);
            namDisplayNames[slot] = displayName;
        }
        boostNAMFilePath = state.getProperty ("boostNAMFile").toString();
        const juce::File boostNAMFile (boostNAMFilePath);
        if (boostNAMFile.existsAsFile())
            loadNAMCapture (boostNAMSlot, boostNAMFile);
        boostNAMDisplayName = state.getProperty ("boostNAMDisplayName", boostNAMFile.getFileName()).toString();
    }
}

bool NAMplifiedAudioProcessor::loadReverbIR (const juce::File& file)
{
    juce::WavAudioFormat wav;
    auto stream = file.createInputStream();
    if (stream == nullptr)
        return false;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (stream.release(), true));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0
        || reader->numChannels == 0 || reader->numChannels > 2)
        return false;

    const auto length = static_cast<int> (juce::jmin (reader->lengthInSamples,
        static_cast<juce::int64> (reader->sampleRate * 10.0)));
    juce::AudioBuffer<float> impulse (static_cast<int> (reader->numChannels), length);
    if (! reader->read (&impulse, 0, length, 0, true, true))
        return false;
    const juce::SpinLock::ScopedLockType lock (reverbConvolutionLock);
    reverbConvolver.loadImpulseResponse (std::move (impulse), reader->sampleRate,
        juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::no,
        juce::dsp::Convolution::Normalise::yes);
    reverbIRPath = file.getFullPathName();
    reverbIRLoaded.store (true);
    return true;
}

juce::String NAMplifiedAudioProcessor::getReverbIRName() const
{
    return reverbIRPath.isEmpty() ? juce::String ("No IR loaded")
                                  : juce::File (reverbIRPath).getFileName();
}

void NAMplifiedAudioProcessor::loadImpulseResponse (int slot, const juce::File& file)
{
    if (slot < 0 || slot >= numIRSlots || ! file.existsAsFile())
        return;
    irConvolvers[slot].loadImpulseResponse (file, juce::dsp::Convolution::Stereo::yes,
                                            juce::dsp::Convolution::Trim::yes, 0,
                                            juce::dsp::Convolution::Normalise::yes);
    irFilePaths[slot] = file.getFullPathName();
    irLoaded[slot].store (true);
}

juce::String NAMplifiedAudioProcessor::getImpulseResponseName (int slot) const
{
    if (slot < 0 || slot >= numIRSlots || irFilePaths[slot].isEmpty())
        return "No IR loaded";
    return juce::File (irFilePaths[slot]).getFileName();
}

bool NAMplifiedAudioProcessor::isImpulseResponseActive (int slot) const
{
    if (slot < 0 || slot >= numIRSlots || ! irLoaded[slot].load (std::memory_order_relaxed))
        return false;

    const auto parameterId = "ir" + juce::String (slot + 1) + "On";
    const auto* enabledParameter = parameters.getRawParameterValue (parameterId);
    return enabledParameter != nullptr && enabledParameter->load (std::memory_order_relaxed) >= 0.5f;
}

void NAMplifiedAudioProcessor::loadNAMCapture (int slot, const juce::File& file)
{
    if (slot == boostNAMSlot)
    {
        if (! file.existsAsFile())
            return;
        boostNAMFilePath = file.getFullPathName();
        boostNAMDisplayName = file.getFileName();
        boostNAMModel->setStatus ("Loading NAM capture...");
        modelLoader.addJob ([this, file]
        {
            juce::String error;
            boostNAMModel->create (file, modelSampleRate.load(), modelMaxBlockSize.load(), error);
        });
        return;
    }
    if (slot < 0 || slot >= numAmpSlots || ! file.existsAsFile())
        return;
    namFilePaths[slot] = file.getFullPathName();
    namDisplayNames[slot] = file.getFileName();
    namModels[slot]->setStatus ("Loading NAM capture...");
    modelLoader.addJob ([this, slot, file]
    {
        juce::String error;
        namModels[slot]->create (file, modelSampleRate.load(), modelMaxBlockSize.load(), error);
    });
}

bool NAMplifiedAudioProcessor::cycleNAMCapture (int slot, int direction)
{
    const auto currentPath = slot == boostNAMSlot ? boostNAMFilePath
        : (slot >= 0 && slot < numAmpSlots ? namFilePaths[slot] : juce::String());
    const juce::File currentFile (currentPath);
    if (! currentFile.existsAsFile() || direction == 0)
        return false;
    auto captures = currentFile.getParentDirectory().findChildFiles (juce::File::findFiles, false, "*.nam");
    captures.sort();
    if (captures.size() < 2)
        return false;
    auto currentIndex = captures.indexOf (currentFile);
    if (currentIndex < 0)
        return false;
    const auto step = direction > 0 ? 1 : -1;
    currentIndex = (currentIndex + step + captures.size()) % captures.size();
    loadNAMCapture (slot, captures.getReference (currentIndex));
    return true;
}

juce::String NAMplifiedAudioProcessor::getNAMCaptureName (int slot) const
{
    if (slot == boostNAMSlot)
    {
        if (boostNAMFilePath.isEmpty())
            return "No NAM capture loaded";
        const auto name = boostNAMDisplayName.isNotEmpty() ? boostNAMDisplayName
                                                            : juce::File (boostNAMFilePath).getFileName();
        return name.equalsIgnoreCase ("BOOST_NAM.nam") ? "NAM capture" : name;
    }
    if (slot < 0 || slot >= numAmpSlots || namFilePaths[slot].isEmpty())
        return "No NAM capture loaded";
    const auto name = namDisplayNames[slot].isNotEmpty() ? namDisplayNames[slot]
                                                          : juce::File (namFilePaths[slot]).getFileName();
    if (name.equalsIgnoreCase ("NAM1.nam") || name.equalsIgnoreCase ("NAM2.nam")
        || name.equalsIgnoreCase ("NAM1_capture.nam") || name.equalsIgnoreCase ("NAM2_capture.nam"))
        return "NAM capture";
    return name;
}

juce::File NAMplifiedAudioProcessor::getNAMCaptureFile (int slot) const
{
    if (slot == boostNAMSlot)
        return juce::File (boostNAMFilePath);
    if (slot < 0 || slot >= numAmpSlots)
        return {};
    return juce::File (namFilePaths[slot]);
}

juce::String NAMplifiedAudioProcessor::getNAMCaptureStatus (int slot) const
{
    if (slot == boostNAMSlot)
        return boostNAMModel->getStatus();
    if (slot < 0 || slot >= numAmpSlots)
        return "Invalid slot";
    return namModels[slot]->getStatus();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NAMplifiedAudioProcessor();
}
