#include <juce_core/system/juce_TargetPlatform.h>
#include <juce_audio_plugin_client/detail/juce_CheckSettingMacros.h>
#include <juce_audio_plugin_client/detail/juce_IncludeSystemHeaders.h>
#include <juce_audio_plugin_client/detail/juce_IncludeModuleHeaders.h>
#include <juce_audio_plugin_client/detail/juce_PluginUtilities.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace
{
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
        pluginHolder = nullptr;
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
            mainWindow->saveState();
        else if (pluginHolder != nullptr)
            pluginHolder->savePluginState();

        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            juce::Timer::callAfterDelay (100, []
            {
                if (auto* app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
            return;
        }

        quit();
    }

private:
    std::unique_ptr<juce::StandalonePluginHolder> createPluginHolder()
    {
       #ifdef JucePlugin_PreferredChannelConfigurations
        constexpr juce::StandalonePluginHolder::PluginInOuts channels[] { JucePlugin_PreferredChannelConfigurations };
        const juce::Array<juce::StandalonePluginHolder::PluginInOuts> channelConfig (
            channels, juce::numElementsInArray (channels));
       #else
        const juce::Array<juce::StandalonePluginHolder::PluginInOuts> channelConfig;
       #endif

        return std::make_unique<juce::StandalonePluginHolder> (appProperties.getUserSettings(),
                                                                false,
                                                                juce::String{}, nullptr,
                                                                channelConfig, false);
    }

    juce::ApplicationProperties appProperties;
    std::unique_ptr<NAMplifiedStandaloneWindow> mainWindow;
    std::unique_ptr<juce::StandalonePluginHolder> pluginHolder;
};
} // namespace

JUCE_CREATE_APPLICATION_DEFINE (NAMplifiedStandaloneApp)
