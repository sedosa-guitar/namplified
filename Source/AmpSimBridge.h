#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NAMinatorAmpSim NAMinatorAmpSim;

NAMinatorAmpSim* naminAmpSimCreate (int model);
void naminAmpSimDestroy (NAMinatorAmpSim* amp);
void naminAmpSimSetModel (NAMinatorAmpSim* amp, int model);
void naminAmpSimSetParameter (NAMinatorAmpSim* amp, int parameter, float value);
void naminAmpSimProcess (NAMinatorAmpSim* amp, int channel, float* samples, int numSamples, float sampleRate);

#ifdef __cplusplus
}
#endif
