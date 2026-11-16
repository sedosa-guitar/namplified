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

    void prepare (double sampleRate)
    {
        rate = sampleRate;
        for (auto& track : tracks)
            track.audio.setSize (2, static_cast<int> (maxSeconds * rate));
        reset();
    }

    void request (int track, Action action) { commands[static_cast<size_t> (track)].store (action); }
    TrackState getState (int track) const { return states[static_cast<size_t> (track)].load(); }
    bool isPlaying() const { return playing.load(); }
    bool isSettingLength() const { return settingLength.load(); }
    int getCountInTrack() const { return countInTrack.load(); }
    int getCountInBeat() const { return countInBeat.load(); }
    float getElapsedSeconds() const { return elapsedSeconds.load(); }
    float getLengthSeconds() const { return lengthSeconds.load(); }
    void togglePlayback() { toggleRequested.store (true); }
    void requestReset() { resetRequested.store (true); }

    void process (juce::AudioBuffer<float>& audio, int channels, int samples, bool enabled,
                  bool clickEnabled, float bpm, int meterBeats)
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
                if (static_cast<int> (index) == firstRecording && position > 0)
                {
                    length = position;
                    finishRecording();
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
                        samplesPerBeat = rate * 60.0 / juce::jlimit (60.0f, 180.0f, bpm);
                        beatsPerBar = meterBeats;
                        length = track.audio.getNumSamples();
                        position = 0;
                        firstRecording = static_cast<int> (index);
                    }
                    countInSamples = juce::roundToInt (samplesPerBeat * beatsPerBar);
                    samplesUntilRecord = countInSamples;
                    if (firstRecording < 0)
                    {
                        const auto toBoundary = length - position;
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
                    firstRecording = -1;
            }
        }

        if (std::all_of (tracks.begin(), tracks.end(), [] (const auto& track) { return track.state == TrackState::empty; }))
        {
            length = position = 0;
            firstRecording = armedTrack = -1;
            playing.store (false);
        }

        int flashBeat = 0;
        if (enabled && playing.load() && length > 0)
        {
            const auto edgeSamples = juce::jmax (1.0f, static_cast<float> (0.003 * rate));
            for (int sample = 0; sample < samples; ++sample)
            {
                if (armedTrack >= 0 && samplesUntilRecord == 0)
                {
                    tracks[static_cast<size_t> (armedTrack)].state = TrackState::recording;
                    armedTrack = -1;
                }

                const auto counting = armedTrack >= 0 && samplesUntilRecord <= countInSamples;
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

                for (int channel = 0; channel < channels; ++channel)
                {
                    const auto input = audio.getSample (channel, sample);
                    auto output = input + click;
                    // Every track records the processed guitar alone, before playback and the click are added.
                    for (auto& track : tracks)
                    {
                        if (track.state == TrackState::recording)
                        {
                            track.audio.setSample (channel, position, input);
                            if (channels == 1)
                                track.audio.setSample (1, position, input);
                        }
                        else if (track.state == TrackState::playing)
                            output += playbackGain * track.audio.getSample (channel, position);
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
        TrackState state = TrackState::empty;
    };

    std::array<Track, trackCount> tracks;
    std::array<std::atomic<Action>, trackCount> commands {};
    std::array<std::atomic<TrackState>, trackCount> states {};
    std::atomic<bool> playing { false }, toggleRequested { false }, resetRequested { false };
    double rate = 44100.0;
    std::atomic<bool> settingLength { false };
    std::atomic<float> elapsedSeconds { 0.0f }, lengthSeconds { 0.0f };
    double samplesPerBeat = 22050.0;
    std::atomic<int> countInTrack { -1 }, countInBeat { 0 };
    int length = 0, position = 0, firstRecording = -1;
    int armedTrack = -1, samplesUntilRecord = 0, countInSamples = 0, beatsPerBar = 4;
};
