#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class ChainControls;
class PresetBuilderDialog;

class NAMplifiedAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer,
                                    private juce::ComboBox::Listener
{
public:
    explicit NAMplifiedAudioProcessorEditor (NAMplifiedAudioProcessor&);
    ~NAMplifiedAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void showSavePresetPrompt (std::function<void()> onSaved = {});

private:
    void refreshUserPresets (const juce::String& selectName = {});
    void updateRoutingDisplay();
    void updateBlendValue();
    void updateMasterVolumeValueLabel();
    void setEqFullscreen (bool shouldBeFullscreen);
    void showDeletePresetPrompt();
    void showAboutDialog();
    void showPresetBuilder();
    void showHeaderMenu();
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
    juce::DrawableButton savePresetButton { "Save preset", juce::DrawableButton::ImageFitted };
    juce::DrawableButton deletePresetButton { "Delete preset", juce::DrawableButton::ImageFitted };
    juce::DrawableButton resetChainButton { "New chain", juce::DrawableButton::ImageFitted };
    juce::DrawableButton presetBuilderButton { "Build preset", juce::DrawableButton::ImageFitted };
    std::unique_ptr<PresetBuilderDialog> presetBuilder;
    std::unique_ptr<juce::Component> aboutDialog;
    juce::DrawableButton headerMenuButton { "Menu", juce::DrawableButton::ImageFitted };
    juce::Label pitchLabel, pitchValueLabel, masterVolumeLabel, masterVolumeValueLabel;
    juce::Slider pitchSlider;
    juce::Slider masterVolumeSlider;
    juce::Label blendLabel;
    juce::Label blendValueLabel;
    juce::Slider blendSlider;
    juce::DrawableButton seriesRoutingButton { "Series", juce::DrawableButton::ImageOnButtonBackground };
    juce::DrawableButton parallelRoutingButton { "Parallel Blend", juce::DrawableButton::ImageOnButtonBackground };
    juce::ComboBox parallelIRBox;
    juce::TextButton ampCrossoverButton { "FREQ SPLIT" };
    juce::Slider ampCrossoverSlider;
    juce::Label ampCrossoverValueLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> parallelIRAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> ampCrossoverAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ampCrossoverSliderAttachment;
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
