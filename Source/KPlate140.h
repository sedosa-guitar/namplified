/* ========================================
 *  kPlate140 - kPlate140.h
 *  Copyright (c) airwindows, Airwindows uses the MIT license
 * ======================================== */

#pragma once

#include <cstdint>

namespace airwindows
{
class KPlate140
{
public:
    void prepare (double newSampleRate);
    void process (float* left, float* right, int sampleFrames, float decay);

private:
    std::uint32_t fpdL = 0, fpdR = 0;
    double sampleRate = 44100.0;
    static constexpr const int predelay = 16000;
    static constexpr int vlfpredelay = 16000;

    static constexpr int delayA = 466;
    static constexpr int delayB = 233;
    static constexpr int delayC = 747;
    static constexpr int delayD = 70;
    static constexpr int delayE = 776;
    static constexpr int delayF = 719;
    static constexpr int delayG = 130;
    static constexpr int delayH = 511;
    static constexpr int delayI = 148;
    static constexpr int delayJ = 84;
    static constexpr int delayK = 787;
    static constexpr int delayL = 124;
    static constexpr int delayM = 72;
    static constexpr int delayN = 667;
    static constexpr int delayO = 773;
    static constexpr int delayP = 29;
    static constexpr int delayQ = 710;
    static constexpr int delayR = 309;
    static constexpr int delayS = 773;
    static constexpr int delayT = 487;
    static constexpr int delayU = 11;
    static constexpr int delayV = 73;
    static constexpr int delayW = 756;
    static constexpr int delayX = 627;
    static constexpr int delayY = 328;
    double iirAL;
    double iirBL;

    double gainIn;
    double gainOutL;
    double gainOutR;

    double aAL[delayA+5];
    double aBL[delayB+5];
    double aCL[delayC+5];
    double aDL[delayD+5];
    double aEL[delayE+5];
    double aFL[delayF+5];
    double aGL[delayG+5];
    double aHL[delayH+5];
    double aIL[delayI+5];
    double aJL[delayJ+5];
    double aKL[delayK+5];
    double aLL[delayL+5];
    double aML[delayM+5];
    double aNL[delayN+5];
    double aOL[delayO+5];
    double aPL[delayP+5];
    double aQL[delayQ+5];
    double aRL[delayR+5];
    double aSL[delayS+5];
    double aTL[delayT+5];
    double aUL[delayU+5];
    double aVL[delayV+5];
    double aWL[delayW+5];
    double aXL[delayX+5];
    double aYL[delayY+5];

    double aAR[delayA+5];
    double aBR[delayB+5];
    double aCR[delayC+5];
    double aDR[delayD+5];
    double aER[delayE+5];
    double aFR[delayF+5];
    double aGR[delayG+5];
    double aHR[delayH+5];
    double aIR[delayI+5];
    double aJR[delayJ+5];
    double aKR[delayK+5];
    double aLR[delayL+5];
    double aMR[delayM+5];
    double aNR[delayN+5];
    double aOR[delayO+5];
    double aPR[delayP+5];
    double aQR[delayQ+5];
    double aRR[delayR+5];
    double aSR[delayS+5];
    double aTR[delayT+5];
    double aUR[delayU+5];
    double aVR[delayV+5];
    double aWR[delayW+5];
    double aXR[delayX+5];
    double aYR[delayY+5];

    double aZL[predelay+5];
    double aZR[predelay+5];
    double aVLFL[vlfpredelay+5];
    double aVLFR[vlfpredelay+5];

    double feedbackAL;
    double feedbackBL;
    double feedbackCL;
    double feedbackDL;
    double feedbackEL;

    double feedbackER;
    double feedbackJR;
    double feedbackOR;
    double feedbackTR;
    double feedbackYR;

    double feedblurAL;
    double feedblurBL;
    double feedblurCL;
    double feedblurDL;
    double feedblurEL;

    double feedblurER;
    double feedblurJR;
    double feedblurOR;
    double feedblurTR;
    double feedblurYR;

    double sbAL;
    double sbBL;
    double sbCL;
    double sbDL;
    double sbEL;

    double sbER;
    double sbJR;
    double sbOR;
    double sbTR;
    double sbYR;


    enum {
        prevSampL1,
        prevSlewL1,
        prevSampR1,
        prevSlewR1,
        prevSampL2,
        prevSlewL2,
        prevSampR2,
        prevSlewR2,
        prevSampL3,
        prevSlewL3,
        prevSampR3,
        prevSlewR3,
        prevSampL4,
        prevSlewL4,
        prevSampR4,
        prevSlewR4,
        prevSampL5,
        prevSlewL5,
        prevSampR5,
        prevSlewR5,
        prevSampL6,
        prevSlewL6,
        prevSampR6,
        prevSlewR6,
        prevSampL7,
        prevSlewL7,
        prevSampR7,
        prevSlewR7,
        prevSampL8,
        prevSlewL8,
        prevSampR8,
        prevSlewR8,
        prevSampL9,
        prevSlewL9,
        prevSampR9,
        prevSlewR9,
        prevSampL10,
        prevSlewL10,
        prevSampR10,
        prevSlewR10,
        pear_total
    }; //fixed frequency pear filter for ultrasonics, stereo

    double pearA[pear_total]; //probably worth just using a number here
    double pearB[pear_total]; //probably worth just using a number here
    double pearC[pear_total]; //probably worth just using a number here
    double pearD[pear_total]; //probably worth just using a number here
    double pearE[pear_total]; //probably worth just using a number here
    double pearF[pear_total]; //probably worth just using a number here

    double vibratoL;
    double vibratoR;
    double vibAL;
    double vibAR;
    double vibBL;
    double vibBR;

    double subAL;
    double subAR;
    double subBL;
    double subBR;
    double subCL;
    double subCR;

    double previousAL;
    double previousBL;
    double previousCL;
    double previousDL;
    double previousEL;

    double iirAR;
    double iirBR;

    double previousAR;
    double previousBR;
    double previousCR;
    double previousDR;
    double previousER;

    int countAL;
    int countBL;
    int countCL;
    int countDL;
    int countEL;
    int countFL;
    int countGL;
    int countHL;
    int countIL;
    int countJL;
    int countKL;
    int countLL;
    int countML;
    int countNL;
    int countOL;
    int countPL;
    int countQL;
    int countRL;
    int countSL;
    int countTL;
    int countUL;
    int countVL;
    int countWL;
    int countXL;
    int countYL;

    int countAR;
    int countBR;
    int countCR;
    int countDR;
    int countER;
    int countFR;
    int countGR;
    int countHR;
    int countIR;
    int countJR;
    int countKR;
    int countLR;
    int countMR;
    int countNR;
    int countOR;
    int countPR;
    int countQR;
    int countRR;
    int countSR;
    int countTR;
    int countUR;
    int countVR;
    int countWR;
    int countXR;
    int countYR;

    int countZ;
    int countVLF;

    double prevMulchBL;
    double prevMulchBR;
    double prevMulchCL;
    double prevMulchCR;
    double prevMulchDL;
    double prevMulchDR;
    double prevMulchEL;
    double prevMulchER;

    double prevOutDL;
    double prevOutDR;
    double prevOutEL;
    double prevOutER;

    double prevInDL;
    double prevInDR;
    double prevInEL;
    double prevInER;

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
        bez_cycle,
        bez_total
    }; //the new undersampling. bez signifies the bezier curve reconstruction
    double bez[bez_total];
};
}
