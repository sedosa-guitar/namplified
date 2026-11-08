#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NAMplifiedAmpSim NAMplifiedAmpSim;

NAMplifiedAmpSim* naminAmpSimCreate (int model);
void naminAmpSimDestroy (NAMplifiedAmpSim* amp);
void naminAmpSimSetModel (NAMplifiedAmpSim* amp, int model);
void naminAmpSimSetParameter (NAMplifiedAmpSim* amp, int parameter, float value);
void naminAmpSimProcess (NAMplifiedAmpSim* amp, int channel, float* samples, int numSamples, float sampleRate);

#ifdef __cplusplus
}
#endif
