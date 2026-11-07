/* Narrow C bridge to 0xFX's MIT-licensed amplifier DSP. */
#include "AmpSimBridge.h"
#include "engine/internal/engine_internal.h"

struct NAMinatorAmpSim
{
    fx_amp_state_t channels[2];
    int model;
    float parameters[FX_AMP_PARAM_COUNT];
};

NAMinatorAmpSim* naminAmpSimCreate (int model)
{
    NAMinatorAmpSim* amp = (NAMinatorAmpSim*) calloc (1, sizeof (NAMinatorAmpSim));
    if (amp == NULL)
        return NULL;
    amp->model = model;
    for (int channel = 0; channel < 2; ++channel)
        fx_amp_init (&amp->channels[channel], (fx_amp_type_t) model);
    for (int parameter = 0; parameter < FX_AMP_PARAM_COUNT; ++parameter)
        amp->parameters[parameter] = amp->channels[0].params[parameter];
    return amp;
}

void naminAmpSimDestroy (NAMinatorAmpSim* amp)
{
    free (amp);
}

void naminAmpSimSetModel (NAMinatorAmpSim* amp, int model)
{
    if (amp == NULL || model < 0 || model >= FX_AMP_COUNT || amp->model == model)
        return;
    amp->model = model;
    for (int channel = 0; channel < 2; ++channel)
    {
        fx_amp_init (&amp->channels[channel], (fx_amp_type_t) model);
        for (int parameter = 0; parameter < FX_AMP_PARAM_COUNT; ++parameter)
            amp->channels[channel].params[parameter] = amp->parameters[parameter];
    }
}

void naminAmpSimSetParameter (NAMinatorAmpSim* amp, int parameter, float value)
{
    if (amp == NULL || parameter < 0 || parameter >= FX_AMP_PARAM_COUNT)
        return;
    amp->parameters[parameter] = value;
    for (int channel = 0; channel < 2; ++channel)
        amp->channels[channel].params[parameter] = value;
}

void naminAmpSimProcess (NAMinatorAmpSim* amp, int channel, float* samples, int numSamples, float sampleRate)
{
    if (amp == NULL || samples == NULL || channel < 0 || channel > 1 || numSamples <= 0)
        return;
    fx_amp_process (&amp->channels[channel], samples, numSamples, sampleRate);
}
