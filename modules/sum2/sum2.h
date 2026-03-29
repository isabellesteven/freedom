#ifndef AWE_SUM2_MODULE_H
#define AWE_SUM2_MODULE_H

#include "module_abi.h"

extern const AweModuleDescriptor g_sum2_desc;

const AweModuleDescriptor*
awe_get_sum2_module_descriptor(
    uint32_t abi_major,
    uint32_t abi_minor);

#endif
