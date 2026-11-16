// TakeCare DSP adapted from Airwindows, revision 143a86df3642881e0be4bf827815050721063b41.
// Copyright (c) airwindows. MIT license.
#include "TakeCareChorus.h"
#include <cmath>
#include <cstdint>

struct TakeCareChorus::State
{
    State()
    {
        for(int x = 0; x < 32767+5; x++) {a3AL[x] = 0.0; a3AR[x] = 0.0;}
        for(int x = 0; x < 32767+5; x++) {a3BL[x] = 0.0; a3BR[x] = 0.0;}
        for(int x = 0; x < 32767+5; x++) {a3CL[x] = 0.0; a3CR[x] = 0.0;}
        for(int x = 0; x < 32767+5; x++) {a3DL[x] = 0.0; a3DR[x] = 0.0;}
        for(int x = 0; x < 32767+5; x++) {a3EL[x] = 0.0; a3ER[x] = 0.0;}
        for(int x = 0; x < 32767+5; x++) {a3FL[x] = 0.0; a3FR[x] = 0.0;}
        for(int x = 0; x < 32767+5; x++) {a3GL[x] = 0.0; a3GR[x] = 0.0;}
        for(int x = 0; x < 32767+5; x++) {a3HL[x] = 0.0; a3HR[x] = 0.0;}
        for(int x = 0; x < 32767+5; x++) {a3IL[x] = 0.0; a3IR[x] = 0.0;}
        c3AL = c3BL = c3CL = c3DL = c3EL = c3FL = c3GL = c3HL = c3IL = 1;
        c3AR = c3BR = c3CR = c3DR = c3ER = c3FR = c3GR = c3HR = c3IR = 1;
        f3AL = f3BL = f3CL = 0.0;
        f3CR = f3FR = f3IR = 0.0;
        avg3L = avg3R = 0.0;
        for (int x = 0; x < bez_total; x++) bez[x] = 0.0;
        bez[bez_cycle] = 1.0;
        rotate = 0.0;
        oldfpd = 0.4294967295;
        buf = 8192;
        vibDepth = 0.0;
        lastSampleL = 0.0;
        wasPosClipL = false;
        wasNegClipL = false;
        lastSampleR = 0.0;
        wasPosClipR = false;
        wasNegClipR = false;
    }

    void process (juce::AudioBuffer<float>& buffer, int channels, int samples,
                  double sampleRate, float rateHz, float depth, float mix, const Advanced& controls)
    {
        constexpr auto pi = juce::MathConstants<double>::pi;
        constexpr auto halfPi = juce::MathConstants<double>::halfPi;
        // TakeCare's Derez control switches from stepped to continuous
        // undersampling above the midpoint, with full resolution at the midpoint.
        const auto scale = sampleRate / 44100.0;
        const auto stepped = controls.derez <= 0.5f;
        const auto resolution = 1.0 - std::abs (2.0 * controls.derez - 1.0);
        const auto fraction = std::clamp (resolution / scale, 0.0005, 1.0);
        const auto subdivision = static_cast<int> (1.0 / fraction);
        const auto derez = stepped ? 1.0 / subdivision
            : fraction * 0.5 * std::pow (scale, 0.5 - (scale - 1.0) * 0.0375);
        const auto bezTrim = stepped ? 1.0 - 1.0 / (subdivision + 1.0)
            : 1.0 - std::pow (derez * 0.5, 2.0 / derez);
        const auto vibSpeed = pi * 2.0 * rateHz * (stepped ? subdivision : 1.0 / fraction) / sampleRate;
        const auto vibRandom = std::pow (controls.random, 3.0) * 0.99;
        const auto targetDepth = depth * depth * 0.5;
        const auto reg3n = controls.regen * 0.0625;
        const auto targetBuf = static_cast<int> (std::pow (controls.buffer, 3.0) * 32510.0 + 256);
        if (previousOutput < 0.0)
            previousOutput = controls.output;
        const double targetWet = 1.0 - (1.0 - mix) * (1.0 - mix);
        if (previousWet < 0.0)
            previousWet = targetWet;

        auto* left = buffer.getWritePointer (0);
        auto* right = channels > 1 ? buffer.getWritePointer (1) : nullptr;
        for (int i = 0; i < samples; ++i)
        {
            const double drySampleL = left[i];
            const double drySampleR = right != nullptr ? right[i] : drySampleL;
            double inputSampleL = drySampleL;
            double inputSampleR = drySampleR;
            const auto wet = std::lerp (previousWet, targetWet, (i + 1.0) / samples);
            bez[bez_cycle] += derez;
                bez[bez_SampL] += ((inputSampleL+bez[bez_InL]) * derez);
                bez[bez_SampR] += ((inputSampleR+bez[bez_InR]) * derez);
                bez[bez_InL] = inputSampleL; bez[bez_InR] = inputSampleR;
                if (bez[bez_cycle] > 1.0) { //hit the end point and we do a reverb sample
                    bez[bez_cycle] = stepped ? 0.0 : bez[bez_cycle] - 1.0;

                    inputSampleL = (bez[bez_SampL]+bez[bez_AvgInSampL])*0.5;
                    bez[bez_AvgInSampL] = bez[bez_SampL];
                    inputSampleR = (bez[bez_SampR]+bez[bez_AvgInSampR])*0.5;
                    bez[bez_AvgInSampR] = bez[bez_SampR];

                    rotate += (oldfpd*vibSpeed);
                    if (rotate > (pi*2.0)) {
                        rotate -= (pi*2.0);
                        oldfpd = (1.0-vibRandom) + ((fpdL*0.000000000618)*vibRandom);
                    }
                    vibDepth = (vibDepth*0.99)+(targetDepth*0.01);
                    if (buf < targetBuf) buf++;
                    if (buf > targetBuf) buf--;

                    double mA = (std::sin(rotate)+1.0)*vibDepth*buf;
                    double mB = (std::sin(rotate+(pi/9.0))+1.0)*vibDepth*buf;
                    double mC = (std::sin(rotate+((pi/9.0)*2.0))+1.0)*vibDepth*buf;
                    double mD = (std::sin(rotate+((pi/9.0)*3.0))+1.0)*vibDepth*buf;
                    double mE = (std::sin(rotate+((pi/9.0)*4.0))+1.0)*vibDepth*buf;
                    double mF = (std::sin(rotate+((pi/9.0)*5.0))+1.0)*vibDepth*buf;
                    double mG = (std::sin(rotate+((pi/9.0)*6.0))+1.0)*vibDepth*buf;
                    double mH = (std::sin(rotate+((pi/9.0)*7.0))+1.0)*vibDepth*buf;
                    double mI = (std::sin(rotate+((pi/9.0)*8.0))+1.0)*vibDepth*buf;

                    inputSampleL = std::sin(std::fmin(std::fmax(inputSampleL*0.5,-halfPi),halfPi));
                    inputSampleR = std::sin(std::fmin(std::fmax(inputSampleR*0.5,-halfPi),halfPi));

                    a3AL[c3AL] = inputSampleL + std::fmin(std::fmax(f3AL*reg3n,-pi),pi);
                    a3BL[c3BL] = inputSampleL + std::fmin(std::fmax(f3BL*reg3n,-pi),pi);
                    a3CL[c3CL] = inputSampleL + std::fmin(std::fmax(f3CL*reg3n,-pi),pi);

                    a3CR[c3CR] = inputSampleR + std::fmin(std::fmax(f3CR*reg3n,-pi),pi);
                    a3FR[c3FR] = inputSampleR + std::fmin(std::fmax(f3FR*reg3n,-pi),pi);
                    a3IR[c3IR] = inputSampleR + std::fmin(std::fmax(f3IR*reg3n,-pi),pi);

                    c3AL++; if (c3AL < 0 || c3AL > buf) c3AL = 0;
                    c3BL++; if (c3BL < 0 || c3BL > buf) c3BL = 0;
                    c3CL++; if (c3CL < 0 || c3CL > buf) c3CL = 0;
                    c3CR++; if (c3CR < 0 || c3CR > buf) c3CR = 0;
                    c3FR++; if (c3FR < 0 || c3FR > buf) c3FR = 0;
                    c3IR++; if (c3IR < 0 || c3IR > buf) c3IR = 0;

                    double o3AL = a3AL[(int)(c3AL+mA-((c3AL+mA>buf)?buf+1:0))] *(1.0-(mA-std::floor(mA)));
                    o3AL += (a3AL[(int)(c3AL+mA+1-((c3AL+mA+1>buf)?buf+1:0))] *((mA-std::floor(mA))));
                    double o3BL = a3BL[(int)(c3BL+mB-((c3BL+mB>buf)?buf+1:0))] *(1.0-(mB-std::floor(mB)));
                    o3BL += (a3BL[(int)(c3BL+mB+1-((c3BL+mB+1>buf)?buf+1:0))] *((mB-std::floor(mB))));
                    double o3CL = a3CL[(int)(c3CL+mC-((c3CL+mC>buf)?buf+1:0))] *(1.0-(mC-std::floor(mC)));
                    o3CL += (a3CL[(int)(c3CL+mC+1-((c3CL+mC+1>buf)?buf+1:0))] *((mC-std::floor(mC))));
                    double o3CR = a3CR[(int)(c3CR+mC-((c3CR+mC>buf)?buf+1:0))] *(1.0-(mC-std::floor(mC)));
                    o3CR += (a3CR[(int)(c3CR+mC+1-((c3CR+mC+1>buf)?buf+1:0))] *((mC-std::floor(mC))));
                    double o3FR = a3FR[(int)(c3FR+mF-((c3FR+mF>buf)?buf+1:0))] *(1.0-(mF-std::floor(mF)));
                    o3FR += (a3FR[(int)(c3FR+mF+1-((c3FR+mF+1>buf)?buf+1:0))] *((mF-std::floor(mF))));
                    double o3IR = a3IR[(int)(c3IR+mI-((c3IR+mI>buf)?buf+1:0))] *(1.0-(mI-std::floor(mI)));
                    o3IR += (a3IR[(int)(c3IR+mI+1-((c3IR+mI+1>buf)?buf+1:0))] *((mI-std::floor(mI))));

                    a3DL[c3DL] = (((o3BL + o3CL) * -2.0) + o3AL);
                    a3EL[c3EL] = (((o3AL + o3CL) * -2.0) + o3BL);
                    a3FL[c3FL] = (((o3AL + o3BL) * -2.0) + o3CL);
                    a3BR[c3BR] = (((o3FR + o3IR) * -2.0) + o3CR);
                    a3ER[c3ER] = (((o3CR + o3IR) * -2.0) + o3FR);
                    a3HR[c3HR] = (((o3CR + o3FR) * -2.0) + o3IR);

                    c3DL++; if (c3DL < 0 || c3DL > buf) c3DL = 0;
                    c3EL++; if (c3EL < 0 || c3EL > buf) c3EL = 0;
                    c3FL++; if (c3FL < 0 || c3FL > buf) c3FL = 0;
                    c3BR++; if (c3BR < 0 || c3BR > buf) c3BR = 0;
                    c3ER++; if (c3ER < 0 || c3ER > buf) c3ER = 0;
                    c3HR++; if (c3HR < 0 || c3HR > buf) c3HR = 0;

                    double o3DL = a3DL[(int)(c3DL+mD-((c3DL+mD>buf)?buf+1:0))] *(1.0-(mD-std::floor(mD)));
                    o3DL += (a3DL[(int)(c3DL+mD+1-((c3DL+mD+1>buf)?buf+1:0))] *((mD-std::floor(mD))));
                    double o3EL = a3EL[(int)(c3EL+mE-((c3EL+mE>buf)?buf+1:0))] *(1.0-(mE-std::floor(mE)));
                    o3EL += (a3EL[(int)(c3EL+mE+1-((c3EL+mE+1>buf)?buf+1:0))] *((mE-std::floor(mE))));
                    double o3FL = a3FL[(int)(c3FL+mF-((c3FL+mF>buf)?buf+1:0))] *(1.0-(mF-std::floor(mF)));
                    o3FL += (a3FL[(int)(c3FL+mF+1-((c3FL+mF+1>buf)?buf+1:0))] *((mF-std::floor(mF))));
                    double o3BR = a3BR[(int)(c3BR+mB-((c3BR+mB>buf)?buf+1:0))] *(1.0-(mB-std::floor(mB)));
                    o3BR += (a3BR[(int)(c3BR+mB+1-((c3BR+mB+1>buf)?buf+1:0))] *((mB-std::floor(mB))));
                    double o3ER = a3ER[(int)(c3ER+mE-((c3ER+mE>buf)?buf+1:0))] *(1.0-(mE-std::floor(mE)));
                    o3ER += (a3ER[(int)(c3ER+mE+1-((c3ER+mE+1>buf)?buf+1:0))] *((mE-std::floor(mE))));
                    double o3HR = a3HR[(int)(c3HR+mH-((c3HR+mH>buf)?buf+1:0))] *(1.0-(mH-std::floor(mH)));
                    o3HR += (a3HR[(int)(c3HR+mH+1-((c3HR+mH+1>buf)?buf+1:0))] *((mH-std::floor(mH))));

                    a3GL[c3GL] = (((o3EL + o3FL) * -2.0) + o3DL);
                    a3HL[c3HL] = (((o3DL + o3FL) * -2.0) + o3EL);
                    a3IL[c3IL] = (((o3DL + o3EL) * -2.0) + o3FL);
                    a3AR[c3AR] = (((o3ER + o3HR) * -2.0) + o3BR);
                    a3DR[c3DR] = (((o3BR + o3HR) * -2.0) + o3ER);
                    a3GR[c3GR] = (((o3BR + o3ER) * -2.0) + o3HR);


                    c3GL++; if (c3GL < 0 || c3GL > buf) c3GL = 0;
                    c3HL++; if (c3HL < 0 || c3HL > buf) c3HL = 0;
                    c3IL++; if (c3IL < 0 || c3IL > buf) c3IL = 0;
                    c3AR++; if (c3AR < 0 || c3AR > buf) c3AR = 0;
                    c3DR++; if (c3DR < 0 || c3DR > buf) c3DR = 0;
                    c3GR++; if (c3GR < 0 || c3GR > buf) c3GR = 0;

                    double o3GL = a3GL[(int)(c3GL+mG-((c3GL+mG>buf)?buf+1:0))] *(1.0-(mG-std::floor(mG)));
                    o3GL += (a3GL[(int)(c3GL+mG+1-((c3GL+mG+1>buf)?buf+1:0))] *((mG-std::floor(mG))));
                    double o3HL = a3HL[(int)(c3HL+mH-((c3HL+mH>buf)?buf+1:0))] *(1.0-(mH-std::floor(mH)));
                    o3HL += (a3HL[(int)(c3HL+mH+1-((c3HL+mH+1>buf)?buf+1:0))] *((mH-std::floor(mH))));
                    double o3IL = a3IL[(int)(c3IL+mI-((c3IL+mI>buf)?buf+1:0))] *(1.0-(mI-std::floor(mI)));
                    o3IL += (a3IL[(int)(c3IL+mI+1-((c3IL+mI+1>buf)?buf+1:0))] *((mI-std::floor(mI))));
                    double o3AR = a3AR[(int)(c3AR+mA-((c3AR+mA>buf)?buf+1:0))] *(1.0-(mA-std::floor(mA)));
                    o3AR += (a3AR[(int)(c3AR+mA+1-((c3AR+mA+1>buf)?buf+1:0))] *((mA-std::floor(mA))));
                    double o3DR = a3DR[(int)(c3DR+mD-((c3DR+mD>buf)?buf+1:0))] *(1.0-(mD-std::floor(mD)));
                    o3DR += (a3DR[(int)(c3DR+mD+1-((c3DR+mD+1>buf)?buf+1:0))] *((mD-std::floor(mD))));
                    double o3GR = a3GR[(int)(c3GR+mG-((c3GR+mG>buf)?buf+1:0))] *(1.0-(mG-std::floor(mG)));
                    o3GR += (a3GR[(int)(c3GR+mG+1-((c3GR+mG+1>buf)?buf+1:0))] *((mG-std::floor(mG))));

                    f3AL = (((o3GR + o3HR) * -2.0) + o3IR);
                    f3BL = (((o3GR + o3HR) * -2.0) + o3IR);
                    f3CL = (((o3GR + o3HR) * -2.0) + o3IR);

                    f3CR = (((o3AL + o3DL) * -2.0) + o3GL);
                    f3FR = (((o3AL + o3DL) * -2.0) + o3GL);
                    f3IR = (((o3AL + o3DL) * -2.0) + o3GL);

                    inputSampleL = (o3GL + o3HL + o3IL)*0.03125;
                    inputSampleR = (o3AR + o3DR + o3GR)*0.03125;

                    f3AL = (f3AL+f3AL+f3AL+std::fabs(avg3L))*0.25; avg3L = f3AL;
                    f3CR = (f3CR+f3CR+f3CR+std::fabs(avg3R))*0.25; avg3R = f3CR;
                    //manipulating deep reverb tail for realism

                    //begin ClipOnly stereo as a little, compressed chunk that can be dropped into undersampled code
                    inputSampleL = std::fmin(std::fmax(inputSampleL,-4.0),4.0);
                    if (wasPosClipL == true) { //current will be over
                        if (inputSampleL<lastSampleL) lastSampleL=0.79+(inputSampleL*0.2);
                        else lastSampleL = 0.2+(lastSampleL*0.79);
                    } wasPosClipL = false;
                    if (inputSampleL>0.99) {wasPosClipL=true;inputSampleL=0.79+(lastSampleL*0.2);}
                    if (wasNegClipL == true) { //current will be -over
                        if (inputSampleL > lastSampleL) lastSampleL=-0.79+(inputSampleL*0.2);
                        else lastSampleL=-0.2+(lastSampleL*0.79);
                    } wasNegClipL = false;
                    if (inputSampleL<-0.99) {wasNegClipL=true;inputSampleL=-0.79+(lastSampleL*0.2);}
                    lastSampleL = inputSampleL;
                    inputSampleR = std::fmin(std::fmax(inputSampleR,-4.0),4.0);
                    if (wasPosClipR == true) { //current will be over
                        if (inputSampleR<lastSampleR) lastSampleR=0.79+(inputSampleR*0.2);
                        else lastSampleR = 0.2+(lastSampleR*0.79);
                    } wasPosClipR = false;
                    if (inputSampleR>0.99) {wasPosClipR=true;inputSampleR=0.79+(lastSampleR*0.2);}
                    if (wasNegClipR == true) { //current will be -over
                        if (inputSampleR > lastSampleR) lastSampleR=-0.79+(inputSampleR*0.2);
                        else lastSampleR=-0.2+(lastSampleR*0.79);
                    } wasNegClipR = false;
                    if (inputSampleR<-0.99) {wasNegClipR=true;inputSampleR=-0.79+(lastSampleR*0.2);}
                    lastSampleR = inputSampleR;
                    //end ClipOnly stereo as a little, compressed chunk that can be dropped into undersampled code

                    inputSampleL = std::asin(inputSampleL*0.7);
                    inputSampleR = std::asin(inputSampleR*0.7);

                    bez[bez_CL] = bez[bez_BL];
                    bez[bez_BL] = bez[bez_AL];
                    bez[bez_AL] = inputSampleL;
                    bez[bez_SampL] = 0.0;

                    bez[bez_CR] = bez[bez_BR];
                    bez[bez_BR] = bez[bez_AR];
                    bez[bez_AR] = inputSampleR;
                    bez[bez_SampR] = 0.0;
                }

                double X = bez[bez_cycle] * bezTrim;
                double CBL = (bez[bez_CL]*(1.0-X))+(bez[bez_BL]*X);
                double CBR = (bez[bez_CR]*(1.0-X))+(bez[bez_BR]*X);
                double BAL = (bez[bez_BL]*(1.0-X))+(bez[bez_AL]*X);
                double BAR = (bez[bez_BR]*(1.0-X))+(bez[bez_AR]*X);
                double CBAL = (bez[bez_BL]+(CBL*(1.0-X))+(BAL*X))*-0.25;
                double CBAR = (bez[bez_BR]+(CBR*(1.0-X))+(BAR*X))*-0.25;
                inputSampleL = CBAL+bez[bez_AvgOutSampL]; bez[bez_AvgOutSampL] = CBAL;
                inputSampleR = CBAR+bez[bez_AvgOutSampR]; bez[bez_AvgOutSampR] = CBAR;

                const auto output = std::lerp (previousOutput, static_cast<double> (controls.output),
                                               (i + 1.0) / samples);
                inputSampleL *= output;
                inputSampleR *= output;
                if (wet != 1.0) {
                    inputSampleL = (inputSampleL * wet) + (drySampleL * (1.0-wet));
                    inputSampleR = (inputSampleR * wet) + (drySampleR * (1.0-wet));
                }
                //Dry/Wet control, defaults to the last slider
            fpdL ^= fpdL << 13;
            fpdL ^= fpdL >> 17;
            fpdL ^= fpdL << 5;
            left[i] = static_cast<float> (right != nullptr ? inputSampleL : (inputSampleL + inputSampleR) * 0.5);
            if (right != nullptr)
                right[i] = static_cast<float> (inputSampleR);
        }
        previousWet = targetWet;
        previousOutput = controls.output;
    }

    double a3AL[32767+5];
    double a3BL[32767+5];
    double a3CL[32767+5];
    double a3DL[32767+5];
    double a3EL[32767+5];
    double a3FL[32767+5];
    double a3GL[32767+5];
    double a3HL[32767+5];
    double a3IL[32767+5];
    double a3AR[32767+5];
    double a3BR[32767+5];
    double a3CR[32767+5];
    double a3DR[32767+5];
    double a3ER[32767+5];
    double a3FR[32767+5];
    double a3GR[32767+5];
    double a3HR[32767+5];
    double a3IR[32767+5];
    int c3AL,c3AR,c3BL,c3BR,c3CL,c3CR,c3DL,c3DR,c3EL,c3ER;
    int c3FL,c3FR,c3GL,c3GR,c3HL,c3HR,c3IL,c3IR;
    double f3AL,f3BL,f3CL,f3CR,f3FR,f3IR;
    double avg3L,avg3R;
    enum {
    bez_AL,
    bez_AR,
    bez_BL,
    bez_BR,
    bez_CL,
    bez_CR,
    bez_InL,
    bez_InR,
    bez_UnInL,
    bez_UnInR,
    bez_SampL,
    bez_SampR,
    bez_AvgInSampL,
    bez_AvgInSampR,
    bez_AvgOutSampL,
    bez_AvgOutSampR,
    bez_cycle,
    bez_total
    }; //the new undersampling. bez signifies the bezier curve reconstruction
    double bez[bez_total];
    double rotate;
    double oldfpd;
    int buf;
    double vibDepth;
    double previousWet = -1.0;
    double previousOutput = -1.0;
    double lastSampleL;
    bool wasPosClipL;
    bool wasNegClipL;
    double lastSampleR;
    bool wasPosClipR;
    bool wasNegClipR; //Stereo ClipOnly
    uint32_t fpdL = 0x6d2b79f5;
};

TakeCareChorus::TakeCareChorus() = default;
TakeCareChorus::~TakeCareChorus() = default;

void TakeCareChorus::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    state = std::make_unique<State>();
}

void TakeCareChorus::process (juce::AudioBuffer<float>& buffer, int channels, int samples,
                             float rateHz, float depth, float mix, const Advanced& controls)
{
    if (state != nullptr && channels > 0 && samples > 0)
        state->process (buffer, channels, samples, sampleRate, rateHz, depth, mix, controls);
}
