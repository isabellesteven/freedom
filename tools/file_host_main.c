#include "runtime/host/file_io_host.h"

/*
 * Thin CLI wrapper around the offline file host.
 *
 * Argument parsing stays minimal here; the reusable host logic lives in the
 * runtime/host layer so tests can execute the same code path.
 */

#include <stdio.h>

int main(int argc, char **argv) {
  grph_file_host_options options;

  if (argc != 4) {
    fprintf(stderr, "usage: file_host <blob> <input.wav> <output.wav>\n");
    return 2;
  }

  options.blob_path = argv[1];
  options.input_wav_path = argv[2];
  options.output_wav_path = argv[3];

  return (int)grph_file_host_run(&options, stderr);
}
