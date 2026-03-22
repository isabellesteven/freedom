#include "sum2.h"

typedef struct Sum2State
{
    uint32_t reserved;
} Sum2State;

static AweStatus sum2_init(
    void* state,
    const AweRuntimeApi* api,
    const void* init_blob,
    uint32_t init_bytes,
    const AweProcessCtx* ctx)
{
    (void)api;
    (void)init_blob;
    (void)init_bytes;
    (void)ctx;

    if (state == NULL)
        return AWE_EINVAL;

    ((Sum2State*)state)->reserved = 0u;
    return AWE_OK;
}

static AweStatus sum2_process(
    void* state,
    const void* const* inputs,
    void* const* outputs,
    const AweProcessCtx* ctx)
{
    const float* in_a;
    const float* in_b;
    float* out;

    (void)state;

    if (inputs == NULL || outputs == NULL || ctx == NULL)
        return AWE_EINVAL;
    if (inputs[0] == NULL || inputs[1] == NULL || outputs[0] == NULL)
        return AWE_EINVAL;

    in_a = (const float*)inputs[0];
    in_b = (const float*)inputs[1];
    out = (float*)outputs[0];

    for (uint32_t i = 0; i < ctx->block_frames; ++i)
        out[i] = in_a[i] + in_b[i];

    return AWE_OK;
}

static AweStatus sum2_set_param(
    void* state,
    uint32_t param_id,
    const void* data,
    uint32_t size_bytes)
{
    (void)state;
    (void)param_id;
    (void)data;
    (void)size_bytes;
    return AWE_EINVAL;
}

static AweStatus sum2_get_param(
    void* state,
    uint32_t param_id,
    void* data,
    uint32_t* size_bytes)
{
    (void)state;
    (void)param_id;
    (void)data;
    (void)size_bytes;
    return AWE_EINVAL;
}

static void sum2_deinit(void* state)
{
    (void)state;
}

static const AweModuleDescriptor g_sum2_desc = {
    .desc_bytes = sizeof(AweModuleDescriptor),
    .module_id = 0x00001002u,
    .ver_major = 1u,
    .ver_minor = 0u,
    .abi_major = AWE_ABI_MAJOR,
    .abi_minor = AWE_ABI_MINOR,
    .caps = 0u,
    .state_bytes = sizeof(Sum2State),
    .state_align = 4u,
    .n_in = 2u,
    .n_out = 1u,
    .fixed_block_frames = 0u,
    .vtable = {
        .init = sum2_init,
        .process = sum2_process,
        .set_param = sum2_set_param,
        .get_param = sum2_get_param,
        .deinit = sum2_deinit,
    }
};

const AweModuleDescriptor*
awe_get_sum2_module_descriptor(uint32_t abi_major, uint32_t abi_minor)
{
    (void)abi_minor;

    if (abi_major != AWE_ABI_MAJOR)
        return NULL;

    return &g_sum2_desc;
}
