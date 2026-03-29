/* Verifies lookup and duplicate detection for the central runtime module registry.
   It checks built-in hits/misses and rejects a deliberately duplicated test registry. */
#include "modules/gain/gain.h"
#include "runtime/engine/module_registry.h"

#include <stdint.h>
#include <stdio.h>

int main(void) {
  const ModuleRegistry *builtin;
  const AweModuleDescriptor *desc;
  static const AweModuleDescriptor *const dup_modules[] = {
      &g_gain_desc,
      &g_gain_desc,
  };
  static const ModuleRegistry dup_registry = {
      dup_modules,
      2u,
  };

  builtin = grph_builtin_module_registry();
  if (!builtin) {
    fprintf(stderr, "built-in registry unavailable\n");
    return 1;
  }
  if (!grph_module_registry_validate(builtin)) {
    fprintf(stderr, "built-in registry failed validation\n");
    return 1;
  }

  desc = grph_module_registry_find(builtin, GAIN_MODULE_ID);
  if (desc != &g_gain_desc) {
    fprintf(stderr, "failed to find built-in gain descriptor\n");
    return 1;
  }
  if (grph_module_registry_find(builtin, 0xFFFFFFFFu) != NULL) {
    fprintf(stderr, "unexpected registry hit for unknown module\n");
    return 1;
  }
  if (grph_module_registry_validate(&dup_registry)) {
    fprintf(stderr, "duplicate registry unexpectedly validated\n");
    return 1;
  }

  return 0;
}
