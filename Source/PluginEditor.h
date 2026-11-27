#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class ChainControls;
class PresetBuilderDialog;
class CaptureCompareDialog;

juce::FontOptions namplifiedFontOptions (float height, int style = juce::Font::plain);

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
    void updateMasterVolumeValueLabel();
    void setEqFullscreen (bool shouldBeFullscreen);
    void showDeletePresetPrompt();
    void showAboutDialog();
    void showPresetBuilder();
    void showCaptureCompare();
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
    juce::Label presetLabel;
    juce::ComboBox presetBox;
    juce::StringArray presetMenuKeys;
    int adjacentPresetId (int direction) const;
    juce::DrawableButton previousPresetButton { "Previous preset", juce::DrawableButton::ImageFitted };
    juce::DrawableButton nextPresetButton { "Next preset", juce::DrawableButton::ImageFitted };
    juce::DrawableButton savePresetButton { "Save preset", juce::DrawableButton::ImageFitted };
    juce::DrawableButton deletePresetButton { "Delete preset", juce::DrawableButton::ImageFitted };
    juce::DrawableButton resetChainButton { "New chain", juce::DrawableButton::ImageFitted };
    juce::DrawableButton captureCompareButton { "Amp & IR Comparison", juce::DrawableButton::ImageFitted };
    juce::DrawableButton presetBuilderButton { "Build preset", juce::DrawableButton::ImageFitted };
    std::unique_ptr<PresetBuilderDialog> presetBuilder;
    std::unique_ptr<CaptureCompareDialog> captureCompare;
    std::unique_ptr<juce::Component> savePresetDialog, aboutDialog;
    juce::DrawableButton headerMenuButton { "Menu", juce::DrawableButton::ImageFitted };
    juce::Label pitchLabel, pitchValueLabel, masterVolumeLabel, masterVolumeValueLabel;
    juce::Slider pitchSlider;
    juce::Slider masterVolumeSlider;
    juce::ComboBox outputModeBox;
    juce::Label blendLabel;
    juce::Label blendValueLabel;
    juce::Slider blendSlider;
    juce::DrawableButton seriesRoutingButton { "Series", juce::DrawableButton::ImageOnButtonBackground };
    juce::DrawableButton parallelRoutingButton { "Parallel Blend", juce::DrawableButton::ImageOnButtonBackground };
    juce::ComboBox parallelIRBox, parallelOutputBox;
    juce::TextButton ampCrossoverButton { "FREQ SPLIT" };
    juce::Slider ampCrossoverSlider;
    juce::Label ampCrossoverValueLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> parallelIRAttachment, parallelOutputAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> ampCrossoverAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ampCrossoverSliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pitchAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterVolumeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> outputModeAttachment;
    std::unique_ptr<ChainControls> chainControls;
    bool presetDirty = false;
    bool eqFullscreen = false;
    int lastBlendDisplayState = -1;
    bool midiLearnArmed = false;
    bool midiTargetArmed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NAMplifiedAudioProcessorEditor)
};
