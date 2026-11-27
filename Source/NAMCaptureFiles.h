#pragma once

#include "NAMCaptureSession.h"
#include <juce_cryptography/juce_cryptography.h>

namespace NAMCaptureFiles
{
struct Stimulus
{
    juce::MemoryBlock wav;
    juce::AudioBuffer<float> audio;
};

inline juce::Result readStimulus (const juce::File& file, Stimulus& target)
{
    constexpr auto maxWavBytes = static_cast<juce::int64> (NAMCaptureSession::maxSamples) * 3 + 65536;
    if (! file.hasFileExtension ("wav") || file.getSize() <= 0 || file.getSize() > maxWavBytes)
        return juce::Result::fail ("Choose a mono 48 kHz, 24-bit NAM test WAV under five minutes.");

    Stimulus loaded;
    if (! file.loadFileAsData (loaded.wav))
        return juce::Result::fail ("Could not read the NAM test WAV.");

    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader (
        format.createReaderFor (new juce::MemoryInputStream (loaded.wav, false), true));
    if (reader == nullptr || reader->sampleRate != NAMCaptureSession::sampleRate
        || reader->bitsPerSample != 24 || reader->numChannels != 1
        || reader->lengthInSamples <= 0 || reader->lengthInSamples > NAMCaptureSession::maxSamples)
        return juce::Result::fail ("Choose a mono 48 kHz, 24-bit NAM test WAV under five minutes.");

    loaded.audio.setSize (1, static_cast<int> (reader->lengthInSamples));
    if (! reader->read (&loaded.audio, 0, loaded.audio.getNumSamples(), 0, true, false)
        || loaded.audio.getMagnitude (0, loaded.audio.getNumSamples()) == 0.0f)
        return juce::Result::fail ("The NAM test WAV contains no readable signal.");

    reader.reset();
    target = std::move (loaded);
    return juce::Result::ok();
}

inline juce::Result loadOfficialStimulus (const juce::File& cacheDirectory, Stimulus& target)
{
    constexpr juce::int64 expectedSize = 27360080;
    constexpr auto expectedHash = "70f8ec7f25686a1bd77f25973de8e51a6721e957e81eec121822e5e53366bc41";
    const auto matches = [] (const juce::File& file)
    {
        return file.getSize() == expectedSize && juce::SHA256 (file).toHexString() == expectedHash;
    };

    const auto cached = cacheDirectory.getChildFile ("input.wav");
    if (matches (cached))
        return readStimulus (cached, target);
    if (const auto created = cacheDirectory.createDirectory(); created.failed())
        return created;

    // Official 48 kHz trainer input linked from the NAM GUI documentation.
    const juce::URL url ("https://drive.usercontent.google.com/download"
                         "?id=1KbaS4oXXNEuh2aCPLwKrPdf5KFOjda8G&export=download&confirm=t");
    juce::WebInputStream stream (url, false);
    stream.withConnectionTimeout (15000).withNumRedirectsToFollow (5);
    if (! stream.connect (nullptr) || stream.getStatusCode() != 200)
        return juce::Result::fail ("Could not download the official NAM test signal.");

    juce::TemporaryFile temporary (cached, juce::TemporaryFile::useHiddenFile);
    {
        auto output = temporary.getFile().createOutputStream();
        if (output == nullptr
            || output->writeFromInputStream (stream, expectedSize + 1) != expectedSize)
            return juce::Result::fail ("The official NAM test signal download was incomplete.");
        output->flush();
        if (output->getStatus().failed())
            return output->getStatus();
    }

    if (! matches (temporary.getFile()))
        return juce::Result::fail ("The downloaded file did not match the official NAM test signal.");
    if (! temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("Could not save the official NAM test signal.");
    return readStimulus (cached, target);
}

inline juce::Result exportPair (const juce::File& parent, const Stimulus& stimulus,
                                const juce::AudioBuffer<float>& take, juce::File& destination)
{
    if (! parent.isDirectory() || stimulus.wav.isEmpty() || take.getNumChannels() != 1
        || take.getNumSamples() <= 0 || take.getNumSamples() != stimulus.audio.getNumSamples())
        return juce::Result::fail ("A complete capture and a destination folder are required.");

    const auto folder = parent.getNonexistentChildFile ("NAM Capture", {}, false);
    if (const auto result = folder.createDirectory(); result.failed())
        return result;

    const auto input = folder.getChildFile ("input.wav");
    const auto output = folder.getChildFile ("output.wav");
    const auto writeOutput = [&]
    {
        std::unique_ptr<juce::OutputStream> stream = output.createOutputStream();
        if (stream == nullptr)
            return false;
        juce::WavAudioFormat format;
        auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions()
            .withSampleRate (NAMCaptureSession::sampleRate).withNumChannels (1).withBitsPerSample (24));
        return writer != nullptr && writer->writeFromAudioSampleBuffer (take, 0, take.getNumSamples())
            && writer->flush();
    };

    if (! input.replaceWithData (stimulus.wav.getData(), stimulus.wav.getSize()) || ! writeOutput())
    {
        input.deleteFile();
        output.deleteFile();
        folder.deleteFile();
        return juce::Result::fail ("Could not write both WAVs. Choose a writable destination.");
    }

    destination = folder;
    return juce::Result::ok();
}
}
