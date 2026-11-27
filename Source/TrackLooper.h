#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

class TrackLooper
{
public:
    static constexpr int trackCount = 4;
    static constexpr int maxSeconds = 60;
    enum class Action { none, record, mute, clear };
    enum class TrackState { empty, queued, recording, playing, muted };

    struct HostTiming
    {
        double bpm = 120.0;
        int beatsPerBar = 4;
        int beatUnit = 4;
        double ppq = 0.0;
        double barStartPpq = 0.0;
        bool hasBarPosition = false;
        bool playing = false;
    };

    void prepare (double sampleRate)
    {
        rate = sampleRate;
        for (auto& track : tracks)
        {
            track.audio.setSize (2, static_cast<int> (maxSeconds * rate));
            track.gain.reset (rate, 0.02);
            track.gain.setCurrentAndTargetValue (track.level.load (std::memory_order_relaxed));
        }
        reset();
    }

    void request (int track, Action action) { commands[static_cast<size_t> (track)].store (action); }
    void setTrackLevel (int track, float level)
    {
        tracks[static_cast<size_t> (track)].level.store (juce::jlimit (0.0f, 1.0f, level), std::memory_order_relaxed);
    }
    float getTrackLevel (int track) const
    {
        return tracks[static_cast<size_t> (track)].level.load (std::memory_order_relaxed);
    }
    TrackState getState (int track) const { return states[static_cast<size_t> (track)].load(); }
    bool isPlaying() const { return playing.load(); }
    bool isFirstTakeFixed() const { return fixedFirstTake.load(); }
    bool isWaitingForHost() const { return waitingForHost.load(); }
    bool isSettingLength() const { return settingLength.load(); }
    bool isFinishingAtBarEnd() const { return finishingAtBarEnd.load(); }
    int getCountInTrack() const { return countInTrack.load(); }
    int getCountInBeat() const { return countInBeat.load(); }
    float getElapsedSeconds() const { return elapsedSeconds.load(); }
    float getLengthSeconds() const { return lengthSeconds.load(); }
    void togglePlayback() { toggleRequested.store (true); }
    void requestReset() { resetRequested.store (true); }

    void process (juce::AudioBuffer<float>& audio, int channels, int samples, bool enabled,
                  bool clickEnabled, float bpm, int meterBeats, int fixedBars = 0,
                  bool useCountIn = true, const HostTiming* host = nullptr)
    {
        if (resetRequested.exchange (false))
            reset();

        if (toggleRequested.exchange (false) && length > 0)
            playing.store (! playing.load());

        for (size_t index = 0; index < tracks.size(); ++index)
        {
            auto& track = tracks[index];
            const auto action = commands[index].exchange (Action::none);
            if (action == Action::record && enabled)
            {
                if (static_cast<int> (index) == firstRecording && position > 0 && ! finishPending && ! fixedLength)
                {
                    const auto samplesPerBar = samplesPerBeat * beatsPerBar;
                    const auto barsElapsed = position / samplesPerBar;
                    const auto completedBars = static_cast<int> (barsElapsed);
                    const auto previousBar = juce::roundToInt (completedBars * samplesPerBar);
                    const auto lateClick = completedBars > 0 && position - previousBar <= samplesPerBeat * 0.5;
                    const auto targetBar = lateClick ? completedBars : completedBars + 1;
                    length = juce::jmin (length, juce::roundToInt (targetBar * samplesPerBar));
                    if (position >= length)
                        finishRecording();
                    else
                        finishPending = true;
                }
                else if (track.state == TrackState::queued)
                {
                    track.state = TrackState::empty;
                    armedTrack = -1;
                }
                else if (track.state == TrackState::empty && armedTrack < 0 && firstRecording < 0)
                {
                    if (length == 0)
                    {
                        const auto tempo = host != nullptr ? host->bpm : juce::jlimit (60.0f, 180.0f, bpm);
                        const auto beatUnit = host != nullptr ? host->beatUnit : 4;
                        samplesPerBeat = rate * 60.0 / tempo * (4.0 / beatUnit);
                        beatsPerBar = host != nullptr ? host->beatsPerBar : meterBeats;
                        const auto samplesPerBar = samplesPerBeat * beatsPerBar;
                        const auto maxBars = static_cast<int> (track.audio.getNumSamples() / samplesPerBar);
                        if (maxBars == 0)
                            continue;
                        fixedLength = fixedBars > 0;
                        length = juce::roundToInt ((fixedLength ? juce::jmin (fixedBars, maxBars) : maxBars)
                                                   * samplesPerBar);
                        position = 0;
                        firstRecording = static_cast<int> (index);
                    }
                    countInSamples = useCountIn ? juce::roundToInt (samplesPerBeat * beatsPerBar) : 0;
                    samplesUntilRecord = countInSamples;
                    alignFirstTake = firstRecording >= 0 && host != nullptr;
                    if (firstRecording < 0)
                    {
                        const auto toBoundary = position == 0 && ! useCountIn ? 0 : length - position;
                        const auto missing = juce::jmax (0, countInSamples - toBoundary);
                        samplesUntilRecord = toBoundary + ((missing + length - 1) / length) * length;
                    }
                    armedTrack = static_cast<int> (index);
                    track.state = TrackState::queued;
                    playing.store (true);
                }
            }
            else if (action == Action::mute)
            {
                if (track.state == TrackState::playing)
                    track.state = TrackState::muted;
                else if (track.state == TrackState::muted)
                    track.state = TrackState::playing;
            }
            else if (action == Action::clear)
            {
                track.state = TrackState::empty;
                if (static_cast<int> (index) == armedTrack)
                    armedTrack = -1;
                if (static_cast<int> (index) == firstRecording)
                {
                    firstRecording = -1;
                    finishPending = false;
                }
            }
        }

        if (std::all_of (tracks.begin(), tracks.end(), [] (const auto& track) { return track.state == TrackState::empty; }))
        {
            length = position = 0;
            firstRecording = armedTrack = -1;
            playing.store (false);
        }

        const auto hostPaused = host != nullptr && ! host->playing;
        waitingForHost.store (hostPaused && (armedTrack >= 0 || length > 0));
        if (hostPaused && firstRecording >= 0 && armedTrack >= 0)
            alignFirstTake = true;
        if (alignFirstTake && ! hostPaused && firstRecording >= 0 && armedTrack >= 0)
        {
            samplesUntilRecord = countInSamples;
            if (host != nullptr && host->hasBarPosition)
            {
                const auto quarterNotesPerBar = host->beatsPerBar * 4.0 / host->beatUnit;
                auto phase = std::fmod (host->ppq - host->barStartPpq, quarterNotesPerBar);
                if (phase < 0.0)
                    phase += quarterNotesPerBar;
                const auto quarterNoteSamples = rate * 60.0 / host->bpm;
                const auto toBar = phase * quarterNoteSamples <= 0.5 ? 0
                    : juce::roundToInt ((quarterNotesPerBar - phase) * quarterNoteSamples);
                samplesUntilRecord += toBar;
            }
            alignFirstTake = false;
        }

        int flashBeat = 0;
        if (enabled && playing.load() && length > 0 && ! hostPaused)
        {
            for (auto& track : tracks)
                track.gain.setTargetValue (track.level.load (std::memory_order_relaxed));
            const auto edgeSamples = juce::jmax (1.0f, static_cast<float> (0.003 * rate));
            for (int sample = 0; sample < samples; ++sample)
            {
                if (armedTrack >= 0 && samplesUntilRecord == 0)
                {
                    tracks[static_cast<size_t> (armedTrack)].state = TrackState::recording;
                    armedTrack = -1;
                }

                const auto counting = armedTrack >= 0 && countInSamples > 0 && samplesUntilRecord <= countInSamples;
                const auto phaseSamples = counting ? countInSamples - samplesUntilRecord : position;
                const auto beatPosition = phaseSamples / samplesPerBeat;
                const auto beat = static_cast<int> (beatPosition);
                const auto beatFraction = beatPosition - beat;
                flashBeat = counting && beatFraction < 0.45 ? beat + 1 : 0;
                const auto seconds = beatFraction * samplesPerBeat / rate;
                const auto click = clickEnabled && seconds < 0.025
                    ? static_cast<float> (0.12 * (1.0 - seconds / 0.025)
                         * std::sin (juce::MathConstants<double>::twoPi * (beat % beatsPerBar == 0 ? 1600.0 : 1000.0) * seconds))
                    : 0.0f;
                const auto distanceToEdge = juce::jmin (position, length - 1 - position);
                const auto playbackGain = 0.65f * juce::jmin (1.0f, static_cast<float> (distanceToEdge) / edgeSamples);
                std::array<float, trackCount> trackGains;
                for (size_t index = 0; index < tracks.size(); ++index)
                    trackGains[index] = playbackGain * tracks[index].gain.getNextValue();

                for (int channel = 0; channel < channels; ++channel)
                {
                    const auto input = audio.getSample (channel, sample);
                    auto output = input + click;
                    // Every track records the processed guitar alone, before playback and the click are added.
                    for (size_t index = 0; index < tracks.size(); ++index)
                    {
                        auto& track = tracks[index];
                        if (track.state == TrackState::recording)
                        {
                            track.audio.setSample (channel, position, input);
                            if (channels == 1)
                                track.audio.setSample (1, position, input);
                        }
                        else if (track.state == TrackState::playing)
                            output += trackGains[index] * track.audio.getSample (channel, position);
                    }
                    audio.setSample (channel, sample, output);
                }

                if (armedTrack >= 0)
                    --samplesUntilRecord;
                if ((firstRecording < 0 || armedTrack < 0) && ++position == length)
                    finishRecording();
            }
        }

        countInTrack.store (armedTrack);
        countInBeat.store (flashBeat);
        settingLength.store (firstRecording >= 0);
        fixedFirstTake.store (firstRecording >= 0 && fixedLength);
        finishingAtBarEnd.store (finishPending);
        elapsedSeconds.store (static_cast<float> (position / rate));
        lengthSeconds.store (static_cast<float> (length / rate));
        for (size_t index = 0; index < tracks.size(); ++index)
            states[index].store (tracks[index].state);
    }

private:
    void finishRecording()
    {
        position = 0;
        firstRecording = -1;
        finishPending = false;
        for (auto& track : tracks)
            if (track.state == TrackState::recording)
                track.state = TrackState::playing;
    }

    void reset()
    {
        length = position = 0;
        firstRecording = armedTrack = -1;
        countInTrack.store (-1);
        countInBeat.store (0);
        settingLength.store (false);
        fixedFirstTake.store (false);
        waitingForHost.store (false);
        fixedLength = alignFirstTake = false;
        finishPending = false;
        finishingAtBarEnd.store (false);
        elapsedSeconds.store (0.0f);
        lengthSeconds.store (0.0f);
        playing.store (false);
        for (size_t index = 0; index < tracks.size(); ++index)
        {
            tracks[index].state = TrackState::empty;
            states[index].store (TrackState::empty);
            commands[index].store (Action::none);
        }
    }

    struct Track
    {
        juce::AudioBuffer<float> audio;
        std::atomic<float> level { 1.0f };
        juce::SmoothedValue<float> gain { 1.0f };
        TrackState state = TrackState::empty;
    };

    std::array<Track, trackCount> tracks;
    std::array<std::atomic<Action>, trackCount> commands {};
    std::array<std::atomic<TrackState>, trackCount> states {};
    std::atomic<bool> playing { false }, toggleRequested { false }, resetRequested { false };
    double rate = 44100.0;
    std::atomic<bool> settingLength { false }, fixedFirstTake { false }, waitingForHost { false };
    std::atomic<bool> finishingAtBarEnd { false };
    bool finishPending = false, fixedLength = false, alignFirstTake = false;
    std::atomic<float> elapsedSeconds { 0.0f }, lengthSeconds { 0.0f };
    double samplesPerBeat = 22050.0;
    std::atomic<int> countInTrack { -1 }, countInBeat { 0 };
    int length = 0, position = 0, firstRecording = -1;
    int armedTrack = -1, samplesUntilRecord = 0, countInSamples = 0, beatsPerBar = 4;
};
