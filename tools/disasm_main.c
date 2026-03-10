#include "runtime/loader/blob.h"

#include <stdio.h>
#include <stdlib.h>

static int load_file(const char *path, unsigned char **out, size_t *out_bytes) {
  FILE *f;
  long n;
  size_t rn;
  unsigned char *buf;

  if (!path || !out || !out_bytes) {
    return -1;
  }

  f = fopen(path, "rb");
  if (!f) {
    return -1;
  }

  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return -1;
  }
  n = ftell(f);
  if (n < 0) {
    fclose(f);
    return -1;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return -1;
  }

  buf = (unsigned char *)malloc((size_t)n);
  if (!buf) {
    fclose(f);
    return -1;
  }

  rn = fread(buf, 1u, (size_t)n, f);
  fclose(f);
  if (rn != (size_t)n) {
    free(buf);
    return -1;
  }

  *out = buf;
  *out_bytes = (size_t)n;
  return 0;
}

int main(int argc, char **argv) {
  unsigned char *blob = NULL;
  size_t blob_bytes = 0;
  char err[256];
  int rc;

  if (argc != 2) {
    fprintf(stderr, "usage: disasm <blob.grph>\n");
    return 2;
  }

  if (load_file(argv[1], &blob, &blob_bytes) != 0) {
    fprintf(stderr, "error: failed to read '%s'\n", argv[1]);
    return 1;
  }

  rc = grph_blob_disassemble(stdout, blob, blob_bytes, err, sizeof(err));
  free(blob);

  if (rc != GRPH_BLOB_OK) {
    fprintf(stderr, "error: %s\n", err[0] ? err : "disassembly failed");
    return 1;
  }

  return 0;
}