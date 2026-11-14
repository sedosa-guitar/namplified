/* ========================================
 *  Galactic2 - Galactic2.h
 *  Created 8/12/11 by SPIAdmin 
 *  Copyright (c) Airwindows, Airwindows uses the MIT license
 * ======================================== */

#pragma once

namespace airwindows
{
class Galactic2
{
public:
    void prepare (double sampleRate);
    void process (float* left, float* right, int sampleFrames, float size, float damping);

private:
    double sampleRate = 44100.0;
    static constexpr const int shortA = 683;
    static constexpr int shortB = 2339;
    static constexpr int shortC = 2381;
    static constexpr int shortD = 887;
    static constexpr int shortE = 743;
    static constexpr int shortF = 1823;
    static constexpr int shortG = 1151;
    static constexpr int shortH = 2833;
    static constexpr int shortI = 521;
    static constexpr int shortJ = 3331;
    static constexpr int shortK = 2851;
    static constexpr int shortL = 1747;
    static constexpr int shortM = 3389;
    static constexpr int shortN = 83;
    static constexpr int shortO = 443;
    static constexpr int shortP = 3221; //290 ms, 10004 seat stadium. Scarcity, 1 in 55796
    double feedbackA;
    double feedbackB;
    double feedbackC;
    double feedbackD;

    double iirA;
    double iirB;
    double iirC;
    double iirD;
    double iirE;
    double iirF;
    double iirG;
    double iirH;

    double aAL[shortA+5];
    double aBL[shortB+5];
    double aCL[shortC+5];
    double aDL[shortD+5];
    double aEL[shortE+5];
    double aFL[shortF+5];
    double aGL[shortG+5];
    double aHL[shortH+5];
    double aIL[shortI+5];
    double aJL[shortJ+5];
    double aKL[shortK+5];
    double aLL[shortL+5];
    double aML[shortM+5];
    double aNL[shortN+5];
    double aOL[shortO+5];
    double aPL[shortP+5];

    double aAR[shortA+5];
    double aBR[shortB+5];
    double aCR[shortC+5];
    double aDR[shortD+5];
    double aER[shortE+5];
    double aFR[shortF+5];
    double aGR[shortG+5];
    double aHR[shortH+5];
    double aIR[shortI+5];
    double aJR[shortJ+5];
    double aKR[shortK+5];
    double aLR[shortL+5];
    double aMR[shortM+5];
    double aNR[shortN+5];
    double aOR[shortO+5];
    double aPR[shortP+5];

    double feedbackAL;
    double feedbackBL;
    double feedbackCL;
    double feedbackDL;

    double feedbackDR;
    double feedbackHR;
    double feedbackLR;
    double feedbackPR;

    double previousAL;
    double previousBL;
    double previousCL;
    double previousDL;
    double previousEL;

    double lastRefL[7];

    double previousAR;
    double previousBR;
    double previousCR;
    double previousDR;
    double previousER;

    double lastRefR[7];

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

    int countZ;

    int cycle;

    double prevMulchAL;
    double prevMulchAR;
    double prevMulchBL;
    double prevMulchBR;
    double prevMulchCL;
    double prevMulchCR;
    double prevMulchDL;
    double prevMulchDR;
    double prevMulchEL;
    double prevMulchER;

    double prevOutAL;
    double prevOutAR;
    double prevOutBL;
    double prevOutBR;
    double prevOutCL;
    double prevOutCR;
    double prevOutDL;
    double prevOutDR;
    double prevOutEL;
    double prevOutER;

    double finalOutAL;
    double finalOutAR;
    double finalOutBL;
    double finalOutBR;
    double finalOutCL;
    double finalOutCR;
    double finalOutDL;
    double finalOutDR;
    double finalOutEL;
    double finalOutER;

    double prevInAL;
    double prevInAR;
    double prevInBL;
    double prevInBR;
    double prevInCL;
    double prevInCR;
    double prevInDL;
    double prevInDR;
    double prevInEL;
    double prevInER;

};
}
