/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <unordered_map>
#include <vector>
#include "CleanBoost.h"

//==============================================================================
/**
*/
class TheLeadAudioProcessor  : public juce::AudioProcessor
{
public:
    using IRParameters = juce::AudioProcessorValueTreeState;
    static constexpr int numIRSlots = 2;
    static constexpr int numAmpSlots = 2;
    static constexpr int numPreFxStages = 4;
    static constexpr int numPostFxStages = 4;

    //==============================================================================
    TheLeadAudioProcessor();
    ~TheLeadAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    float getInputPeak() const noexcept { return inputPeak.load (std::memory_order_relaxed); }
    float getOutputPeak() const noexcept { return outputPeak.load (std::memory_order_relaxed); }
    IRParameters parameters;
    void loadImpulseResponse (int slot, const juce::File& file);
    void loadNAMCapture (int slot, const juce::File& file);
    juce::String getImpulseResponseName (int slot) const;
    juce::String getNAMCaptureName (int slot) const;
    juce::String getNAMCaptureStatus (int slot) const;
    std::array<int, numPreFxStages> getPreFxOrder() const;
    void setPreFxOrder (const std::array<int, numPreFxStages>& order);
    std::array<int, numAmpSlots> getAmpSlotOrder() const;
    void setAmpSlotOrder (const std::array<int, numAmpSlots>& order);
    std::array<int, numIRSlots> getIRSlotOrder() const;
    void setIRSlotOrder (const std::array<int, numIRSlots>& order);
    std::array<int, numPostFxStages> getPostFxOrder() const;
    void setPostFxOrder (const std::array<int, numPostFxStages>& order);
    void resetToDefaults();
    void capturePresetSnapshot();
    bool hasUnsavedParameterChanges() const;
    bool isParameterModified (const juce::String& parameterId) const;
    juce::StringArray getUserPresetNames() const;
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::String& name);
    bool deleteUserPreset (const juce::String& name);

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    static IRParameters::ParameterLayout createParameterLayout();
    juce::File getUserPresetDirectory() const;
    juce::File getUserPresetFile (const juce::String& name) const;
    struct BiquadState
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;
        void set (float nb0, float nb1, float nb2, float na0, float na1, float na2);
        float process (float input);
    };
    void processEqualizer (juce::AudioBuffer<float>& buffer, int channels, int samples,
                           const juce::String& prefix, bool pre);
    void processIRShapeEqualizer (juce::AudioBuffer<float>& buffer, int channels, int samples, int slot);
    void setEqCoefficients (BiquadState& filter, int band, float frequency, float gainDb, double sampleRate);
    struct AmpModel;
    std::unique_ptr<AmpModel> ampModels[numAmpSlots];
    juce::ThreadPool modelLoader { 1 };
    juce::dsp::Convolution irConvolvers[numIRSlots];
    juce::AudioBuffer<float> irWorkBuffers[numIRSlots];
    juce::AudioBuffer<float> irInputBuffer;
    juce::AudioBuffer<float> ampInputBuffer;
    juce::AudioBuffer<float> ampMixBuffer;
    juce::AudioBuffer<float> ampWorkBuffers[numAmpSlots];
    juce::String irFilePaths[numIRSlots];
    juce::String namFilePaths[numAmpSlots];
    std::atomic<bool> irLoaded[numIRSlots] {};
    std::atomic<double> modelSampleRate { 48000.0 };
    std::atomic<int> modelMaxBlockSize { 512 };
    std::array<std::atomic<int>, numPreFxStages> preFxOrder { 0, 1, 2, 3 };
    std::array<std::atomic<int>, numAmpSlots> ampSlotOrder { 0, 1 };
    std::array<std::atomic<int>, numIRSlots> irSlotOrder { 0, 1 };
    std::array<std::atomic<int>, numPostFxStages> postFxOrder { 0, 1, 2, 3 };
    std::unordered_map<std::string, float> presetSnapshot;
    std::array<float, 2> gateEnvelope {};
    std::array<float, 2> gateGain { 1.0f, 1.0f };
    std::array<float, 2> compressorEnvelope {};
    std::array<float, 2> analogDelayToneState {};
    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f };
    std::array<std::vector<float>, 2> pitchBuffers;
    int pitchBufferSize = 0, pitchWritePosition = 0;
    float pitchPhase = 0.0f, pitchCurrentSemitones = 0.0f;
    std::array<std::array<BiquadState, 5>, 2> preEqFilters {};
    std::array<std::array<BiquadState, 5>, 2> postEqFilters {};
    std::array<std::array<std::array<BiquadState, 3>, 2>, numIRSlots> irShapeEqFilters {};
    std::array<std::array<BiquadState, 3>, 2> boostEqFilters {};
    std::array<WdfBoostCircuit, 2> boostCircuits {};
    std::array<TubeScreamerCircuit, 2> tubeScreamerCircuits {};
    std::array<BiquadState, 2> compressorToneFilters {};
    std::array<BiquadState, 2> compressorLowEqFilters {};
    std::array<BiquadState, 2> compressorBrightFilters {};
    std::array<float, 2> compressorBrightMix {};
    float delayDuckEnvelope = 0.0f;
    std::array<float, 2> preLowState {};
    std::array<float, 2> preHighState {};
    std::array<float, 2> postLowState {};
    std::array<float, 2> postHighState {};
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLines[2];
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> reverbPreDelayLines[2];
    juce::AudioBuffer<float> reverbWorkBuffer;
    juce::dsp::Chorus<float> chorus;
    juce::dsp::Limiter<float> outputSafetyLimiter;
    juce::Reverb reverb;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TheLeadAudioProcessor)
};
