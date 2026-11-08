#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class ChainControls;

class NAMplifiedAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer,
                                    private juce::ComboBox::Listener
{
public:
    explicit NAMplifiedAudioProcessorEditor (NAMplifiedAudioProcessor&);
    ~NAMplifiedAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void refreshUserPresets (const juce::String& selectName = {});
    void updateRoutingDisplay();
    void updateBlendValue();
    void updateMasterVolumeValueLabel();
    void setEqFullscreen (bool shouldBeFullscreen);
    void showSavePresetPrompt();
    void showDeletePresetPrompt();
    void showAboutDialog();
    void timerCallback() override;
    void comboBoxChanged (juce::ComboBox*) override;
    void mouseDown (const juce::MouseEvent&) override;
    juce::String selectedPresetName() const;

    NAMplifiedAudioProcessor& audioProcessor;
    juce::TooltipWindow tooltipWindow;
    std::unique_ptr<juce::LookAndFeel_V4> editorLookAndFeel;
    juce::ImageComponent brandIcon;
    juce::Label title;
    juce::Label subtitle;
    juce::Label presetLabel;
    juce::ComboBox presetBox;
    juce::TextButton savePresetButton { "Save" };
    juce::TextButton deletePresetButton { "Delete" };
    juce::TextButton resetChainButton { "Reset Chain" };
    juce::TextButton aboutButton { "About" };
    juce::TextButton midiLearnButton { "MIDI LEARN" };
    juce::TextButton audioSettingsButton { "AUDIO" };
    juce::Label pitchLabel, pitchValueLabel, masterVolumeLabel, masterVolumeValueLabel;
    juce::Slider pitchSlider;
    juce::Slider masterVolumeSlider;
    juce::Label blendLabel;
    juce::Label blendValueLabel;
    juce::Slider blendSlider;
    juce::ComboBox ampRoutingBox;
    juce::ComboBox parallelIRBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> ampRoutingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> parallelIRAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> blendAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pitchAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterVolumeAttachment;
    std::unique_ptr<ChainControls> chainControls;
    bool presetDirty = false;
    bool eqFullscreen = false;
    int lastBlendDisplayState = -1;
    bool midiLearnArmed = false;
    bool midiTargetArmed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NAMplifiedAudioProcessorEditor)
};
