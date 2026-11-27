#pragma once

#include <JuceHeader.h>

class NAMCaptureSession
{
public:
    static constexpr int sampleRate = 48000;
    static constexpr int maxSamples = sampleRate * 60 * 5;
    enum class State { closed, ready, recording, complete, interrupted };

    void open()
    {
        const juce::SpinLock::ScopedLockType guard (lock);
        state.store (State::ready);
        progress.store (0.0f);
    }

    void close()
    {
        const juce::SpinLock::ScopedLockType guard (lock);
        state.store (State::closed);
        input.setSize (0, 0);
        returned.setSize (0, 0);
    }

    void cancel()
    {
        const juce::SpinLock::ScopedLockType guard (lock);
        if (state.load() == State::recording)
        {
            state.store (State::ready);
            progress.store (0.0f);
        }
    }

    void interrupt()
    {
        const juce::SpinLock::ScopedLockType guard (lock);
        if (state.load() == State::recording)
            state.store (State::interrupted);
    }

    bool start (const juce::AudioBuffer<float>& stimulus, int inputChannel, int outputChannel, float gain,
                double deviceRate, int inputChannels, int outputChannels)
    {
        const auto length = stimulus.getNumSamples();
        if (deviceRate != sampleRate || stimulus.getNumChannels() != 1 || length <= 0 || length > maxSamples
            || ! std::isfinite (gain) || gain <= 0.0f || gain > 1.0f
            || ! juce::isPositiveAndBelow (inputChannel, inputChannels)
            || ! juce::isPositiveAndBelow (outputChannel, outputChannels))
            return false;

        const juce::SpinLock::ScopedLockType guard (lock);
        if (state.load() == State::closed || state.load() == State::recording)
            return false;
        input.makeCopyOf (stimulus);
        returned.setSize (1, length);
        returnChannel = inputChannel;
        sendChannel = outputChannel;
        sendGain = gain;
        offset = 0;
        progress.store (0.0f);
        state.store (State::recording);
        return true;
    }

    bool process (juce::AudioBuffer<float>& buffer, double rate, int inputChannels, int outputChannels)
    {
        if (getState() == State::closed)
            return false;

        const juce::SpinLock::ScopedTryLockType guard (lock);
        if (guard.isLocked() && getState() == State::recording)
        {
            if (rate != sampleRate
                || returnChannel >= juce::jmin (buffer.getNumChannels(), inputChannels)
                || sendChannel >= juce::jmin (buffer.getNumChannels(), outputChannels))
            {
                state.store (State::interrupted);
                buffer.clear();
                return true;
            }

            const auto length = input.getNumSamples();
            const auto count = juce::jmin (buffer.getNumSamples(), length - offset);
            returned.copyFrom (0, offset, buffer, returnChannel, 0, count);
            buffer.clear();
            buffer.copyFrom (sendChannel, 0, input.getReadPointer (0, offset), count);
            buffer.applyGain (sendChannel, 0, count, sendGain);
            offset += count;
            progress.store (static_cast<float> (offset) / static_cast<float> (length));
            if (offset == length)
                state.store (State::complete);
        }
        else
        {
            buffer.clear();
        }
        return true;
    }

    juce::AudioBuffer<float> takeRecording()
    {
        const juce::SpinLock::ScopedLockType guard (lock);
        return state.load() == State::complete ? std::move (returned) : juce::AudioBuffer<float>();
    }

    State getState() const noexcept { return state.load(); }
    float getProgress() const noexcept { return progress.load(); }

private:
    juce::SpinLock lock;
    std::atomic<State> state { State::closed };
    std::atomic<float> progress { 0.0f };
    juce::AudioBuffer<float> input, returned;
    int offset = 0, returnChannel = 0, sendChannel = 0;
    float sendGain = 1.0f;
};
