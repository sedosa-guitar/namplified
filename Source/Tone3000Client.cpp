#include "Tone3000Client.h"
#include "Tone3000Config.h"
#include <juce_cryptography/juce_cryptography.h>
#include <array>
#include <memory>

namespace tone3000
{
namespace
{
constexpr int callbackPort = 49157;
constexpr auto callbackPath = "/tone3000/callback";
constexpr auto apiRoot = "https://www.tone3000.com/api/v1";

juce::String urlEncode (const juce::String& value)
{
    const auto utf8 = value.toUTF8();
    juce::String result;
    constexpr char hex[] = "0123456789ABCDEF";
    for (auto* p = reinterpret_cast<const unsigned char*> (utf8.getAddress()); *p != 0; ++p)
    {
        const auto c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
            || c == '-' || c == '_' || c == '.' || c == '~')
            result += juce::String::charToString (static_cast<juce::juce_wchar> (c));
        else if (c == ' ')
            result += "+";
        else
        {
            result += "%";
            result += juce::String::charToString (hex[c >> 4]);
            result += juce::String::charToString (hex[c & 0x0f]);
        }
    }
    return result;
}

juce::String randomHex (int count)
{
    auto& random = juce::Random::getSystemRandom();
    juce::String result;
    for (int i = 0; i < count; ++i)
        result += juce::String::toHexString (static_cast<int> (random.nextInt (256))).paddedLeft ('0', 2);
    return result;
}

juce::String getCodeChallenge (const juce::String& verifier)
{
    const auto utf8 = verifier.toUTF8();
    const juce::SHA256 hash (utf8);
    const auto raw = hash.getRawData();
    return juce::Base64::toBase64 (raw.getData(), raw.getSize())
        .replace ("+", "-").replace ("/", "_").replace ("=", "");
}

juce::String formBody (const juce::StringPairArray& values)
{
    juce::StringArray fields;
    for (const auto& key : values.getAllKeys())
        fields.add (urlEncode (key) + "=" + urlEncode (values[key]));
    return fields.joinIntoString ("&");
}

juce::var requestJson (const juce::URL& url, const juce::String& bearerToken,
                       int& statusCode, bool post = false)
{
    juce::String headers ("Accept: application/json\r\n");
    if (bearerToken.isNotEmpty())
        headers += "Authorization: Bearer " + bearerToken + "\r\n";
    if (post)
        headers += "Content-Type: application/x-www-form-urlencoded\r\n";

    auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
        .withExtraHeaders (headers)
        .withConnectionTimeoutMs (20000)
        .withStatusCode (&statusCode);
    auto input = url.createInputStream (options);
    if (input == nullptr || (statusCode != 0 && (statusCode < 200 || statusCode >= 300)))
        return {};
    return juce::JSON::parse (input->readEntireStreamAsString());
}

juce::String getQueryParameter (const juce::URL& url, const juce::String& name)
{
    const auto& names = url.getParameterNames();
    const auto& values = url.getParameterValues();
    for (int i = 0; i < names.size() && i < values.size(); ++i)
        if (names[i] == name)
            return values[i];
    return {};
}

juce::String safeFilePart (juce::String text)
{
    text = text.trim().retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-.()");
    while (text.contains ("..")) text = text.replace ("..", ".");
    return text.isEmpty() ? "NAM Capture" : text.substring (0, 80).trim();
}

juce::String architectureName (const juce::var& value)
{
    if (value.isInt() && static_cast<int> (value) == 1) return "A1";
    if (value.isInt() && static_cast<int> (value) == 2) return "A2";
    return value.toString().isNotEmpty() ? value.toString().toUpperCase() : "NAM";
}
}

Client::Client() : Thread ("TONE3000 OAuth and model transfer") {}

Client::~Client()
{
    cancel();
}

bool Client::startSelectFlow (int architecture, ModelsCallback callback)
{
    if (isThreadRunning() || callback == nullptr)
        return false;
    if (juce::String (NAMPLIFIED_TONE3000_CLIENT_ID).isEmpty()
        || juce::String (NAMPLIFIED_TONE3000_REDIRECT_URI).isEmpty())
        return false;

    modelsCallback = std::move (callback);
    architectureToBrowse = architecture == 1 ? 1 : 2;
    expectedState = randomHex (32);
    codeVerifier = randomHex (48);
    authorizationCode.clear();
    selectedToneId.clear();
    operation = Operation::select;

    if (! listener.createListener (callbackPort, "127.0.0.1"))
    {
        operation = Operation::none;
        return false;
    }

    juce::URL auth ("https://www.tone3000.com/api/v1/oauth/authorize");
    auth = auth.withParameter ("client_id", NAMPLIFIED_TONE3000_CLIENT_ID)
               .withParameter ("redirect_uri", NAMPLIFIED_TONE3000_REDIRECT_URI)
               .withParameter ("response_type", "code")
               .withParameter ("code_challenge", getCodeChallenge (codeVerifier))
               .withParameter ("code_challenge_method", "S256")
               .withParameter ("state", expectedState)
               .withParameter ("prompt", "select_tone")
               .withParameter ("format", "nam")
               .withParameter ("architecture", juce::String (architectureToBrowse))
               .withParameter ("locale", "en")
               .withParameter ("menubar", "true");

    if (! auth.launchInDefaultBrowser())
    {
        listener.close();
        operation = Operation::none;
        return false;
    }

    return startThread();
}

bool Client::downloadModel (const Model& model, DownloadCallback callback)
{
    if (isThreadRunning() || accessToken.isEmpty()
        || model.modelUrl.isEmpty() || callback == nullptr)
        return false;
    selectedModel = model;
    downloadCallback = std::move (callback);
    operation = Operation::download;
    return startThread();
}

void Client::cancel()
{
    signalThreadShouldExit();
    listener.close();
    stopThread (2000);
    operation = Operation::none;
    accessToken.clear();
}

void Client::run()
{
    if (operation == Operation::select)
        runSelectFlow();
    else if (operation == Operation::download)
        runModelDownload();
}

void Client::runSelectFlow()
{
    const auto finish = [this] (std::vector<Model> models, juce::String error)
    {
        if (error.isNotEmpty())
            accessToken.clear();
        auto callback = std::move (modelsCallback);
        if (callback != nullptr)
            juce::MessageManager::callAsync ([callback = std::move (callback), models = std::move (models), error]() mutable
            {
                callback (std::move (models), error);
            });
    };

    std::unique_ptr<juce::StreamingSocket> client (listener.waitForNextConnection());
    listener.close();
    if (client == nullptr || threadShouldExit())
    {
        finish ({}, "TONE3000 selection was cancelled.");
        return;
    }

    juce::String requestData;
    std::array<char, 1024> requestBuffer {};
    while (! threadShouldExit() && requestData.length() < 8192 && ! requestData.contains ("\r\n"))
    {
        if (client->waitUntilReady (true, 5000) <= 0)
            break;
        const auto bytesRead = client->read (requestBuffer.data(), static_cast<int> (requestBuffer.size()), false);
        if (bytesRead <= 0)
            break;
        requestData += juce::String::fromUTF8 (requestBuffer.data(), bytesRead);
    }

    if (requestData.isEmpty() || ! requestData.contains ("\r\n"))
    {
        finish ({}, "Could not read the TONE3000 callback request.");
        return;
    }

    const auto requestLine = requestData.upToFirstOccurrenceOf ("\r\n", false, false);
    const auto firstSpace = requestLine.indexOfChar (' ');
    const auto relativeSecondSpace = firstSpace >= 0 ? requestLine.substring (firstSpace + 1).indexOfChar (' ') : -1;
    const auto secondSpace = relativeSecondSpace >= 0 ? relativeSecondSpace + firstSpace + 1 : -1;
    const auto target = firstSpace >= 0 && secondSpace > firstSpace
        ? requestLine.substring (firstSpace + 1, secondSpace) : juce::String();

    auto response = juce::String ("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n")
                  + "<!doctype html><html><body style='font:16px sans-serif;background:#10131a;color:#eee;padding:3em'>"
                    "TONE3000 selection received. You can return to NAMplified and close this tab."
                    "</body></html>";
    client->write (response.toRawUTF8(), static_cast<int> (response.getNumBytesAsUTF8()));
    client->close();

    if (! target.startsWith (callbackPath))
    {
        finish ({}, "The TONE3000 callback used an unexpected path.");
        return;
    }

    const juce::URL callbackUrl ("http://127.0.0.1" + target);
    if (getQueryParameter (callbackUrl, "state") != expectedState)
    {
        finish ({}, "TONE3000 login state did not match. Please try again.");
        return;
    }

    authorizationCode = getQueryParameter (callbackUrl, "code");
    selectedToneId = getQueryParameter (callbackUrl, "tone_id");
    if (getQueryParameter (callbackUrl, "canceled") == "true")
    {
        finish ({}, "TONE3000 selection was cancelled.");
        return;
    }
    if (authorizationCode.isEmpty() || selectedToneId.isEmpty())
    {
        finish ({}, "TONE3000 did not return a selected tone.");
        return;
    }

    juce::StringPairArray tokenFields;
    tokenFields.set ("grant_type", "authorization_code");
    tokenFields.set ("code", authorizationCode);
    tokenFields.set ("code_verifier", codeVerifier);
    tokenFields.set ("redirect_uri", NAMPLIFIED_TONE3000_REDIRECT_URI);
    tokenFields.set ("client_id", NAMPLIFIED_TONE3000_CLIENT_ID);

    int statusCode = 0;
    const auto tokenResponse = requestJson (
        juce::URL (juce::String (apiRoot) + "/oauth/token").withPOSTData (formBody (tokenFields)),
        {}, statusCode, true);
    accessToken = tokenResponse.getProperty ("access_token", {}).toString();
    if (accessToken.isEmpty())
    {
        finish ({}, "TONE3000 sign-in failed (HTTP " + juce::String (statusCode) + ").");
        return;
    }

    int toneStatus = 0;
    const auto tone = requestJson (juce::URL (juce::String (apiRoot) + "/tones/" + selectedToneId),
                                   accessToken, toneStatus);
    if (tone.isVoid() || (toneStatus != 0 && (toneStatus < 200 || toneStatus >= 300)))
    {
        finish ({}, "Could not load TONE3000 tone details.");
        return;
    }

    std::vector<Model> models;
    for (const auto architecture : { 1, 2 })
    {
        juce::URL modelsUrl (juce::String (apiRoot) + "/models");
        modelsUrl = modelsUrl.withParameter ("tone_id", selectedToneId)
                             .withParameter ("page", "1")
                             .withParameter ("page_size", "300")
                             .withParameter ("architecture", juce::String (architecture));
        int modelsStatus = 0;
        const auto responseJson = requestJson (modelsUrl, accessToken, modelsStatus);
        const auto dataValue = responseJson.getProperty ("data", {});
        const auto* data = dataValue.getArray();
        if (data == nullptr || (modelsStatus != 0 && (modelsStatus < 200 || modelsStatus >= 300)))
            continue;

        const auto owner = tone.getProperty ("user", {});
        const auto displayName = owner.getProperty ("display_name", {}).toString();
        const auto username = owner.getProperty ("username", {}).toString();
        for (const auto& item : *data)
        {
            Model model;
            model.name = item.getProperty ("name", {}).toString();
            model.architecture = architectureName (item.getProperty ("architecture_version", {}));
            model.modelUrl = item.getProperty ("model_url", {}).toString();
            model.toneTitle = tone.getProperty ("title", {}).toString();
            model.toneUrl = tone.getProperty ("url", {}).toString();
            model.creator = displayName.isNotEmpty() ? displayName : username;
            model.creatorUrl = owner.getProperty ("url", {}).toString();
            model.license = tone.getProperty ("license", {}).toString();
            if (model.name.isNotEmpty() && model.modelUrl.startsWith ("https://")
                && ! model.architecture.isEmpty())
                models.push_back (std::move (model));
        }
    }

    if (models.empty())
    {
        finish ({}, "No compatible NAM captures were found in the selected TONE3000 tone.");
        return;
    }
    finish (std::move (models), {});
}

void Client::runModelDownload()
{
    auto finish = [this] (juce::File file, juce::String error)
    {
        accessToken.clear();
        selectedModel = {};
        auto callback = std::move (downloadCallback);
        if (callback != nullptr)
            juce::MessageManager::callAsync ([callback = std::move (callback), file, error]
            {
                callback (file, error);
            });
    };

    if (! selectedModel.modelUrl.startsWith ("https://"))
    {
        finish ({}, "The TONE3000 model URL is invalid.");
        return;
    }

    auto folder = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                      .getChildFile ("NAMplified").getChildFile ("TONE3000");
    if (! folder.createDirectory().wasOk())
    {
        finish ({}, "Could not create the local TONE3000 model folder.");
        return;
    }

    auto modelName = selectedModel.name;
    if (modelName.endsWithIgnoreCase (".nam"))
        modelName = modelName.dropLastCharacters (4);
    const auto baseName = safeFilePart (modelName
                                       + " - " + selectedModel.creator + " - " + selectedModel.license);
    auto destination = folder.getNonexistentChildFile (baseName, ".nam", false);
    int statusCode = 0;
    const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
        .withExtraHeaders ("Authorization: Bearer " + accessToken + "\r\n")
        .withConnectionTimeoutMs (30000)
        .withStatusCode (&statusCode);
    auto input = juce::URL (selectedModel.modelUrl).createInputStream (options);
    if (input == nullptr || (statusCode != 0 && (statusCode < 200 || statusCode >= 300)))
    {
        finish ({}, "Could not download the selected TONE3000 capture (HTTP " + juce::String (statusCode) + ").");
        return;
    }

    juce::FileOutputStream output (destination);
    if (! output.openedOk())
    {
        finish ({}, "Could not save the selected NAM capture locally.");
        return;
    }
    std::array<char, 32768> buffer {};
    while (! threadShouldExit())
    {
        const auto count = input->read (buffer.data(), static_cast<int> (buffer.size()));
        if (count <= 0) break;
        if (! output.write (buffer.data(), static_cast<size_t> (count)))
        {
            destination.deleteFile();
            finish ({}, "Could not write the downloaded NAM capture.");
            return;
        }
    }
    output.flush();
    if (threadShouldExit() || destination.getSize() == 0)
    {
        destination.deleteFile();
        finish ({}, "The TONE3000 download did not complete.");
        return;
    }

    juce::DynamicObject::Ptr metadata = new juce::DynamicObject();
    metadata->setProperty ("source", "TONE3000");
    metadata->setProperty ("model", selectedModel.name);
    metadata->setProperty ("architecture", selectedModel.architecture);
    metadata->setProperty ("tone", selectedModel.toneTitle);
    metadata->setProperty ("toneUrl", selectedModel.toneUrl);
    metadata->setProperty ("creator", selectedModel.creator);
    metadata->setProperty ("creatorUrl", selectedModel.creatorUrl);
    metadata->setProperty ("license", selectedModel.license);
    destination.getSiblingFile (destination.getFileNameWithoutExtension() + ".t3k.json")
        .replaceWithText (juce::JSON::toString (juce::var (metadata.get()), true));
    finish (destination, {});
}
}
