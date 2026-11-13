#include <juce_core/system/juce_TargetPlatform.h>
#include <juce_audio_plugin_client/detail/juce_CheckSettingMacros.h>
#include <juce_audio_plugin_client/detail/juce_IncludeSystemHeaders.h>
#include <juce_audio_plugin_client/detail/juce_IncludeModuleHeaders.h>
#include <juce_audio_plugin_client/detail/juce_PluginUtilities.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include "PluginProcessor.h"

namespace
{
class AudioMidiSettingsWindow final : public juce::DialogWindow
{
public:
    explicit AudioMidiSettingsWindow (juce::StandalonePluginHolder& holder)
        : DialogWindow ("Audio / MIDI Settings", juce::Colour (0xff141922), true, true)
    {
        setUsingNativeTitleBar (false);
        setResizable (false, false);
        if (auto* editor = holder.processor->getActiveEditor())
            setLookAndFeel (&editor->getLookAndFeel());
        setContentOwned (new Content (holder, [this] { closeButtonPressed(); }), true);
    }

    ~AudioMidiSettingsWindow() override
    {
        clearContentComponent();
        setLookAndFeel (nullptr);
    }

    void closeButtonPressed() override { setVisible (false); }

private:
    class Content final : public Component
    {
    public:
        Content (juce::StandalonePluginHolder& holder, std::function<void()> onClose)
            : selector (holder.deviceManager,
                        0, holder.processor->getBus (true, 0) != nullptr
                            ? holder.processor->getBus (true, 0)->getDefaultLayout().size() : 0,
                        0, holder.processor->getBus (false, 0) != nullptr
                            ? holder.processor->getBus (false, 0)->getDefaultLayout().size() : 0,
                        true, holder.processor->producesMidi(), true, false)
        {
            heading.setText ("Audio / MIDI", juce::dontSendNotification);
            heading.setFont (juce::FontOptions (24.0f, juce::Font::bold));
            heading.setColour (juce::Label::textColourId, juce::Colour (0xffeef2f7));
            addAndMakeVisible (heading);

            mute.getToggleStateValue().referTo (holder.getMuteInputValue());
            mute.setColour (juce::ToggleButton::textColourId, juce::Colour (0xffc5cfdd));
            mute.setColour (juce::ToggleButton::tickColourId, juce::Colour (0xff70d9b6));
            addChildComponent (mute);
            mute.setVisible (holder.getProcessorHasPotentialFeedbackLoop());

            selector.setItemHeight (32);
            viewport.setViewedComponent (&selector, false);
            viewport.setScrollBarsShown (true, false);
            addAndMakeVisible (viewport);
            close.onClick = std::move (onClose);
            addAndMakeVisible (close);
            setSize (620, 580);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff141922));
            g.setColour (juce::Colour (0xff384553));
            g.drawHorizontalLine (58, 24.0f, static_cast<float> (getWidth() - 24));
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (24, 14);
            heading.setBounds (area.removeFromTop (40));
            area.removeFromTop (12);
            if (mute.isVisible())
                mute.setBounds (area.removeFromTop (36));
            auto footer = area.removeFromBottom (38);
            close.setBounds (footer.removeFromRight (94));
            area.removeFromBottom (8);
            viewport.setBounds (area);
            selector.setSize (juce::jmax (1, area.getWidth() - viewport.getScrollBarThickness()), area.getHeight());
        }

    private:
        juce::Label heading;
        juce::ToggleButton mute { "Mute audio input to prevent feedback" };
        juce::AudioDeviceSelectorComponent selector;
        juce::Viewport viewport;
        juce::TextButton close { "Close" };
    };
};

class NAMplifiedStandaloneWindow final : public juce::StandaloneFilterWindow
{
public:
    NAMplifiedStandaloneWindow (const juce::String& title,
                               juce::Colour background,
                               std::unique_ptr<juce::StandalonePluginHolder> holder)
        : StandaloneFilterWindow (title, background, std::move (holder))
    {
       #if JUCE_MAC
        setUsingNativeTitleBar (true);
        setTitleBarButtonsRequired (juce::DocumentWindow::allButtons, false);
       #endif
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplicationBase::getInstance()->systemRequestedQuit();
    }

    NAMplifiedAudioProcessor* getNAMplifiedProcessor() const
    {
        return dynamic_cast<NAMplifiedAudioProcessor*> (pluginHolder->processor.get());
    }

    void saveState()
    {
        if (pluginHolder != nullptr)
            pluginHolder->savePluginState();
    }
};

class NAMplifiedStandaloneApp final : public juce::JUCEApplication
{
public:
    NAMplifiedStandaloneApp()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = juce::CharPointer_UTF8 (JucePlugin_Name);
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        options.folderName = "~/.config";
       #else
        options.folderName = "";
       #endif
        appProperties.setStorageParameters (options);
    }

    const juce::String getApplicationName() override { return juce::CharPointer_UTF8 (JucePlugin_Name); }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted (const juce::String&) override {}

    void initialise (const juce::String&) override
    {
        if (! juce::Desktop::getInstance().getDisplays().displays.isEmpty())
        {
            mainWindow = std::make_unique<NAMplifiedStandaloneWindow> (
                getApplicationName(),
                juce::LookAndFeel::getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId),
                createPluginHolder());
            mainWindow->setVisible (true);
        }
        else
            pluginHolder = createPluginHolder();
    }

    void shutdown() override
    {
        audioSettings.reset();
        pluginHolder = nullptr;
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (quitPromptOpen)
            return;

        auto* processor = mainWindow != nullptr ? mainWindow->getNAMplifiedProcessor() : nullptr;
        if (processor == nullptr || ! processor->hasUnsavedParameterChanges())
        {
            finishQuit();
            return;
        }

        quitPromptOpen = true;
        auto* alert = new juce::AlertWindow ("Unsaved preset changes",
                                             "Save your preset changes before quitting?",
                                             juce::MessageBoxIconType::NoIcon, mainWindow.get());
        if (auto* editor = processor->getActiveEditor())
            alert->setLookAndFeel (&editor->getLookAndFeel());
        alert->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        alert->addButton ("Discard", 2);
        alert->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        alert->enterModalState (true, juce::ModalCallbackFunction::create ([this, processor, alert] (int result)
        {
            alert->setLookAndFeel (nullptr);
            quitPromptOpen = false;
            if (result == 2)
            {
                processor->restorePresetSnapshot();
                finishQuit();
            }
            else if (result == 1)
            {
                processor->promptToSavePreset ([this] { finishQuit(); });
            }
        }), true);
    }

private:
    void finishQuit()
    {
        if (mainWindow != nullptr)
            mainWindow->saveState();
        else if (pluginHolder != nullptr)
            pluginHolder->savePluginState();

        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            juce::Timer::callAfterDelay (100, [this] { finishQuit(); });
            return;
        }
        quit();
    }

    bool quitPromptOpen = false;

    std::unique_ptr<juce::StandalonePluginHolder> createPluginHolder()
    {
       #ifdef JucePlugin_PreferredChannelConfigurations
        constexpr juce::StandalonePluginHolder::PluginInOuts channels[] { JucePlugin_PreferredChannelConfigurations };
        const juce::Array<juce::StandalonePluginHolder::PluginInOuts> channelConfig (
            channels, juce::numElementsInArray (channels));
       #else
        const juce::Array<juce::StandalonePluginHolder::PluginInOuts> channelConfig;
       #endif

        auto holder = std::make_unique<juce::StandalonePluginHolder> (appProperties.getUserSettings(),
                                                                       false,
                                                                       juce::String{}, nullptr,
                                                                       channelConfig, false);
        if (auto* processor = dynamic_cast<NAMplifiedAudioProcessor*> (holder->processor.get()))
        {
            auto* holderForCallback = holder.get();
            processor->setAudioSettingsCallback ([this, holderForCallback]
            {
                if (audioSettings == nullptr)
                {
                    audioSettings = std::make_unique<AudioMidiSettingsWindow> (*holderForCallback);
                    audioSettings->centreAroundComponent (mainWindow.get(), audioSettings->getWidth(),
                                                          audioSettings->getHeight());
                }
                audioSettings->setVisible (true);
                audioSettings->toFront (true);
            });
        }
        return holder;
    }

    juce::ApplicationProperties appProperties;
    std::unique_ptr<NAMplifiedStandaloneWindow> mainWindow;
    std::unique_ptr<juce::StandalonePluginHolder> pluginHolder;
    std::unique_ptr<AudioMidiSettingsWindow> audioSettings;
};
} // namespace

JUCE_CREATE_APPLICATION_DEFINE (NAMplifiedStandaloneApp)
