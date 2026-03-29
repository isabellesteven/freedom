#ifndef GAIN_H
#define GAIN_H

#include "module_abi.h"

#define GAIN_MODULE_ID       0x00001001u
#define GAIN_PARAM_GAIN_DB   1u

extern const AweModuleDescriptor g_gain_desc;

const AweModuleDescriptor*
awe_get_module_descriptor(uint32_t abi_major, uint32_t abi_minor);

#endif
