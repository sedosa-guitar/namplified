#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <BinaryData.h>
#include <unordered_set>
#include <limits>
#include <array>

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

static juce::Typeface::Ptr getLeadTypeface()
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

static juce::FontOptions leadFontOptions (float height, int style = juce::Font::plain)
{
    const auto weight = (style & juce::Font::bold) != 0 ? 650.0f : 450.0f;
    return juce::FontOptions().withTypeface (getLeadTypeface())
        .withHeight (height)
        .withVariableSetting ({ juce::FontFeatureTag ("wght"), weight });
}

class LeadLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    explicit LeadLookAndFeel (TheLeadAudioProcessor& processorToUse)
        : processor (processorToUse), interTypeface (getLeadTypeface())
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
        return juce::Font (leadFontOptions (juce::jlimit (16.0f, 20.0f, buttonHeight * 0.50f)));
    }

    juce::Font getComboBoxFont (juce::ComboBox& box) override
    {
        return juce::Font (leadFontOptions (juce::jlimit (14.0f, 18.0f, box.getHeight() * 0.48f)));
    }

    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText,
                                           juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override
    {
        constexpr int horizontalPadding = 14;
        constexpr int verticalPadding = 10;
        const auto maxTextWidth = juce::jmax (120, juce::jmin (380, parentArea.getWidth() - horizontalPadding * 2));
        juce::AttributedString attributedText (tipText);
        attributedText.setFont (juce::Font (leadFontOptions (14.5f)));
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
        g.fillRoundedRectangle (juce::Rectangle<float> (12.0f, 9.0f, 3.0f,
                                                        juce::jmax (8.0f, static_cast<float> (height) - 18.0f)), 1.5f);
        juce::AttributedString attributedText (tipText);
        attributedText.setFont (juce::Font (leadFontOptions (14.5f)));
        attributedText.setColour (text);
        juce::TextLayout layout;
        layout.createLayout (attributedText, juce::jmax (100.0f, static_cast<float> (width) - 30.0f),
                             juce::jmax (30.0f, static_cast<float> (height) - 16.0f));
        layout.draw (g, juce::Rectangle<float> (22.0f, 8.0f,
                                                juce::jmax (50.0f, static_cast<float> (width) - 30.0f),
                                                juce::jmax (18.0f, static_cast<float> (height) - 16.0f)));
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
    TheLeadAudioProcessor& processor;
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
    if (juce::String (control.id) == "gateThreshold")
        return juce::String (value, 0) + " dB";
    if (control.inputDb)
        return juce::String (-6.0 + value * 30.0, 1) + " dB";
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
    if (juce::String (control.label).containsIgnoreCase ("dB"))
        return juce::String (value, 1) + " dB";
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

class ParameterDetailPanel final : public juce::Component, private juce::Timer
{
public:
    ParameterDetailPanel (TheLeadAudioProcessor& processor, LeadLookAndFeel& lookAndFeelToUse,
                          juce::String title, const std::vector<ParameterControl>& controls)
        : audioProcessor (processor), lookAndFeel (lookAndFeelToUse),
          isPostEq (title == "Post EQ"),
          isIRShapeEq (title.startsWith ("Cab IR ")),
          isBoost (title == "Boost Pedal"),
          titleNumber (isIRShapeEq ? title.fromLastOccurrenceOf (" ", false, false) : juce::String())
    {
        heading.setText ((getVisibleStageName (title) + " · Detailed controls").toUpperCase(), juce::dontSendNotification);
        heading.setFont (leadFontOptions (20.0f, juce::Font::bold));
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
                choice->addItemList (control.choices, 1);
                addAndMakeVisible (*choice);
                comboAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                    audioProcessor.parameters, control.id, *choice));
                if (isBoost && juce::String (control.id) == "boostModel")
                    choice->onChange = [this] { updateBoostControls(); resized(); };
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
                if (control.percent)
                {
                    slider->textFromValueFunction = [] (double value) { return juce::String (value * 100.0, 0); };
                    slider->valueFromTextFunction = [] (const juce::String& value) { return value.getDoubleValue() / 100.0; };
                    slider->setTextValueSuffix (" %");
                }
                if (control.inputDb)
                {
                    slider->textFromValueFunction = [] (double value) { return juce::String (-6.0 + value * 30.0, 1); };
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
        startTimerHz (20);
    }

    void paint (juce::Graphics& g) override { g.fillAll (background); }
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
    void timerCallback() override
    {
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
        int visibleCount = 0;
        for (size_t i = 0; i < controlWidgets.size(); ++i)
        {
            const auto& id = controlIds[i];
            const auto visible = id == "boostModel"
                || (use808 ? (id == "boost808Drive" || id == "boost808Tone" || id == "boost808Level")
                           : (id == "boostGain" || id == "boostBass" || id == "boostMid" || id == "boostTreble"));
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

    TheLeadAudioProcessor& audioProcessor;
    LeadLookAndFeel& lookAndFeel;
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
        g.setFont (leadFontOptions (14.0f, juce::Font::bold));
        g.drawText (getButtonText(), getLocalBounds().removeFromBottom (16), juce::Justification::centred);
    }
};

class GraphicEqEditor final : public juce::Component
{
public:
    explicit GraphicEqEditor (TheLeadAudioProcessor& processor, bool postEq) : audioProcessor (processor)
    {
        if (postEq)
        {
            parameterIds = { "postLow", "postGraphic2", "postGraphic3", "postGraphic4", "postMid",
                             "postGraphic6", "postGraphic7", "postGraphic8", "postHigh" };
            frequencyNames = { "50 Hz", "100 Hz", "200 Hz", "400 Hz", "800 Hz", "1.6 kHz", "3.2 kHz", "6.4 kHz", "16 kHz" };
        }
        else
        {
            parameterIds = { "preLow", "preBand2", "preMid", "preBand4", "preHigh" };
            frequencyNames = { "80 Hz", "250 Hz", "800 Hz", "2.5 kHz", "8 kHz" };
        }
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
        for (int band = 0; band < parameterIds.size(); ++band)
        {
            auto button = std::make_unique<juce::TextButton> ("RESET");
            button->setTooltip ("Reset " + frequencyNames[band] + " band to 0 dB");
            button->onClick = [this, band] { resetBand (band); };
            addAndMakeVisible (*button);
            resetButtons.push_back (std::move (button));
        }
    }

    void resized() override
    {
        const auto plot = getPlotBounds();
        for (int band = 0; band < parameterIds.size(); ++band)
            resetButtons[static_cast<size_t> (band)]->setBounds (
                juce::roundToInt (pointX (band, plot) - 25.0f), juce::roundToInt (plot.getBottom() + 34.0f), 50, 22);
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
        g.setFont (leadFontOptions (15.0f));
        for (int db = -12; db <= 12; db += 6)
        {
            const auto y = gainToY (static_cast<float> (db), plot);
            g.setColour (db == 0 ? juce::Colour::fromRGB (88, 99, 114) : juce::Colour::fromRGB (43, 51, 63));
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
            g.setColour (secondaryText);
            g.drawText (juce::String (db) + " dB", 4, juce::roundToInt (y - 9.0f), 54, 18,
                        juce::Justification::centredRight);
        }

        juce::Path response;
        for (int i = 0; i < parameterIds.size(); ++i)
        {
            const auto x = pointX (i, plot);
            g.setColour (juce::Colour::fromRGB (41, 49, 61));
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
            const auto* value = audioProcessor.parameters.getRawParameterValue (parameterIds[i]);
            const auto gain = value != nullptr ? value->load() : 0.0f;
            const juce::Point<float> point (x, gainToY (gain, plot));
            if (i == 0)
                response.startNewSubPath (point);
            else
                response.lineTo (point);
            g.setFont (leadFontOptions (14.0f));
            g.setColour (text);
            g.drawText (frequencyNames[i], juce::Rectangle<int> (juce::roundToInt (x - 42.0f),
                        juce::roundToInt (plot.getBottom() + 12.0f), 84, 18), juce::Justification::centred);
            g.setColour (secondaryText);
            g.drawText ((gain > 0.0f ? "+" : "") + juce::String (gain, 1),
                        juce::Rectangle<int> (juce::roundToInt (x - 38.0f), juce::roundToInt (point.y - 28.0f), 76, 17),
                        juce::Justification::centred);
        }
        g.setColour (juce::Colour (0xff66d6b2));
        g.strokePath (response, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        for (int i = 0; i < parameterIds.size(); ++i)
        {
            const auto* value = audioProcessor.parameters.getRawParameterValue (parameterIds[i]);
            const auto gain = value != nullptr ? value->load() : 0.0f;
            const juce::Point<float> point (pointX (i, plot), gainToY (gain, plot));
            g.setColour (juce::Colour::fromRGB (19, 24, 32));
            g.fillEllipse (point.x - 9.0f, point.y - 9.0f, 18.0f, 18.0f);
            g.setColour (juce::Colour (0xff66d6b2));
            g.fillEllipse (point.x - 5.0f, point.y - 5.0f, 10.0f, 10.0f);
        }
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        const auto plot = getPlotBounds();
        auto nearest = 0;
        auto nearestDistance = std::numeric_limits<float>::max();
        for (int i = 0; i < parameterIds.size(); ++i)
        {
            const auto distance = std::abs (event.position.x - pointX (i, plot));
            if (distance < nearestDistance) { nearestDistance = distance; nearest = i; }
        }
        activeBand = nearest;
        if (auto* parameter = audioProcessor.parameters.getParameter (parameterIds[activeBand]))
            parameter->beginChangeGesture();
        updateBand (event.position.y, plot);
    }

    void mouseDrag (const juce::MouseEvent& event) override { updateBand (event.position.y, getPlotBounds()); }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (activeBand >= 0)
            if (auto* parameter = audioProcessor.parameters.getParameter (parameterIds[activeBand]))
                parameter->endChangeGesture();
        activeBand = -1;
    }

private:
    juce::Rectangle<float> getPlotBounds() const
    {
        auto plot = getLocalBounds().toFloat().reduced (66.0f, 46.0f);
        plot.removeFromBottom (58.0f);
        return plot;
    }

    float pointX (int index, juce::Rectangle<float> plot) const
    {
        return plot.getX() + plot.getWidth() * static_cast<float> (index)
             / static_cast<float> (juce::jmax (1, parameterIds.size() - 1));
    }

    static float gainToY (float gain, juce::Rectangle<float> plot)
    {
        return juce::jmap (juce::jlimit (-12.0f, 12.0f, gain), -12.0f, 12.0f,
                           plot.getBottom(), plot.getY());
    }

    void updateBand (float y, juce::Rectangle<float> plot)
    {
        if (activeBand < 0)
            return;
        const auto gain = juce::jmap (juce::jlimit (plot.getY(), plot.getBottom(), y),
                                      plot.getBottom(), plot.getY(), -12.0f, 12.0f);
        if (auto* parameter = audioProcessor.parameters.getParameter (parameterIds[activeBand]))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (gain));
        repaint();
    }

    void resetBand (int band)
    {
        if (auto* parameter = audioProcessor.parameters.getParameter (parameterIds[band]))
            parameter->setValueNotifyingHost (parameter->getDefaultValue());
        repaint();
    }

    TheLeadAudioProcessor& audioProcessor;
    juce::StringArray parameterIds, frequencyNames;
    std::vector<std::unique_ptr<juce::TextButton>> resetButtons;
    int activeBand = -1;
};

class StageCard final : public juce::Component, private juce::Timer
{
public:
    StageCard (TheLeadAudioProcessor& processor, juce::String name, juce::String enableId,
               std::vector<ParameterControl> controls, FileKind fileKind = FileKind::none,
               int fileSlot = 0, std::function<void (int)> moveHandler = {},
               std::vector<ParameterControl> detailControls = {}, std::function<void()> contentSizeChanged = {})
        : audioProcessor (processor), lookAndFeel (processor), stageName (std::move (name)), enableParameterId (std::move (enableId)), loadKind (fileKind),
          slot (fileSlot), hasMoveControls (false), moveHandler (std::move (moveHandler)),
          contentSizeChangedHandler (std::move (contentSizeChanged))
    {
        setName (stageName);
        heading.setText (getVisibleStageName (stageName).toUpperCase(), juce::dontSendNotification);
        heading.setFont (leadFontOptions (19.0f, juce::Font::bold));
        heading.setColour (juce::Label::textColourId, text);
        addAndMakeVisible (heading);

        if (enableParameterId.isNotEmpty())
        {
            hasEnableControl = true;
            enableButton.setButtonText ("ENABLED");
            enableButton.setColour (juce::ToggleButton::textColourId, secondaryText);
            addAndMakeVisible (enableButton);
            enableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                audioProcessor.parameters, enableParameterId, enableButton);
        }

        if (hasMoveControls)
        {
            dragHandle.setText ("MOVE", juce::dontSendNotification);
            dragHandle.setFont (leadFontOptions (18.0f, juce::Font::bold));
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
            addAndMakeVisible (loadButton);

            fileName.setText (loadKind == FileKind::nam ? audioProcessor.getNAMCaptureName (slot)
                                                        : audioProcessor.getImpulseResponseName (slot),
                              juce::dontSendNotification);
            fileName.setFont (leadFontOptions (15.0f));
            fileName.setColour (juce::Label::textColourId, secondaryText);
            fileName.setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (fileName);

            loadButton.onClick = [this]
            {
                const auto filter = loadKind == FileKind::nam ? "*.nam" : "*.wav;*.aif;*.aiff;*.flac;*.ogg";
                fileChooser = std::make_unique<juce::FileChooser> (
                    loadKind == FileKind::nam ? "Choose a NAM capture" : "Choose an impulse response",
                    juce::File {}, filter);
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
                            safeThis->audioProcessor.loadNAMCapture (safeThis->slot, file);
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
            label->setFont (leadFontOptions (15.0f));
            label->setColour (juce::Label::textColourId, secondaryText);
            addAndMakeVisible (*label);
            controlLabels.push_back (std::move (label));

            if (control.toggle)
            {
                auto toggle = std::make_unique<juce::ToggleButton>();
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
            else if (stageName.startsWith ("NAM Profile") && control.id.endsWith ("Source"))
            {
                auto sourceButton = std::make_unique<juce::TextButton> ("NAM CAPTURE");
                sourceButton->setClickingTogglesState (false);
                sourceButton->setColour (juce::TextButton::buttonColourId, juce::Colour::fromRGB (40, 45, 54));
                sourceButton->setColour (juce::TextButton::textColourOffId, text);
                sourceButton->setTooltip ("Click to switch this amp slot between a NAM capture and a built-in amp sim.");
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
                choice->addItemList (control.choices, 1);
                addAndMakeVisible (*choice);
                comboAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                    audioProcessor.parameters, control.id, *choice));
                if ((stageName == "Boost Pedal" && juce::String (control.id) == "boostModel")
                    || (stageName.startsWith ("NAM Profile")
                        && (juce::String (control.id).endsWith ("Source") || juce::String (control.id).endsWith ("Model"))))
                    choice->onChange = [this]
                    {
                        updateConditionalControlVisibility();
                        resized();
                        if (contentSizeChangedHandler)
                            contentSizeChangedHandler();
                    };
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
                valueLabel->setFont (leadFontOptions (16.0f));
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
        const auto rowPitch = compactMode ? 112 : 148;
        const auto controlHeight = juce::jmax (selectorCount * 72, rows * rowPitch);
        const auto fileControlsHeight = loadKind == FileKind::none || isUsingInternalAmp() ? 0 : 46;
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

        if (loadKind != FileKind::none && ! isUsingInternalAmp())
        {
            auto fileRow = area.removeFromTop (40);
            loadButton.setBounds (fileRow.removeFromLeft (130));
            fileRow.removeFromLeft (10);
            fileName.setBounds (fileRow);
            area.removeFromTop (3);
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
        const auto rowPitch = compactMode ? 112 : 148;
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
        const auto columns = juce::jmin (selectors.empty() ? 5 : 4, static_cast<int> (knobs.size()));
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
            controlLabels[index]->setBounds (cell.removeFromTop (compactMode ? 19 : 22));
            if (dynamic_cast<juce::Slider*> (controlWidgets[index]) != nullptr)
            {
                auto valueArea = cell.removeFromBottom (compactMode ? 18 : 20);
                if (widgetValueLabels[index] != nullptr)
                    widgetValueLabels[index]->setBounds (valueArea);
                const auto knobWidth = juce::jmin (compactMode ? 86 : 102, cell.getWidth());
                auto knob = cell.withSizeKeepingCentre (knobWidth, compactMode ? 72 : 94);
                controlWidgets[index]->setBounds (knob);
            }
            else
                controlWidgets[index]->setBounds (cell.removeFromTop (42));
        }
    }

private:
    bool isControlVisible (size_t index) const
    {
        if (index >= controlIds.size())
            return true;
        if (stageName.startsWith ("NAM Profile"))
        {
            const auto prefix = "amp" + stageName.fromLastOccurrenceOf (" ", false, false);
            const auto* source = audioProcessor.parameters.getRawParameterValue (prefix + "Source");
            const auto isInternal = source != nullptr && source->load() >= 0.5f;
            const auto& id = controlIds[index];
            if (id == prefix + "Source" || id == prefix + "In" || id == prefix + "Out")
                return true;
            if (! isInternal)
                return false;
            if (id == prefix + "Model")
                return true;
            const auto* selectedModel = audioProcessor.parameters.getRawParameterValue (prefix + "Model");
            const auto model = selectedModel != nullptr
                ? juce::jlimit (0, 11, juce::roundToInt (selectedModel->load())) : 0;
            const auto suffix = id.fromFirstOccurrenceOf (prefix, false, false);
            return usesAmpSimControl (model, suffix);
        }
        if (stageName != "Boost Pedal")
            return true;
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
                    sourceButton->setButtonText (isSim ? "AMP SIM" : "NAM CAPTURE");
                    sourceButton->setColour (juce::TextButton::buttonColourId,
                                             isSim ? juce::Colour (0xff21473f) : juce::Colour::fromRGB (40, 45, 54));
                    sourceButton->setColour (juce::TextButton::textColourOffId,
                                             isSim ? knobAccent : text);
                    sourceButton->setTooltip (isSim
                        ? "Built-in algorithmic amp sim. Click to switch to a NAM capture."
                        : "NAM capture profile. Click to switch to a built-in amp sim.");
                }
            }
        }
        if (loadKind == FileKind::nam)
        {
            const auto showCapture = ! isUsingInternalAmp();
            loadButton.setVisible (showCapture);
            fileName.setVisible (showCapture);
        }
    }

    bool isUsingInternalAmp() const
    {
        if (loadKind != FileKind::nam)
            return false;
        const auto parameterId = "amp" + juce::String (slot + 1) + "Source";
        const auto* source = audioProcessor.parameters.getRawParameterValue (parameterId);
        return source != nullptr && source->load() >= 0.5f;
    }

    static bool usesAmpSimControl (int model, const juce::String& suffix)
    {
        // Parameter indices follow 0xFX's fx_amp_param_t ordering.
        static const std::array<std::array<int, 7>, 12> models {{
            {{ 1, 4, 3, 2, 5, 0, 6 }}, {{ 5, 2, 3, 4, 1, 0, 7 }},
            {{ 0, 2, 3, 4, 5, 7, -1 }}, {{ 1, 2, 4, 9, 5, 0, -1 }},
            {{ 1, 2, 4, 0, 7, -1, -1 }}, {{ 0, 2, 3, 4, 5, 1, 7 }},
            {{ 0, 2, 3, 4, 1, -1, -1 }}, {{ 0, 10, 1, -1, -1, -1, -1 }},
            {{ 0, 2, 3, 4, 5, 1, 7 }}, {{ 0, 2, 3, 4, 1, 7, -1 }},
            {{ 0, 2, 3, 4, 11, 1, -1 }}, {{ 1, 4, 3, 2, 5, 0, 6 }}
        }};
        static const juce::StringArray parameterNames {
            "Gain", "Volume", "Bass", "Mid", "Treble", "Presence",
            "Sag", "Master", "Bright", "Cut", "Tone", "Feedback"
        };
        if (model < 0 || model >= static_cast<int> (models.size()))
            return false;
        for (const auto parameter : models[static_cast<size_t> (model)])
            if (parameter >= 0 && parameterNames[parameter] == suffix)
                return true;
        return false;
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
        if (loadKind == FileKind::nam && loadButton.isVisible() == isUsingInternalAmp())
        {
            updateConditionalControlVisibility();
            resized();
            if (contentSizeChangedHandler)
                contentSizeChangedHandler();
        }
        if (loadKind == FileKind::nam && ! isUsingInternalAmp())
        {
            const auto status = audioProcessor.getNAMCaptureStatus (slot);
            if (status == "Loaded")
                fileName.setText (("LOADED: " + audioProcessor.getNAMCaptureName (slot)).toUpperCase(), juce::dontSendNotification);
            else if (status.isNotEmpty())
                fileName.setText (status, juce::dontSendNotification);
            else
                fileName.setText (audioProcessor.getNAMCaptureName (slot), juce::dontSendNotification);
        }
        repaint();
    }

    TheLeadAudioProcessor& audioProcessor;
    LeadLookAndFeel lookAndFeel;
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
    juce::TextButton loadButton, moveUpButton, moveDownButton;
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
};

static void drawAmpHeadIcon (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour);

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
            setTooltip ("Choose a NAM capture or one of NAMinator's built-in algorithmic amp models for this slot.");
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

    void setParallelRouting (bool parallel) { parallelRouting = parallel; }

    void paint (juce::Graphics& g) override
    {
        g.beginTransparencyLayer (isBlockEnabled() ? 1.0f : 0.32f);
        auto r = getLocalBounds().toFloat();
        const auto cornerRadius = getWidth() < 104 ? 23.0f : 26.0f;
        g.setColour (isSelected ? juce::Colour::fromRGB (28, 39, 54) : tileSurface);
        g.fillRoundedRectangle (r, cornerRadius);
        const auto kind = getModuleKind();
        const auto ioBlock = kind == ModuleKind::input || kind == ModuleKind::output;
        const auto borderColour = isSelected ? selectionAccent
                               : ioBlock ? juce::Colour::fromRGB (72, 79, 92)
                                         : juce::Colour (0xff70d9b6);
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
            const auto kind = getModuleKind();
            if (kind == ModuleKind::nam && isInternalAmpSelected())
                drawAmpHeadIcon (g, iconBounds, getIconColour (ModuleKind::nam));
            else
                drawModuleIcon (g, iconBounds, kind);
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
            g.setFont (leadFontOptions (markerSize * 0.72f, juce::Font::bold));
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

    bool isInternalAmpSelected() const
    {
        if (! stageName.startsWith ("NAM Profile"))
            return false;
        const auto slot = stageName.fromLastOccurrenceOf (" ", false, false);
        const auto* source = parameterState.getRawParameterValue ("amp" + slot + "Source");
        return source != nullptr && source->load() >= 0.5f;
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
            case ModuleKind::nam: return juce::Colour (0xff79baff);
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
    bool parallelRouting = false;
    std::function<void (int)> reorderHandler;
    std::function<void()> selectHandler;
    juce::TextButton moveUp, moveDown;
};

static void drawAmpHeadIcon (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour)
{
    g.setColour (colour);
    const auto body = r.reduced (2.5f, 4.0f);
    g.drawRoundedRectangle (body, 3.0f, 1.8f);
    g.drawLine (body.getX() + 2.0f, body.getY() + 7.0f,
                body.getRight() - 2.0f, body.getY() + 7.0f, 1.4f);
    for (int i = 0; i < 3; ++i)
        g.drawEllipse (body.getX() + 4.0f + i * 6.0f, body.getY() + 2.0f, 2.0f, 2.0f, 1.1f);
    g.drawRoundedRectangle (juce::Rectangle<float> (body.getX() + 4.0f, body.getY() + 11.0f,
                                                      body.getWidth() - 8.0f, body.getHeight() - 14.0f),
                             1.5f, 1.2f);
}

void StageCard::drawBlockIcon (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    if (stageName.startsWith ("NAM Profile") && isUsingInternalAmp())
        drawAmpHeadIcon (g, bounds, juce::Colour (0xff79baff));
    else
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
            tile->setParallelRouting (parallelRouting);
            tile->setVisible (! (parallelRouting && parallelSharedIR && tile->getName() == "Cab IR 2"));
        }
        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto wire = juce::Colour (0xff70d9b6);
        g.setFont (leadFontOptions (15.0f, juce::Font::bold));
        g.setColour (secondaryText);
        if (parallelRouting)
        {
            g.drawText (parallelSharedIR ? "AMP PARALLEL  |  BOTH AMPS -> IR 1"
                                         : "AMP PARALLEL  |  AMP 1 -> IR 1  /  AMP 2 -> IR 2",
                        0, 2, getWidth(), 18, juce::Justification::centred);
            const auto tileWidth = juce::jmax (90, juce::jmin (92, juce::roundToInt ((getWidth() - 20) / 8.0f) - 12));
            const auto postY = 50 + 2 * (tileWidth + 12) + 10 + tileWidth + 8;
            g.drawText ("POST FX  /  OUTPUT", 10, postY, getWidth() - 20, 18, juce::Justification::centredLeft);
            drawParallelWiring (g, wire);
            return;
        }

        g.drawText ("AMP SERIES  |  CAB 1 / CAB 2 BLEND", 0, 2, getWidth(), 18,
                    juce::Justification::centred);
        drawSerialWiring (g, wire);
    }

    void setOrderedTiles (std::vector<StageTile*> ordered)
    {
        tiles = std::move (ordered);
        for (auto* tile : tiles)
        {
            tile->setParallelRouting (parallelRouting);
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
    explicit ChainControls (TheLeadAudioProcessor& processor, std::function<void (bool)> fullscreenChanged)
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
        addStage ("Boost Pedal", "boostOn", {{ "boostGain", "Post Volume (dB)" }}, FileKind::none, 0, mover (true, 2),
                  {{ "boostModel", "Model", false, juce::StringArray { "Clean Boost", "808 Drive" } },
                   { "boostGain", "Post Volume (dB)" }, { "boostBass", "Bass (dB)" },
                   { "boostMid", "Mid (dB)" }, { "boostTreble", "Treble (dB)" },
                   { "boost808Drive", "Drive (dB)" }, { "boost808Tone", "Tone (Hz)" },
                   { "boost808Level", "Level (dB)" }});
        addStage ("PreEQ", "preEqOn",
                  {{ "preLow", "Low (dB)" }, { "preMid", "Mid (dB)" }, { "preHigh", "High (dB)" }},
                  FileKind::none, 0, mover (true, 3),
                  {{ "preLow", "80 Hz" }, { "preBand2", "250 Hz" }, { "preMid", "800 Hz" },
                   { "preBand4", "2.5 kHz" }, { "preHigh", "8 kHz" }});

        for (int slot = 0; slot < TheLeadAudioProcessor::numAmpSlots; ++slot)
        {
            const auto number = juce::String (slot + 1);
            const auto prefix = "amp" + number;
            const juce::StringArray ampModels {
                "Fullerton Clean", "British Crunch", "Southwest Lead", "Essex Chime",
                "Tweed Blues", "Meridian High Gain", "Citrus Roar", "Citrus Terror",
                "Regent 800", "Solar Monolith", "Eclipse Drone", "Emerald Ratrod Deluxe"
            };
            addStage ("NAM Profile " + number, "amp" + number + "On",
                      {{ prefix + "Source", "Source", false, juce::StringArray { "NAM Capture", "Amp Sim" } },
                       { prefix + "Model", "Amp Model", false, ampModels },
                       { prefix + "Gain", "Gain", true }, { prefix + "Volume", "Volume", true },
                       { prefix + "Bass", "Bass", true }, { prefix + "Mid", "Mid", true },
                       { prefix + "Treble", "Treble", true }, { prefix + "Presence", "Presence", true },
                       { prefix + "Sag", "Sag", true }, { prefix + "Master", "Master", true },
                       { prefix + "Bright", "Bright", true }, { prefix + "Cut", "Cut", true },
                       { prefix + "Tone", "Tone", true }, { prefix + "Feedback", "Feedback", true },
                       { prefix + "In", "Input (dB)" }, { prefix + "Out", "Output (dB)" }},
                      FileKind::nam, slot, ampMover (slot));
        }

        for (int slot = 0; slot < TheLeadAudioProcessor::numIRSlots; ++slot)
        {
            const auto number = juce::String (slot + 1);
            addStage ("Cab IR " + number, "ir" + number + "On", {},
                      FileKind::impulseResponse, slot, irMover (slot),
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
                   { "delayDuck", "Ducking", true }, { "delayDuckThreshold", "Duck Threshold (dB)" },
                   { "delayDuckRelease", "Duck Release (ms)" }});
        addStage ("Reverb", "reverbOn", {{ "reverbSize", "Size", true }, { "reverbMix", "Mix", true }},
                  FileKind::none, 0, mover (false, 1),
                  {{ "reverbMode", "Algorithm", false, juce::StringArray { "Room", "Plate", "Cloud" } },
                   { "reverbSize", "Size / Decay", true }, { "reverbDamping", "Damping", true },
                   { "reverbPreDelay", "Pre-Delay (ms)" }, { "reverbWidth", "Width", true },
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
        g.setFont (leadFontOptions (14.0f, juce::Font::bold));
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

    std::function<void (int)> irMover (int stageId)
    {
        return [this, stageId] (int delta)
        {
            auto order = audioProcessor.getIRSlotOrder();
            swapNeighbor (order, stageId, delta);
            audioProcessor.setIRSlotOrder (order);
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
        for (const auto slot : audioProcessor.getIRSlotOrder())
            ordered.push_back (findCard ("Cab IR " + juce::String (slot + 1)));
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

    TheLeadAudioProcessor& audioProcessor;
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

TheLeadAudioProcessorEditor::TheLeadAudioProcessorEditor (TheLeadAudioProcessor& processor)
    : AudioProcessorEditor (&processor), audioProcessor (processor),
      tooltipWindow (this, 650),
      editorLookAndFeel (std::make_unique<LeadLookAndFeel> (processor)),
      chainControls (std::make_unique<ChainControls> (processor,
          [this] (bool fullscreen) { setEqFullscreen (fullscreen); }))
{
    audioProcessor.capturePresetSnapshot();
    startTimerHz (20);
    setLookAndFeel (editorLookAndFeel.get());
    setSize (1600, 900);
    setResizable (true, true);
    setResizeLimits (1200, 760, 1920, 1200);
    title.setText ("NAMinator", juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions().withTypeface (getBrandTypeface()).withHeight (29.0f).withStyle ("Bold")));
    title.setColour (juce::Label::textColourId, text);
    addAndMakeVisible (title);
    subtitle.setText ("CAPTURE SIGNAL WORKSTATION", juce::dontSendNotification);
    subtitle.setFont (leadFontOptions (9.0f, juce::Font::bold));
    subtitle.setColour (juce::Label::textColourId, secondaryText);
    addAndMakeVisible (subtitle);

    presetLabel.setText ("PRESET", juce::dontSendNotification);
    presetLabel.setFont (leadFontOptions (14.0f, juce::Font::bold));
    presetLabel.setColour (juce::Label::textColourId, secondaryText);
    presetLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (presetLabel);
    presetBox.setTextWhenNothingSelected ("CHOOSE PRESET");
    savePresetButton.setButtonText ("SAVE");
    deletePresetButton.setButtonText ("DELETE");
    resetChainButton.setButtonText ("RESET CHAIN");
    resetChainButton.setTooltip ("Restore the default signal chain and default parameter values");
    presetBox.setTooltip ("Load a saved NAMinator preset");
    pitchLabel.setText ("PITCH", juce::dontSendNotification);
    pitchLabel.setFont (leadFontOptions (14.0f, juce::Font::bold));
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
    pitchValueLabel.setFont (leadFontOptions (14.0f, juce::Font::bold));
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
    addAndMakeVisible (presetBox);
    savePresetButton.setTooltip ("Save the current signal chain as a preset");
    deletePresetButton.setTooltip ("Delete the selected user preset");
    addAndMakeVisible (savePresetButton);
    addAndMakeVisible (deletePresetButton);
    addAndMakeVisible (resetChainButton);
    refreshUserPresets();
    presetBox.onChange = [this]
    {
        const auto name = presetBox.getText().trim();
        if (name.isNotEmpty() && audioProcessor.loadUserPreset (name))
        {
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
        audioProcessor.capturePresetSnapshot();
        presetDirty = false;
        presetBox.setSelectedId (0, juce::dontSendNotification);
        refreshUserPresets();
        updateRoutingDisplay();
    };

    ampRoutingBox.addItem ("SERIES", 1);
    ampRoutingBox.addItem ("PARALLEL BLEND", 2);
    ampRoutingBox.setTooltip (
        "Series: NAM captures feed one another, e.g. Boost capture > OD capture > Amp capture.\n"
        "Parallel Blend: NAM profiles run side by side and blend into the shared Cab IR section.");
    addAndMakeVisible (ampRoutingBox);
    ampRoutingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.parameters, "ampRouting", ampRoutingBox);

    parallelIRBox.addItem ("PAIRED IRs", 1);
    parallelIRBox.addItem ("SHARED IR 1", 2);
    parallelIRBox.setTooltip ("Paired IRs: Amp 1 to IR 1 and Amp 2 to IR 2. Shared IR 1: blend both amp outputs, then process them through IR 1.");
    addAndMakeVisible (parallelIRBox);
    parallelIRAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.parameters, "parallelIRMode", parallelIRBox);
    parallelIRBox.onChange = [this] { updateRoutingDisplay(); };
    parallelIRBox.setVisible (false);

    blendLabel.setText ("CAB 1 / CAB 2", juce::dontSendNotification);
    blendLabel.setFont (leadFontOptions (14.0f, juce::Font::bold));
    blendLabel.setColour (juce::Label::textColourId, secondaryText);
    blendLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (blendLabel);
    blendValueLabel.setFont (leadFontOptions (14.0f, juce::Font::bold));
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

    ampRoutingBox.onChange = [this] { updateRoutingDisplay(); };
    updateRoutingDisplay();
    addAndMakeVisible (*chainControls);

}

TheLeadAudioProcessorEditor::~TheLeadAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void TheLeadAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (juce::Colour (0xff66d6b2));
    g.fillRect (getLocalBounds().removeFromTop (3));
}

void TheLeadAudioProcessorEditor::resized()
{
    if (eqFullscreen)
    {
        chainControls->setBounds (getLocalBounds().reduced (12));
        return;
    }
    auto area = getLocalBounds().reduced (20);
    auto top = area.removeFromTop (42);
    auto titleArea = top.removeFromLeft (260);
    title.setBounds (titleArea.removeFromTop (28));
    subtitle.setBounds (titleArea);
    auto presetArea = top;
    presetLabel.setBounds (presetArea.removeFromLeft (58));
    presetArea.removeFromLeft (8);
    presetBox.setBounds (presetArea.removeFromLeft (190).reduced (0, 4));
    presetArea.removeFromLeft (8);
    savePresetButton.setBounds (presetArea.removeFromLeft (76).reduced (0, 4));
    presetArea.removeFromLeft (6);
    deletePresetButton.setBounds (presetArea.removeFromLeft (76).reduced (0, 4));
    presetArea.removeFromLeft (6);
    resetChainButton.setBounds (presetArea.removeFromLeft (104).reduced (0, 4));
    presetArea.removeFromLeft (18);
    pitchLabel.setBounds (presetArea.removeFromLeft (54).reduced (0, 4));
    presetArea.removeFromLeft (8);
    pitchSlider.setBounds (presetArea.removeFromLeft (160).reduced (0, 10));
    presetArea.removeFromLeft (4);
    pitchValueLabel.setBounds (presetArea.removeFromLeft (54).reduced (0, 4));

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

void TheLeadAudioProcessorEditor::setEqFullscreen (bool shouldBeFullscreen)
{
    eqFullscreen = shouldBeFullscreen;
    const auto showChrome = ! shouldBeFullscreen;
    title.setVisible (showChrome);
    subtitle.setVisible (showChrome);
    presetLabel.setVisible (showChrome);
    presetBox.setVisible (showChrome);
    savePresetButton.setVisible (showChrome);
    deletePresetButton.setVisible (showChrome);
    resetChainButton.setVisible (showChrome);
    pitchLabel.setVisible (showChrome);
    pitchSlider.setVisible (showChrome);
    pitchValueLabel.setVisible (showChrome);
    blendLabel.setVisible (showChrome);
    blendValueLabel.setVisible (showChrome);
    blendSlider.setVisible (showChrome);
    ampRoutingBox.setVisible (showChrome);
    parallelIRBox.setVisible (showChrome && parallelIRBox.getSelectedId() == 2);
    if (showChrome)
        updateRoutingDisplay();
    resized();
}

void TheLeadAudioProcessorEditor::refreshUserPresets (const juce::String& selectName)
{
    const auto previousName = selectName.isNotEmpty() ? selectName : selectedPresetName();
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

juce::String TheLeadAudioProcessorEditor::selectedPresetName() const
{
    auto name = presetBox.getText().trim();
    if (name.endsWith (" *"))
        name = name.dropLastCharacters (2).trimEnd();
    return name;
}

void TheLeadAudioProcessorEditor::timerCallback()
{
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

void TheLeadAudioProcessorEditor::updateRoutingDisplay()
{
    const auto* parameter = audioProcessor.parameters.getRawParameterValue ("ampRouting");
    const auto parallel = parameter != nullptr && juce::roundToInt (parameter->load()) == 1;
    const auto* irMode = audioProcessor.parameters.getRawParameterValue ("parallelIRMode");
    const auto sharedIR = irMode != nullptr && irMode->load() >= 0.5f;
    blendAttachment.reset();
    const auto blendParameterId = parallel ? juce::String ("ampParallelBlend") : juce::String ("irParallelBlend");
    blendSlider.setName (blendParameterId);
    blendAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.parameters, blendParameterId, blendSlider);
    ampRoutingBox.setSelectedId (parallel ? 2 : 1, juce::dontSendNotification);
    parallelIRBox.setVisible (parallel);
    blendLabel.setText (parallel ? "AMP 1 / AMP 2" : "CAB 1 / CAB 2", juce::dontSendNotification);
    blendSlider.setTooltip (parallel
        ? "Amp 1 to IR 1 and Amp 2 to IR 2. This controls the Amp 1 share; disabling either amp sends 100% through the other path."
        : "Centre is 0 (equal blend); negative favours CAB 2, positive favours CAB 1. If one cab is disabled, the other is 100%.");
    chainControls->setParallelRouting (parallel, sharedIR);
    updateBlendValue();
    resized();
}

void TheLeadAudioProcessorEditor::updateBlendValue()
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

void TheLeadAudioProcessorEditor::showSavePresetPrompt()
{
    auto* alert = new juce::AlertWindow ("Save preset", "Name this signal-chain preset.",
                                         juce::MessageBoxIconType::NoIcon);
    const auto currentName = presetBox.getText().trim();
    alert->addTextEditor ("presetName", currentName.isNotEmpty() ? currentName : "My Preset", "Preset name:");
    alert->addButton ("Save", 1);
    alert->addButton ("Cancel", 0);
    juce::Component::SafePointer<TheLeadAudioProcessorEditor> safeThis (this);
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
        safeThis->presetDirty = false;
        safeThis->refreshUserPresets (name);
    }), true);
}

void TheLeadAudioProcessorEditor::showDeletePresetPrompt()
{
    const auto name = presetBox.getText().trim();
    if (name.isEmpty()) return;
    auto* alert = new juce::AlertWindow ("Delete preset?", "Delete “" + name + "” from your user presets?",
                                         juce::MessageBoxIconType::WarningIcon);
    alert->addButton ("Delete", 1);
    alert->addButton ("Cancel", 0);
    juce::Component::SafePointer<TheLeadAudioProcessorEditor> safeThis (this);
    alert->enterModalState (true, juce::ModalCallbackFunction::create ([safeThis, name] (int result)
    {
        if (result != 1 || safeThis == nullptr) return;
        safeThis->audioProcessor.deleteUserPreset (name);
        safeThis->audioProcessor.capturePresetSnapshot();
        safeThis->presetDirty = false;
        safeThis->refreshUserPresets();
    }), true);
}
