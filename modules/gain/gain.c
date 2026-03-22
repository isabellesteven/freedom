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

static AweStatus read_gain_init_blob(const void* data, uint32_t size_bytes, float* out_value)
{
    const uint8_t* p = (const uint8_t*)data;

    if (data == NULL || out_value == NULL)
        return AWE_EINVAL;

    if (size_bytes == sizeof(float))
        return read_f32_param(data, size_bytes, out_value);

    /* Temporary compatibility path for compiler-emitted init blobs:
       [u32 param_id=1][f32 gain_db]. */
    if (size_bytes == 8u && p[0] == (uint8_t)GAIN_PARAM_GAIN_DB && p[1] == 0u &&
        p[2] == 0u && p[3] == 0u)
    {
        return read_f32_param(p + 4, sizeof(float), out_value);
    }

    return AWE_EINVAL;
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
        AweStatus st = read_gain_init_blob(init_blob, init_bytes, &gain_db);
        if (st != AWE_OK)
            return st;

        s->gain_db = gain_db;
        s->gain_lin = gain_db_to_lin(gain_db);
    }

    return AWE_OK;
}

static AweStatus gain_process(
    void* state,
    const void* const* inputs,
    void* const* outputs,
    const AweProcessCtx* ctx)
{
    if (state == NULL || inputs == NULL || outputs == NULL || ctx == NULL)
        return AWE_EINVAL;
    const GainState* s = (const GainState*)state;
    const float* in;
    float* out;
    const uint32_t frames = ctx->block_frames;
    const float gain = s->gain_lin;

    if (inputs[0] == NULL || outputs[0] == NULL)
        return AWE_EINVAL;

    in = (const float*)inputs[0];
    out = (float*)outputs[0];

    for (uint32_t i = 0; i < frames; ++i)
        out[i] = in[i] * gain;

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
