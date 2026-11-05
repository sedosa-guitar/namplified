#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class ChainControls;

class TheLeadAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit TheLeadAudioProcessorEditor (TheLeadAudioProcessor&);
    ~TheLeadAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void refreshUserPresets (const juce::String& selectName = {});
    void updateRoutingDisplay();
    void updateBlendValue();
    void showSavePresetPrompt();
    void showDeletePresetPrompt();

    TheLeadAudioProcessor& audioProcessor;
    std::unique_ptr<juce::LookAndFeel_V4> editorLookAndFeel;
    juce::Label title;
    juce::Label presetLabel;
    juce::ComboBox presetBox;
    juce::TextButton savePresetButton { "Save" };
    juce::TextButton deletePresetButton { "Delete" };
    juce::TextButton resetChainButton { "Reset Chain" };
    juce::Label blendLabel;
    juce::Label blendValueLabel;
    juce::Slider blendSlider;
    juce::ComboBox ampRoutingBox;
    juce::ComboBox parallelIRBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> ampRoutingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> parallelIRAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> blendAttachment;
    std::unique_ptr<ChainControls> chainControls;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TheLeadAudioProcessorEditor)
};
