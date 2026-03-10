#include "module_abi.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------
   Module identity
   ------------------------------------------------------------------ */

#define GAIN_MODULE_ID  0x00001001u
#define GAIN_VER_MAJOR  1u
#define GAIN_VER_MINOR  0u

#define GAIN_PARAM_GAIN_DB  1u

/* ------------------------------------------------------------------
   Init blob / param encoding
   v1: both init blob and set_param use a little-endian float32 gain_db
   ------------------------------------------------------------------ */

typedef struct GainState
{
    float gain_db;
    float gain_lin;
} GainState;

/* ------------------------------------------------------------------
   Helpers
   ------------------------------------------------------------------ */

static float gain_db_to_lin(float gain_db)
{
    return powf(10.0f, gain_db / 20.0f);
}

static AweStatus read_f32_param(const void* data, uint32_t size_bytes, float* out_value)
{
    if (data == NULL || out_value == NULL || size_bytes != sizeof(float))
        return AWE_EINVAL;

    /* ABI assumes native float is IEEE-754 single precision.
       For same-endian targets this memcpy is sufficient. */
    memcpy(out_value, data, sizeof(float));
    return AWE_OK;
}

/* ------------------------------------------------------------------
   ABI functions
   ------------------------------------------------------------------ */

static AweStatus gain_init(
    void* state,
    const AweRuntimeApi* api,
    const void* init_blob,
    uint32_t init_bytes,
    const AweProcessCtx* ctx)
{
    (void)api;
    (void)ctx;

    if (state == NULL)
        return AWE_EINVAL;

    GainState* s = (GainState*)state;

    /* Default gain = 0 dB */
    s->gain_db = 0.0f;
    s->gain_lin = 1.0f;

    if (init_blob != NULL && init_bytes != 0)
    {
        float gain_db = 0.0f;
        AweStatus st = read_f32_param(init_blob, init_bytes, &gain_db);
        if (st != AWE_OK)
            return st;

        s->gain_db = gain_db;
        s->gain_lin = gain_db_to_lin(gain_db);
    }

    return AWE_OK;
}

static AweStatus gain_process(
    void* state,
    const AweBufView* inputs,
    uint32_t n_in,
    AweBufView* outputs,
    uint32_t n_out,
    const AweProcessCtx* ctx)
{
    (void)ctx;

    if (state == NULL || inputs == NULL || outputs == NULL)
        return AWE_EINVAL;

    if (n_in != 1 || n_out != 1)
        return AWE_EINVAL;

    const AweBufView* in = &inputs[0];
    AweBufView* out = &outputs[0];
    const GainState* s = (const GainState*)state;

    if (in->data == NULL || out->data == NULL)
        return AWE_EINVAL;

    if (in->format != AWE_FMT_F32 || out->format != AWE_FMT_F32)
        return AWE_ENOTSUP;

    if (in->channels != out->channels || in->frames != out->frames)
        return AWE_EINVAL;

    if ((in->stride_bytes != 0 && in->stride_bytes != sizeof(float)) ||
        (out->stride_bytes != 0 && out->stride_bytes != sizeof(float)))
    {
        return AWE_ENOTSUP;
    }

    const uint32_t frames = in->frames;
    const uint32_t channels = in->channels;
    const float gain = s->gain_lin;

    /* v1 planar, tightly packed by channel:
       [ch0 frames][ch1 frames]... */
    const float* in_base = (const float*)in->data;
    float* out_base = (float*)out->data;

    for (uint32_t ch = 0; ch < channels; ++ch)
    {
        const float* in_ch = in_base + (size_t)ch * frames;
        float* out_ch = out_base + (size_t)ch * frames;

        for (uint32_t i = 0; i < frames; ++i)
            out_ch[i] = in_ch[i] * gain;
    }

    return AWE_OK;
}

static AweStatus gain_set_param(
    void* state,
    uint32_t param_id,
    const void* data,
    uint32_t size_bytes)
{
    if (state == NULL)
        return AWE_EINVAL;

    GainState* s = (GainState*)state;

    switch (param_id)
    {
        case GAIN_PARAM_GAIN_DB:
        {
            float gain_db = 0.0f;
            AweStatus st = read_f32_param(data, size_bytes, &gain_db);
            if (st != AWE_OK)
                return st;

            s->gain_db = gain_db;
            s->gain_lin = gain_db_to_lin(gain_db);
            return AWE_OK;
        }

        default:
            return AWE_EINVAL;
    }
}

static AweStatus gain_get_param(
    void* state,
    uint32_t param_id,
    void* data,
    uint32_t* size_bytes)
{
    if (state == NULL || size_bytes == NULL)
        return AWE_EINVAL;

    GainState* s = (GainState*)state;

    switch (param_id)
    {
        case GAIN_PARAM_GAIN_DB:
        {
            if (data == NULL || *size_bytes < sizeof(float))
            {
                *size_bytes = sizeof(float);
                return AWE_EINVAL;
            }

            memcpy(data, &s->gain_db, sizeof(float));
            *size_bytes = sizeof(float);
            return AWE_OK;
        }

        default:
            return AWE_EINVAL;
    }
}

static void gain_deinit(void* state)
{
    (void)state;
}

/* ------------------------------------------------------------------
   Descriptor
   ------------------------------------------------------------------ */

static const AweModuleDescriptor g_gain_desc = {
    .desc_bytes = sizeof(AweModuleDescriptor),

    .module_id = GAIN_MODULE_ID,

    .ver_major = GAIN_VER_MAJOR,
    .ver_minor = GAIN_VER_MINOR,

    .abi_major = AWE_ABI_MAJOR,
    .abi_minor = AWE_ABI_MINOR,

    .caps = AWE_CAP_INPLACE_IO0,

    .state_bytes = sizeof(GainState),
    .state_align = (uint32_t)_Alignof(GainState),

    .n_in = 1,
    .n_out = 1,

    .fixed_block_frames = 0,

    .vtable = {
        .init = gain_init,
        .process = gain_process,
        .set_param = gain_set_param,
        .get_param = gain_get_param,
        .deinit = gain_deinit,
    }
};

const AweModuleDescriptor*
awe_get_module_descriptor(uint32_t abi_major, uint32_t abi_minor)
{
    (void)abi_minor;

    if (abi_major != AWE_ABI_MAJOR)
        return NULL;

    return &g_gain_desc;
}