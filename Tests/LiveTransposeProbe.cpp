// Standalone experiment: clang++ -O3 -std=c++20 Tests/LiveTransposeProbe.cpp -o /tmp/live-transpose-probe
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <span>
#include <string_view>
#include <vector>

#ifdef NAM_TRANSPOSE_COMPARE_RUBBERBAND
#include <rubberband/RubberBandLiveShifter.h>
#endif

namespace
{
constexpr int sampleRate = 48000;
constexpr int analysisHop = 32;
constexpr double twoPi = 2.0 * std::numbers::pi;
constexpr std::array openStrings { 82.4069, 110.0, 146.8324, 195.9977, 246.9417, 329.6276 };
using Complex = std::complex<double>;

class LiveTranspose
{
public:
    LiveTranspose (double shift, double analysisMilliseconds, bool adaptive = false)
        : ratio (std::exp2 (shift / 12.0)),
          steadyStep (stepFor (analysisMilliseconds)),
          gainStep (stepFor (2.0)), adaptiveAttack (adaptive)
    {
        for (size_t i = 0; i < bands.size(); ++i)
        {
            auto& band = bands[i];
            const auto hz = 440.0 * std::exp2 ((static_cast<double> (i) + 35.0 - 69.0) / 12.0);
            band.carrierStep = std::polar (1.0, twoPi * hz / sampleRate);
        }
    }

    double process (double input)
    {
        auto analysisStep = steadyStep;
        if (adaptiveAttack)
        {
            fastEnvelope += stepFor (1.0) * (std::abs (input) - fastEnvelope);
            slowEnvelope += stepFor (25.0) * (std::abs (input) - slowEnvelope);
            const auto attacking = fastEnvelope > 0.003 && fastEnvelope > 3.0 * slowEnvelope;
            if (attacking && ! wasAttacking && attackLeft == 0)
                attackLeft = sampleRate / 100;
            wasAttacking = attacking;
            if (attackLeft > 0)
            {
                const auto blend = static_cast<double> (attackLeft--) / (sampleRate / 100);
                analysisStep += blend * (stepFor (4.0) - steadyStep);
            }
        }
        for (auto& band : bands)
        {
            band.carrier *= band.carrierStep;
            const auto baseband = input * std::conj (band.carrier);
            band.first += analysisStep * (baseband - band.first);
            band.second += analysisStep * (band.first - band.second);
        }
        if (++hopPosition == analysisHop)
        {
            updateBands();
            hopPosition = 0;
        }

        double output = 0.0;
        for (auto& band : bands)
        {
            if (band.gain == 0.0 && ! band.selected)
                continue;

            band.gain += gainStep * ((band.selected ? 1.0 : 0.0) - band.gain);
            if (band.gain < 1.0e-5 && ! band.selected)
            {
                band.gain = 0.0;
                continue;
            }
            band.shiftPhase *= band.shiftStep;
            output += 2.0 * band.gain * std::real (band.second * band.carrier * band.shiftPhase);
        }
        return output;
    }

private:
    static double stepFor (double ms)
    {
        return 1.0 - std::exp (-1000.0 / (ms * sampleRate));
    }

    struct Band
    {
        Complex carrier { 1.0 }, carrierStep { 1.0 }, shiftPhase { 1.0 }, shiftStep { 1.0 };
        Complex first {}, second {}, previous {};
        double gain = 0.0;
        bool selected = false;
    };

    void updateBands()
    {
        std::array<double, 72> power {};
        for (size_t i = 0; i < bands.size(); ++i)
            power[i] = std::norm (bands[i].second);
        const auto threshold = std::max (1.0e-12, *std::max_element (power.begin(), power.end()) * 0.001);

        for (size_t i = 0; i < bands.size(); ++i)
        {
            auto& band = bands[i];
            const auto aboveLeft = i == 0 || power[i] > power[i - 1];
            const auto aboveRight = i + 1 == bands.size() || power[i] >= power[i + 1];
            band.selected = power[i] >= threshold && aboveLeft && aboveRight;

            if (band.selected || band.gain > 0.0)
            {
                const auto phaseChange = std::arg (band.second * std::conj (band.previous));
                const auto angularFrequency = std::arg (band.carrierStep) + phaseChange / analysisHop;
                if (band.gain == 0.0)
                    band.shiftPhase = { 1.0, 0.0 };
                band.shiftStep = std::polar (1.0, angularFrequency * (ratio - 1.0));
            }
            band.previous = band.second;
        }
    }

    std::array<Band, 72> bands;
    double ratio, steadyStep, gainStep;
    const bool adaptiveAttack;
    double fastEnvelope = 0.0, slowEnvelope = 0.0;
    int hopPosition = 0, attackLeft = 0;
    bool wasAttacking = false;
};

#ifdef NAM_TRANSPOSE_COMPARE_RUBBERBAND
class RubberBandProbe
{
public:
    explicit RubberBandProbe (int semitones)
        : engine (sampleRate, 1, RubberBand::RubberBandLiveShifter::OptionWindowShort)
    {
        engine.setPitchScale (std::exp2 (semitones / 12.0));
        input.resize (engine.getBlockSize());
        output.resize (input.size());
    }

    double process (double sample)
    {
        const auto shifted = output[position];
        input[position++] = static_cast<float> (sample);
        if (position == input.size())
        {
            const float* in = input.data();
            auto* out = output.data();
            engine.shift (&in, &out);
            position = 0;
        }
        return shifted;
    }

private:
    RubberBand::RubberBandLiveShifter engine;
    std::vector<float> input, output;
    size_t position = 0;
};
#endif

struct Partial
{
    double frequency;
    double amplitude;
};

std::vector<Partial> guitarPartials (std::span<const double> strings, int harmonics)
{
    std::vector<Partial> partials;
    for (const auto frequency : strings)
        for (int harmonic = 1; harmonic <= harmonics; ++harmonic)
        {
            const auto hz = frequency * harmonic;
            const auto amplitude = 0.08 / (harmonic * std::sqrt (harmonic));
            // Avoid counting shared and nearly coincident harmonics twice in
            // the two-second spectral measurement.
            const auto shared = std::find_if (partials.begin(), partials.end(), [hz] (const auto& partial)
            {
                return std::abs (partial.frequency - hz) < 1.5;
            });
            if (shared == partials.end())
                partials.push_back ({ hz, amplitude });
            else
                shared->amplitude += amplitude;
        }
    return partials;
}

template <typename Shifter>
void measure (const char* name, const std::vector<Partial>& partials, int shift,
              const char* mode, double analysisMs, Shifter shifter)
{
    const auto ratio = std::exp2 (shift / 12.0);
    constexpr int onset = sampleRate / 10;
    constexpr int segment = sampleRate * 3;
    constexpr int hostBlock = 128;
    double windowPower = 0.0, weights = 0.0;
    std::vector<Complex> bins (partials.size());
    std::vector<double> blockTimes;
    std::vector<double> output (segment);
    std::vector<double> input (segment);
    for (int i = onset; i < segment; ++i)
        for (const auto [frequency, amplitude] : partials)
            input[i] += amplitude * std::sin (twoPi * frequency * (i - onset) / sampleRate);

    for (int offset = 0; offset < segment; offset += hostBlock)
    {
        const auto begin = std::chrono::steady_clock::now();
        for (int i = offset; i < std::min (offset + hostBlock, segment); ++i)
            output[i] = shifter.process (input[i]);
        const auto micros = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - begin);
        if (offset >= sampleRate)
            blockTimes.push_back (micros.count());
    }

    for (int i = sampleRate; i < segment; ++i)
    {
        const auto t = static_cast<double> (i) / sampleRate;
        const auto window = 0.5 - 0.5 * std::cos (twoPi * (t - 1.0) / 2.0);
        weights += window;
        windowPower += window * output[i] * output[i];
        for (size_t voice = 0; voice < partials.size(); ++voice)
            bins[voice] += window * output[i] * std::polar (1.0, -twoPi * partials[voice].frequency * ratio * t);
    }
    double targetPower = 0.0;
    for (const auto bin : bins)
        targetPower += 2.0 * std::norm (bin) / weights;

    // Relative onset is the difference between the first 1-ms RMS crossing of
    // 20% of the same signal's steady-state RMS, with the same detector on both paths.
    const auto onsetAt = [] (const std::vector<double>& audio, double steadyRms)
    {
        constexpr int rmsSamples = sampleRate / 1000;
        double sum = 0.0;
        for (int i = onset; i < onset + sampleRate / 2; ++i)
        {
            sum += std::pow (audio[i], 2) - std::pow (audio[i - rmsSamples], 2);
            if (sum >= rmsSamples * std::pow (0.2 * steadyRms, 2))
                return i;
        }
        return onset + sampleRate / 2;
    };
    const auto outputRms = std::sqrt (windowPower / weights);
    double inputPower = 0.0;
    for (const auto& partial : partials)
        inputPower += partial.amplitude * partial.amplitude / 2.0;
    const auto inputRms = std::sqrt (inputPower);
    const auto delay = 1000.0 * (onsetAt (output, outputRms) - onsetAt (input, inputRms)) / sampleRate;

    std::sort (blockTimes.begin(), blockTimes.end());
    std::cout << mode << ' ' << analysisMs << " ms decay | " << shift << " st | " << name
              << " | target " << 100.0 * targetPower / windowPower << "%"
              << " | onset +" << delay << " ms | p99 " << blockTimes[blockTimes.size() * 99 / 100] << " us\n";
}
}

int main (int argc, char** argv)
{
    // Clip mode reads and writes mono, 48 kHz, little-endian float32 PCM.
    if (argc == 5)
    {
        const std::string_view shiftOption = argv[1];
        if (shiftOption != "-1" && shiftOption != "-2")
            return 2;
        const int shift = shiftOption == "-1" ? -1 : -2;
        std::ifstream input (argv[3], std::ios::binary);
        if (! input)
            return 2;

        const auto render = [&] (auto shifter)
        {
            std::ofstream output (argv[4], std::ios::binary);
            for (float sample; input.read (reinterpret_cast<char*> (&sample), sizeof (sample));)
            {
                const auto shifted = static_cast<float> (shifter.process (sample));
                output.write (reinterpret_cast<const char*> (&shifted), sizeof (shifted));
            }
            return output && input.eof() && input.gcount() == 0 ? 0 : 2;
        };
        const std::string_view mode = argv[2];
        if (mode == "adaptive")
            return render (LiveTranspose (shift, 10.0, true));
#ifdef NAM_TRANSPOSE_COMPARE_RUBBERBAND
        if (mode == "rubber-band")
            return render (RubberBandProbe (shift));
#endif
        return 2;
    }
    if (argc != 1)
        return 2;

    std::cout << std::fixed << std::setprecision (2);
    constexpr std::array lowE { openStrings.front() };
    constexpr std::array highE { openStrings.back() };
    constexpr std::array powerChord { openStrings[0], 123.4708, 164.8138 };
    constexpr std::array openEChord { 82.4069, 123.4708, 164.8138, 207.6523, 246.9417, 329.6276 };
    const auto run = [&] (auto makeShifter, const char* mode, double decay)
    {
        for (const int shift : { -1, -2 })
        {
            measure ("low E", guitarPartials (lowE, 1), shift, mode, decay, makeShifter (shift));
            measure ("high E", guitarPartials (highE, 1), shift, mode, decay, makeShifter (shift));
            measure ("power chord", guitarPartials (powerChord, 1), shift, mode, decay, makeShifter (shift));
            measure ("six open strings", guitarPartials (openStrings, 1), shift, mode, decay, makeShifter (shift));
            measure ("E major with partials", guitarPartials (openEChord, 5), shift, mode, decay, makeShifter (shift));
        }
    };
    for (const auto decay : { 8.0, 10.0, 12.0, 16.0 })
        run ([decay] (int shift) { return LiveTranspose (shift, decay); }, "fixed", decay);
    run ([] (int shift) { return LiveTranspose (shift, 10.0, true); }, "adaptive", 10.0);
    run ([] (int shift) { return LiveTranspose (shift, 12.0, true); }, "adaptive", 12.0);
#ifdef NAM_TRANSPOSE_COMPARE_RUBBERBAND
    run ([] (int shift) { return RubberBandProbe (shift); }, "rubber-band", 0.0);
#endif
}
