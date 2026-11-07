#pragma once

#include <JuceHeader.h>
#include <functional>
#include <vector>

namespace tone3000
{
struct Model
{
    juce::String name;
    juce::String architecture;
    juce::String modelUrl;
    juce::String toneTitle;
    juce::String toneUrl;
    juce::String creator;
    juce::String creatorUrl;
    juce::String license;
};

class Client final : private juce::Thread
{
public:
    using ModelsCallback = std::function<void (std::vector<Model>, juce::String)>;
    using DownloadCallback = std::function<void (juce::File, juce::String)>;

    Client();
    ~Client() override;

    bool startSelectFlow (int architecture, ModelsCallback callback);
    bool downloadModel (const Model& model, DownloadCallback callback);
    void cancel();

private:
    enum class Operation { none, select, download };
    void run() override;
    void runSelectFlow();
    void runModelDownload();

    juce::StreamingSocket listener;
    juce::String codeVerifier, expectedState, accessToken;
    juce::String authorizationCode;
    juce::String selectedToneId;
    int architectureToBrowse = 2;
    Model selectedModel;
    Operation operation = Operation::none;
    ModelsCallback modelsCallback;
    DownloadCallback downloadCallback;
};
}
