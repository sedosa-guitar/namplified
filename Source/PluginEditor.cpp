#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Tone3000Client.h"
#include <BinaryData.h>
#include <unordered_set>
#include <limits>
#include <array>
#include <cmath>

const auto background = juce::Colour::fromRGB (10, 12, 18);
const auto panel = juce::Colour::fromRGB (22, 25, 32);
const auto chainSurface = juce::Colour::fromRGB (15, 18, 24);
const auto tileSurface = juce::Colour::fromRGB (26, 30, 38);
const auto detailSurface = juce::Colour::fromRGB (13, 16, 22);
const auto accent = juce::Colour::fromRGB (238, 119, 57);
const auto selectionAccent = juce::Colour (0xff5ca9ff);
const auto knobAccent = juce::Colour (0xff70d9b6);
const auto text = juce::Colour::fromRGB (235, 236, 239);
const auto secondaryText = juce::Colour::fromRGB (155, 160, 170);

namespace
{
juce::PropertiesFile& namplifiedSettings()
{
    static const auto options = []
    {
        juce::PropertiesFile::Options result;
        result.applicationName = "NAMplified";
        result.filenameSuffix = "settings";
        result.folderName = "NAMplified";
        result.osxLibrarySubFolder = "Application Support";
        return result;
    }();
    static juce::PropertiesFile settings (options);
    static const bool migratedLegacySettings = []
    {
        auto legacyOptions = options;
        legacyOptions.applicationName = "NAMinator";
        legacyOptions.folderName = "NAMinator";
        juce::PropertiesFile legacySettings (legacyOptions);
        const auto lastDirectory = legacySettings.getValue ("lastNAMDirectory");
        if (settings.getValue ("lastNAMDirectory").isEmpty() && lastDirectory.isNotEmpty())
        {
            settings.setValue ("lastNAMDirectory", lastDirectory);
            settings.saveIfNeeded();
        }
        return true;
    }();
    juce::ignoreUnused (migratedLegacySettings);
    return settings;
}

juce::File lastNAMDirectory()
{
    const juce::File directory (namplifiedSettings().getValue ("lastNAMDirectory"));
    return directory.isDirectory() ? directory : juce::File();
}

void rememberNAMDirectory (const juce::File& directory)
{
    if (! directory.isDirectory())
        return;
    auto& settings = namplifiedSettings();
    settings.setValue ("lastNAMDirectory", directory.getFullPathName());
    settings.saveIfNeeded();
}

juce::String tone3000AttributionFor (const juce::File& capture)
{
    if (! capture.existsAsFile())
        return {};
    const auto sidecar = capture.getSiblingFile (capture.getFileNameWithoutExtension() + ".t3k.json");
    if (! sidecar.existsAsFile())
        return {};
    const auto metadata = juce::JSON::parse (sidecar.loadFileAsString());
    if (metadata.isVoid() || metadata.getProperty ("source", {}).toString() != "TONE3000")
        return {};
    juce::String attribution ("POWERED BY TONE3000");
    const auto tone = metadata.getProperty ("tone", {}).toString();
    const auto model = metadata.getProperty ("model", {}).toString();
    const auto creator = metadata.getProperty ("creator", {}).toString();
    const auto license = metadata.getProperty ("license", {}).toString();
    if (tone.isNotEmpty()) attribution += " | " + tone;
    if (model.isNotEmpty()) attribution += " | " + model;
    if (creator.isNotEmpty()) attribution += " | " + creator;
    if (license.isNotEmpty()) attribution += " | " + license;
    return attribution;
}
}

static juce::Typeface::Ptr getNAMplifiedTypeface()
{
    static const auto typeface = juce::Typeface::createSystemTypefaceFor (
        BinaryData::InterVariable_ttf, static_cast<size_t> (BinaryData::InterVariable_ttfSize));
    return typeface;
}

static juce::Typeface::Ptr getBrandTypeface()
{
    static const auto typeface = juce::Typeface::createSystemTypefaceFor (
        BinaryData::SpaceGrotesk_ttf, static_cast<size_t> (BinaryData::SpaceGrotesk_ttfSize));
    return typeface;
}

static juce::FontOptions namplifiedFontOptions (float height, int style = juce::Font::plain)
{
    const auto weight = (style & juce::Font::bold) != 0 ? 650.0f : 450.0f;
    return juce::FontOptions().withTypeface (getNAMplifiedTypeface())
        .withHeight (height)
        .withVariableSetting ({ juce::FontFeatureTag ("wght"), weight });
}

class NAMplifiedLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    explicit NAMplifiedLookAndFeel (NAMplifiedAudioProcessor& processorToUse)
        : processor (processorToUse), interTypeface (getNAMplifiedTypeface())
    {
        if (interTypeface != nullptr)
            setDefaultSansSerifTypeface (interTypeface);
        setColour (juce::Slider::textBoxTextColourId, text);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour::fromRGB (19, 24, 32));
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colour::fromRGB (52, 59, 72));
        setColour (juce::ComboBox::backgroundColourId, juce::Colour::fromRGB (19, 24, 32));
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::outlineColourId, juce::Colour::fromRGB (52, 59, 72));
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return juce::Font (namplifiedFontOptions (juce::jlimit (16.0f, 20.0f, buttonHeight * 0.50f)));
    }

    juce::Font getComboBoxFont (juce::ComboBox& box) override
    {
        return juce::Font (namplifiedFontOptions (juce::jlimit (14.0f, 18.0f, box.getHeight() * 0.48f)));
    }

    juce::Font getPopupMenuFont() override
    {
        return juce::Font (namplifiedFontOptions (16.0f));
    }

    juce::Font getAlertWindowTitleFont() override
    {
        return juce::Font (namplifiedFontOptions (20.0f, juce::Font::bold));
    }

    juce::Font getAlertWindowMessageFont() override
    {
        return juce::Font (namplifiedFontOptions (16.0f));
    }

    juce::Font getAlertWindowFont() override
    {
        return juce::Font (namplifiedFontOptions (15.0f));
    }

    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText,
                                           juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override
    {
        constexpr int horizontalPadding = 14;
        constexpr int verticalPadding = 16;
        const auto maxTextWidth = juce::jmax (120, juce::jmin (380, parentArea.getWidth() - horizontalPadding * 2));
        juce::AttributedString attributedText (tipText);
        attributedText.setFont (juce::Font (namplifiedFontOptions (14.5f)));
        juce::TextLayout layout;
        layout.createLayout (attributedText, static_cast<float> (maxTextWidth),
                             static_cast<float> (parentArea.getHeight()));
        const auto width = juce::jmin (parentArea.getWidth() - 12,
                                       juce::roundToInt (layout.getWidth()) + horizontalPadding * 2);
        const auto height = juce::jmin (parentArea.getHeight() - 12,
                                        juce::roundToInt (layout.getHeight()) + verticalPadding * 2);
        return juce::Rectangle<int> (screenPos.x + 14, screenPos.y + 18, width, height)
            .constrainedWithin (parentArea.reduced (6));
    }

    void drawTooltip (juce::Graphics& g, const juce::String& tipText, int width, int height) override
    {
        const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (width),
                                                     static_cast<float> (height));
        g.setColour (detailSurface);
        g.fillRoundedRectangle (bounds, 9.0f);
        g.setColour (juce::Colour (0xff465362));
        g.drawRoundedRectangle (bounds.reduced (0.75f), 8.5f, 1.5f);
        g.setColour (knobAccent);
        g.fillRoundedRectangle (juce::Rectangle<float> (12.0f, 10.0f, 3.0f,
                                                        juce::jmax (8.0f, static_cast<float> (height) - 20.0f)), 1.5f);
        juce::AttributedString attributedText (tipText);
        attributedText.setFont (juce::Font (namplifiedFontOptions (14.5f)));
        attributedText.setColour (text);
        juce::TextLayout layout;
        layout.createLayout (attributedText, juce::jmax (100.0f, static_cast<float> (width) - 30.0f),
                             juce::jmax (30.0f, static_cast<float> (height) - 20.0f));
        layout.draw (g, juce::Rectangle<float> (22.0f, 10.0f,
                                                juce::jmax (50.0f, static_cast<float> (width) - 30.0f),
                                                juce::jmax (18.0f, static_cast<float> (height) - 20.0f)));
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        const auto isModified = processor.isParameterModified (slider.getName());
        const auto colour = isModified ? accent : knobAccent;
        const auto oldTrack = slider.findColour (juce::Slider::trackColourId);
        const auto oldThumb = slider.findColour (juce::Slider::thumbColourId);
        slider.setColour (juce::Slider::trackColourId, colour);
        slider.setColour (juce::Slider::thumbColourId, colour);
        juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos,
                                                minSliderPos, maxSliderPos, style, slider);
        slider.setColour (juce::Slider::trackColourId, oldTrack);
        slider.setColour (juce::Slider::thumbColourId, oldThumb);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float proportion, float startAngle, float endAngle,
                           juce::Slider& slider) override
    {
        const auto diameter = static_cast<float> (juce::jmin (width, height) - 8);
        const auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                                     static_cast<float> (width), static_cast<float> (height));
        const auto centre = bounds.getCentre();
        const auto radius = diameter * 0.5f;
        g.setColour (juce::Colour::fromRGB (18, 23, 31));
        g.fillEllipse (centre.x - radius, centre.y - radius, diameter, diameter);
        g.setColour (juce::Colour::fromRGB (57, 66, 80));
        g.drawEllipse (centre.x - radius, centre.y - radius, diameter, diameter, 2.0f);
        const auto inner = radius * 0.76f;
        g.setColour (juce::Colour::fromRGB (34, 42, 54));
        g.fillEllipse (centre.x - inner, centre.y - inner, inner * 2.0f, inner * 2.0f);

        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, radius - 3.0f, radius - 3.0f, 0.0f,
                           startAngle, endAngle, true);
        g.setColour (juce::Colour::fromRGB (62, 70, 82));
        g.strokePath (arc, juce::PathStrokeType (2.0f));
        juce::Path active;
        active.addCentredArc (centre.x, centre.y, radius - 3.0f, radius - 3.0f, 0.0f,
                              startAngle, startAngle + proportion * (endAngle - startAngle), true);
        const auto valueColour = processor.isParameterModified (slider.getName()) ? accent : knobAccent;
        g.setColour (valueColour);
        g.strokePath (active, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        const auto angle = startAngle + proportion * (endAngle - startAngle);
        const auto pointerEnd = centre + juce::Point<float> (std::sin (angle), -std::cos (angle)) * (radius - 7.0f);
        g.setColour (valueColour);
        g.drawLine (centre.x, centre.y, pointerEnd.x, pointerEnd.y, 3.0f);
        g.fillEllipse (centre.x - 2.5f, centre.y - 2.5f, 5.0f, 5.0f);
    }

private:
    NAMplifiedAudioProcessor& processor;
    juce::Typeface::Ptr interTypeface;
};

struct ParameterControl
{
    juce::String id;
    juce::String label;
    bool percent = false;
    juce::StringArray choices;
    bool toggle = false;
    bool inputDb = false;
};

static juce::String formatControlValue (const ParameterControl& control, double value)
{
    const auto cleanDb = [] (double db) { return std::abs (db) < 0.05 ? 0.0 : db; };
    if (juce::String (control.id) == "gateThreshold")
        return juce::String (value, 0) + " dB";
    if (control.inputDb)
        return juce::String (cleanDb (-6.0 + value * 30.0), 1) + " dB";
    if (control.percent)
        return juce::String (value * 100.0, 0) + "%";
    const juce::String id (control.id);
    if (id.containsIgnoreCase ("Freq"))
        return juce::String (value, 0) + " Hz";
    if (juce::String (control.label).containsIgnoreCase ("Hz"))
        return juce::String (value, 0) + " Hz";
    if (id.containsIgnoreCase ("Time") || id.containsIgnoreCase ("Attack")
        || id.containsIgnoreCase ("Release") || id.containsIgnoreCase ("PreDelay"))
        return juce::String (value, value < 10.0 ? 2 : 0) + " ms";
    if (id.endsWith ("EqBass") || id.endsWith ("EqMid") || id.endsWith ("EqTreble"))
        return juce::String (cleanDb (value), 1) + " dB";
    if (juce::String (control.label).containsIgnoreCase ("dB"))
        return juce::String (cleanDb (value), 1) + " dB";
    return juce::String (value, 2);
}

enum class FileKind { none, nam, impulseResponse };

static juce::String getVisibleStageName (const juce::String& stageName)
{
    if (stageName == "1176-Style FET Compressor")
        return "Compressor";
    if (stageName == "PreEQ")
        return "Pre EQ";
    if (stageName == "Boost Pedal")
        return "Boost";
    if (stageName.startsWith ("NAM Profile "))
        return "AMP " + stageName.fromLastOccurrenceOf (" ", false, false);
    if (stageName.startsWith ("Cab IR "))
        return "IR " + stageName.fromLastOccurrenceOf (" ", false, false);
    return stageName;
}

static void drawBoostPedalIcon (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour ink)
{
    g.setColour (ink);
    const auto body = r.reduced (2.0f, 1.0f);
    g.drawRoundedRectangle (body, 4.5f, 2.0f);
    for (int i = 0; i < 3; ++i)
        g.fillEllipse (body.getX() + 5.0f + i * 6.0f, body.getY() + 4.0f, 2.7f, 2.7f);
    const auto cx = body.getCentreX();
    const auto top = body.getY() + 10.0f;
    const auto bottom = body.getBottom() - 5.0f;
    juce::Path arrow;
    arrow.startNewSubPath (cx - 2.5f, bottom);
    arrow.lineTo (cx + 2.5f, bottom);
    arrow.lineTo (cx + 2.5f, top + 6.0f);
    arrow.lineTo (cx + 7.0f, top + 6.0f);
    arrow.lineTo (cx, top);
    arrow.lineTo (cx - 7.0f, top + 6.0f);
    arrow.lineTo (cx - 2.5f, top + 6.0f);
    arrow.closeSubPath();
    g.fillPath (arrow);
}

class ParameterDetailPanel final : public juce::Component, private juce::Timer, private juce::ComboBox::Listener
{
public:
    ParameterDetailPanel (NAMplifiedAudioProcessor& processor, NAMplifiedLookAndFeel& lookAndFeelToUse,
                          juce::String title, const std::vector<ParameterControl>& controls)
        : audioProcessor (processor), lookAndFeel (lookAndFeelToUse),
          isPostEq (title == "Post EQ"),
          isIRShapeEq (title.startsWith ("Cab IR ")),
          isBoost (title == "Boost Pedal"),
          titleNumber (isIRShapeEq ? title.fromLastOccurrenceOf (" ", false, false) : juce::String())
    {
        heading.setText ((getVisibleStageName (title) + " · Detailed controls").toUpperCase(), juce::dontSendNotification);
        heading.setFont (namplifiedFontOptions (20.0f, juce::Font::bold));
        heading.setColour (juce::Label::textColourId, text);
        addAndMakeVisible (heading);
        for (const auto& control : controls)
        {
            auto label = std::make_unique<juce::Label>();
            label->setText (juce::String (control.label).replace ("(dB)", "").trim().toUpperCase(), juce::dontSendNotification);
            label->setColour (juce::Label::textColourId, secondaryText);
            addAndMakeVisible (*label);
            labels.push_back (std::move (label));
            controlIds.emplace_back (control.id);
            if (control.toggle)
            {
                auto toggle = std::make_unique<juce::ToggleButton>();
                toggle->setName (control.id);
                toggle->setTooltip (control.label);
                toggle->setColour (juce::ToggleButton::textColourId, text);
                addAndMakeVisible (*toggle);
                buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                    audioProcessor.parameters, control.id, *toggle));
                controlWidgets.push_back (toggle.get());
                toggles.push_back (std::move (toggle));
            }
            else if (! control.choices.isEmpty())
            {
                auto choice = std::make_unique<juce::ComboBox>();
                choice->setName (control.id);
                choice->addItemList (control.choices, 1);
                addAndMakeVisible (*choice);
                comboAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                    audioProcessor.parameters, control.id, *choice));
                if (isBoost && juce::String (control.id) == "boostModel")
                    choice->addListener (this);
                controlWidgets.push_back (choice.get());
                comboBoxes.push_back (std::move (choice));
            }
            else
            {
                auto slider = std::make_unique<juce::Slider>();
                slider->setSliderStyle (juce::Slider::LinearHorizontal);
                slider->setName (control.id);
                slider->setLookAndFeel (&lookAndFeel);
                slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 66, 22);
                slider->setTooltip (control.label);
                slider->setColour (juce::Slider::trackColourId, knobAccent);
                slider->setColour (juce::Slider::textBoxTextColourId, text);
                slider->setColour (juce::Slider::textBoxBackgroundColourId, panel);
                slider->setColour (juce::Slider::textBoxOutlineColourId, panel);
                const juce::String parameterId (control.id);
                if (parameterId == "gateThreshold")
                {
                    slider->textFromValueFunction = [] (double value) { return juce::String (value, 0); };
                    slider->valueFromTextFunction = [] (const juce::String& value) { return value.getDoubleValue(); };
                    slider->setTextValueSuffix (" dB");
                }
                else if (parameterId.containsIgnoreCase ("Freq"))
                    slider->setTextValueSuffix (" Hz");
                else if (juce::String (control.label).containsIgnoreCase ("Hz"))
                    slider->setTextValueSuffix (" Hz");
                else if (parameterId == "delayTone")
                    slider->setTextValueSuffix (" Hz");
                else if (parameterId.containsIgnoreCase ("Time") || parameterId.containsIgnoreCase ("Attack")
                         || parameterId.containsIgnoreCase ("Release") || parameterId.containsIgnoreCase ("PreDelay"))
                    slider->setTextValueSuffix (" ms");
                else if (parameterId.containsIgnoreCase ("Threshold"))
                    slider->setTextValueSuffix (" dB");
                if (! control.percent && ! control.inputDb && parameterId != "gateThreshold"
                    && juce::String (control.label).containsIgnoreCase ("dB"))
                {
                    slider->textFromValueFunction = [] (double value)
                    {
                        return juce::String (std::abs (value) < 0.05 ? 0.0 : value, 1);
                    };
                    slider->valueFromTextFunction = [] (const juce::String& value) { return value.getDoubleValue(); };
                    slider->setTextValueSuffix (" dB");
                }
                if (control.percent)
                {
                    slider->textFromValueFunction = [] (double value) { return juce::String (value * 100.0, 0); };
                    slider->valueFromTextFunction = [] (const juce::String& value) { return value.getDoubleValue() / 100.0; };
                    slider->setTextValueSuffix (" %");
                }
                if (control.inputDb)
                {
                    slider->textFromValueFunction = [] (double value)
                    {
                        const auto db = -6.0 + value * 30.0;
                        return juce::String (std::abs (db) < 0.05 ? 0.0 : db, 1);
                    };
                    slider->valueFromTextFunction = [] (const juce::String& value)
                    {
                        return juce::jlimit (0.0, 1.0, (value.getDoubleValue() + 6.0) / 30.0);
                    };
                    slider->setTextValueSuffix (" dB");
                }
                addAndMakeVisible (*slider);
                attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                    audioProcessor.parameters, control.id, *slider));
                controlWidgets.push_back (slider.get());
                sliders.push_back (std::move (slider));
            }
        }
        updateBoostControls();
        if (isPostEq || isIRShapeEq)
        {
            constexpr const char* names[] { "LOW", "MID", "HIGH" };
            for (int band = 0; band < 3; ++band)
            {
                auto button = std::make_unique<juce::TextButton> ("RESET");
                button->setTooltip ("Reset " + juce::String (names[band]) + " EQ band to its default frequency and 0 dB");
                button->onClick = [this, band] { resetEqBand (band); };
                addAndMakeVisible (*button);
                resetButtons.push_back (std::move (button));
            }
        }
        if (isBoost)
            updateBoostControls();
        else
            setSize (430, 48 + static_cast<int> (controlWidgets.size()) * 46);
        addMouseListener (this, true);
        startTimerHz (20);
    }

    void paint (juce::Graphics& g) override { g.fillAll (background); }
    void mouseDown (const juce::MouseEvent& event) override
    {
        if (! audioProcessor.isMidiLearnArmed())
            return;
        auto* component = event.originalComponent;
        while (component != nullptr && component != this)
        {
            const auto id = component->getName();
            if (id.isNotEmpty() && audioProcessor.parameters.getParameter (id) != nullptr
                && audioProcessor.setMidiLearnTarget (id))
                return;
            component = component->getParentComponent();
        }
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced (16);
        heading.setBounds (area.removeFromTop (28));
        area.removeFromTop (4);
        for (size_t i = 0; i < controlWidgets.size(); ++i)
        {
            if (isBoost && ! controlWidgets[i]->isVisible())
                continue;
            auto row = area.removeFromTop (44);
            labels[i]->setBounds (row.removeFromLeft (130));
            if ((isPostEq || isIRShapeEq) && i % 2 == 1)
                resetButtons[i / 2]->setBounds (row.removeFromRight (66).reduced (0, 7));
            controlWidgets[i]->setBounds (row);
        }
    }
private:
    void comboBoxChanged (juce::ComboBox* box) override
    {
        if (! isBoost || box == nullptr || box->getName() != "boostModel")
            return;
        const juce::Component::SafePointer<ParameterDetailPanel> safeThis (this);
        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis != nullptr)
            {
                safeThis->updateBoostControls();
                safeThis->resized();
            }
        });
    }

    void timerCallback() override
    {
        if (isBoost)
            updateBoostControls();
        for (auto* widget : controlWidgets)
            if (auto* slider = dynamic_cast<juce::Slider*> (widget))
                slider->repaint();
    }

    void updateBoostControls()
    {
        if (! isBoost)
            return;
        const auto* modelParameter = audioProcessor.parameters.getRawParameterValue ("boostModel");
        const auto use808 = modelParameter != nullptr && modelParameter->load() >= 0.5f;
        const auto* sourceParameter = audioProcessor.parameters.getRawParameterValue ("boostSource");
        const auto useNAM = sourceParameter != nullptr && sourceParameter->load() >= 0.5f;
        int visibleCount = 0;
        for (size_t i = 0; i < controlWidgets.size(); ++i)
        {
            const auto& id = controlIds[i];
            const auto visible = useNAM ? (id == "boostNAMIn" || id == "boostNAMOut")
                : (id == "boostModel"
                   || (use808 ? (id == "boost808Drive" || id == "boost808Tone" || id == "boost808Level")
                              : (id == "boostGain" || id == "boostBass" || id == "boostMid" || id == "boostTreble")));
            controlWidgets[i]->setVisible (visible);
            labels[i]->setVisible (visible);
            if (visible)
                ++visibleCount;
        }
        setSize (430, 54 + visibleCount * 48);
    }

    void resetEqBand (int band)
    {
        constexpr const char* bandNames[] { "Low", "Mid", "High" };
        const auto prefix = isPostEq ? juce::String ("post") : "ir" + titleNumber + "Shape";
        const auto frequencyId = prefix + bandNames[band] + "Freq";
        const auto gainId = prefix + bandNames[band];
        for (const auto& id : { frequencyId, gainId })
            if (auto* parameter = audioProcessor.parameters.getParameter (id))
                parameter->setValueNotifyingHost (parameter->getDefaultValue());
    }

    NAMplifiedAudioProcessor& audioProcessor;
    NAMplifiedLookAndFeel& lookAndFeel;
    bool isPostEq = false;
    bool isIRShapeEq = false;
    bool isBoost = false;
    juce::String titleNumber;
    juce::Label heading;
    std::vector<std::unique_ptr<juce::Label>> labels;
    std::vector<juce::String> controlIds;
    std::vector<std::unique_ptr<juce::Slider>> sliders;
    std::vector<std::unique_ptr<juce::ComboBox>> comboBoxes;
    std::vector<std::unique_ptr<juce::ToggleButton>> toggles;
    std::vector<juce::Component*> controlWidgets;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
    std::vector<std::unique_ptr<juce::TextButton>> resetButtons;
};

class ToneCurveButton final : public juce::Button
{
public:
    explicit ToneCurveButton (juce::String name) : juce::Button (name) { setTooltip ("Click to open detailed tone controls"); }
    void paintButton (juce::Graphics& g, bool, bool) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (juce::Colour::fromRGB (39, 43, 51));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (juce::Colour::fromRGB (77, 82, 93));
        for (int i = 1; i < 4; ++i)
            g.drawHorizontalLine (static_cast<int> (r.getY() + r.getHeight() * i / 4.0f), r.getX(), r.getRight());
        juce::Path curve;
        curve.startNewSubPath (r.getX() + 6.0f, r.getCentreY());
        curve.cubicTo (r.getX() + r.getWidth() * 0.28f, r.getCentreY() - 5.0f,
                       r.getX() + r.getWidth() * 0.35f, r.getCentreY() + 7.0f,
                       r.getX() + r.getWidth() * 0.52f, r.getCentreY());
        curve.cubicTo (r.getX() + r.getWidth() * 0.72f, r.getCentreY() - 9.0f,
                       r.getX() + r.getWidth() * 0.82f, r.getCentreY() - 11.0f,
                       r.getRight() - 6.0f, r.getCentreY() - 7.0f);
        g.setColour (accent);
        g.strokePath (curve, juce::PathStrokeType (2.2f));
        g.setColour (juce::Colour::fromRGB (235, 236, 239));
        g.setFont (namplifiedFontOptions (14.0f, juce::Font::bold));
        g.drawText (getButtonText(), getLocalBounds().removeFromBottom (16), juce::Justification::centred);
    }
};

class GraphicEqEditor final : public juce::Component, private juce::Timer
{
public:
    explicit GraphicEqEditor (NAMplifiedAudioProcessor& processor, bool postEq)
        : audioProcessor (processor), isPostEq (postEq), qParameterId (postEq ? "postEqQ" : "preEqQ")
    {
        if (postEq)
        {
            parameterIds = { "postLow", "postGraphic2", "postGraphic3", "postGraphic4", "postMid",
                             "postGraphic6", "postGraphic7", "postGraphic8", "postHigh" };
            frequencyIds = { "postLowFreq", "postGraphic2Freq", "postGraphic3Freq", "postGraphic4Freq",
                             "postMidFreq", "postGraphic6Freq", "postGraphic7Freq", "postGraphic8Freq",
                             "postHighFreq" };
        }
        else
        {
            parameterIds = { "preLow", "preBand2", "preMid", "preBand4", "preHigh" };
            frequencyIds = { "preLowFreq", "preBand2Freq", "preMidFreq", "preBand4Freq", "preHighFreq" };
        }

        qLabel.setText ("BAND WIDTH / Q", juce::dontSendNotification);
        qLabel.setFont (namplifiedFontOptions (14.0f, juce::Font::bold));
        qLabel.setColour (juce::Label::textColourId, secondaryText);
        qLabel.setTooltip ("Controls the width of the bell bands. Lower Q is wider; higher Q is narrower.");
        addAndMakeVisible (qLabel);

        qSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        qSlider.setName (qParameterId);
        qSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 54, 22);
        qSlider.setDoubleClickReturnValue (true, 0.707);
        qSlider.setColour (juce::Slider::trackColourId, knobAccent);
        qSlider.setColour (juce::Slider::textBoxTextColourId, text);
        qSlider.setColour (juce::Slider::textBoxBackgroundColourId, panel);
        qSlider.setColour (juce::Slider::textBoxOutlineColourId, panel);
        qSlider.textFromValueFunction = [] (double value) { return juce::String (value, 2); };
        qSlider.valueFromTextFunction = [] (const juce::String& value) { return value.getDoubleValue(); };
        qSlider.setTooltip ("Controls the width of the bell bands. Lower Q is wider; higher Q is narrower.");
        addAndMakeVisible (qSlider);
        qAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            audioProcessor.parameters, qParameterId, qSlider);

        setMouseCursor (juce::MouseCursor::CrosshairCursor);
        for (int band = 0; band < parameterIds.size(); ++band)
        {
            auto button = std::make_unique<juce::TextButton> ("RESET");
            button->setTooltip ("Reset this band's frequency and gain");
            button->onClick = [this, band] { resetBand (band); };
            addAndMakeVisible (*button);
            resetButtons.push_back (std::move (button));
        }
        startTimerHz (30);
    }

    void resized() override
    {
        const auto plot = getPlotBounds();
        qLabel.setBounds (juce::roundToInt (plot.getRight() - 230.0f), 18, 94, 24);
        qSlider.setBounds (juce::roundToInt (plot.getRight() - 132.0f), 17, 132, 26);
        positionResetButtons (plot);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        auto graph = bounds.reduced (22.0f, 18.0f);
        g.setColour (juce::Colour::fromRGB (19, 24, 32));
        g.fillRoundedRectangle (graph, 12.0f);
        g.setColour (juce::Colour::fromRGB (52, 59, 72));
        g.drawRoundedRectangle (graph, 12.0f, 1.0f);

        const auto plot = getPlotBounds();
        g.setFont (namplifiedFontOptions (15.0f));
        for (int db = -12; db <= 12; db += 6)
        {
            const auto y = gainToY (static_cast<float> (db), plot);
            g.setColour (db == 0 ? juce::Colour::fromRGB (88, 99, 114) : juce::Colour::fromRGB (43, 51, 63));
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
            g.setColour (secondaryText);
            g.drawText (juce::String (db) + " dB", 4, juce::roundToInt (y - 9.0f), 54, 18,
                        juce::Justification::centredRight);
        }

        for (const auto frequency : { 100.0f, 1000.0f, 10000.0f })
        {
            const auto x = frequencyToX (frequency, plot);
            g.setColour (juce::Colour::fromRGB (36, 44, 56));
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
            g.setColour (secondaryText);
            g.setFont (namplifiedFontOptions (12.0f));
            g.drawText (formatFrequency (frequency), juce::Rectangle<int> (
                juce::roundToInt (x - 30.0f), juce::roundToInt (plot.getBottom() + 12.0f), 60, 16),
                juce::Justification::centred);
        }

        std::vector<juce::Point<float>> points;
        points.reserve (static_cast<size_t> (parameterIds.size()));
        for (int band = 0; band < parameterIds.size(); ++band)
        {
            const auto x = bandX (band, plot);
            const auto gain = bandGain (band);
            const juce::Point<float> point (x, gainToY (gain, plot));
            points.push_back (point);
            g.setColour (juce::Colour::fromRGB (41, 49, 61));
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
            g.setFont (namplifiedFontOptions (12.0f));
            g.setColour (text);
            g.drawText (formatFrequency (bandFrequency (band)), juce::Rectangle<int> (
                juce::roundToInt (x - 34.0f), juce::roundToInt (plot.getBottom() + 12.0f), 68, 18),
                juce::Justification::centred);
            g.setColour (secondaryText);
            g.drawText ((gain > 0.0f ? "+" : "") + juce::String (gain, 1),
                        juce::Rectangle<int> (juce::roundToInt (x - 34.0f), juce::roundToInt (point.y - 27.0f), 68, 16),
                        juce::Justification::centred);
        }

        if (! points.empty())
        {
            juce::Path response;
            const auto firstShelfGain = bandGain (0);
            response.startNewSubPath (plot.getX(), gainToY (firstShelfGain, plot));
            if (points.front().x > plot.getX())
                response.lineTo (points.front());
            else
                response.startNewSubPath (points.front());
            for (size_t i = 0; i + 1 < points.size(); ++i)
            {
                const auto p0 = i == 0 ? points[i] : points[i - 1];
                const auto p1 = points[i];
                const auto p2 = points[i + 1];
                const auto p3 = i + 2 < points.size() ? points[i + 2] : points[i + 1];
                auto c1 = p1 + (p2 - p0) * (1.0f / 6.0f);
                auto c2 = p2 - (p3 - p1) * (1.0f / 6.0f);
                c1.x = juce::jlimit (p1.x, p2.x, c1.x);
                c2.x = juce::jlimit (p1.x, p2.x, c2.x);
                response.cubicTo (c1, c2, p2);
            }
            if (points.back().x < plot.getRight())
                response.lineTo (plot.getRight(), gainToY (bandGain (parameterIds.size() - 1), plot));
            g.setColour (juce::Colour (0xff66d6b2));
            g.strokePath (response, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved,
                                                           juce::PathStrokeType::rounded));
        }

        for (const auto& point : points)
        {
            g.setColour (juce::Colour::fromRGB (19, 24, 32));
            g.fillEllipse (point.x - 10.0f, point.y - 10.0f, 20.0f, 20.0f);
            g.setColour (juce::Colour (0xff66d6b2));
            g.fillEllipse (point.x - 5.5f, point.y - 5.5f, 11.0f, 11.0f);
        }
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        const auto plot = getPlotBounds();
        auto nearest = 0;
        auto nearestDistance = std::numeric_limits<float>::max();
        for (int band = 0; band < parameterIds.size(); ++band)
        {
            const auto point = juce::Point<float> (bandX (band, plot), gainToY (bandGain (band), plot));
            const auto distance = point.getDistanceFrom (event.position);
            if (distance < nearestDistance) { nearestDistance = distance; nearest = band; }
        }
        activeBand = nearest;
        if (auto* parameter = audioProcessor.parameters.getParameter (parameterIds[activeBand]))
            parameter->beginChangeGesture();
        if (auto* parameter = audioProcessor.parameters.getParameter (frequencyIds[activeBand]))
            parameter->beginChangeGesture();
        updateBand (event.position.x, event.position.y, plot);
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        updateBand (event.position.x, event.position.y, getPlotBounds());
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (activeBand >= 0)
        {
            if (auto* parameter = audioProcessor.parameters.getParameter (parameterIds[activeBand]))
                parameter->endChangeGesture();
            if (auto* parameter = audioProcessor.parameters.getParameter (frequencyIds[activeBand]))
                parameter->endChangeGesture();
        }
        activeBand = -1;
    }

private:
    void timerCallback() override
    {
        positionResetButtons (getPlotBounds());
        repaint();
    }

    juce::Rectangle<float> getPlotBounds() const
    {
        auto plot = getLocalBounds().toFloat().reduced (66.0f, 46.0f);
        plot.translate (0.0f, 6.0f);
        plot.removeFromBottom (58.0f);
        return plot;
    }

    float bandFrequency (int band) const
    {
        const auto* value = audioProcessor.parameters.getRawParameterValue (frequencyIds[band]);
        return value != nullptr ? value->load() : 1000.0f;
    }

    float bandGain (int band) const
    {
        const auto* value = audioProcessor.parameters.getRawParameterValue (parameterIds[band]);
        return value != nullptr ? value->load() : 0.0f;
    }

    float bandX (int band, juce::Rectangle<float> plot) const
    {
        return frequencyToX (bandFrequency (band), plot);
    }

    float frequencyToX (float frequency, juce::Rectangle<float> plot) const
    {
        const auto minAxis = 20.0f;
        const auto maxAxis = isPostEq ? 20000.0f : 16000.0f;
        const auto proportion = std::log (juce::jlimit (minAxis, maxAxis, frequency) / minAxis)
                              / std::log (maxAxis / minAxis);
        return plot.getX() + proportion * plot.getWidth();
    }

    float xToFrequency (float x, juce::Rectangle<float> plot) const
    {
        const auto proportion = juce::jlimit (0.0f, 1.0f, (x - plot.getX()) / plot.getWidth());
        const auto minAxis = 20.0f;
        const auto maxAxis = isPostEq ? 20000.0f : 16000.0f;
        return minAxis * std::pow (maxAxis / minAxis, proportion);
    }

    float allowedMinFrequency() const { return 50.0f; }
    float allowedMaxFrequency() const { return isPostEq ? 16000.0f : 12000.0f; }

    static juce::String formatFrequency (float frequency)
    {
        if (frequency >= 1000.0f)
        {
            const auto kHz = frequency / 1000.0f;
            return juce::String (kHz, kHz >= 10.0f || std::abs (kHz - std::round (kHz)) < 0.05f ? 0 : 1) + " kHz";
        }
        return juce::String (juce::roundToInt (frequency)) + " Hz";
    }

    void positionResetButtons (juce::Rectangle<float> plot)
    {
        for (int band = 0; band < parameterIds.size(); ++band)
        {
            constexpr int buttonWidth = 48;
            const auto x = juce::jlimit (0.0f, static_cast<float> (getWidth() - buttonWidth),
                                         bandX (band, plot) - buttonWidth * 0.5f);
            resetButtons[static_cast<size_t> (band)]->setBounds (
                juce::roundToInt (x), juce::roundToInt (plot.getBottom() + 34.0f), buttonWidth, 22);
        }
    }

    static float gainToY (float gain, juce::Rectangle<float> plot)
    {
        return juce::jmap (juce::jlimit (-12.0f, 12.0f, gain), -12.0f, 12.0f,
                           plot.getBottom(), plot.getY());
    }

    void updateBand (float x, float y, juce::Rectangle<float> plot)
    {
        if (activeBand < 0)
            return;

        const auto gap = juce::jmin (42.0f, plot.getWidth() / static_cast<float> (parameterIds.size() + 1) * 0.8f);
        auto minX = plot.getX();
        auto maxX = plot.getRight();
        if (activeBand > 0)
            minX = bandX (activeBand - 1, plot) + gap;
        if (activeBand + 1 < parameterIds.size())
            maxX = bandX (activeBand + 1, plot) - gap;
        const auto constrainedX = minX <= maxX ? juce::jlimit (minX, maxX, x) : bandX (activeBand, plot);
        const auto frequency = juce::jlimit (allowedMinFrequency(), allowedMaxFrequency(), xToFrequency (constrainedX, plot));
        const auto gain = juce::jmap (juce::jlimit (plot.getY(), plot.getBottom(), y),
                                      plot.getBottom(), plot.getY(), -12.0f, 12.0f);

        if (auto* parameter = audioProcessor.parameters.getParameter (frequencyIds[activeBand]))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (frequency));
        if (auto* parameter = audioProcessor.parameters.getParameter (parameterIds[activeBand]))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (gain));
        positionResetButtons (plot);
        repaint();
    }

    void resetBand (int band)
    {
        for (const auto& id : { frequencyIds[band], parameterIds[band] })
            if (auto* parameter = audioProcessor.parameters.getParameter (id))
            {
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost (parameter->getDefaultValue());
                parameter->endChangeGesture();
            }
        positionResetButtons (getPlotBounds());
        repaint();
    }

    NAMplifiedAudioProcessor& audioProcessor;
    bool isPostEq = false;
    juce::String qParameterId;
    juce::StringArray parameterIds, frequencyIds;
    juce::Label qLabel;
    juce::Slider qSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> qAttachment;
    std::vector<std::unique_ptr<juce::TextButton>> resetButtons;
    int activeBand = -1;
};

class StageCard final : public juce::Component, private juce::Timer, private juce::ComboBox::Listener
{
public:
    StageCard (NAMplifiedAudioProcessor& processor, juce::String name, juce::String enableId,
               std::vector<ParameterControl> controls, FileKind fileKind = FileKind::none,
               int fileSlot = 0, std::function<void (int)> moveHandler = {},
               std::vector<ParameterControl> detailControls = {}, std::function<void()> contentSizeChanged = {})
        : audioProcessor (processor), lookAndFeel (processor), stageName (std::move (name)), enableParameterId (std::move (enableId)), loadKind (fileKind),
          slot (fileSlot), hasMoveControls (false), moveHandler (std::move (moveHandler)),
          contentSizeChangedHandler (std::move (contentSizeChanged))
    {
        setName (stageName);
        heading.setText (getVisibleStageName (stageName).toUpperCase(), juce::dontSendNotification);
        heading.setFont (namplifiedFontOptions (19.0f, juce::Font::bold));
        heading.setColour (juce::Label::textColourId, text);
        addAndMakeVisible (heading);

        if (enableParameterId.isNotEmpty())
        {
            hasEnableControl = true;
            enableButton.setName (enableParameterId);
            enableButton.setButtonText ("ENABLED");
            enableButton.setColour (juce::ToggleButton::textColourId, secondaryText);
            addAndMakeVisible (enableButton);
            enableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                audioProcessor.parameters, enableParameterId, enableButton);
        }

        if (hasMoveControls)
        {
            dragHandle.setText ("MOVE", juce::dontSendNotification);
            dragHandle.setFont (namplifiedFontOptions (18.0f, juce::Font::bold));
            dragHandle.setColour (juce::Label::textColourId, secondaryText);
            dragHandle.setTooltip ("Drag this block to reorder it in this section");
            dragHandle.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (dragHandle);
            moveUpButton.setButtonText ("UP");
            moveDownButton.setButtonText ("DN");
            moveUpButton.setTooltip ("Move earlier within this section");
            moveDownButton.setTooltip ("Move later within this section");
            addAndMakeVisible (moveUpButton);
            addAndMakeVisible (moveDownButton);
            moveUpButton.onClick = [moveHandler] { moveHandler (-1); };
            moveDownButton.onClick = [moveHandler] { moveHandler (1); };
        }

        if (stageName == "PreEQ" || stageName == "Post EQ")
        {
            graphicEq = std::make_unique<GraphicEqEditor> (audioProcessor, stageName == "Post EQ");
            addAndMakeVisible (*graphicEq);
            expandButton = std::make_unique<juce::TextButton> ("FULL SCREEN");
            expandButton->setTooltip ("Expand this graphic EQ to fill the plugin window.");
            addAndMakeVisible (*expandButton);
            expandButton->onClick = [this]
            {
                if (fullscreenHandler)
                    fullscreenHandler (! isFullscreen);
            };
            return;
        }

        if (! detailControls.empty())
        {
            toneCurveButton = std::make_unique<ToneCurveButton> ("Edit detailed controls");
            toneCurveButton->setButtonText (stageName.containsIgnoreCase ("Compressor") ? "COMPRESSOR"
                                            : stageName.containsIgnoreCase ("Boost") ? "EDIT BOOST"
                                            : stageName.containsIgnoreCase ("Delay") ? "EDIT DELAY"
                                            : stageName.containsIgnoreCase ("Reverb") ? "EDIT REVERB" : "EDIT EQ");
            addAndMakeVisible (*toneCurveButton);
            toneCurveButton->onClick = [this, details = std::move (detailControls)]
            {
                auto content = std::make_unique<ParameterDetailPanel> (audioProcessor, lookAndFeel, stageName, details);
                juce::CallOutBox::launchAsynchronously (std::move (content), *toneCurveButton, nullptr);
            };
        }

        if (loadKind != FileKind::none)
        {
            loadButton.setButtonText (loadKind == FileKind::nam ? "LOAD NAM CAPTURE" : "LOAD IR");
            loadButton.setColour (juce::TextButton::buttonColourId, juce::Colour::fromRGB (54, 58, 67));
            loadButton.setColour (juce::TextButton::textColourOffId, text);
            if (loadKind == FileKind::nam)
                loadButton.setTooltip ("Load a capture from a folder containing this amp's other captures. The arrows step through NAM files in that folder. Saving a preset copies its NAM and IR files into that preset's Assets folder.");
            addAndMakeVisible (loadButton);

            if (loadKind == FileKind::nam)
            {
                previousCaptureButton.setButtonText ("PREV");
                nextCaptureButton.setButtonText ("NEXT");
                previousCaptureButton.setTooltip ("Load the previous NAM capture from this folder.");
                nextCaptureButton.setTooltip ("Load the next NAM capture from this folder.");
                previousCaptureButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1e3935));
                nextCaptureButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1e3935));
                previousCaptureButton.setColour (juce::TextButton::textColourOffId, knobAccent);
                nextCaptureButton.setColour (juce::TextButton::textColourOffId, knobAccent);
                addAndMakeVisible (previousCaptureButton);
                addAndMakeVisible (nextCaptureButton);
                previousCaptureButton.onClick = [this] { stepCapture (-1); };
                nextCaptureButton.onClick = [this] { stepCapture (1); };
                tone3000Button.setButtonText ({});
                tone3000Button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff21473f));
                tone3000Button.setColour (juce::TextButton::textColourOffId, knobAccent);
                tone3000Button.setTooltip ("Choose a NAM tone on TONE3000, then select and download one capture locally.");
                addAndMakeVisible (tone3000Button);
                tone3000Button.onClick = [this] { showToneArchitectureMenu(); };
                tone3000Logo = juce::Drawable::createFromSVGString (juce::String::fromUTF8 (
                    BinaryData::TONE3000Logo_svg, BinaryData::TONE3000Logo_svgSize));
            }

            fileName.setText (loadKind == FileKind::nam ? audioProcessor.getNAMCaptureName (slot)
                                                        : audioProcessor.getImpulseResponseName (slot),
                              juce::dontSendNotification);
            fileName.setFont (namplifiedFontOptions (15.0f));
            fileName.setColour (juce::Label::textColourId, secondaryText);
            fileName.setJustificationType (loadKind == FileKind::nam
                ? juce::Justification::centred : juce::Justification::centredLeft);
            addAndMakeVisible (fileName);

            loadButton.onClick = [this]
            {
                const auto filter = loadKind == FileKind::nam ? "*.nam" : "*.wav;*.aif;*.aiff;*.flac;*.ogg";
                auto initialDirectory = loadKind == FileKind::nam
                    ? audioProcessor.getNAMCaptureFile (slot).getParentDirectory() : juce::File();
                if (loadKind == FileKind::nam && ! initialDirectory.isDirectory())
                    initialDirectory = lastNAMDirectory();
                if (! initialDirectory.isDirectory())
                    initialDirectory = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
                fileChooser = std::make_unique<juce::FileChooser> (
                    loadKind == FileKind::nam ? "Choose a NAM capture" : "Choose an impulse response",
                    initialDirectory, filter);
                juce::Component::SafePointer<StageCard> safeThis (this);
                fileChooser->launchAsync (
                    juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                    [safeThis] (const juce::FileChooser& chooser)
                    {
                        if (safeThis == nullptr)
                            return;
                        const auto file = chooser.getResult();
                        if (! file.existsAsFile())
                            return;
                        if (safeThis->loadKind == FileKind::nam)
                        {
                            safeThis->toneModels.clear();
                            safeThis->tone3000Attribution.clear();
                            rememberNAMDirectory (file.getParentDirectory());
                            safeThis->audioProcessor.loadNAMCapture (safeThis->slot, file);
                        }
                        else
                            safeThis->audioProcessor.loadImpulseResponse (safeThis->slot, file);
                        safeThis->fileName.setText (file.getFileName(), juce::dontSendNotification);
                    });
            };
            if (loadKind == FileKind::nam || loadKind == FileKind::impulseResponse)
                startTimerHz (3);
        }


        for (const auto& control : controls)
        {
            auto label = std::make_unique<juce::Label>();
            label->setText (juce::String (control.label).replace ("(dB)", "").trim().toUpperCase(), juce::dontSendNotification);
            label->setFont (namplifiedFontOptions (15.0f));
            label->setColour (juce::Label::textColourId, secondaryText);
            if (stageName == "Input" || stageName == "Noise Gate" || stageName == "Output")
                label->setJustificationType (juce::Justification::centred);
            addAndMakeVisible (*label);
            controlLabels.push_back (std::move (label));

            if (control.toggle)
            {
                auto toggle = std::make_unique<juce::ToggleButton>();
                toggle->setName (control.id);
                toggle->setButtonText (juce::String (control.label).replace ("(dB)", "").trim().toUpperCase());
                toggle->setColour (juce::ToggleButton::textColourId, text);
                addAndMakeVisible (*toggle);
                buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                    audioProcessor.parameters, control.id, *toggle));
                controlWidgets.push_back (toggle.get());
                controlIds.emplace_back (control.id);
                toggles.push_back (std::move (toggle));
                widgetValueLabels.push_back (nullptr);
            }
            else if (stageName == "Boost Pedal" && control.id == "boostSource")
            {
                auto sourceButton = std::make_unique<juce::TextButton> (
                    stageName == "Boost Pedal" ? "BOOST" : "NAM CAPTURE");
                sourceButton->setName (control.id);
                sourceButton->setClickingTogglesState (false);
                sourceButton->setColour (juce::TextButton::buttonColourId, juce::Colour::fromRGB (40, 45, 54));
                sourceButton->setColour (juce::TextButton::textColourOffId, text);
                sourceButton->setTooltip ("Switch this block between the built-in boost effects and a NAM capture.");
                addAndMakeVisible (*sourceButton);
                const auto parameterId = control.id;
                sourceButton->onClick = [this, parameterId]
                {
                    auto* parameter = audioProcessor.parameters.getParameter (parameterId);
                    const auto* current = audioProcessor.parameters.getRawParameterValue (parameterId);
                    if (parameter != nullptr && current != nullptr)
                    {
                        const auto nextChoice = current->load() >= 0.5f ? 0.0f : 1.0f;
                        parameter->beginChangeGesture();
                        parameter->setValueNotifyingHost (parameter->convertTo0to1 (nextChoice));
                        parameter->endChangeGesture();
                        updateConditionalControlVisibility();
                        resized();
                        if (contentSizeChangedHandler)
                            contentSizeChangedHandler();
                    }
                };
                controlWidgets.push_back (sourceButton.get());
                controlIds.emplace_back (control.id);
                sourceButtons.push_back (std::move (sourceButton));
                widgetValueLabels.push_back (nullptr);
            }
            else if (! control.choices.isEmpty())
            {
                auto choice = std::make_unique<juce::ComboBox>();
                choice->setName (control.id);
                choice->addItemList (control.choices, 1);
                addAndMakeVisible (*choice);
                comboAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                    audioProcessor.parameters, control.id, *choice));
                if (stageName == "Boost Pedal" && juce::String (control.id) == "boostModel")
                    choice->addListener (this);
                controlWidgets.push_back (choice.get());
                controlIds.emplace_back (control.id);
                comboBoxes.push_back (std::move (choice));
                widgetValueLabels.push_back (nullptr);
            }
            else
            {
                auto slider = std::make_unique<juce::Slider>();
                slider->setName (control.id);
                slider->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
                slider->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
                slider->setLookAndFeel (&lookAndFeel);
                slider->setColour (juce::Slider::trackColourId, knobAccent);
                slider->setColour (juce::Slider::textBoxTextColourId, text);
                slider->setColour (juce::Slider::textBoxBackgroundColourId, panel);
                slider->setColour (juce::Slider::textBoxOutlineColourId, panel);
                addAndMakeVisible (*slider);
                sliderAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                    audioProcessor.parameters, control.id, *slider));
                controlWidgets.push_back (slider.get());
                controlIds.emplace_back (control.id);
                auto valueLabel = std::make_unique<juce::Label>();
                valueLabel->setFont (namplifiedFontOptions (16.0f));
                valueLabel->setColour (juce::Label::textColourId, text);
                valueLabel->setJustificationType (juce::Justification::centred);
                addAndMakeVisible (*valueLabel);
                auto* valueLabelPtr = valueLabel.get();
                auto* sliderPtr = slider.get();
                const auto displayControl = control;
                slider->onValueChange = [valueLabelPtr, sliderPtr, displayControl]
                {
                    valueLabelPtr->setText (formatControlValue (displayControl, sliderPtr->getValue()),
                                            juce::dontSendNotification);
                };
                valueLabelPtr->setText (formatControlValue (control, slider->getValue()), juce::dontSendNotification);
                widgetValueLabels.push_back (valueLabelPtr);
                valueLabels.push_back (std::move (valueLabel));
                sliders.push_back (std::move (slider));
            }
        }
        updateConditionalControlVisibility();
        updateAmpCabDependency();
    }

    void paint (juce::Graphics& g) override
    {
        auto card = getLocalBounds().toFloat();
        g.setColour (detailSurface);
        g.fillRoundedRectangle (card, 16.0f);
        g.setColour (juce::Colour::fromRGB (62, 73, 86));
        g.drawRoundedRectangle (card.reduced (1.0f), 16.0f, 2.5f);
        drawBlockIcon (g, juce::Rectangle<float> (26.0f, 18.0f, 32.0f, 32.0f));
    }

    const juce::String& getStageName() const { return stageName; }
    const juce::String& getEnableParameterId() const { return enableParameterId; }
    std::function<void (int)> getMoveHandler() const { return moveHandler; }
    void setFullscreenHandler (std::function<void (bool)> handler) { fullscreenHandler = std::move (handler); }
    void setFullscreenMode (bool fullscreen)
    {
        isFullscreen = fullscreen;
        if (expandButton != nullptr)
        {
            expandButton->setButtonText (fullscreen ? "RESTORE" : "FULL SCREEN");
            expandButton->setTooltip (fullscreen ? "Return to the signal chain view." : "Expand this graphic EQ to fill the plugin window.");
        }
    }
    void setCompactMode (bool shouldBeCompact)
    {
        if (compactMode == shouldBeCompact)
            return;
        compactMode = shouldBeCompact;
        resized();
    }

    int getPreferredHeight() const
    {
        if (graphicEq != nullptr)
            return 310;
        int selectorCount = 0;
        int knobCount = 0;
        for (size_t i = 0; i < controlWidgets.size(); ++i)
        {
            if (! isControlVisible (i))
                continue;
            if (controlIds[i].endsWith ("Source"))
                continue;
            if (dynamic_cast<juce::ComboBox*> (controlWidgets[i]) != nullptr)
                ++selectorCount;
            else
                ++knobCount;
        }
        const auto columns = selectorCount > 0 ? 4 : 5;
        const auto rows = (knobCount + columns - 1) / columns;
        const auto denseControls = compactMode || isUsingBuiltInBoost() || stageName == "Delay";
        const auto rowPitch = denseControls ? 104 : 148;
        const auto controlHeight = juce::jmax (selectorCount * 72, rows * rowPitch);
        const auto fileControlsHeight = loadKind == FileKind::none || isUsingBuiltInBoost() || loadKind == FileKind::nam ? 0 : 46;
        return 80 + fileControlsHeight + juce::jmax (rowPitch, controlHeight);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (hasMoveControls)
        {
            dragStartY = event.getScreenPosition().y;
            lastDragStep = 0;
        }
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (! hasMoveControls)
            return;
        const auto step = event.getScreenPosition().y - dragStartY;
        const auto wanted = step / 168;
        while (lastDragStep < wanted) { moveHandler (1); ++lastDragStep; }
        while (lastDragStep > wanted) { moveHandler (-1); --lastDragStep; }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (26, 20);
        auto header = area.removeFromTop (40);
        int sourceIndex = -1;
        for (size_t i = 0; i < controlIds.size(); ++i)
            if (controlIds[i].endsWith ("Source"))
                sourceIndex = static_cast<int> (i);
        const auto rightWidth = (hasEnableControl ? 92 : 0) + (hasMoveControls ? 68 : 0)
                              + (toneCurveButton != nullptr ? 94 : 0) + (expandButton != nullptr ? 110 : 0);
        auto right = header.removeFromRight (rightWidth);
        if (hasMoveControls)
        {
            dragHandle.setBounds (header.removeFromLeft (24).withHeight (32));
            header.removeFromLeft (8);
        }
        if (sourceIndex >= 0)
        {
            heading.setBounds (header.withTrimmedLeft (38).withWidth (96));
            controlWidgets[static_cast<size_t> (sourceIndex)]->setBounds (
                header.withTrimmedLeft (142).withWidth (142).withHeight (30).withY (header.getY() + 3));
        }
        else
            heading.setBounds (header.withTrimmedLeft (38));

        // NAM capture controls belong to NAM profile blocks as well as the NAM
        // source of the Boost block. They must not depend on a Source selector:
        // NAM profile blocks deliberately have no source selector.
        if (loadKind == FileKind::nam && ! isUsingBuiltInBoost())
        {
            auto actions = header.withTrimmedLeft (sourceIndex >= 0 ? 294 : 142);
            const auto gap = 8;
            const auto loadWidth = juce::jmin (160, juce::jmax (112, actions.getWidth() / 6));
            const auto navWidth = 56;
            const auto toneWidth = juce::jmax (120, juce::jmin (220, actions.getWidth() - loadWidth - navWidth * 2 - gap * 4));
            loadButton.setBounds (actions.removeFromLeft (loadWidth).withHeight (34).withY (header.getY() + 1));
            actions.removeFromLeft (gap);
            previousCaptureButton.setBounds (actions.removeFromLeft (navWidth).withHeight (34).withY (header.getY() + 1));
            actions.removeFromLeft (4);
            nextCaptureButton.setBounds (actions.removeFromLeft (navWidth).withHeight (34).withY (header.getY() + 1));
            actions.removeFromLeft (gap);
            tone3000Button.setBounds (actions.removeFromLeft (toneWidth).withHeight (34).withY (header.getY() + 1));
            actions.removeFromLeft (8);
            fileName.setBounds (actions);
            fileName.setTooltip (fileName.getText());
        }
        if (hasEnableControl)
            enableButton.setBounds (right.removeFromLeft (92));
        if (hasMoveControls)
        {
            moveUpButton.setBounds (right.removeFromLeft (32));
            right.removeFromLeft (4);
            moveDownButton.setBounds (right.removeFromLeft (32));
        }
        if (toneCurveButton != nullptr)
            toneCurveButton->setBounds (right.removeFromLeft (88).withY (header.getY() + 4).withHeight (28));
        if (expandButton != nullptr)
            expandButton->setBounds (right.removeFromLeft (104).withY (header.getY() + 4).withHeight (28));

        if (loadKind != FileKind::none && ! isUsingBuiltInBoost())
        {
            if (loadKind != FileKind::nam)
            {
                auto fileRow = area.removeFromTop (40);
                loadButton.setBounds (fileRow.removeFromLeft (130));
                fileRow.removeFromLeft (10);
                fileName.setBounds (fileRow);
                area.removeFromTop (3);
            }
        }

        if (graphicEq != nullptr)
        {
            graphicEq->setBounds (area.reduced (8, 4));
            return;
        }

        if (controlWidgets.empty())
            return;
        constexpr int maxControlWidth = 1320;
        if (area.getWidth() > maxControlWidth)
            area = area.withSizeKeepingCentre (maxControlWidth, area.getHeight());
        const auto denseControls = compactMode || isUsingBuiltInBoost() || stageName == "Delay";
        const auto rowPitch = denseControls ? 104 : 148;
        std::vector<int> selectors;
        std::vector<int> knobs;
        for (size_t i = 0; i < controlWidgets.size(); ++i)
        {
            if (! isControlVisible (i))
                continue;
            if (controlIds[i].endsWith ("Source"))
                continue;
            if (dynamic_cast<juce::ComboBox*> (controlWidgets[i]) != nullptr)
                selectors.push_back (static_cast<int> (i));
            else
                knobs.push_back (static_cast<int> (i));
        }

        if (! selectors.empty())
        {
            const auto selectorWidth = juce::jmin (260, juce::jmax (220, area.getWidth() / 6));
            auto selectorArea = area.removeFromLeft (selectorWidth).reduced (6, 2);
            for (const auto index : selectors)
            {
                auto cell = selectorArea.removeFromTop (70);
                controlLabels[static_cast<size_t> (index)]->setBounds (cell.removeFromTop (22));
                controlWidgets[static_cast<size_t> (index)]->setBounds (cell.removeFromTop (42));
            }
            area.removeFromLeft (14);
        }

        if (knobs.empty())
            return;
        const auto isNamCapture = stageName.startsWith ("NAM Profile");
        const auto columns = isNamCapture ? juce::jmin (3, static_cast<int> (knobs.size()))
                                         : juce::jmin (selectors.empty() ? 5 : 4, static_cast<int> (knobs.size()));
        const auto cellWidth = area.getWidth() / columns;
        for (size_t position = 0; position < knobs.size(); ++position)
        {
            const auto index = static_cast<size_t> (knobs[position]);
            const auto row = static_cast<int> (position) / columns;
            const auto column = static_cast<int> (position) % columns;
            auto cell = juce::Rectangle<int> (area.getX() + column * cellWidth,
                                               area.getY() + row * rowPitch,
                                               column + 1 == columns ? area.getWidth() - column * cellWidth : cellWidth,
                                               rowPitch - 6).reduced (8, 3);
            controlLabels[index]->setBounds (cell.removeFromTop (denseControls ? 18 : 22));
            if (dynamic_cast<juce::Slider*> (controlWidgets[index]) != nullptr)
            {
                auto valueArea = cell.removeFromBottom (denseControls ? 17 : 20);
                if (widgetValueLabels[index] != nullptr)
                    widgetValueLabels[index]->setBounds (valueArea);
                const auto knobWidth = juce::jmin (denseControls ? 78 : 102, cell.getWidth());
                auto knob = cell.withSizeKeepingCentre (knobWidth, denseControls ? 68 : 94);
                controlWidgets[index]->setBounds (knob);
            }
            else
                controlWidgets[index]->setBounds (cell.removeFromTop (42));
        }
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (tone3000Logo == nullptr || ! tone3000Button.isVisible() || tone3000Button.getButtonText().isNotEmpty())
            return;

        auto bounds = tone3000Button.getBounds().toFloat().reduced (8.0f, 5.0f);
        const auto logoWidth = juce::jmin (bounds.getWidth() * 0.62f, bounds.getHeight() * 6.56f);
        const auto logoBounds = bounds.removeFromLeft (logoWidth);
        tone3000Logo->drawWithin (g, logoBounds, juce::RectanglePlacement::centred, 1.0f);
        bounds.removeFromLeft (6.0f);
        g.setColour (knobAccent);
        g.setFont (namplifiedFontOptions (14.0f, juce::Font::bold));
        g.drawText ("BROWSE", bounds.toNearestInt(), juce::Justification::centred);
    }

private:
    void comboBoxChanged (juce::ComboBox* box) override
    {
        if (box == nullptr)
            return;
        const auto id = box->getName();
        const auto isBoostModel = stageName == "Boost Pedal" && id == "boostModel";
        if (! isBoostModel)
            return;
        const juce::Component::SafePointer<StageCard> safeThis (this);
        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis != nullptr)
            {
                safeThis->updateConditionalControlVisibility();
                safeThis->resized();
                if (safeThis->contentSizeChangedHandler)
                    safeThis->contentSizeChangedHandler();
            }
        });
    }

    void showToneArchitectureMenu()
    {
        juce::PopupMenu menu;
        menu.addItem (1, "A1 Model Architecture");
        menu.addItem (2, "A2 Model Architecture");
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&tone3000Button),
                            [safeThis = juce::Component::SafePointer<StageCard> (this)] (int result)
        {
            if (safeThis != nullptr && (result == 1 || result == 2))
                safeThis->beginToneSelection (result);
        });
    }

    void beginToneSelection (int architecture)
    {
        if (tone3000Client == nullptr)
            tone3000Client = std::make_unique<tone3000::Client>();

        tone3000Button.setEnabled (false);
        tone3000Button.setButtonText ("OPENING TONE3000...");
        const juce::Component::SafePointer<StageCard> safeThis (this);
        const auto started = tone3000Client->startSelectFlow (architecture,
            [safeThis] (std::vector<tone3000::Model> models, juce::String error) mutable
            {
                if (safeThis == nullptr)
                    return;
                safeThis->tone3000Button.setEnabled (true);
                safeThis->tone3000Button.setButtonText ({});
                if (error.isNotEmpty())
                {
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                            "TONE3000", error);
                    return;
                }
                safeThis->showToneModelMenu (std::move (models));
            });
        if (! started)
        {
            tone3000Button.setEnabled (true);
            tone3000Button.setButtonText ({});
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    "TONE3000", "Could not start the TONE3000 browser. Check that the local callback port is available.");
        }
    }

    void showToneModelMenu (std::vector<tone3000::Model> models)
    {
        if (models.size() == 1)
        {
            toneModels = std::move (models);
            toneModelIndex = 0;
            downloadToneModel (toneModels.front());
            return;
        }

        juce::PopupMenu menu;
        menu.addSectionHeader ("CHOOSE A CAPTURE FOR " + stageName.toUpperCase());
        for (size_t i = 0; i < models.size(); ++i)
        {
            const auto& model = models[i];
            auto title = model.name + "  |  " + model.architecture;
            if (model.creator.isNotEmpty()) title += "  |  " + model.creator;
            if (model.license.isNotEmpty()) title += "  |  " + model.license;
            menu.addItem (static_cast<int> (i + 1), title);
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&tone3000Button),
            [safeThis = juce::Component::SafePointer<StageCard> (this), models = std::move (models)] (int result) mutable
            {
                if (safeThis == nullptr)
                    return;
                if (result <= 0 || result > static_cast<int> (models.size()))
                {
                    if (safeThis->tone3000Client != nullptr)
                        safeThis->tone3000Client->cancel();
                    return;
                }
                safeThis->toneModels = std::move (models);
                safeThis->toneModelIndex = result - 1;
                safeThis->downloadToneModel (safeThis->toneModels[static_cast<size_t> (safeThis->toneModelIndex)]);
            });
    }

    juce::File findDownloadedToneModel (const tone3000::Model& model) const
    {
        const auto slotFolder = slot >= NAMplifiedAudioProcessor::numAmpSlots ? juce::String ("BOOST_NAM")
            : "NAM" + juce::String (juce::jlimit (0, 1, slot) + 1);
        const auto folder = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("NAMplified").getChildFile ("TONE3000").getChildFile (slotFolder)
            .getChildFile (model.toneId);
        for (const auto& metadataFile : folder.findChildFiles (juce::File::findFiles, false, "*.t3k.json"))
        {
            const auto metadata = juce::JSON::parse (metadataFile.loadFileAsString());
            if (metadata.getProperty ("modelUrl", {}).toString() == model.modelUrl)
            {
                auto captureBase = metadataFile.getFileName();
                if (captureBase.endsWithIgnoreCase (".t3k.json"))
                    captureBase = captureBase.dropLastCharacters (9);
                return metadataFile.getSiblingFile (captureBase + ".nam");
            }
        }
        return {};
    }

    void stepCapture (int direction)
    {
        if (! toneModels.empty())
        {
            toneModelIndex = (toneModelIndex + (direction > 0 ? 1 : -1)
                              + static_cast<int> (toneModels.size())) % static_cast<int> (toneModels.size());
            const auto& model = toneModels[static_cast<size_t> (toneModelIndex)];
            const auto localFile = findDownloadedToneModel (model);
            if (localFile.existsAsFile())
            {
                audioProcessor.loadNAMCapture (slot, localFile);
                rememberNAMDirectory (localFile.getParentDirectory());
            }
            else
                downloadToneModel (model);
            return;
        }
        audioProcessor.cycleNAMCapture (slot, direction);
    }

    void downloadToneModel (const tone3000::Model& model)
    {
        if (tone3000Client == nullptr)
            return;
        tone3000Button.setEnabled (false);
        tone3000Button.setButtonText ("DOWNLOADING...");
        fileName.setText ("Downloading selected capture from TONE3000...", juce::dontSendNotification);
        const juce::Component::SafePointer<StageCard> safeThis (this);
        if (! tone3000Client->downloadModel (model, slot,
            [safeThis, model] (juce::File file, juce::String error)
            {
                if (safeThis == nullptr)
                    return;
                safeThis->tone3000Button.setEnabled (true);
                safeThis->tone3000Button.setButtonText ({});
                safeThis->previousCaptureButton.setEnabled (true);
                safeThis->nextCaptureButton.setEnabled (true);
                if (error.isNotEmpty() || ! file.existsAsFile())
                {
                    safeThis->fileName.setText (safeThis->audioProcessor.getNAMCaptureName (safeThis->slot),
                                                juce::dontSendNotification);
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                            "TONE3000", error.isNotEmpty() ? error : "Download failed.");
                    return;
                }
                safeThis->audioProcessor.loadNAMCapture (safeThis->slot, file);
                rememberNAMDirectory (file.getParentDirectory());
                safeThis->tone3000Attribution = "POWERED BY TONE3000 | " + model.toneTitle + " | " + model.name
                    + (model.creator.isNotEmpty() ? " | " + model.creator : "")
                    + (model.license.isNotEmpty() ? " | " + model.license : "");
                safeThis->fileName.setText (safeThis->tone3000Attribution, juce::dontSendNotification);
                safeThis->fileName.setTooltip (safeThis->tone3000Attribution);
            }))
        {
            tone3000Button.setEnabled (true);
            tone3000Button.setButtonText ({});
            previousCaptureButton.setEnabled (true);
            nextCaptureButton.setEnabled (true);
            fileName.setText (audioProcessor.getNAMCaptureName (slot), juce::dontSendNotification);
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    "TONE3000", "Could not start the selected capture download.");
        }
        else
        {
            previousCaptureButton.setEnabled (false);
            nextCaptureButton.setEnabled (false);
        }
    }

    bool isControlVisible (size_t index) const
    {
        if (index >= controlIds.size())
            return true;
        if (stageName.startsWith ("NAM Profile"))
        {
            const auto prefix = "amp" + stageName.fromLastOccurrenceOf (" ", false, false);
            const auto& id = controlIds[index];
            return id == prefix + "In" || id == prefix + "Out"
                || id == prefix + "EqPosition" || id == prefix + "EqBass"
                || id == prefix + "EqMid" || id == prefix + "EqTreble";
        }
        if (stageName != "Boost Pedal")
            return true;
        const auto* source = audioProcessor.parameters.getRawParameterValue ("boostSource");
        const auto useNAM = source != nullptr && source->load() >= 0.5f;
        if (controlIds[index] == "boostSource")
            return true;
        if (controlIds[index] == "boostNAMIn" || controlIds[index] == "boostNAMOut")
            return useNAM;
        if (useNAM)
            return false;
        const auto* model = audioProcessor.parameters.getRawParameterValue ("boostModel");
        const auto use808 = model != nullptr && model->load() >= 0.5f;
        const auto& id = controlIds[index];
        return id == "boostModel"
            || (use808 ? (id == "boost808Drive" || id == "boost808Tone" || id == "boost808Level")
                       : (id == "boostGain" || id == "boostBass" || id == "boostMid" || id == "boostTreble"));
    }

    void updateConditionalControlVisibility()
    {
        for (size_t i = 0; i < controlWidgets.size(); ++i)
        {
            const auto visible = isControlVisible (i);
            controlWidgets[i]->setVisible (visible);
            controlLabels[i]->setVisible (visible && ! controlIds[i].endsWith ("Source"));
            if (widgetValueLabels[i] != nullptr)
                widgetValueLabels[i]->setVisible (visible);
            if (controlIds[i].endsWith ("Source"))
            {
                const auto* source = audioProcessor.parameters.getRawParameterValue (controlIds[i]);
                const auto isSim = source != nullptr && source->load() >= 0.5f;
                if (auto* sourceButton = dynamic_cast<juce::TextButton*> (controlWidgets[i]))
                {
                    sourceButton->setButtonText (isSim ? "NAM CAPTURE" : "BOOST");
                    sourceButton->setColour (juce::TextButton::buttonColourId,
                                             isSim ? juce::Colour (0xff21473f) : juce::Colour::fromRGB (40, 45, 54));
                    sourceButton->setColour (juce::TextButton::textColourOffId,
                                             isSim ? knobAccent : text);
                    sourceButton->setTooltip (isSim ? "NAM capture loaded in this boost slot. Click to switch to the built-in boost."
                                                     : "Built-in Clean Boost / 808 Drive. Click to switch to a NAM capture.");
                }
            }
        }
        if (loadKind == FileKind::nam)
        {
            const auto showCapture = ! isUsingBuiltInBoost();
            loadButton.setVisible (showCapture);
            previousCaptureButton.setVisible (showCapture);
            nextCaptureButton.setVisible (showCapture);
            tone3000Button.setVisible (showCapture);
            fileName.setVisible (showCapture);
        }
    }

    bool isUsingBuiltInBoost() const
    {
        if (loadKind != FileKind::nam || stageName != "Boost Pedal")
            return false;
        const auto* source = audioProcessor.parameters.getRawParameterValue ("boostSource");
        return source == nullptr || source->load() < 0.5f;
    }

    friend class StageTile;
    void drawBlockIcon (juce::Graphics& g, juce::Rectangle<float> bounds);

    static void drawPedalIcon (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour)
    {
        g.setColour (colour);
        g.drawRoundedRectangle (r.reduced (2.0f), 4.0f, 1.8f);
        g.fillEllipse (r.getCentreX() - 2.5f, r.getBottom() - 8.0f, 5.0f, 5.0f);
        for (int i = 0; i < 3; ++i)
            g.fillEllipse (r.getX() + 5.0f + i * 6.0f, r.getY() + 4.0f, 2.5f, 2.5f);
    }

    static void drawPreampIcon (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour)
    {
        g.setColour (colour);
        g.drawRoundedRectangle (r.reduced (2.0f), 3.0f, 1.8f);
        for (int i = 0; i < 3; ++i)
            g.drawEllipse (r.getX() + 5.0f + i * 6.0f, r.getY() + 5.0f, 4.0f, 4.0f, 1.2f);
        g.drawLine (r.getX() + 5.0f, r.getBottom() - 5.0f, r.getRight() - 5.0f, r.getBottom() - 5.0f, 1.4f);
    }

    static void drawAmpIcon (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour)
    {
        g.setColour (colour);
        g.drawRoundedRectangle (r.reduced (2.0f), 3.0f, 1.8f);
        g.drawLine (r.getX() + 4.0f, r.getY() + 6.0f, r.getRight() - 4.0f, r.getY() + 6.0f, 1.2f);
        g.drawEllipse (r.getCentreX() - 5.0f, r.getY() + 9.0f, 10.0f, 10.0f, 1.6f);
        g.drawEllipse (r.getCentreX() - 2.0f, r.getY() + 12.0f, 4.0f, 4.0f, 1.1f);
    }

    static void drawCabinetIcon (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour)
    {
        g.setColour (colour);
        g.drawRoundedRectangle (r.reduced (2.0f), 3.0f, 1.8f);
        g.drawEllipse (r.getX() + 5.0f, r.getY() + 5.0f, 8.0f, 8.0f, 1.4f);
        g.drawEllipse (r.getX() + 5.0f, r.getY() + 14.0f, 8.0f, 7.0f, 1.4f);
    }

    static void drawRoomIRIcon (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour)
    {
        g.setColour (colour);
        g.drawRoundedRectangle (r.reduced (2.0f), 3.0f, 1.8f);
        g.drawRect (r.getX() + 6.0f, r.getY() + 6.0f, 12.0f, 12.0f, 1.4f);
        g.drawLine (r.getX() + 6.0f, r.getY() + 11.0f, r.getX() + 12.0f, r.getY() + 6.0f, 1.2f);
        g.drawLine (r.getX() + 12.0f, r.getY() + 6.0f, r.getRight() - 6.0f, r.getY() + 12.0f, 1.2f);
    }

    static void drawReverbIRIcon (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour)
    {
        g.setColour (colour);
        g.drawRoundedRectangle (r.reduced (2.0f), 3.0f, 1.8f);
        g.drawLine (r.getX() + 4.0f, r.getCentreY(), r.getX() + 8.0f, r.getCentreY(), 2.0f);
        g.drawLine (r.getX() + 8.0f, r.getCentreY() - 5.0f, r.getX() + 8.0f, r.getCentreY() + 5.0f, 1.5f);
        g.drawLine (r.getX() + 12.0f, r.getCentreY() - 3.0f, r.getX() + 12.0f, r.getCentreY() + 3.0f, 1.4f);
        g.drawLine (r.getX() + 16.0f, r.getCentreY() - 1.5f, r.getX() + 16.0f, r.getCentreY() + 1.5f, 1.2f);
    }

    void timerCallback() override
    {
        updateAmpCabDependency();
        if (loadKind == FileKind::nam)
        {
            const auto currentFile = audioProcessor.getNAMCaptureFile (slot);
            const auto currentPath = currentFile.getFullPathName();
            if (currentPath != displayedNAMPath)
            {
                displayedNAMPath = currentPath;
                tone3000Attribution = tone3000AttributionFor (currentFile);
            }
        }
        if (loadKind == FileKind::nam && loadButton.isVisible() == isUsingBuiltInBoost())
        {
            updateConditionalControlVisibility();
            resized();
            if (contentSizeChangedHandler)
                contentSizeChangedHandler();
        }
        if (loadKind == FileKind::nam && ! isUsingBuiltInBoost())
        {
            const auto status = audioProcessor.getNAMCaptureStatus (slot);
            if (status == "Loaded")
                fileName.setText (tone3000Attribution.isNotEmpty() ? tone3000Attribution
                    : ("LOADED: " + audioProcessor.getNAMCaptureName (slot)).toUpperCase(), juce::dontSendNotification);
            else if (status.isNotEmpty())
                fileName.setText (status, juce::dontSendNotification);
            else
                fileName.setText (audioProcessor.getNAMCaptureName (slot), juce::dontSendNotification);
            fileName.setTooltip (fileName.getText());
        }
        repaint();
    }

    void updateAmpCabDependency()
    {
        if (! stageName.startsWith ("NAM Profile") || ! hasEnableControl)
            return;
        enableButton.setEnabled (true);
        enableButton.setButtonText ("ENABLED");
        const auto* ampEnabled = audioProcessor.parameters.getRawParameterValue (enableParameterId);
        enableButton.setToggleState (ampEnabled != nullptr && ampEnabled->load() >= 0.5f,
                                     juce::dontSendNotification);
        enableButton.setTooltip ("The amp can run with or without a cabinet IR. Disable the IR independently to bypass its cabinet processing.");
    }

    NAMplifiedAudioProcessor& audioProcessor;
    NAMplifiedLookAndFeel lookAndFeel;
    juce::String stageName;
    juce::String enableParameterId;
    FileKind loadKind;
    int slot;
    bool hasEnableControl = false;
    bool hasMoveControls = false;
    int dragStartY = 0, lastDragStep = 0;
    std::function<void (int)> moveHandler;
    std::function<void()> contentSizeChangedHandler;
    std::function<void (bool)> fullscreenHandler;
    bool compactMode = false;
    bool isFullscreen = false;
    juce::Label heading, fileName, dragHandle;
    juce::ToggleButton enableButton;
    juce::TextButton loadButton, tone3000Button, previousCaptureButton, nextCaptureButton, moveUpButton, moveDownButton;
    std::unique_ptr<ToneCurveButton> toneCurveButton;
    std::unique_ptr<juce::TextButton> expandButton;
    std::unique_ptr<GraphicEqEditor> graphicEq;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableAttachment;
    std::vector<std::unique_ptr<juce::Label>> controlLabels;
    std::vector<juce::String> controlIds;
    std::vector<std::unique_ptr<juce::Label>> valueLabels;
    std::vector<juce::Component*> controlWidgets;
    std::vector<juce::Label*> widgetValueLabels;
    std::vector<std::unique_ptr<juce::Slider>> sliders;
    std::vector<std::unique_ptr<juce::ComboBox>> comboBoxes;
    std::vector<std::unique_ptr<juce::ToggleButton>> toggles;
    std::vector<std::unique_ptr<juce::TextButton>> sourceButtons;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<tone3000::Client> tone3000Client;
    std::unique_ptr<juce::Drawable> tone3000Logo;
    juce::String tone3000Attribution;
    std::vector<tone3000::Model> toneModels;
    int toneModelIndex = 0;
    juce::String displayedNAMPath;
};

class StageTile final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    StageTile (juce::AudioProcessorValueTreeState& state, juce::String name, juce::String enableId,
               std::function<void (int)> reorder, std::function<void()> select)
        : parameterState (state), stageName (std::move (name)), enableParameterId (std::move (enableId)),
          canReorder (static_cast<bool> (reorder)), reorderHandler (std::move (reorder)),
          selectHandler (std::move (select))
    {
        setName (stageName);
        if (stageName.startsWith ("NAM Profile"))
            setTooltip ("Load a NAM capture into this slot. Capture switching uses other files in the same folder.");
        startTimerHz (4);
    }

    void setSelected (bool selected)
    {
        isSelected = selected;
        repaint();
    }

    void setDragging (bool dragging)
    {
        isDragging = dragging;
        repaint();
    }

    void setParallelRouting (bool parallel, bool sharedIR)
    {
        parallelRouting = parallel;
        parallelSharedIR = sharedIR;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.beginTransparencyLayer (isBlockEnabled() ? 1.0f : 0.32f);
        auto r = getLocalBounds().toFloat();
        const auto cornerRadius = getWidth() < 104 ? 23.0f : 26.0f;
        g.setColour (isSelected ? juce::Colour::fromRGB (37, 49, 66) : tileSurface);
        g.fillRoundedRectangle (r, cornerRadius);
        const auto kind = getModuleKind();
        const auto ioBlock = kind == ModuleKind::input || kind == ModuleKind::output;
        const auto borderColour = isSelected ? selectionAccent
                               : ioBlock ? juce::Colour::fromRGB (72, 79, 92)
                                         : getIconColour (kind);
        g.setColour (borderColour);
        g.drawRoundedRectangle (r.reduced (2.0f), cornerRadius - 2.0f, isSelected ? 4.5f : 4.0f);
        if (isDragging)
        {
            g.setColour (selectionAccent.withAlpha (0.18f));
            g.fillRoundedRectangle (r.reduced (2.0f), cornerRadius - 2.0f);
            g.setColour (selectionAccent);
            g.drawRoundedRectangle (r.reduced (1.0f), cornerRadius - 1.0f, 2.5f);
            const auto verticalBranch = parallelRouting
                                     && (stageName.startsWith ("NAM Profile") || stageName.startsWith ("Cab IR"));
            g.setColour (juce::Colour (0xff70d9b6));
            if (verticalBranch)
            {
                const auto y = dragDirection > 0 ? r.getBottom() - 3.0f : r.getY() + 1.0f;
                g.fillRoundedRectangle (r.getX() + 12.0f, y, r.getWidth() - 24.0f, 3.0f, 1.5f);
            }
            else
            {
                const auto x = dragDirection > 0 ? r.getRight() - 3.0f : r.getX() + 1.0f;
                g.fillRoundedRectangle (x, r.getY() + 12.0f, 3.0f, r.getHeight() - 24.0f, 1.5f);
            }
        }
        const auto iconSize = juce::jmin (30.0f, juce::jmin (r.getWidth(), r.getHeight()) * 0.48f);
        const auto iconBounds = juce::Rectangle<float> (0.0f, 0.0f, iconSize, iconSize)
                                    .withCentre (r.getCentre());
        {
            const juce::Graphics::ScopedSaveState iconState (g);
            g.addTransform (juce::AffineTransform::scale (1.28f, 1.28f,
                                                           iconBounds.getCentreX(), iconBounds.getCentreY()));
            drawModuleIcon (g, iconBounds, getModuleKind());
        }
        if (kind == ModuleKind::nam)
        {
            const auto markerSize = juce::jlimit (15.0f, 19.0f, r.getWidth() * 0.18f);
            auto marker = juce::Rectangle<float> (markerSize, markerSize)
                              .withPosition (r.getRight() - markerSize - 8.0f, r.getY() + 8.0f);
            g.setColour (juce::Colour (0xff12171e).withAlpha (0.96f));
            g.fillEllipse (marker);
            g.setColour (juce::Colour (0xff70d9b6));
            g.drawEllipse (marker, 1.5f);
            g.setFont (namplifiedFontOptions (markerSize * 0.72f, juce::Font::bold));
            g.drawText ("?", marker.translated (0.0f, -0.5f), juce::Justification::centred, false);
        }
        g.endTransparencyLayer();
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        dragStartPosition = event.getScreenPosition();
        lastDragStep = 0;
        isDragging = false;
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (! canReorder)
            return;
        const auto verticalBranch = parallelRouting
                                 && (stageName.startsWith ("NAM Profile") || stageName.startsWith ("Cab IR"));
        const auto drag = verticalBranch ? event.getScreenPosition().y - dragStartPosition.y
                                         : event.getScreenPosition().x - dragStartPosition.x;
        if (drag != 0)
            dragDirection = drag > 0 ? 1 : -1;
        if (std::abs (drag) > 5 && ! isDragging)
        {
            isDragging = true;
            repaint();
        }
        const auto direction = verticalBranch ? 1 : (getY() >= 140 ? -1 : 1);
        const auto wanted = juce::roundToInt ((drag * direction) / (getWidth() * 0.55f));
        while (lastDragStep < wanted) { reorderHandler (1); ++lastDragStep; }
        while (lastDragStep > wanted) { reorderHandler (-1); --lastDragStep; }
    }

    void mouseUp (const juce::MouseEvent& event) override
    {
        isDragging = false;
        repaint();
        if (event.getDistanceFromDragStart() < 5 && selectHandler)
            selectHandler();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (enableParameterId.isEmpty())
            return;
        if (auto* parameter = parameterState.getParameter (enableParameterId))
        {
            const auto nextValue = isBlockEnabled() ? 0.0f : 1.0f;
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (nextValue));
            parameter->endChangeGesture();
            repaint();
        }
    }

public:
    enum class ModuleKind { pedal, noiseGate, compressor, boost, equalizer, nam, ir, input, output, delay, reverb, chorus };

    static ModuleKind getKindForStageName (const juce::String& name)
    {
        if (name == "Input") return ModuleKind::input;
        if (name == "Output") return ModuleKind::output;
        if (name.startsWith ("NAM Profile")) return ModuleKind::nam;
        if (name.startsWith ("Cab IR")) return ModuleKind::ir;
        if (name == "Noise Gate") return ModuleKind::noiseGate;
        if (name.containsIgnoreCase ("Compressor")) return ModuleKind::compressor;
        if (name.containsIgnoreCase ("EQ")) return ModuleKind::equalizer;
        if (name.containsIgnoreCase ("Boost")) return ModuleKind::boost;
        if (name == "Delay") return ModuleKind::delay;
        if (name == "Reverb") return ModuleKind::reverb;
        if (name == "Chorus") return ModuleKind::chorus;
        return ModuleKind::pedal;
    }

    static void drawStageIcon (juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& name)
    {
        const juce::Graphics::ScopedSaveState state (g);
        g.addTransform (juce::AffineTransform::scale (1.28f, 1.28f, bounds.getCentreX(), bounds.getCentreY()));
        drawModuleIcon (g, bounds, getKindForStageName (name));
    }

private:

    bool isBlockEnabled() const
    {
        if (enableParameterId.isEmpty()) return true;
        if (const auto* value = parameterState.getRawParameterValue (enableParameterId))
            return value->load() >= 0.5f;
        return true;
    }

    ModuleKind getModuleKind() const
    {
        if (stageName == "Input") return ModuleKind::input;
        if (stageName == "Output") return ModuleKind::output;
        if (stageName.startsWith ("NAM Profile")) return ModuleKind::nam;
        if (stageName.startsWith ("Cab IR")) return ModuleKind::ir;
        if (stageName == "Noise Gate") return ModuleKind::noiseGate;
        if (stageName.containsIgnoreCase ("Compressor")) return ModuleKind::compressor;
        if (stageName.containsIgnoreCase ("EQ")) return ModuleKind::equalizer;
        if (stageName == "Boost Pedal") return ModuleKind::boost;
        if (stageName == "Delay") return ModuleKind::delay;
        if (stageName == "Reverb") return ModuleKind::reverb;
        if (stageName == "Chorus") return ModuleKind::chorus;
        return ModuleKind::pedal;
    }

    static void drawModuleIcon (juce::Graphics& g, juce::Rectangle<float> r, ModuleKind kind)
    {
        const auto ink = getIconColour (kind);
        g.setColour (ink);
        if (kind == ModuleKind::noiseGate)
        {
            const auto box = r.reduced (2.0f, 5.0f);
            g.drawRoundedRectangle (box, 4.0f, 1.8f);
            const auto mid = r.getCentreY();
            juce::Path waveform;
            waveform.startNewSubPath (r.getX() + 5.0f, mid);
            waveform.lineTo (r.getX() + 9.0f, mid);
            waveform.lineTo (r.getX() + 12.0f, mid - 8.0f);
            waveform.lineTo (r.getX() + 16.0f, mid + 8.0f);
            waveform.lineTo (r.getX() + 19.0f, mid);
            waveform.lineTo (r.getX() + 22.0f, mid);
            waveform.lineTo (r.getX() + 24.0f, mid - 3.0f);
            waveform.lineTo (r.getX() + 26.0f, mid);
            waveform.lineTo (r.getRight() - 5.0f, mid);
            g.strokePath (waveform, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded));
            g.drawLine (r.getX() + 21.0f, r.getY() + 5.0f, r.getX() + 21.0f, r.getBottom() - 5.0f, 1.5f);
        }
        else if (kind == ModuleKind::compressor)
        {
            const auto centreY = r.getCentreY();
            juce::Path inputWave;
            inputWave.startNewSubPath (r.getX() + 2.0f, centreY);
            inputWave.lineTo (r.getX() + 4.0f, centreY - 3.0f);
            inputWave.lineTo (r.getX() + 6.0f, centreY + 5.0f);
            inputWave.lineTo (r.getX() + 8.0f, centreY - 7.0f);
            inputWave.lineTo (r.getX() + 10.0f, centreY + 7.0f);
            inputWave.lineTo (r.getX() + 12.0f, centreY - 5.0f);
            inputWave.lineTo (r.getX() + 14.0f, centreY);
            g.strokePath (inputWave, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved,
                                                           juce::PathStrokeType::rounded));
            g.drawLine (r.getX() + 14.5f, centreY, r.getX() + 19.0f, centreY, 1.8f);
            g.drawLine (r.getX() + 16.5f, centreY - 2.0f, r.getX() + 19.0f, centreY, 1.8f);
            g.drawLine (r.getX() + 16.5f, centreY + 2.0f, r.getX() + 19.0f, centreY, 1.8f);
            juce::Path outputWave;
            outputWave.startNewSubPath (r.getX() + 20.0f, centreY);
            outputWave.lineTo (r.getX() + 22.0f, centreY - 2.0f);
            outputWave.lineTo (r.getX() + 24.0f, centreY + 3.0f);
            outputWave.lineTo (r.getX() + 26.0f, centreY - 3.0f);
            outputWave.lineTo (r.getRight() - 2.0f, centreY);
            g.strokePath (outputWave, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved,
                                                            juce::PathStrokeType::rounded));
        }
        else if (kind == ModuleKind::boost)
            drawBoostPedalIcon (g, r, ink);
        else if (kind == ModuleKind::equalizer)
        {
            juce::Path curve;
            curve.startNewSubPath (r.getX() + 2.0f, r.getCentreY());
            curve.cubicTo (r.getX() + 8.0f, r.getCentreY(), r.getX() + 7.0f, r.getY() + 2.0f,
                           r.getCentreX(), r.getCentreY());
            curve.cubicTo (r.getRight() - 7.0f, r.getBottom() - 2.0f, r.getRight() - 8.0f,
                           r.getCentreY(), r.getRight() - 2.0f, r.getCentreY());
            g.strokePath (curve, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
            g.drawLine (r.getX() + 3.0f, r.getBottom() - 3.0f, r.getRight() - 3.0f, r.getBottom() - 3.0f, 1.0f);
        }
        else if (kind == ModuleKind::pedal)
        {
            g.drawRoundedRectangle (r.reduced (2.0f), 4.0f, 1.5f);
            for (int i = 0; i < 3; ++i)
                g.drawEllipse (r.getX() + 4.0f + i * 5.0f, r.getY() + 4.0f, 3.0f, 3.0f, 1.2f);
            g.drawEllipse (r.getCentreX() - 2.3f, r.getY() + 12.0f, 4.6f, 4.6f, 1.4f);
        }
        else if (kind == ModuleKind::nam)
        {
            const auto signet = r.reduced (1.0f);
            g.setColour (juce::Colour (0xff68d9b4));
            g.fillRoundedRectangle (signet, signet.getWidth() * 0.27f);
            juce::Path monogram;
            monogram.startNewSubPath (signet.getX() + signet.getWidth() * 0.27f,
                                      signet.getY() + signet.getHeight() * 0.73f);
            monogram.lineTo (signet.getX() + signet.getWidth() * 0.27f,
                             signet.getY() + signet.getHeight() * 0.27f);
            monogram.lineTo (signet.getX() + signet.getWidth() * 0.73f,
                             signet.getY() + signet.getHeight() * 0.73f);
            monogram.lineTo (signet.getX() + signet.getWidth() * 0.73f,
                             signet.getY() + signet.getHeight() * 0.27f);
            g.setColour (juce::Colour (0xff141920));
            g.strokePath (monogram, juce::PathStrokeType (juce::jmax (2.0f, signet.getWidth() * 0.09f),
                                                           juce::PathStrokeType::curved,
                                                           juce::PathStrokeType::rounded));
        }
        else if (kind == ModuleKind::ir)
        {
            g.drawEllipse (r.getX() + 2.0f, r.getY() + 2.0f, r.getWidth() - 4.0f, r.getHeight() - 4.0f, 1.5f);
            g.drawEllipse (r.getCentreX() - 4.0f, r.getCentreY() - 4.0f, 8.0f, 8.0f, 1.4f);
            g.drawEllipse (r.getCentreX() - 1.3f, r.getCentreY() - 1.3f, 2.6f, 2.6f, 1.2f);
        }
        else if (kind == ModuleKind::delay)
        {
            const auto centreY = r.getCentreY();
            for (int echo = 0; echo < 3; ++echo)
            {
                const auto x = r.getX() + 5.0f + 7.0f * echo;
                const auto height = 10.0f - 2.5f * echo;
                g.drawLine (x, centreY + height, x, centreY - height, 1.5f);
                g.drawLine (x - 2.5f, centreY - height + 3.0f, x, centreY - height, 1.5f);
                g.drawLine (x + 2.5f, centreY - height + 3.0f, x, centreY - height, 1.5f);
            }
        }
        else if (kind == ModuleKind::reverb)
        {
            juce::Path room;
            room.startNewSubPath (r.getX() + 4.0f, r.getBottom() - 3.0f);
            room.lineTo (r.getX() + 4.0f, r.getY() + 4.0f);
            room.lineTo (r.getRight() - 4.0f, r.getY() + 4.0f);
            room.lineTo (r.getRight() - 4.0f, r.getBottom() - 3.0f);
            g.strokePath (room, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
            g.fillEllipse (r.getCentreX() - 1.7f, r.getCentreY() - 1.7f, 3.4f, 3.4f);
            for (int echo = 0; echo < 3; ++echo)
            {
                const auto radius = 5.0f + 3.0f * echo;
                juce::Path reflection;
                reflection.addCentredArc (r.getCentreX(), r.getCentreY(), radius, radius, 0.0f,
                                          -1.25f, 1.25f, true);
                g.strokePath (reflection, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved,
                                                                 juce::PathStrokeType::rounded));
            }
        }
        else if (kind == ModuleKind::chorus)
        {
            for (int voice = 0; voice < 2; ++voice)
            {
                juce::Path wave;
                for (int step = 0; step <= 24; ++step)
                {
                    const auto x = r.getX() + 3.0f + (r.getWidth() - 6.0f) * static_cast<float> (step) / 24.0f;
                    const auto phase = static_cast<float> (step) * juce::MathConstants<float>::twoPi / 24.0f
                                     + static_cast<float> (voice) * 1.2f;
                    const auto y = r.getCentreY() + std::sin (phase) * 6.0f;
                    if (step == 0) wave.startNewSubPath (x, y);
                    else wave.lineTo (x, y);
                }
                g.strokePath (wave, juce::PathStrokeType (voice == 0 ? 1.7f : 1.1f,
                                                           juce::PathStrokeType::curved,
                                                           juce::PathStrokeType::rounded));
            }
        }
        else if (kind == ModuleKind::input || kind == ModuleKind::output)
        {
            g.drawRoundedRectangle (r.reduced (2.0f, 5.0f), 3.0f, 1.5f);
            const auto mid = r.getCentreY();
            const auto left = kind == ModuleKind::input;
            const auto startX = left ? r.getX() + 5.0f : r.getRight() - 5.0f;
            const auto endX = left ? r.getRight() - 5.0f : r.getX() + 5.0f;
            const auto tipX = left ? r.getRight() - 4.0f : r.getX() + 4.0f;
            const auto wingX = left ? tipX - 4.0f : tipX + 4.0f;
            g.drawLine (startX, mid, endX, mid, 1.5f);
            g.drawLine (wingX, mid - 3.0f, tipX, mid, 1.5f);
            g.drawLine (wingX, mid + 3.0f, tipX, mid, 1.5f);
        }
    }

    static juce::Colour getIconColour (ModuleKind kind)
    {
        switch (kind)
        {
            case ModuleKind::input:
            case ModuleKind::output: return juce::Colour (0xffaab6c9);
            case ModuleKind::noiseGate: return juce::Colour (0xff69d9cf);
            case ModuleKind::compressor: return juce::Colour (0xffff9b5b);
            case ModuleKind::boost: return juce::Colour (0xffffd06f);
            case ModuleKind::equalizer: return juce::Colour (0xffb69cff);
            case ModuleKind::nam: return juce::Colour (0xff68d9b4);
            case ModuleKind::ir: return juce::Colour (0xffec8ed2);
            case ModuleKind::delay: return juce::Colour (0xff5bc5ef);
            case ModuleKind::reverb: return juce::Colour (0xffa792f5);
            case ModuleKind::chorus: return juce::Colour (0xffef8fb4);
            case ModuleKind::pedal: return juce::Colour (0xff80d790);
        }
        return juce::Colour (0xff70d9b6);
    }

    void timerCallback() override
    {
        repaint();
    }

    juce::AudioProcessorValueTreeState& parameterState;
    juce::String stageName;
    juce::String enableParameterId;
    bool canReorder = false, isSelected = false, isDragging = false;
    juce::Point<int> dragStartPosition;
    int lastDragStep = 0;
    int dragDirection = 1;
    bool parallelRouting = false, parallelSharedIR = false;
    std::function<void (int)> reorderHandler;
    std::function<void()> selectHandler;
    juce::TextButton moveUp, moveDown;
};

void StageCard::drawBlockIcon (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    StageTile::drawStageIcon (g, bounds, stageName);
}

class StageTileList final : public juce::Component
{
public:
    StageTileList() = default;

    void setParallelRouting (bool shouldBeParallel, bool useSharedIR)
    {
        if (parallelRouting == shouldBeParallel && parallelSharedIR == useSharedIR)
            return;
        parallelRouting = shouldBeParallel;
        parallelSharedIR = useSharedIR;
        for (auto* tile : tiles)
        {
            tile->setParallelRouting (parallelRouting, parallelSharedIR);
            tile->setVisible (! (parallelRouting && parallelSharedIR && tile->getName() == "Cab IR 2"));
        }
        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto wire = juce::Colour (0xff70d9b6);
        g.setFont (namplifiedFontOptions (15.0f, juce::Font::bold));
        g.setColour (secondaryText);
        if (parallelRouting)
        {
            const auto tileWidth = juce::jmax (90, juce::jmin (92, juce::roundToInt ((getWidth() - 20) / 8.0f) - 12));
            const auto postY = 50 + 2 * (tileWidth + 12) + 10 + tileWidth + 8;
            g.drawText ("POST FX  /  OUTPUT", 10, postY, getWidth() - 20, 18, juce::Justification::centredLeft);
            drawParallelWiring (g, wire);
            return;
        }

        drawSerialWiring (g, wire);
    }

    void setOrderedTiles (std::vector<StageTile*> ordered)
    {
        tiles = std::move (ordered);
        for (auto* tile : tiles)
        {
            tile->setParallelRouting (parallelRouting, parallelSharedIR);
            tile->setVisible (! (parallelRouting && parallelSharedIR && tile->getName() == "Cab IR 2"));
        }
        resized();
    }

    int getRequiredWidth() const { return 48 + 8 * 132; }
    int getRequiredHeight() const { return parallelRouting ? 394 : 300; }

    void resized() override
    {
        constexpr int sideInset = 24;
        const auto contentWidth = getWidth() - 2 * sideInset;
        const auto pitch = static_cast<float> (contentWidth) / 8.0f;
        const auto maxTileSize = parallelRouting ? 92 : 102;
        const auto width = juce::jmax (90, juce::jmin (maxTileSize, juce::roundToInt (pitch) - 12));
        if (parallelRouting)
        {
            const auto tileWidth = width;
            const auto tileHeight = tileWidth;
            const auto branchPitch = tileHeight + 12;
            const auto branchY = 50;
            const auto postRowY = branchY + 2 * branchPitch + 10;
            const auto branchX = sideInset + juce::roundToInt (contentWidth * 0.63f);
            const auto irX = sideInset + juce::roundToInt (contentWidth * 0.80f);
            int preIndex = 0, namIndex = 0, irIndex = 0, postIndex = 0;
            int postCount = 0;
            for (auto* tile : tiles)
                if (isPostEffect (tile->getName())) ++postCount;
            const auto lowerSlots = juce::jmax (1, postCount + 1);
            const auto lowerSpan = static_cast<float> (contentWidth - tileWidth) / lowerSlots;
            for (auto* tile : tiles)
            {
                const auto name = tile->getName();
                if (name == "Input")
                    tile->setBounds (sideInset, 24, tileWidth, tileHeight);
                else if (name == "Noise Gate" || name == "1176-Style FET Compressor"
                         || name == "Boost Pedal" || name == "PreEQ")
                    tile->setBounds (sideInset + juce::roundToInt ((preIndex++ + 1) * pitch), 24, tileWidth, tileHeight);
                else if (name.startsWith ("NAM Profile"))
                    tile->setBounds (branchX, branchY + namIndex++ * branchPitch, tileWidth, tileHeight);
                else if (name.startsWith ("Cab IR"))
                    tile->setBounds (irX, branchY + irIndex++ * branchPitch, tileWidth, tileHeight);
                else if (name == "Output")
                    tile->setBounds (sideInset, postRowY, tileWidth, tileHeight);
                else
                {
                    const auto slot = postCount - postIndex++;
                    tile->setBounds (sideInset + juce::roundToInt (slot * lowerSpan), postRowY, tileWidth, tileHeight);
                }
            }
            return;
        }
        StageTile* input = findTile ("Input");
        StageTile* output = findTile ("Output");
        std::vector<StageTile*> pre, nams, irs, post;
        for (auto* tile : tiles)
        {
            const auto name = tile->getName();
            if (name == "Noise Gate" || name == "1176-Style FET Compressor" || name == "Boost Pedal" || name == "PreEQ") pre.push_back (tile);
            else if (name.startsWith ("NAM Profile")) nams.push_back (tile);
            else if (name.startsWith ("Cab IR")) irs.push_back (tile);
            else if (isPostEffect (name)) post.push_back (tile);
        }
        if (input != nullptr) input->setBounds (sideInset, 24, width, width);
        int preColumn = 1;
        for (auto* tile : pre) tile->setBounds (sideInset + juce::roundToInt (preColumn++ * pitch), 24, width, width);
        int namColumn = 5;
        for (auto* tile : nams) tile->setBounds (sideInset + juce::roundToInt (namColumn++ * pitch), 24, width, width);
        const auto lowerY = 24 + width + 24;
        int irIndex = 0;
        for (auto* tile : irs) tile->setBounds (sideInset + juce::roundToInt (7 * pitch), irIndex++ == 0 ? 24 : lowerY, width, width);
        int postColumn = static_cast<int> (post.size());
        for (auto* tile : post) tile->setBounds (sideInset + juce::roundToInt (postColumn-- * pitch), lowerY, width, width);
        if (output != nullptr) output->setBounds (sideInset, lowerY, width, width);
    }

private:
    static bool isPostEffect (const juce::String& name)
    {
        return name == "Delay" || name == "Reverb" || name == "Chorus" || name == "Post EQ";
    }

    static int getNumberedSlot (const juce::String& name)
    {
        return name.fromLastOccurrenceOf (" ", false, false).getIntValue();
    }

    StageTile* findTile (const juce::String& name) const
    {
        for (auto* tile : tiles)
            if (tile->getName() == name) return tile;
        return nullptr;
    }

    std::vector<StageTile*> namedTiles (const juce::String& prefix) const
    {
        std::vector<StageTile*> found;
        for (auto* tile : tiles)
            if (tile->isVisible() && tile->getName().startsWith (prefix)) found.push_back (tile);
        return found;
    }

    static void drawArrow (juce::Graphics& g, juce::Point<float> start, juce::Point<float> end,
                           juce::Colour colour, float thickness = 2.5f)
    {
        g.setColour (colour);
        juce::Path path;
        path.startNewSubPath (start);
        path.lineTo (end);
        g.strokePath (path, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        const auto angle = std::atan2 (end.y - start.y, end.x - start.x);
        constexpr float headLength = 13.0f, halfWidth = 6.2f;
        const juce::Point<float> base (end.x - headLength * std::cos (angle),
                                       end.y - headLength * std::sin (angle));
        const juce::Point<float> normal (-std::sin (angle), std::cos (angle));
        juce::Path head;
        head.startNewSubPath (end);
        head.lineTo (base + normal * halfWidth);
        head.lineTo (base - normal * halfWidth);
        head.closeSubPath();
        g.fillPath (head);
    }

    static void drawBus (juce::Graphics& g, juce::Point<float> start, juce::Point<float> end,
                         juce::Colour colour, float thickness = 2.5f)
    {
        g.setColour (colour);
        g.drawLine (start.x, start.y, end.x, end.y, thickness);
    }

    static juce::Point<float> leftCentre (juce::Component* c)
    {
        return { static_cast<float> (c->getX()), static_cast<float> (c->getY() + c->getHeight() / 2) };
    }

    static juce::Point<float> rightCentre (juce::Component* c)
    {
        return { static_cast<float> (c->getRight()), static_cast<float> (c->getY() + c->getHeight() / 2) };
    }

    void drawSerialWiring (juce::Graphics& g, juce::Colour wire)
    {
        auto* input = findTile ("Input");
        auto* output = findTile ("Output");
        const auto nams = namedTiles ("NAM Profile");
        const auto irs = namedTiles ("Cab IR");
        std::vector<StageTile*> pre, post;
        for (auto* tile : tiles)
        {
            const auto name = tile->getName();
            if (name == "Noise Gate" || name == "1176-Style FET Compressor" || name == "Boost Pedal" || name == "PreEQ") pre.push_back (tile);
            else if (isPostEffect (name)) post.push_back (tile);
        }
        if (input == nullptr || output == nullptr || nams.empty() || irs.empty() || post.empty()) return;

        std::vector<StageTile*> ampChain { input };
        ampChain.insert (ampChain.end(), pre.begin(), pre.end());
        ampChain.insert (ampChain.end(), nams.begin(), nams.end());
        for (size_t i = 0; i + 1 < ampChain.size(); ++i)
            drawArrow (g, rightCentre (ampChain[i]), leftCentre (ampChain[i + 1]), wire);

        const auto irInX = static_cast<float> (irs.front()->getX() - 18);
        const auto irOutX = static_cast<float> (irs.front()->getRight() + 18);
        std::vector<float> branchY;
        for (auto* ir : irs)
        {
            const auto y = static_cast<float> (ir->getY() + ir->getHeight() / 2);
            branchY.push_back (y);
        }
        const auto splitX = irInX - 14.0f;
        const auto ampOutput = rightCentre (ampChain.back());
        const auto branchTop = *std::min_element (branchY.begin(), branchY.end());
        const auto branchBottom = *std::max_element (branchY.begin(), branchY.end());
        auto irBottomEdge = 0.0f;
        for (auto* ir : irs)
            irBottomEdge = juce::jmax (irBottomEdge, static_cast<float> (ir->getBottom()));
        drawBus (g, ampOutput, { splitX, ampOutput.y }, wire);
        drawBus (g, { splitX, branchTop }, { splitX, branchBottom }, wire);
        for (auto* ir : irs)
            drawArrow (g, { splitX, static_cast<float> (ir->getY() + ir->getHeight() / 2) }, leftCentre (ir), wire);

        const auto mergeX = static_cast<float> (getWidth() - 12);
        drawBus (g, { irOutX, branchTop }, { mergeX, branchTop }, wire);
        drawBus (g, { irOutX, branchBottom }, { mergeX, branchBottom }, wire);
        drawBus (g, { mergeX, branchTop }, { mergeX, branchBottom }, wire);
        for (auto* ir : irs)
            drawArrow (g, rightCentre (ir), { mergeX, static_cast<float> (ir->getY() + ir->getHeight() / 2) }, wire);
        const auto postInput = rightCentre (post.front());
        const auto routeY = irBottomEdge + 12.0f;
        const auto postRouteX = static_cast<float> (post.front()->getRight() + 26);
        drawBus (g, { mergeX, branchBottom }, { mergeX, routeY }, wire);
        drawBus (g, { postRouteX, routeY }, { mergeX, routeY }, wire);
        drawBus (g, { postRouteX, routeY }, { postRouteX, postInput.y }, wire);
        drawArrow (g, { postRouteX, postInput.y }, { postInput.x + 2.0f, postInput.y }, wire);
        for (size_t i = 0; i + 1 < post.size(); ++i)
            drawArrow (g, leftCentre (post[i]), rightCentre (post[i + 1]), wire);
        drawArrow (g, leftCentre (post.back()), rightCentre (output), wire);
    }

    void drawParallelWiring (juce::Graphics& g, juce::Colour wire)
    {
        auto* input = findTile ("Input");
        if (input == nullptr) return;
        std::vector<StageTile*> pre;
        for (auto* tile : tiles)
        {
            if (tile == input || tile->getName() == "Noise Gate" || tile->getName() == "1176-Style FET Compressor"
                || tile->getName() == "Boost Pedal" || tile->getName() == "PreEQ")
                pre.push_back (tile);
            else if (tile->getName().startsWith ("NAM Profile"))
                break;
        }
        for (size_t i = 0; i + 1 < pre.size(); ++i)
            drawArrow (g, rightCentre (pre[i]), leftCentre (pre[i + 1]), wire);

        const auto nams = namedTiles ("NAM Profile");
        if (nams.empty()) return;
        const auto splitX = static_cast<float> (nams.front()->getX() - 20);
        const auto topY = static_cast<float> (nams.front()->getY() + nams.front()->getHeight() / 2);
        const auto bottomY = static_cast<float> (nams.back()->getY() + nams.back()->getHeight() / 2);
        const auto inputY = static_cast<float> (pre.back()->getY() + pre.back()->getHeight() / 2);
        const juce::Point<float> splitNode (splitX, inputY);
        drawBus (g, rightCentre (pre.back()), splitNode, wire);
        drawBus (g, { splitX, juce::jmin (topY, inputY) }, { splitX, juce::jmax (bottomY, inputY) }, wire);
        for (auto* nam : nams)
            drawArrow (g, { splitX, static_cast<float> (nam->getY() + nam->getHeight() / 2) }, leftCentre (nam), wire);

        const auto irs = namedTiles ("Cab IR");

        const auto post = [&]
        {
            std::vector<StageTile*> found;
            for (auto* tile : tiles)
                if (isPostEffect (tile->getName())) found.push_back (tile);
            return found;
        }();
        auto* output = findTile ("Output");
        if (irs.empty() || post.empty() || output == nullptr) return;

        const auto irOutX = static_cast<float> (irs.front()->getRight() + 18);
        std::vector<float> irCentres;
        for (auto* ir : irs)
            irCentres.push_back (static_cast<float> (ir->getY() + ir->getHeight() / 2));
        const auto irTop = *std::min_element (irCentres.begin(), irCentres.end());
        const auto irBottom = *std::max_element (irCentres.begin(), irCentres.end());
        const auto irMid = (irTop + irBottom) * 0.5f;
        if (parallelSharedIR)
        {
            const auto joinX = static_cast<float> (irs.front()->getX() - 20);
            const auto joinTop = juce::jmin (irMid, topY, bottomY);
            const auto joinBottom = juce::jmax (irMid, topY, bottomY);
            for (auto* nam : nams)
            {
                const auto from = rightCentre (nam);
                drawArrow (g, from, { joinX, from.y }, wire);
            }
            drawBus (g, { joinX, joinTop }, { joinX, joinBottom }, wire);
            drawArrow (g, { joinX, irMid }, leftCentre (irs.front()), wire);
        }
        else
        {
            for (auto* nam : nams)
            {
                const auto slot = getNumberedSlot (nam->getName());
                auto* matchingIR = findTile ("Cab IR " + juce::String (slot));
                if (matchingIR == nullptr) continue;
                const auto from = rightCentre (nam);
                const auto to = leftCentre (matchingIR);
                const auto elbowX = (from.x + to.x) * 0.5f;
                drawBus (g, from, { elbowX, from.y }, wire);
                drawBus (g, { elbowX, from.y }, { elbowX, to.y }, wire);
                drawArrow (g, { elbowX, to.y }, to, wire);
            }
        }

        drawBus (g, { irOutX, irTop }, { irOutX, irBottom }, wire);
        for (auto* ir : irs)
            drawArrow (g, rightCentre (ir), { irOutX, static_cast<float> (ir->getY() + ir->getHeight() / 2) }, wire);

        const auto lowerY = static_cast<float> (post.front()->getY() + post.front()->getHeight() / 2);
        drawBus (g, { irOutX, irMid }, { irOutX, lowerY }, wire);
        drawArrow (g, { irOutX, lowerY }, { rightCentre (post.front()).x + 2.0f, lowerY }, wire);
        for (size_t i = 0; i + 1 < post.size(); ++i)
            drawArrow (g, leftCentre (post[i]), rightCentre (post[i + 1]), wire);
        drawArrow (g, leftCentre (post.back()), rightCentre (output), wire);
    }

    std::vector<StageTile*> tiles;
    bool parallelRouting = false;
    bool parallelSharedIR = false;
};

class ChainControls final : public juce::Component, private juce::Timer
{
public:
    explicit ChainControls (NAMplifiedAudioProcessor& processor, std::function<void (bool)> fullscreenChanged)
        : audioProcessor (processor), fullscreenChangedHandler (std::move (fullscreenChanged))
    {
        startTimerHz (30);
        addAndMakeVisible (chainViewport);
        chainViewport.setViewedComponent (tileList.get(), false);
        chainViewport.setScrollBarsShown (false, false);
        chainViewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
        addAndMakeVisible (detailsViewport);
        detailsViewport.setScrollBarsShown (true, false);

        addStage ("Input", {}, {{ "inputGain", "Trim (dB)" }});
        addStage ("Noise Gate", "gateOn", {{ "gateThreshold", "Threshold" }}, FileKind::none, 0, mover (true, 0));
        addStage ("1176-Style FET Compressor", "compOn",
                  {{ "compSustain", "Input (dB)", false, {}, false, true },
                   { "compBlend", "Blend", true }, { "compVolume", "Output (dB)" }},
                  FileKind::none, 0, mover (true, 1),
                  {{ "compSustain", "Input (dB)", false, {}, false, true },
                   { "compAttack", "Attack (ms)" }, { "compRelease", "Release (ms)" },
                   { "compRatioMode", "Ratio", false, juce::StringArray { "4:1", "8:1", "12:1", "20:1", "All Buttons In" } },
                   { "compTone", "Tone (dB)" }, { "compBlend", "Blend", true },
                   { "compVolume", "Output (dB)" }, { "compEQ", "EQ", true }});
        addStage ("Boost Pedal", "boostOn",
                  {{ "boostSource", "Source" }, { "boostGain", "Post Volume (dB)" },
                   { "boostNAMIn", "NAM Input (dB)" }, { "boostNAMOut", "NAM Output (dB)" }},
                  FileKind::nam, NAMplifiedAudioProcessor::boostNAMSlot, mover (true, 2),
                  {{ "boostModel", "Model", false, juce::StringArray { "Clean Boost", "808 Drive" } },
                   { "boostGain", "Post Volume (dB)" }, { "boostBass", "Bass (dB)" },
                   { "boostMid", "Mid (dB)" }, { "boostTreble", "Treble (dB)" },
                   { "boost808Drive", "Drive (dB)" }, { "boost808Tone", "Tone (Hz)" },
                   { "boost808Level", "Level (dB)" },
                   { "boostNAMIn", "NAM Input (dB)" }, { "boostNAMOut", "NAM Output (dB)" }});
        addStage ("PreEQ", "preEqOn",
                  {{ "preLow", "Low (dB)" }, { "preMid", "Mid (dB)" }, { "preHigh", "High (dB)" }},
                  FileKind::none, 0, mover (true, 3),
                  {{ "preLow", "80 Hz" }, { "preBand2", "250 Hz" }, { "preMid", "800 Hz" },
                   { "preBand4", "2.5 kHz" }, { "preHigh", "8 kHz" }});

        for (int slot = 0; slot < NAMplifiedAudioProcessor::numAmpSlots; ++slot)
        {
            const auto number = juce::String (slot + 1);
            const auto prefix = "amp" + number;
            addStage ("NAM Profile " + number, "amp" + number + "On",
                      {{ prefix + "EqPosition", "EQ Position", false, juce::StringArray { "Pre", "Post" } },
                       { prefix + "EqBass", "Bass" }, { prefix + "EqMid", "Mid" },
                       { prefix + "EqTreble", "Treble" },
                       { prefix + "In", "Input (dB)" }, { prefix + "Out", "Output (dB)" }},
                      FileKind::nam, slot, ampMover (slot));
        }

        for (int slot = 0; slot < NAMplifiedAudioProcessor::numIRSlots; ++slot)
        {
            const auto number = juce::String (slot + 1);
            addStage ("Cab IR " + number, "ir" + number + "On", {},
                      FileKind::impulseResponse, slot, {},
                      {{ ("ir" + number + "ShapeLow").toRawUTF8(), "Low (dB)" },
                       { ("ir" + number + "ShapeLowFreq").toRawUTF8(), "Low Frequency (Hz)" },
                       { ("ir" + number + "ShapeMid").toRawUTF8(), "Mid (dB)" },
                       { ("ir" + number + "ShapeMidFreq").toRawUTF8(), "Mid Frequency (Hz)" },
                       { ("ir" + number + "ShapeHigh").toRawUTF8(), "High (dB)" },
                       { ("ir" + number + "ShapeHighFreq").toRawUTF8(), "High Frequency (Hz)" }});
        }

        addStage ("Delay", "delayOn",
                  {{ "delayTime", "Time (ms)" }, { "delayFeedback", "Feedback", true }, { "delayMix", "Mix", true }},
                  FileKind::none, 0, mover (false, 0),
                  {{ "delayType", "Character", false, juce::StringArray { "Digital", "Analog" } },
                   { "delayMode", "Routing", false, juce::StringArray { "Mono", "Stereo", "Ping Pong" } },
                   { "delayTone", "Tone (Hz)" },
                   { "delayHighPass", "High-Pass (Hz)" }, { "delayLowPass", "Low-Pass (Hz)" },
                   { "delayDuck", "Ducking", true }, { "delayDuckThreshold", "Duck Threshold (dB)" },
                   { "delayDuckRelease", "Duck Release (ms)" }});
        addStage ("Reverb", "reverbOn", {{ "reverbSize", "Size", true }, { "reverbMix", "Mix", true }},
                  FileKind::none, 0, mover (false, 1),
                  {{ "reverbMode", "Algorithm", false, juce::StringArray { "Room", "Plate", "Cloud" } },
                   { "reverbSize", "Size / Decay", true }, { "reverbDamping", "Damping", true },
                   { "reverbPreDelay", "Pre-Delay (ms)" }, { "reverbWidth", "Width", true },
                   { "reverbHighPass", "High-Pass (Hz)" }, { "reverbLowPass", "Low-Pass (Hz)" },
                   { "reverbMix", "Mix", true }});
        addStage ("Chorus", "chorusOn",
                  {{ "chorusRate", "Rate (Hz)" }, { "chorusDepth", "Depth", true }, { "chorusMix", "Mix", true }},
                  FileKind::none, 0, mover (false, 2));
        addStage ("Post EQ", "postOn", {}, FileKind::none, 0, mover (false, 3));
        addStage ("Output", {}, {{ "outputGain", "Level (dB)" }});
        refreshTileOrder();
        selectStage (findCard ("1176-Style FET Compressor"));
    }

    ~ChainControls() override { detailsViewport.setViewedComponent (nullptr, false); }

    void resetToDefaultChain()
    {
        audioProcessor.resetToDefaults();
        refreshTileOrder();
        selectStage (findCard ("Input"));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (background);
        auto area = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (chainSurface);
        g.fillRoundedRectangle (area, 18.0f);
        g.setColour (juce::Colour::fromRGB (52, 59, 72));
        g.drawRoundedRectangle (area, 18.0f, 2.4f);
        const auto now = juce::Time::getMillisecondCounterHiRes();
        drawLevelMeter (g, 25.0f, audioProcessor.getInputPeak(), "INPUT", now < inputClipUntilMs);
        drawLevelMeter (g, static_cast<float> (getWidth() - 41), audioProcessor.getOutputPeak(), "OUTPUT",
                        now < outputClipUntilMs);

        const auto* routing = audioProcessor.parameters.getRawParameterValue ("ampRouting");
        const auto* irMode = audioProcessor.parameters.getRawParameterValue ("parallelIRMode");
        const auto parallel = routing != nullptr && routing->load() >= 0.5f;
        const auto sharedIR = irMode != nullptr && irMode->load() >= 0.5f;
        const auto heading = parallel
            ? (sharedIR ? "AMP PARALLEL  |  BOTH AMPS -> IR 1"
                        : "AMP PARALLEL  |  AMP 1 -> IR 1  /  AMP 2 -> IR 2")
            : "AMP SERIES  |  CAB 1 / CAB 2 BLEND";
        const auto chainBounds = chainViewport.getBounds();
        g.setFont (namplifiedFontOptions (15.0f, juce::Font::bold));
        g.setColour (secondaryText);
        g.drawText (heading, chainBounds.getX() + 60, chainBounds.getY() - 8,
                    chainBounds.getWidth() - 120, 18, juce::Justification::centred);
    }

    void resized() override
    {
        if (fullscreenStage != nullptr)
        {
            chainViewport.setVisible (false);
            detailsViewport.setVisible (true);
            detailsViewport.setBounds (getLocalBounds().reduced (12));
            if (detailsViewport.getViewedComponent() != fullscreenStage)
                detailsViewport.setViewedComponent (fullscreenStage, false);
            fullscreenStage->setSize (juce::jmax (1, detailsViewport.getWidth() - 14),
                                      juce::jmax (1, detailsViewport.getHeight()));
            return;
        }
        chainViewport.setVisible (true);
        auto area = getLocalBounds().reduced (22, 18);
        auto chainArea = area.removeFromTop (tileList->getRequiredHeight());
        chainViewport.setBounds (chainArea.withTrimmedLeft (58).withTrimmedRight (58));
        tileList->setSize (juce::jmax (chainViewport.getWidth(), tileList->getRequiredWidth()),
                           tileList->getRequiredHeight());
        area.removeFromTop (12);
        detailsViewport.setBounds (area);
        if (selectedStage != nullptr)
        {
            if (detailsViewport.getViewedComponent() != selectedStage)
                detailsViewport.setViewedComponent (selectedStage, false);
            selectedStage->setSize (juce::jmax (1, detailsViewport.getWidth() - 14),
                                    selectedStage->getPreferredHeight());
        }
        refreshTileOrder();
    }

    void setParallelRouting (bool parallel, bool sharedIR)
    {
        tileList->setParallelRouting (parallel, sharedIR);
        for (auto& card : cards)
            card->setCompactMode (parallel);
        if (parallel && sharedIR && selectedStage != nullptr
            && selectedStage->getStageName() == "Cab IR 2")
            selectStage (findCard ("Cab IR 1"));
        resized();
    }

private:
    void timerCallback() override
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (audioProcessor.consumeInputClip()) inputClipUntilMs = now + 1500.0;
        if (audioProcessor.consumeOutputClip()) outputClipUntilMs = now + 1500.0;
        repaint();
    }

    void drawLevelMeter (juce::Graphics& g, float x, float peak, const juce::String& label, bool clipped)
    {
        const auto chainBounds = chainViewport.getBounds().toFloat();
        const auto height = juce::jmax (36.0f, chainBounds.getHeight() - 32.0f);
        const juce::Rectangle<float> track (x, chainBounds.getY() + 16.0f, 16.0f, height);
        g.setColour (juce::Colour::fromRGB (35, 43, 53));
        g.fillRoundedRectangle (track, 5.0f);
        const auto teal = knobAccent;
        const auto orange = juce::Colour (0xffffa24f);
        const auto red = juce::Colour (0xffff514e);
        const auto db = peak > 0.000001f ? juce::Decibels::gainToDecibels (peak) : -60.0f;
        const auto amount = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
        const auto drawZone = [&] (float low, float high, juce::Colour colour)
        {
            const auto zoneBottom = juce::jmin (1.0f, (juce::jmax (low, high) + 60.0f) / 66.0f);
            const auto zoneTop = juce::jmax (0.0f, (juce::jmin (low, high) + 60.0f) / 66.0f);
            const auto zone = juce::Rectangle<float> (track.getX(), track.getBottom() - zoneBottom * track.getHeight(),
                                                       track.getWidth(), (zoneBottom - zoneTop) * track.getHeight());
            g.setColour (colour.withAlpha (0.16f));
            g.fillRect (zone);
            const auto filledTop = juce::jmax (zoneTop, juce::jmin (zoneBottom, amount));
            if (filledTop > zoneTop)
            {
                auto lit = juce::Rectangle<float> (zone.getX(), track.getBottom() - filledTop * track.getHeight(),
                                                    zone.getWidth(), (filledTop - zoneTop) * track.getHeight());
                g.setColour (colour);
                g.fillRect (lit);
            }
        };
        drawZone (-60.0f, -18.0f, teal);
        drawZone (-18.0f, -3.0f, orange);
        drawZone (-3.0f, 6.0f, red);
        g.setColour (secondaryText);
        g.setFont (namplifiedFontOptions (14.0f, juce::Font::bold));
        g.drawText (label, juce::Rectangle<int> (juce::roundToInt (x - 22.0f), juce::roundToInt (chainBounds.getY() - 8.0f),
                                                  60, 18), juce::Justification::centred);
        if (clipped)
        {
            g.setColour (red);
            g.drawText ("CLIP", juce::Rectangle<int> (juce::roundToInt (x - 22.0f),
                                                       juce::roundToInt (chainBounds.getY() - 25.0f),
                                                       60, 16), juce::Justification::centred);
        }
    }

    std::function<void (int)> mover (bool pre, int stageId)
    {
        return [this, pre, stageId] (int delta) { moveStage (pre, stageId, delta); };
    }

    std::function<void (int)> ampMover (int stageId)
    {
        return [this, stageId] (int delta)
        {
            auto order = audioProcessor.getAmpSlotOrder();
            swapNeighbor (order, stageId, delta);
            audioProcessor.setAmpSlotOrder (order);
            resized();
        };
    }

    void moveStage (bool pre, int stageId, int delta)
    {
        if (pre)
        {
            auto order = audioProcessor.getPreFxOrder();
            swapNeighbor (order, stageId, delta);
            audioProcessor.setPreFxOrder (order);
        }
        else
        {
            auto order = audioProcessor.getPostFxOrder();
            swapNeighbor (order, stageId, delta);
            audioProcessor.setPostFxOrder (order);
        }
        resized();
    }

    template <size_t N>
    static void swapNeighbor (std::array<int, N>& order, int stageId, int delta)
    {
        for (size_t i = 0; i < order.size(); ++i)
            if (order[i] == stageId)
            {
                const auto next = static_cast<int> (i) + delta;
                if (next >= 0 && next < static_cast<int> (order.size()))
                    std::swap (order[i], order[static_cast<size_t> (next)]);
                return;
            }
    }

    StageCard* findCard (const juce::String& name) const
    {
        for (const auto& card : cards)
            if (card->getName() == name)
                return card.get();
        return nullptr;
    }

    StageTile* findTile (const juce::String& name) const
    {
        for (const auto& tile : tiles)
            if (tile->getName() == name)
                return tile.get();
        return nullptr;
    }

    void selectStage (StageCard* stage)
    {
        if (stage == nullptr)
            return;
        selectedStage = stage;
        detailsViewport.setViewedComponent (selectedStage, false);
        for (auto& tile : tiles)
            tile->setSelected (tile->getName() == selectedStage->getStageName());
        resized();
    }

    void refreshTileOrder()
    {
        std::vector<StageTile*> ordered;
        for (auto* card : getOrderedCards())
            if (auto* tile = findTile (card->getStageName()))
                ordered.push_back (tile);
        tileList->setOrderedTiles (std::move (ordered));
    }

    std::vector<StageCard*> getOrderedCards() const
    {
        std::vector<StageCard*> ordered;
        ordered.reserve (cards.size());
        ordered.push_back (findCard ("Input"));
        constexpr const char* preNames[] { "Noise Gate", "1176-Style FET Compressor", "Boost Pedal", "PreEQ" };
        for (const auto stage : audioProcessor.getPreFxOrder())
            ordered.push_back (findCard (preNames[static_cast<size_t> (stage)]));
        for (const auto slot : audioProcessor.getAmpSlotOrder())
            ordered.push_back (findCard ("NAM Profile " + juce::String (slot + 1)));
        ordered.push_back (findCard ("Cab IR 1"));
        ordered.push_back (findCard ("Cab IR 2"));
        constexpr const char* postNames[] { "Delay", "Reverb", "Chorus", "Post EQ" };
        for (const auto stage : audioProcessor.getPostFxOrder())
            ordered.push_back (findCard (postNames[static_cast<size_t> (stage)]));
        ordered.push_back (findCard ("Output"));
        return ordered;
    }

    void addStage (juce::String name, juce::String enableId, std::vector<ParameterControl> controls,
                   FileKind kind = FileKind::none, int slot = 0, std::function<void (int)> moveHandler = {},
                   std::vector<ParameterControl> detailControls = {})
    {
        std::vector<ParameterControl> fullControls;
        std::unordered_set<std::string> usedParameters;
        const auto appendUnique = [&] (const std::vector<ParameterControl>& source)
        {
            for (const auto& control : source)
                if (usedParameters.insert (control.id.toStdString()).second)
                    fullControls.push_back (control);
        };
        appendUnique (detailControls);
        appendUnique (controls);

        const auto tileName = name;
        const auto tileEnableId = enableId;
        const auto tileMoveHandler = moveHandler;
        auto stage = std::make_unique<StageCard> (audioProcessor, std::move (name), std::move (enableId),
                                                  std::move (fullControls), kind, slot, std::move (moveHandler),
                                                  std::vector<ParameterControl> {}, [this] { resized(); });
        auto* stagePtr = stage.get();
        if (tileName == "PreEQ" || tileName == "Post EQ")
            stagePtr->setFullscreenHandler ([this, stagePtr] (bool fullscreen) { setFullscreenStage (stagePtr, fullscreen); });
        cards.push_back (std::move (stage));
        auto tile = std::make_unique<StageTile> (audioProcessor.parameters, tileName, tileEnableId,
                                                 tileMoveHandler, [this, stagePtr] { selectStage (stagePtr); });
        tileList->addAndMakeVisible (*tile);
        tiles.push_back (std::move (tile));
    }

    void setFullscreenStage (StageCard* stage, bool fullscreen)
    {
        fullscreenStage = fullscreen ? stage : nullptr;
        if (stage != nullptr)
            stage->setFullscreenMode (fullscreen);
        if (fullscreen && stage != nullptr)
        {
            selectedStage = stage;
            detailsViewport.setViewedComponent (stage, false);
            for (auto& tile : tiles)
                tile->setSelected (tile->getName() == stage->getStageName());
        }
        if (fullscreenChangedHandler)
            fullscreenChangedHandler (fullscreen);
        resized();
    }

    NAMplifiedAudioProcessor& audioProcessor;
    std::function<void (bool)> fullscreenChangedHandler;
    juce::Viewport chainViewport;
    juce::Viewport detailsViewport;
    std::unique_ptr<StageTileList> tileList = std::make_unique<StageTileList>();
    StageCard* selectedStage = nullptr;
    StageCard* fullscreenStage = nullptr;
    double inputClipUntilMs = 0.0, outputClipUntilMs = 0.0;
    std::vector<std::unique_ptr<StageCard>> cards;
    std::vector<std::unique_ptr<StageTile>> tiles;
};

NAMplifiedAudioProcessorEditor::NAMplifiedAudioProcessorEditor (NAMplifiedAudioProcessor& processor)
    : AudioProcessorEditor (&processor), audioProcessor (processor),
      tooltipWindow (this, 650),
      editorLookAndFeel (std::make_unique<NAMplifiedLookAndFeel> (processor)),
      chainControls (std::make_unique<ChainControls> (processor,
          [this] (bool fullscreen) { setEqFullscreen (fullscreen); }))
{
    audioProcessor.capturePresetSnapshot();
    startTimerHz (20);
    setLookAndFeel (editorLookAndFeel.get());
    setSize (1600, 900);
    setResizable (true, true);
    setResizeLimits (1200, 760, 1920, 1200);
    brandIcon.setImage (juce::ImageCache::getFromMemory (
        BinaryData::NAMplifiedAppIcon_png, BinaryData::NAMplifiedAppIcon_pngSize));
    brandIcon.setImagePlacement (juce::RectanglePlacement::centred);
    brandIcon.setTooltip ("NAMplified");
    addAndMakeVisible (brandIcon);
    title.setText ("NAMplified", juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions().withTypeface (getBrandTypeface()).withHeight (29.0f).withStyle ("Bold")));
    title.setColour (juce::Label::textColourId, text);
    addAndMakeVisible (title);
    subtitle.setText ("CAPTURE SIGNAL WORKSTATION", juce::dontSendNotification);
    subtitle.setFont (namplifiedFontOptions (9.0f, juce::Font::bold));
    subtitle.setColour (juce::Label::textColourId, secondaryText);
    addAndMakeVisible (subtitle);

    presetLabel.setText ("PRESET", juce::dontSendNotification);
    presetLabel.setFont (namplifiedFontOptions (14.0f, juce::Font::bold));
    presetLabel.setColour (juce::Label::textColourId, secondaryText);
    presetLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (presetLabel);
    presetBox.setTextWhenNothingSelected ("CHOOSE PRESET");
    savePresetButton.setButtonText ("SAVE");
    deletePresetButton.setButtonText ("DELETE");
    resetChainButton.setButtonText ("RESET CHAIN");
    aboutButton.setButtonText ("ABOUT");
    resetChainButton.setTooltip ("Restore the default signal chain and default parameter values");
    aboutButton.setTooltip ("About NAMplified");
    presetBox.setTooltip ("Load a saved NAMplified preset");
    pitchLabel.setText ("PITCH", juce::dontSendNotification);
    pitchLabel.setFont (namplifiedFontOptions (14.0f, juce::Font::bold));
    pitchLabel.setColour (juce::Label::textColourId, secondaryText);
    pitchLabel.setJustificationType (juce::Justification::centredRight);
    pitchLabel.setTooltip ("Global pitch shift before the input signal enters the chain. Range: -24 to +24 semitones.");
    addAndMakeVisible (pitchLabel);
    pitchSlider.setName ("pitchSemitones");
    pitchSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    pitchSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    pitchSlider.setColour (juce::Slider::trackColourId, knobAccent);
    pitchSlider.setColour (juce::Slider::thumbColourId, juce::Colour (0xff4ba9d2));
    pitchSlider.setTooltip ("Pitch shift in semitones. Centre is unchanged pitch.");
    addAndMakeVisible (pitchSlider);
    pitchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.parameters, "pitchSemitones", pitchSlider);
    pitchValueLabel.setFont (namplifiedFontOptions (14.0f, juce::Font::bold));
    pitchValueLabel.setColour (juce::Label::textColourId, text);
    pitchValueLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (pitchValueLabel);
    const auto updatePitchValue = [this]
    {
        const auto semitones = juce::roundToInt (pitchSlider.getValue());
        const auto prefix = semitones > 0 ? "+" : "";
        pitchValueLabel.setText (prefix + juce::String (semitones) + " ST", juce::dontSendNotification);
    };
    pitchSlider.onValueChange = updatePitchValue;
    updatePitchValue();
    masterVolumeLabel.setText ("MASTER", juce::dontSendNotification);
    masterVolumeLabel.setFont (namplifiedFontOptions (12.0f, juce::Font::bold));
    masterVolumeLabel.setColour (juce::Label::textColourId, secondaryText);
    masterVolumeLabel.setJustificationType (juce::Justification::centredRight);
    masterVolumeLabel.setTooltip ("Overall plugin output level, applied after the Output block.");
    addAndMakeVisible (masterVolumeLabel);
    masterVolumeValueLabel.setFont (namplifiedFontOptions (11.0f, juce::Font::bold));
    masterVolumeValueLabel.setColour (juce::Label::textColourId, text);
    masterVolumeValueLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (masterVolumeValueLabel);
    masterVolumeSlider.setName ("masterVolume");
    masterVolumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    masterVolumeSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    masterVolumeSlider.setLookAndFeel (editorLookAndFeel.get());
    masterVolumeSlider.setColour (juce::Slider::trackColourId, knobAccent);
    masterVolumeSlider.setColour (juce::Slider::textBoxTextColourId, text);
    masterVolumeSlider.setColour (juce::Slider::textBoxBackgroundColourId, panel);
    masterVolumeSlider.setColour (juce::Slider::textBoxOutlineColourId, panel);
    masterVolumeSlider.setTooltip ("Master Volume: overall plugin output after the Output block.");
    addAndMakeVisible (masterVolumeSlider);
    masterVolumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.parameters, "masterVolume", masterVolumeSlider);
    updateMasterVolumeValueLabel();
    addAndMakeVisible (presetBox);
    savePresetButton.setTooltip ("Save the current signal chain as a preset");
    deletePresetButton.setTooltip ("Delete the selected user preset");
    addAndMakeVisible (savePresetButton);
    addAndMakeVisible (deletePresetButton);
    addAndMakeVisible (resetChainButton);
    addAndMakeVisible (aboutButton);
    audioSettingsButton.setButtonText ("AUDIO");
    audioSettingsButton.setTooltip ("Open audio and MIDI device settings");
    audioSettingsButton.setVisible (audioProcessor.hasAudioSettingsCallback());
    audioSettingsButton.onClick = [this] { audioProcessor.showAudioSettings(); };
    addAndMakeVisible (audioSettingsButton);
    midiLearnButton.setButtonText ("MIDI LEARN");
    midiLearnButton.setTooltip ("Click MIDI Learn, then a control, then move a hardware MIDI CC knob.");
    midiLearnButton.onClick = [this]
    {
        if (midiLearnArmed)
        {
            audioProcessor.cancelMidiLearn();
            midiLearnArmed = midiTargetArmed = false;
            midiLearnButton.setButtonText ("MIDI LEARN");
            midiLearnButton.setTooltip ("Click MIDI Learn, then a control, then move a hardware MIDI CC knob.");
        }
        else
        {
            midiLearnArmed = true;
            midiTargetArmed = false;
            audioProcessor.armMidiLearn();
            midiLearnButton.setButtonText ("CLICK CONTROL");
            midiLearnButton.setTooltip ("Now click the parameter you want to control, then send a MIDI CC.");
        }
    };
    addAndMakeVisible (midiLearnButton);
    refreshUserPresets (audioProcessor.getSelectedUserPresetName());
    presetBox.onChange = [this]
    {
        const auto name = presetBox.getText().trim();
        if (name.isNotEmpty() && audioProcessor.loadUserPreset (name))
        {
            audioProcessor.setSelectedUserPresetName (name);
            audioProcessor.capturePresetSnapshot();
            presetDirty = false;
            refreshUserPresets (name);
            updateRoutingDisplay();
        }
    };
    savePresetButton.onClick = [this] { showSavePresetPrompt(); };
    deletePresetButton.onClick = [this] { showDeletePresetPrompt(); };
    resetChainButton.onClick = [this]
    {
        chainControls->resetToDefaultChain();
        audioProcessor.setSelectedUserPresetName ({});
        audioProcessor.capturePresetSnapshot();
        presetDirty = false;
        presetBox.setSelectedId (0, juce::dontSendNotification);
        refreshUserPresets();
        updateRoutingDisplay();
    };
    aboutButton.onClick = [this] { showAboutDialog(); };

    ampRoutingBox.addItem ("SERIES", 1);
    ampRoutingBox.addItem ("PARALLEL BLEND", 2);
    ampRoutingBox.setName ("ampRouting");
    ampRoutingBox.setTooltip (
        "Series: NAM captures feed one another, e.g. Boost capture > OD capture > Amp capture.\n"
        "Parallel Blend: NAM profiles run side by side and blend into the shared Cab IR section.");
    addAndMakeVisible (ampRoutingBox);
    ampRoutingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.parameters, "ampRouting", ampRoutingBox);
    ampRoutingBox.addListener (this);

    parallelIRBox.addItem ("PAIRED IRs", 1);
    parallelIRBox.addItem ("SHARED IR 1", 2);
    parallelIRBox.setName ("parallelIRMode");
    parallelIRBox.setTooltip ("Paired IRs: Amp 1 to IR 1 and Amp 2 to IR 2. Shared IR 1: blend both amp outputs, then process them through IR 1.");
    addAndMakeVisible (parallelIRBox);
    parallelIRAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.parameters, "parallelIRMode", parallelIRBox);
    parallelIRBox.addListener (this);
    parallelIRBox.setVisible (false);

    blendLabel.setText ("CAB 1 / CAB 2", juce::dontSendNotification);
    blendLabel.setFont (namplifiedFontOptions (14.0f, juce::Font::bold));
    blendLabel.setColour (juce::Label::textColourId, secondaryText);
    blendLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (blendLabel);
    blendValueLabel.setFont (namplifiedFontOptions (14.0f, juce::Font::bold));
    blendValueLabel.setColour (juce::Label::textColourId, text);
    blendValueLabel.setColour (juce::Label::backgroundColourId, panel);
    blendValueLabel.setColour (juce::Label::outlineColourId, panel);
    blendValueLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (blendValueLabel);
    blendSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    blendSlider.setName ("irParallelBlend");
    blendSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    blendSlider.setColour (juce::Slider::trackColourId, knobAccent);
    blendSlider.setColour (juce::Slider::textBoxTextColourId, text);
    blendSlider.setColour (juce::Slider::textBoxBackgroundColourId, panel);
    blendSlider.setColour (juce::Slider::textBoxOutlineColourId, panel);
    addAndMakeVisible (blendSlider);
    blendSlider.onValueChange = [this] { updateBlendValue(); };

    updateRoutingDisplay();
    addAndMakeVisible (*chainControls);
    addMouseListener (this, true);

}

NAMplifiedAudioProcessorEditor::~NAMplifiedAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void NAMplifiedAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (juce::Colour (0xff66d6b2));
    g.fillRect (getLocalBounds().removeFromTop (3));
}

void NAMplifiedAudioProcessorEditor::resized()
{
    if (eqFullscreen)
    {
        chainControls->setBounds (getLocalBounds().reduced (12));
        return;
    }
    auto area = getLocalBounds().reduced (20);
    auto top = area.removeFromTop (42);
    auto titleArea = top.removeFromLeft (240);
    brandIcon.setBounds (titleArea.removeFromLeft (34).withSizeKeepingCentre (32, 32));
    titleArea.removeFromLeft (8);
    title.setBounds (titleArea.removeFromTop (28));
    subtitle.setBounds (titleArea);
    auto aboutArea = top.removeFromRight (76);
    top.removeFromRight (8);
    aboutButton.setBounds (aboutArea.reduced (0, 4));
    auto audioSettingsArea = top.removeFromRight (76);
    top.removeFromRight (8);
    audioSettingsButton.setBounds (audioSettingsArea.reduced (0, 4));
    auto midiArea = top.removeFromRight (108);
    top.removeFromRight (8);
    midiLearnButton.setBounds (midiArea.reduced (0, 4));
    auto presetArea = top;
    const auto compactHeader = presetArea.getWidth() < 920;
    const auto presetWidth = compactHeader ? 145 : 165;
    const auto smallButtonWidth = compactHeader ? 70 : 76;
    const auto resetButtonWidth = compactHeader ? 92 : 104;
    const auto pitchSliderWidth = compactHeader ? 92 : 112;
    const auto pitchLabelWidth = compactHeader ? 42 : 46;
    const auto pitchValueWidth = compactHeader ? 42 : 44;
    const auto masterLabelWidth = compactHeader ? 52 : 60;
    const auto masterValueWidth = compactHeader ? 42 : 48;
    const auto masterSliderWidth = compactHeader ? 76 : 96;
    presetLabel.setBounds (presetArea.removeFromLeft (compactHeader ? 52 : 58));
    presetArea.removeFromLeft (compactHeader ? 5 : 8);
    presetBox.setBounds (presetArea.removeFromLeft (presetWidth).reduced (0, 4));
    presetArea.removeFromLeft (compactHeader ? 6 : 8);
    savePresetButton.setBounds (presetArea.removeFromLeft (smallButtonWidth).reduced (0, 4));
    presetArea.removeFromLeft (compactHeader ? 4 : 6);
    deletePresetButton.setBounds (presetArea.removeFromLeft (smallButtonWidth).reduced (0, 4));
    presetArea.removeFromLeft (compactHeader ? 4 : 6);
    resetChainButton.setBounds (presetArea.removeFromLeft (resetButtonWidth).reduced (0, 4));
    presetArea.removeFromLeft (compactHeader ? 6 : 10);
    pitchLabel.setBounds (presetArea.removeFromLeft (pitchLabelWidth).reduced (0, 4));
    presetArea.removeFromLeft (compactHeader ? 4 : 6);
    pitchSlider.setBounds (presetArea.removeFromLeft (pitchSliderWidth).reduced (0, 10));
    presetArea.removeFromLeft (compactHeader ? 2 : 4);
    pitchValueLabel.setBounds (presetArea.removeFromLeft (pitchValueWidth).reduced (0, 4));
    presetArea.removeFromLeft (compactHeader ? 5 : 8);
    masterVolumeLabel.setBounds (presetArea.removeFromLeft (masterLabelWidth).reduced (0, 4));
    presetArea.removeFromLeft (compactHeader ? 3 : 4);
    masterVolumeSlider.setBounds (presetArea.removeFromLeft (masterSliderWidth).reduced (0, 10));
    presetArea.removeFromLeft (compactHeader ? 3 : 4);
    masterVolumeValueLabel.setBounds (presetArea.removeFromLeft (masterValueWidth).reduced (0, 4));

    auto controlRow = area.removeFromTop (38);
    ampRoutingBox.setBounds (controlRow.removeFromRight (180).reduced (0, 2));
    if (parallelIRBox.isVisible())
    {
        controlRow.removeFromRight (8);
        parallelIRBox.setBounds (controlRow.removeFromRight (190).reduced (0, 2));
    }
    controlRow.removeFromRight (16);
    blendLabel.setBounds (controlRow.removeFromLeft (104));
    auto blendControls = controlRow.reduced (4, 5);
    blendValueLabel.setBounds (blendControls.removeFromRight (104));
    blendControls.removeFromRight (6);
    blendSlider.setBounds (blendControls);
    chainControls->setBounds (area);
}

void NAMplifiedAudioProcessorEditor::setEqFullscreen (bool shouldBeFullscreen)
{
    eqFullscreen = shouldBeFullscreen;
    const auto showChrome = ! shouldBeFullscreen;
    brandIcon.setVisible (showChrome);
    title.setVisible (showChrome);
    subtitle.setVisible (showChrome);
    presetLabel.setVisible (showChrome);
    presetBox.setVisible (showChrome);
    savePresetButton.setVisible (showChrome);
    deletePresetButton.setVisible (showChrome);
    resetChainButton.setVisible (showChrome);
    aboutButton.setVisible (showChrome);
    audioSettingsButton.setVisible (showChrome && audioProcessor.hasAudioSettingsCallback());
    midiLearnButton.setVisible (showChrome);
    pitchLabel.setVisible (showChrome);
    pitchSlider.setVisible (showChrome);
    pitchValueLabel.setVisible (showChrome);
    masterVolumeLabel.setVisible (showChrome);
    masterVolumeSlider.setVisible (showChrome);
    masterVolumeValueLabel.setVisible (showChrome);
    blendLabel.setVisible (showChrome);
    blendValueLabel.setVisible (showChrome);
    blendSlider.setVisible (showChrome);
    ampRoutingBox.setVisible (showChrome);
    parallelIRBox.setVisible (showChrome && parallelIRBox.getSelectedId() == 2);
    if (showChrome)
        updateRoutingDisplay();
    resized();
}

void NAMplifiedAudioProcessorEditor::refreshUserPresets (const juce::String& selectName)
{
    auto previousName = selectName.isNotEmpty() ? selectName : selectedPresetName();
    if (previousName.isEmpty())
        previousName = audioProcessor.getSelectedUserPresetName();
    const auto names = audioProcessor.getUserPresetNames();
    presetBox.clear (juce::dontSendNotification);
    for (int i = 0; i < names.size(); ++i)
        presetBox.addItem (names[i] + ((presetDirty && names[i] == previousName) ? " *" : juce::String()), i + 1);
    const auto index = names.indexOf (previousName);
    if (index >= 0)
        presetBox.setSelectedId (index + 1, juce::dontSendNotification);
    else
        presetBox.setSelectedId (0, juce::dontSendNotification);
    deletePresetButton.setEnabled (presetBox.getSelectedId() != 0);
}

juce::String NAMplifiedAudioProcessorEditor::selectedPresetName() const
{
    auto name = presetBox.getText().trim();
    if (name.endsWith (" *"))
        name = name.dropLastCharacters (2).trimEnd();
    return name;
}

void NAMplifiedAudioProcessorEditor::timerCallback()
{
    updateMasterVolumeValueLabel();
    if (midiLearnArmed && audioProcessor.hasMidiLearnTarget())
    {
        midiTargetArmed = true;
        midiLearnButton.setButtonText ("SEND MIDI CC");
    }
    int learnedCC = -1;
    juce::String learnedParameter;
    if (audioProcessor.takeMidiLearnResult (learnedCC, learnedParameter))
    {
        midiLearnArmed = midiTargetArmed = false;
        audioProcessor.saveMidiMappings();
        midiLearnButton.setButtonText ("MIDI LEARN");
        midiLearnButton.setTooltip ("CC " + juce::String (learnedCC) + " now controls " + learnedParameter
                                    + ". Click to learn another mapping. "
                                    + juce::String (audioProcessor.getMidiMappingCount()) + " mappings saved.");
    }
    const auto* routingParameter = audioProcessor.parameters.getRawParameterValue ("ampRouting");
    const auto* irModeParameter = audioProcessor.parameters.getRawParameterValue ("parallelIRMode");
    const auto parallel = routingParameter != nullptr && routingParameter->load() >= 0.5f;
    const auto sharedIR = irModeParameter != nullptr && irModeParameter->load() >= 0.5f;
    const auto ir1Active = audioProcessor.isImpulseResponseActive (0);
    const auto ir2Active = audioProcessor.isImpulseResponseActive (1);
    const auto blendDisplayState = (parallel ? 1 : 0) | (sharedIR ? 2 : 0)
                                 | (ir1Active ? 4 : 0) | (ir2Active ? 8 : 0);
    if (blendDisplayState != lastBlendDisplayState)
        updateRoutingDisplay();

    const auto changed = audioProcessor.hasUnsavedParameterChanges();
    if (changed != presetDirty)
    {
        presetDirty = changed;
        if (presetBox.getSelectedId() != 0)
            refreshUserPresets (selectedPresetName());
    }
    const auto repaintChildren = [] (auto&& self, juce::Component& component) -> void
    {
        if (auto* slider = dynamic_cast<juce::Slider*> (&component))
            slider->repaint();
        for (auto* child : component.getChildren())
            self (self, *child);
    };
    repaintChildren (repaintChildren, *this);
}

void NAMplifiedAudioProcessorEditor::mouseDown (const juce::MouseEvent& event)
{
    if (! midiLearnArmed || midiTargetArmed)
        return;
    auto* component = event.originalComponent;
    while (component != nullptr && component != this)
    {
        const auto id = component->getName();
        if (id.isNotEmpty() && audioProcessor.parameters.getParameter (id) != nullptr
            && audioProcessor.setMidiLearnTarget (id))
        {
            midiTargetArmed = true;
            midiLearnButton.setButtonText ("SEND MIDI CC");
            midiLearnButton.setTooltip ("Move the MIDI controller now to assign it to " + id + ".");
            return;
        }
        component = component->getParentComponent();
    }
}

void NAMplifiedAudioProcessorEditor::updateMasterVolumeValueLabel()
{
    const auto value = masterVolumeSlider.getValue();
    const auto roundedDb = std::abs (value) < 0.05 ? 0.0 : std::round (value * 10.0) / 10.0;
    const auto valueText = roundedDb == 0.0 ? juce::String ("0") : juce::String (roundedDb, 1);
    masterVolumeValueLabel.setText (valueText + " dB", juce::dontSendNotification);
}

void NAMplifiedAudioProcessorEditor::comboBoxChanged (juce::ComboBox* box)
{
    if (box != &ampRoutingBox && box != &parallelIRBox)
        return;
    const juce::Component::SafePointer<NAMplifiedAudioProcessorEditor> safeThis (this);
    juce::MessageManager::callAsync ([safeThis]
    {
        if (safeThis != nullptr)
            safeThis->updateRoutingDisplay();
    });
}

void NAMplifiedAudioProcessorEditor::updateRoutingDisplay()
{
    const auto* parameter = audioProcessor.parameters.getRawParameterValue ("ampRouting");
    const auto parallel = parameter != nullptr && juce::roundToInt (parameter->load()) == 1;
    const auto* irMode = audioProcessor.parameters.getRawParameterValue ("parallelIRMode");
    const auto sharedIR = irMode != nullptr && irMode->load() >= 0.5f;
    const auto ir1Active = audioProcessor.isImpulseResponseActive (0);
    const auto ir2Active = audioProcessor.isImpulseResponseActive (1);
    lastBlendDisplayState = (parallel ? 1 : 0) | (sharedIR ? 2 : 0)
                          | (ir1Active ? 4 : 0) | (ir2Active ? 8 : 0);
    blendAttachment.reset();
    const auto onlyOneCabIsActive = ! parallel && (ir1Active != ir2Active);
    if (onlyOneCabIsActive)
    {
        // Keep the saved two-cab blend intact. With one usable IR, show its
        // effective 100% position and disable a control that cannot blend.
        blendSlider.setValue (ir1Active ? 1.0 : 0.0, juce::dontSendNotification);
        blendSlider.setEnabled (false);
    }
    else
    {
        const auto blendParameterId = parallel ? juce::String ("ampParallelBlend") : juce::String ("irParallelBlend");
        blendSlider.setName (blendParameterId);
        blendAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            audioProcessor.parameters, blendParameterId, blendSlider);
        blendSlider.setEnabled (true);
    }
    ampRoutingBox.setSelectedId (parallel ? 2 : 1, juce::dontSendNotification);
    parallelIRBox.setVisible (parallel);
    blendLabel.setText (parallel ? "AMP 1 / AMP 2" : "CAB 1 / CAB 2", juce::dontSendNotification);
    blendSlider.setTooltip (parallel
        ? "Amp 1 to IR 1 and Amp 2 to IR 2. This controls the Amp 1 share; disabling either amp sends 100% through the other path."
        : onlyOneCabIsActive
            ? (ir1Active ? "IR 1 is the only active cabinet and receives 100% of the signal. Enable IR 2 to blend both cabinets."
                         : "IR 2 is the only active cabinet and receives 100% of the signal. Enable IR 1 to blend both cabinets.")
            : "Centre is 0 (equal blend); negative favours CAB 2, positive favours CAB 1. If one cab is disabled, the other is 100%.");
    chainControls->setParallelRouting (parallel, sharedIR);
    updateBlendValue();
    resized();
}

void NAMplifiedAudioProcessorEditor::updateBlendValue()
{
    const auto* parameter = audioProcessor.parameters.getRawParameterValue ("ampRouting");
    const auto parallel = parameter != nullptr && juce::roundToInt (parameter->load()) == 1;
    const auto value = blendSlider.getValue();
    if (parallel)
    {
        const auto nam1 = juce::jlimit (0, 100, juce::roundToInt (value * 100.0));
        blendValueLabel.setText (juce::String (nam1) + "/" + juce::String (100 - nam1),
                                 juce::dontSendNotification);
        return;
    }

    const auto cab1 = juce::jlimit (0, 100, juce::roundToInt (value * 100.0));
    const auto offset = juce::roundToInt ((value - 0.5) * 200.0);
    const auto offsetText = offset > 0 ? "+" + juce::String (offset) : juce::String (offset);
    blendValueLabel.setText (offsetText + " · " + juce::String (cab1) + "/" + juce::String (100 - cab1),
                             juce::dontSendNotification);
}

void NAMplifiedAudioProcessorEditor::showSavePresetPrompt()
{
    auto* alert = new juce::AlertWindow ("Save preset", "Name this signal-chain preset.",
                                         juce::MessageBoxIconType::NoIcon);
    const auto currentName = selectedPresetName();
    alert->addTextEditor ("presetName", currentName.isNotEmpty() ? currentName : "My Preset", "Preset name:");
    alert->addButton ("Save", 1);
    alert->addButton ("Cancel", 0);
    juce::Component::SafePointer<NAMplifiedAudioProcessorEditor> safeThis (this);
    alert->enterModalState (true, juce::ModalCallbackFunction::create ([safeThis, alert] (int result)
    {
        if (result != 1 || safeThis == nullptr)
            return;
        const auto name = alert->getTextEditorContents ("presetName").trim();
        if (name.isEmpty() || ! safeThis->audioProcessor.saveUserPreset (name))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                     "Could not save preset",
                                                     "Check the preset name and available storage space.");
            return;
        }
        safeThis->audioProcessor.capturePresetSnapshot();
        safeThis->audioProcessor.setSelectedUserPresetName (name);
        safeThis->presetDirty = false;
        safeThis->refreshUserPresets (name);
    }), true);
}

void NAMplifiedAudioProcessorEditor::showDeletePresetPrompt()
{
    const auto name = selectedPresetName();
    if (name.isEmpty()) return;
    auto* alert = new juce::AlertWindow ("Delete preset?", "Delete \"" + name + "\" from your user presets?",
                                         juce::MessageBoxIconType::WarningIcon);
    alert->addButton ("Delete", 1);
    alert->addButton ("Cancel", 0);
    juce::Component::SafePointer<NAMplifiedAudioProcessorEditor> safeThis (this);
    alert->enterModalState (true, juce::ModalCallbackFunction::create ([safeThis, name] (int result)
    {
        if (result != 1 || safeThis == nullptr) return;
        if (! safeThis->audioProcessor.deleteUserPreset (name))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                     "Could not delete preset",
                                                     "The preset file or its asset folder could not be removed.");
            return;
        }
        if (safeThis->audioProcessor.getSelectedUserPresetName() == name)
            safeThis->audioProcessor.setSelectedUserPresetName ({});
        safeThis->audioProcessor.capturePresetSnapshot();
        safeThis->presetDirty = false;
        safeThis->refreshUserPresets();
    }), true);
}

void NAMplifiedAudioProcessorEditor::showAboutDialog()
{
    auto* alert = new juce::AlertWindow (
        "About NAMplified",
        "Free, open-source amp and effects plugin.\n\n"
        "Created and coded by Ronny Wels (sedosa).\n"
        "Project code: AGPL-3.0. Captures, IRs and third-party assets remain under their respective licenses; retain required attribution.",
        juce::MessageBoxIconType::NoIcon, this);
    alert->setLookAndFeel (editorLookAndFeel.get());
    alert->addButton ("CLOSE", 1);
    alert->enterModalState (true, juce::ModalCallbackFunction::create ([alert] (int)
    {
        alert->setLookAndFeel (nullptr);
    }), true);
}
