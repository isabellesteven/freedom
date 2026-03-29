#ifndef FREEDOM_RUNTIME_HOST_FILE_IO_HOST_H
#define FREEDOM_RUNTIME_HOST_FILE_IO_HOST_H

/*
 * Offline file I/O host for the current homogeneous runtime shape.
 *
 * This host exists as an integration harness around the runtime entry points:
 * load a blob, bind it, stream a WAV file through graph_process(), and write a
 * deterministic WAV result. The implementation intentionally stays narrow so it
 * can serve as the base for later offline control-plane experiments.
 */

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum grph_file_host_status {
  GRPH_FILE_HOST_OK = 0,
  GRPH_FILE_HOST_BAD_ARG = 1,
  GRPH_FILE_HOST_IO_ERROR = 2,
  GRPH_FILE_HOST_INVALID_BLOB = 3,
  GRPH_FILE_HOST_UNSUPPORTED_WAV = 4,
  GRPH_FILE_HOST_BIND_FAILED = 5,
  GRPH_FILE_HOST_PROCESS_FAILED = 6,
  GRPH_FILE_HOST_MEMORY_FAILED = 7
} grph_file_host_status;

typedef struct grph_file_host_options {
  const char *blob_path;
  const char *input_wav_path;
  const char *output_wav_path;
} grph_file_host_options;

/*
 * Run one offline file-processing pass.
 *
 * diag receives compact diagnostics and may be NULL. The host owns all memory
 * it allocates internally; callers only provide the file paths.
 */
grph_file_host_status grph_file_host_run(const grph_file_host_options *options,
                                         FILE *diag);

#ifdef __cplusplus
}
#endif

#endif
