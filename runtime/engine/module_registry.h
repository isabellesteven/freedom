#ifndef FREEDOM_RUNTIME_ENGINE_MODULE_REGISTRY_H
#define FREEDOM_RUNTIME_ENGINE_MODULE_REGISTRY_H

/*
 * Built-in runtime module registry.
 *
 * The current implementation is limited to statically known modules compiled
 * into the binary. Lookup is kept behind this small API so a future dynamic
 * overlay can be layered on top without changing binder call sites.
 */

#include <stdint.h>

#include "modules/module_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct grph_module_registry {
  const AweModuleDescriptor *const *modules;
  uint32_t module_count;
} grph_module_registry;

/* Legacy name retained to keep current runtime/test call sites stable. */
typedef grph_module_registry ModuleRegistry;

/*
 * Return the process-wide built-in registry.
 *
 * The returned pointer refers to static read-only storage owned by the runtime.
 */
const grph_module_registry *grph_builtin_module_registry(void);

/*
 * Look up a module descriptor by module_id.
 *
 * The returned descriptor pointer is owned by the registry and must be treated
 * as immutable by callers.
 */
const AweModuleDescriptor *grph_module_registry_find(
    const grph_module_registry *registry, uint32_t module_id);

/*
 * Validate registry structure and reject duplicate module_id entries.
 *
 * This is primarily intended for debug/test use; O(N^2) checking is acceptable
 * because the built-in table is expected to remain small.
 */
int grph_module_registry_validate(const grph_module_registry *registry);

#ifdef __cplusplus
}
#endif

#endif
