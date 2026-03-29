#!/usr/bin/env python3
"""End-to-end tests for the offline WAV file host."""

import argparse
import math
import struct
import subprocess
import sys
import wave
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "tools"))

from compiler import compile_dsl_to_blob  # type: ignore  # noqa: E402


def write_blob(path: Path, dsl: str) -> None:
    path.write_bytes(compile_dsl_to_blob(dsl))


def write_float_wav(path: Path, sample_rate_hz: int, channels: int, frames: list[tuple[float, ...]]) -> None:
    data = bytearray()
    for frame in frames:
        if len(frame) != channels:
            raise ValueError("frame/channel mismatch")
        for sample in frame:
            data += struct.pack("<f", sample)
    riff_size = 36 + len(data)
    header = bytearray()
    header += b"RIFF"
    header += struct.pack("<I", riff_size)
    header += b"WAVE"
    header += b"fmt "
    header += struct.pack("<IHHIIHH", 16, 3, channels, sample_rate_hz,
                          sample_rate_hz * channels * 4, channels * 4, 32)
    header += b"data"
    header += struct.pack("<I", len(data))
    path.write_bytes(bytes(header) + bytes(data))


def read_float_wav(path: Path) -> tuple[int, int, list[tuple[float, ...]], int]:
    blob = path.read_bytes()
    if len(blob) < 44 or blob[:4] != b"RIFF" or blob[8:12] != b"WAVE":
        raise SystemExit(f"bad wav output: {path}")
    offset = 12
    fmt_tag = None
    channels = None
    sample_rate_hz = None
    bits_per_sample = None
    data = None
    while offset + 8 <= len(blob):
        chunk_id = blob[offset:offset + 4]
        chunk_size = struct.unpack_from("<I", blob, offset + 4)[0]
        chunk_data = offset + 8
        chunk_end = chunk_data + chunk_size
        if chunk_end > len(blob):
            raise SystemExit(f"truncated wav output: {path}")
        if chunk_id == b"fmt ":
            fmt_tag, channels, sample_rate_hz = struct.unpack_from("<HHI", blob, chunk_data)
            bits_per_sample = struct.unpack_from("<H", blob, chunk_data + 14)[0]
        elif chunk_id == b"data":
            data = blob[chunk_data:chunk_end]
        offset = chunk_end + (chunk_size & 1)
    if fmt_tag != 3 or channels is None or sample_rate_hz is None or bits_per_sample != 32 or data is None:
        raise SystemExit(f"unexpected wav output format: {path}")
    frame_count = len(data) // (channels * 4)
    values = struct.unpack("<" + "f" * (frame_count * channels), data)
    frames = [tuple(values[i:i + channels]) for i in range(0, len(values), channels)]
    return sample_rate_hz, channels, frames, fmt_tag


def assert_close(actual: float, expected: float, label: str) -> None:
    if abs(actual - expected) > 1e-4:
        raise SystemExit(f"{label}: got {actual}, expected {expected}")


def run_host(host: Path, blob: Path, wav_in: Path, wav_out: Path, expect_ok: bool) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(
        [str(host), str(blob), str(wav_in), str(wav_out)],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
    )
    if expect_ok and completed.returncode != 0:
        raise SystemExit(
            f"host unexpectedly failed\nstdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    if (not expect_ok) and completed.returncode == 0:
        raise SystemExit("host unexpectedly succeeded")
    return completed


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    args = parser.parse_args()

    host = Path(args.host)
    if not host.exists():
        raise SystemExit(f"host not found: {host}")

    tmp = REPO_ROOT / "temp_validate" / "file_host"
    tmp.mkdir(parents=True, exist_ok=True)

    gain_dsl = """\
graph "file_gain"
  io input  mic : f32@48k block=48 ch=1
  io output spk : f32@48k block=48 ch=1

  node g1 : Gain(gain_db=-6.0)

  connect mic -> g1.in
  connect g1.out -> spk
end
"""

    blob_path = tmp / "gain.grph"
    input_path = tmp / "input.wav"
    output_path = tmp / "output.wav"
    write_blob(blob_path, gain_dsl)
    write_float_wav(
        input_path,
        48000,
        1,
        [(0.1 * float(i + 1),) for i in range(96)],
    )
    run_host(host, blob_path, input_path, output_path, expect_ok=True)
    sample_rate_hz, channels, frames, fmt_tag = read_float_wav(output_path)
    if sample_rate_hz != 48000 or channels != 1 or fmt_tag != 3 or len(frames) != 96:
        raise SystemExit("unexpected output wav shape for gain test")
    gain = math.pow(10.0, -6.0 / 20.0)
    for i, frame in enumerate(frames):
        assert_close(frame[0], 0.1 * float(i + 1) * gain, f"gain frame {i}")

    partial_input = tmp / "partial.wav"
    partial_output = tmp / "partial_out.wav"
    write_float_wav(
        partial_input,
        48000,
        1,
        [(0.01 * float(i + 1),) for i in range(70)],
    )
    run_host(host, blob_path, partial_input, partial_output, expect_ok=True)
    _, _, partial_frames, _ = read_float_wav(partial_output)
    if len(partial_frames) != 70:
        raise SystemExit("partial-block output length mismatch")
    for i, frame in enumerate(partial_frames):
        assert_close(frame[0], 0.01 * float(i + 1) * gain, f"partial frame {i}")

    bad_blob = tmp / "bad.grph"
    bad_blob.write_bytes(b"not a blob")
    bad_output = tmp / "bad_out.wav"
    completed = run_host(host, bad_blob, input_path, bad_output, expect_ok=False)
    if "blob" not in completed.stderr.lower():
        raise SystemExit("invalid blob diagnostic missing")

    unsupported_input = tmp / "unsupported.wav"
    with wave.open(str(unsupported_input), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(48000)
        wav.writeframes(struct.pack("<hhhh", 1000, -1000, 500, -500))
    completed = run_host(host, blob_path, unsupported_input, tmp / "unsupported_out.wav", expect_ok=False)
    if "unsupported wav format" not in completed.stderr.lower():
        raise SystemExit("unsupported wav diagnostic missing")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
