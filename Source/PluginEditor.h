#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class ChainControls;

class TheLeadAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
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
    void setEqFullscreen (bool shouldBeFullscreen);
    void showSavePresetPrompt();
    void showDeletePresetPrompt();
    void timerCallback() override;
    juce::String selectedPresetName() const;

    TheLeadAudioProcessor& audioProcessor;
    juce::TooltipWindow tooltipWindow;
    std::unique_ptr<juce::LookAndFeel_V4> editorLookAndFeel;
    juce::Label title;
    juce::Label subtitle;
    juce::Label presetLabel;
    juce::ComboBox presetBox;
    juce::TextButton savePresetButton { "Save" };
    juce::TextButton deletePresetButton { "Delete" };
    juce::TextButton resetChainButton { "Reset Chain" };
    juce::Label pitchLabel, pitchValueLabel;
    juce::Slider pitchSlider;
    juce::Label blendLabel;
    juce::Label blendValueLabel;
    juce::Slider blendSlider;
    juce::ComboBox ampRoutingBox;
    juce::ComboBox parallelIRBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> ampRoutingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> parallelIRAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> blendAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pitchAttachment;
    std::unique_ptr<ChainControls> chainControls;
    bool presetDirty = false;
    bool eqFullscreen = false;
    int lastBlendDisplayState = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TheLeadAudioProcessorEditor)
};
