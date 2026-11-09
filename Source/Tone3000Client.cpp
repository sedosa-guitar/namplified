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
                       int& statusCode, bool post = false, juce::String* responseText = nullptr)
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
    if (input == nullptr)
        return {};
    const auto body = input->readEntireStreamAsString();
    if (responseText != nullptr)
        *responseText = body;
    return juce::JSON::parse (body);
}

juce::String responseError (const juce::var& response, const juce::String& rawBody)
{
    auto message = response.getProperty ("error_description", {}).toString();
    if (message.isEmpty())
        message = response.getProperty ("message", {}).toString();
    if (message.isEmpty())
        message = response.getProperty ("error", {}).toString();
    if (message.isEmpty() && rawBody.isNotEmpty())
        message = rawBody.substring (0, 240).trim();
    return message;
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

bool Client::downloadModel (const Model& model, int slot, DownloadCallback callback)
{
    if (isThreadRunning() || accessToken.isEmpty()
        || model.modelUrl.isEmpty() || callback == nullptr)
        return false;
    selectedModel = model;
    downloadSlot = slot;
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
    std::unique_ptr<juce::StreamingSocket> client;
    bool browserAnswered = false;
    const auto answerBrowser = [&client, &browserAnswered] (bool success)
    {
        if (client == nullptr || browserAnswered)
            return;

        const auto response = juce::String ("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n")
            + "<!doctype html><html lang='en'><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>NAMplified</title></head>"
              "<body style='margin:0;min-height:100vh;display:grid;place-items:center;background:#0b0e14;color:#eef1f6;font:16px -apple-system,BlinkMacSystemFont,sans-serif'>"
              "<main style='box-sizing:border-box;width:min(560px,calc(100% - 32px));padding:36px;border:1px solid #35404d;border-radius:18px;background:#151a22;box-shadow:0 24px 80px #0008'>"
              "<div style='color:#69d7b5;font-size:13px;font-weight:700;letter-spacing:.16em'>NAMPLIFIED</div>"
            + juce::String ("<h1 style='margin:18px 0 10px;font-size:28px'>")
            + (success ? "Tone selected" : "Selection not completed") + "</h1>"
            + (success
                ? "<p style='color:#c0c8d3;line-height:1.6'>NAMplified is opening the capture selection. If there is one compatible capture, it loads automatically; otherwise choose one from the list.</p>"
                  "<p style='color:#c0c8d3;line-height:1.6'>Save your NAMplified preset afterward to keep the capture with it.</p>"
                  "<div style='margin-top:24px;padding:14px 16px;border-radius:10px;background:#1b382f;color:#69d7b5;font-weight:600'>The selected capture will load into the active NAM block.</div>"
                : "<p style='color:#c0c8d3;line-height:1.6'>Return to NAMplified for details, then try selecting the tone again.</p>")
            + "<div style='margin-top:28px;padding-top:18px;border-top:1px solid #303845;color:#8e99a8;font-size:13px'>"
              "TONE selection is complete. You can close this tab.</div></main></body></html>";
        client->write (response.toRawUTF8(), static_cast<int> (response.getNumBytesAsUTF8()));
        client->close();
        browserAnswered = true;
    };

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

    juce::String target;
    for (int attempt = 0; attempt < 5 && ! threadShouldExit(); ++attempt)
    {
        target.clear();
        client.reset (listener.waitForNextConnection());
        if (client == nullptr)
            break;

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

        if (requestData.contains ("\r\n"))
        {
            const auto requestLine = requestData.upToFirstOccurrenceOf ("\r\n", false, false);
            const auto firstSpace = requestLine.indexOfChar (' ');
            const auto relativeSecondSpace = firstSpace >= 0 ? requestLine.substring (firstSpace + 1).indexOfChar (' ') : -1;
            const auto secondSpace = relativeSecondSpace >= 0 ? relativeSecondSpace + firstSpace + 1 : -1;
            target = firstSpace >= 0 && secondSpace > firstSpace
                ? requestLine.substring (firstSpace + 1, secondSpace) : juce::String();
        }

        const auto isCallbackRequest = target == callbackPath || target.startsWith (juce::String (callbackPath) + "?");
        if (isCallbackRequest)
            break;

        const auto notFound = juce::String ("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        client->write (notFound.toRawUTF8(), static_cast<int> (notFound.getNumBytesAsUTF8()));
        client->close();
        client.reset();
    }
    listener.close();

    if (client == nullptr || threadShouldExit())
    {
        finish ({}, "TONE3000 selection was cancelled or its browser callback was not received.");
        return;
    }

    // The OAuth callback is opened by the external browser. Bring the host app
    // forward before presenting the capture picker so it cannot hide behind it.
    juce::MessageManager::callAsync ([] { juce::Process::makeForegroundProcess(); });

    const juce::URL callbackUrl ("http://127.0.0.1" + target);
    if (getQueryParameter (callbackUrl, "state") != expectedState)
    {
        answerBrowser (false);
        finish ({}, "TONE3000 login state did not match. Please try again.");
        return;
    }

    const auto oauthError = getQueryParameter (callbackUrl, "error");
    if (oauthError.isNotEmpty())
    {
        auto description = getQueryParameter (callbackUrl, "error_description");
        if (description.isEmpty())
            description = oauthError;
        answerBrowser (false);
        finish ({}, "TONE3000 authorization failed: " + description);
        return;
    }

    authorizationCode = getQueryParameter (callbackUrl, "code");
    selectedToneId = getQueryParameter (callbackUrl, "tone_id");
    if (getQueryParameter (callbackUrl, "canceled") == "true")
    {
        answerBrowser (false);
        finish ({}, "TONE3000 selection was cancelled.");
        return;
    }
    if (authorizationCode.isEmpty() || selectedToneId.isEmpty())
    {
        answerBrowser (false);
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
    juce::String tokenBody;
    const auto tokenResponse = requestJson (
        juce::URL (juce::String (apiRoot) + "/oauth/token").withPOSTData (formBody (tokenFields)),
        {}, statusCode, true, &tokenBody);
    accessToken = tokenResponse.getProperty ("access_token", {}).toString();
    if (accessToken.isEmpty())
    {
        auto detail = responseError (tokenResponse, tokenBody);
        answerBrowser (false);
        finish ({}, "TONE3000 sign-in failed (HTTP " + juce::String (statusCode)
                    + (detail.isNotEmpty() ? "): " + detail : "). Check the registered redirect URI and try again."));
        return;
    }

    int toneStatus = 0;
    const auto tone = requestJson (juce::URL (juce::String (apiRoot) + "/tones/" + selectedToneId),
                                   accessToken, toneStatus);
    if (tone.isVoid() || (toneStatus != 0 && (toneStatus < 200 || toneStatus >= 300)))
    {
        answerBrowser (false);
        finish ({}, "Could not load TONE3000 tone details (HTTP " + juce::String (toneStatus) + ").");
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
            model.toneId = selectedToneId;
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
        answerBrowser (false);
        finish ({}, "No compatible NAM captures were found in the selected TONE3000 tone.");
        return;
    }
    answerBrowser (true);
    finish (std::move (models), {});
}

void Client::runModelDownload()
{
    auto finish = [this] (juce::File file, juce::String error)
    {
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
    const auto slotFolder = downloadSlot >= 2 ? juce::String ("BOOST_NAM")
        : "NAM" + juce::String (juce::jlimit (0, 1, downloadSlot) + 1);
    folder = folder.getChildFile (slotFolder).getChildFile (safeFilePart (selectedModel.toneId));
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
    metadata->setProperty ("toneId", selectedModel.toneId);
    metadata->setProperty ("model", selectedModel.name);
    metadata->setProperty ("modelUrl", selectedModel.modelUrl);
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
