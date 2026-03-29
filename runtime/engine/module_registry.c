#include "runtime/engine/module_registry.h"

#include "modules/gain/gain.h"
#include "modules/sum2/sum2.h"

static const AweModuleDescriptor *const g_builtin_modules[] = {
    &g_gain_desc,
    &g_sum2_desc,
};

static const grph_module_registry g_builtin_registry = {
    g_builtin_modules,
    (uint32_t)(sizeof(g_builtin_modules) / sizeof(g_builtin_modules[0])),
};

const grph_module_registry *grph_builtin_module_registry(void) {
  return &g_builtin_registry;
}

const AweModuleDescriptor *grph_module_registry_find(
    const grph_module_registry *registry, uint32_t module_id) {
  uint32_t i;
  if (!registry || !registry->modules) {
    return NULL;
  }
  for (i = 0; i < registry->module_count; ++i) {
    const AweModuleDescriptor *desc = registry->modules[i];
    if (desc && desc->module_id == module_id) {
      return desc;
    }
  }
  return NULL;
}

int grph_module_registry_validate(const grph_module_registry *registry) {
  uint32_t i;
  uint32_t j;
  if (!registry || (registry->module_count != 0u && !registry->modules)) {
    return 0;
  }
  for (i = 0; i < registry->module_count; ++i) {
    const AweModuleDescriptor *lhs = registry->modules[i];
    if (!lhs) {
      return 0;
    }
    for (j = i + 1u; j < registry->module_count; ++j) {
      const AweModuleDescriptor *rhs = registry->modules[j];
      if (!rhs || lhs->module_id == rhs->module_id) {
        return 0;
      }
    }
  }
  return 1;
}
