#ifndef AWE_MODULE_ABI_H
#define AWE_MODULE_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
   ABI VERSION
   ================================================================ */

#define AWE_ABI_MAJOR 1
#define AWE_ABI_MINOR 0


/* ================================================================
   STATUS CODES
   ================================================================ */

typedef enum
{
    AWE_OK        = 0,
    AWE_EINVAL    = -1,
    AWE_ENOTSUP   = -2,
    AWE_ESTATE    = -3,
    AWE_EINTERNAL = -4

} AweStatus;


/* ================================================================
   AUDIO FORMATS
   ================================================================ */

typedef enum
{
    AWE_FMT_INVALID = 0,
    AWE_FMT_F32     = 1,
    AWE_FMT_S16     = 2

} AweSampleFormat;


/* ================================================================
   PROCESS CONTEXT
   ================================================================ */

typedef struct AweProcessCtx
{
    uint32_t sample_rate_hz;

    /* graph block size = B × N */
    uint32_t block_frames;

    uint64_t block_index;

} AweProcessCtx;


/* ================================================================
   RUNTIME API
   ================================================================ */

typedef struct AweRuntimeApi
{
    uint32_t api_bytes;

    uint32_t abi_major;
    uint32_t abi_minor;

    /* RT-safe memory helpers */
    void* (*memcpy_fn)(void*, const void*, uint32_t);
    void* (*memset_fn)(void*, int, uint32_t);

    /* logging (NOT RT-safe) */
    void (*log_fn)(uint32_t level, const char* msg);

    /* optional allocation (not RT-safe) */
    void* (*alloc_fn)(uint32_t bytes, uint32_t align);
    void  (*free_fn)(void*);

} AweRuntimeApi;


/* ================================================================
   MODULE CAPABILITIES
   ================================================================ */

typedef enum
{
    AWE_CAP_NONE                = 0,

    /* out[0] may alias in[0] */
    AWE_CAP_INPLACE_IO0         = 1 << 0,

    /* out[0] may alias any input */
    AWE_CAP_OUT0_ALIASES_ANY_IN = 1 << 1,

    /* module requires fixed block size */
    AWE_CAP_FIXED_BLOCK_ONLY    = 1 << 2

} AweModuleCaps;


/* ================================================================
   MODULE VTABLE
   ================================================================ */

typedef struct AweModuleVTable
{
    AweStatus (*init)(
        void* state,
        const AweRuntimeApi* api,
        const void* init_blob,
        uint32_t init_bytes,
        const AweProcessCtx* ctx);

    AweStatus (*process)(
        void* state,
        const void* const* inputs,
        void* const* outputs,
        const AweProcessCtx* ctx);

    AweStatus (*set_param)(
        void* state,
        uint32_t param_id,
        const void* data,
        uint32_t size_bytes);

    AweStatus (*get_param)(
        void* state,
        uint32_t param_id,
        void* data,
        uint32_t* size_bytes);

    void (*deinit)(
        void* state);

} AweModuleVTable;


/* ================================================================
   MODULE DESCRIPTOR
   ================================================================ */

typedef struct AweModuleDescriptor
{
    uint32_t desc_bytes;

    uint32_t module_id;

    uint16_t ver_major;
    uint16_t ver_minor;

    uint16_t abi_major;
    uint16_t abi_minor;

    uint32_t caps;

    uint32_t state_bytes;
    uint32_t state_align;

    uint16_t n_in;
    uint16_t n_out;

    /* required if FIXED_BLOCK_ONLY */
    uint32_t fixed_block_frames;

    AweModuleVTable vtable;

} AweModuleDescriptor;


/* ================================================================
   MODULE DISCOVERY ENTRYPOINT
   ================================================================ */

const AweModuleDescriptor*
awe_get_module_descriptor(
    uint32_t abi_major,
    uint32_t abi_minor);


/* ================================================================
   REALTIME RULES (documentation only)

   process() MUST NOT:
      - allocate memory
      - lock mutexes
      - perform blocking I/O
      - call OS services

   process() MAY:
      - update state
      - run bounded loops
      - call memcpy/memset helpers

   ================================================================ */


#ifdef __cplusplus
}
#endif

#endif
