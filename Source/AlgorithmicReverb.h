#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// Three lightweight, independent feedback-delay networks. The room network
// includes early reflections, the plate uses serial all-pass diffusion, and
// the cloud network uses long, slowly modulated delays.
class AlgorithmicReverb
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate > 1.0 ? newSampleRate : 48000.0;
        const auto lineCapacity = static_cast<size_t> (std::ceil (sampleRate * 0.24)) + 8;
        const auto diffuserCapacity = static_cast<size_t> (std::ceil (sampleRate * 0.016)) + 8;
        const auto earlyCapacity = static_cast<size_t> (std::ceil (sampleRate * 0.12)) + 8;

        for (auto& engine : engines)
            for (auto& channel : engine.channels)
            {
                for (auto& line : channel.lines)
                    line.assign (lineCapacity, 0.0f);
                for (auto& diffuser : channel.diffusers)
                    diffuser.assign (diffuserCapacity, 0.0f);
                channel.early.assign (earlyCapacity, 0.0f);
            }
        reset();
    }

    void reset()
    {
        for (auto& engine : engines)
            for (auto& channel : engine.channels)
            {
                for (auto& line : channel.lines)
                    std::fill (line.begin(), line.end(), 0.0f);
                for (auto& diffuser : channel.diffusers)
                    std::fill (diffuser.begin(), diffuser.end(), 0.0f);
                std::fill (channel.early.begin(), channel.early.end(), 0.0f);
                channel.dampingState.fill (0.0f);
                channel.diffuserIndex.fill (0);
                channel.earlyIndex = 0;
                channel.writeIndex = 0;
                channel.phase.fill (0.0f);
            }
    }

    // mode: 0 = room, 1 = plate, 2 = cloud. Buffers contain wet-only input
    // and are replaced by the selected algorithm's wet output.
    void process (float* left, float* right, int numSamples, int mode,
                  float size, float damping, float width)
    {
        if (left == nullptr || numSamples <= 0)
            return;

        mode = mode < 0 || mode > 2 ? 0 : mode;
        size = clamp01 (size);
        damping = clamp01 (damping);
        width = clamp01 (width);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto inputLeft = left[sample];
            const auto inputRight = right != nullptr ? right[sample] : inputLeft;
            std::array<float, 3> wetLeft {}, wetRight {};

            // Keep all three tails warm so changing the mode does not expose
            // stale buffers or abruptly restart the newly selected algorithm.
            for (int algorithm = 0; algorithm < 3; ++algorithm)
            {
                wetLeft[static_cast<size_t> (algorithm)] = processChannel (
                    engines[static_cast<size_t> (algorithm)].channels[0], algorithm,
                    inputLeft, inputRight, 0, size, damping);
                wetRight[static_cast<size_t> (algorithm)] = right != nullptr
                    ? processChannel (engines[static_cast<size_t> (algorithm)].channels[1], algorithm,
                                      inputRight, inputLeft, 1, size, damping)
                    : wetLeft[static_cast<size_t> (algorithm)];
            }

            auto outLeft = wetLeft[static_cast<size_t> (mode)];
            auto outRight = wetRight[static_cast<size_t> (mode)];
            if (right != nullptr)
            {
                const auto mid = 0.5f * (outLeft + outRight);
                const auto side = 0.5f * (outLeft - outRight) * (width * 1.5f);
                outLeft = mid + side;
                outRight = mid - side;
                right[sample] = outRight;
            }
            left[sample] = outLeft;
        }
    }

private:
    static constexpr int lineCount = 8;
    static constexpr int diffuserCount = 3;
    static constexpr float twoPi = 6.2831853071795864769f;

    struct ChannelState
    {
        std::array<std::vector<float>, lineCount> lines;
        std::array<std::vector<float>, diffuserCount> diffusers;
        std::vector<float> early;
        std::array<float, lineCount> dampingState {};
        std::array<float, lineCount> phase {};
        std::array<int, diffuserCount> diffuserIndex {};
        int writeIndex = 0;
        int earlyIndex = 0;
    };

    struct EngineState
    {
        std::array<ChannelState, 2> channels;
    };

    static float clamp01 (float value) { return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); }

    static float readDelay (const std::vector<float>& buffer, int writeIndex, float delay)
    {
        if (buffer.empty())
            return 0.0f;
        const auto length = static_cast<int> (buffer.size());
        auto position = static_cast<float> (writeIndex) - delay;
        while (position < 0.0f)
            position += static_cast<float> (length);
        while (position >= static_cast<float> (length))
            position -= static_cast<float> (length);
        const auto indexA = static_cast<int> (position);
        const auto indexB = (indexA + 1) % length;
        const auto fraction = position - static_cast<float> (indexA);
        return buffer[static_cast<size_t> (indexA)] * (1.0f - fraction)
             + buffer[static_cast<size_t> (indexB)] * fraction;
    }

    float processChannel (ChannelState& state, int algorithm, float input, float otherInput,
                          int channel, float size, float damping)
    {
        static constexpr std::array<std::array<float, lineCount>, 3> delaySeconds {{
            {{ 0.0297f, 0.0371f, 0.0411f, 0.0437f, 0.0531f, 0.0593f, 0.0617f, 0.0713f }},
            {{ 0.0311f, 0.0367f, 0.0411f, 0.0447f, 0.0523f, 0.0577f, 0.0619f, 0.0673f }},
            {{ 0.0481f, 0.0617f, 0.0721f, 0.0833f, 0.0971f, 0.1117f, 0.1283f, 0.1439f }}
        }};
        static constexpr std::array<float, diffuserCount> diffuserSeconds { 0.0047f, 0.0073f, 0.0113f };
        static constexpr std::array<float, 5> earlySeconds { 0.0113f, 0.0179f, 0.0263f, 0.0371f, 0.0539f };
        static constexpr std::array<float, 5> earlyGains { 0.46f, 0.34f, 0.27f, 0.20f, 0.15f };

        const auto scale = algorithm == 0 ? 0.78f + 0.72f * size
                         : algorithm == 1 ? 0.88f + 0.48f * size
                                          : 0.78f + 0.62f * size;
        const auto maxIndex = static_cast<int> (state.lines[0].size()) - 1;
        const auto earlyCapacity = static_cast<int> (state.early.size());
        auto early = 0.0f;

        if (algorithm == 0 && earlyCapacity > 1)
        {
            state.early[static_cast<size_t> (state.earlyIndex)] = input;
            for (size_t tap = 0; tap < earlySeconds.size(); ++tap)
            {
                const auto delay = std::clamp (earlySeconds[tap] * static_cast<float> (sampleRate),
                                               1.0f, static_cast<float> (earlyCapacity - 2));
                early += readDelay (state.early, state.earlyIndex, delay) * earlyGains[tap];
            }
            state.earlyIndex = (state.earlyIndex + 1) % earlyCapacity;
        }

        auto diffused = input;
        const auto activeDiffusers = algorithm == 0 ? 0 : (algorithm == 1 ? diffuserCount : 2);
        for (int stage = 0; stage < activeDiffusers; ++stage)
        {
            auto& buffer = state.diffusers[static_cast<size_t> (stage)];
            const auto length = static_cast<int> (buffer.size());
            if (length < 2)
                continue;
            const auto delay = std::clamp (diffuserSeconds[static_cast<size_t> (stage)]
                * static_cast<float> (sampleRate) * (0.85f + 0.3f * size),
                1.0f, static_cast<float> (length - 2));
            auto& index = state.diffuserIndex[static_cast<size_t> (stage)];
            const auto delayed = readDelay (buffer, index, delay);
            const auto coefficient = algorithm == 1 ? 0.72f : 0.58f;
            const auto output = delayed - coefficient * diffused;
            buffer[static_cast<size_t> (index)] = diffused + coefficient * output;
            index = (index + 1) % length;
            diffused = output;
        }

        std::array<float, lineCount> delayed {}, mixed {};
        const auto decaySeconds = algorithm == 0 ? 0.22f + 2.2f * size * size
                                : algorithm == 1 ? 0.55f + 5.5f * size
                                                 : 2.0f + 13.0f * size;
        const auto dampingHz = 18000.0f * std::pow (450.0f / 18000.0f, damping * damping);
        const auto dampingCoefficient = 1.0f - std::exp (-twoPi * dampingHz / static_cast<float> (sampleRate));
        const auto modulationDepth = algorithm == 2 ? sampleRate * (0.00018f + 0.0010f * size)
                                  : algorithm == 1 ? sampleRate * 0.00012f
                                                   : sampleRate * 0.000025f;
        const auto sideScale = channel == 0 ? 1.0f : 1.013f;
        auto outputSum = 0.0f;

        for (int line = 0; line < lineCount; ++line)
        {
            const auto lineIndex = static_cast<size_t> (line);
            const auto baseDelay = delaySeconds[static_cast<size_t> (algorithm)][lineIndex]
                                 * static_cast<float> (sampleRate) * scale * sideScale;
            auto mod = 0.0f;
            if (algorithm == 2)
            {
                state.phase[lineIndex] += (0.07f + 0.013f * static_cast<float> (line))
                                        / static_cast<float> (sampleRate);
                if (state.phase[lineIndex] >= 1.0f)
                    state.phase[lineIndex] -= 1.0f;
                mod = std::sin (twoPi * state.phase[lineIndex] + 0.31f * static_cast<float> (line))
                    * modulationDepth;
            }
            delayed[lineIndex] = readDelay (state.lines[lineIndex], state.writeIndex,
                std::clamp (baseDelay + mod, 1.0f, static_cast<float> (maxIndex - 1)));
            outputSum += delayed[lineIndex] * (line % 2 == 0 ? 1.0f : 0.82f);
        }

        // Normalised 8-point Hadamard feedback matrix.
        mixed = delayed;
        for (int stride = 1; stride < lineCount; stride *= 2)
            for (int base = 0; base < lineCount; base += 2 * stride)
                for (int offset = 0; offset < stride; ++offset)
                {
                    const auto a = static_cast<size_t> (base + offset);
                    const auto b = static_cast<size_t> (base + offset + stride);
                    const auto av = mixed[a];
                    const auto bv = mixed[b];
                    mixed[a] = av + bv;
                    mixed[b] = av - bv;
                }

        const auto normalizedInput = (diffused + (algorithm == 0 ? early * 0.55f : 0.0f)) * 0.34f;
        for (int line = 0; line < lineCount; ++line)
        {
            const auto index = static_cast<size_t> (line);
            auto& stateValue = state.dampingState[index];
            stateValue += dampingCoefficient * (mixed[index] * 0.35355339f - stateValue);
            const auto delayTime = delaySeconds[static_cast<size_t> (algorithm)][index] * scale * sideScale;
            const auto feedback = std::pow (0.001f, delayTime / decaySeconds);
            const auto cross = (otherInput - input) * (algorithm == 2 ? 0.035f : 0.012f);
            state.lines[index][static_cast<size_t> (state.writeIndex)] = normalizedInput
                + stateValue * feedback + cross;
        }
        state.writeIndex = (state.writeIndex + 1) % static_cast<int> (state.lines[0].size());

        const auto diffuseGain = algorithm == 0 ? 0.62f : (algorithm == 1 ? 0.58f : 0.52f);
        const auto earlyOutput = algorithm == 0 ? early * 0.22f : 0.0f;
        return (outputSum * (1.0f / 8.0f) * diffuseGain) + earlyOutput;
    }

    double sampleRate = 48000.0;
    std::array<EngineState, 3> engines;
};
