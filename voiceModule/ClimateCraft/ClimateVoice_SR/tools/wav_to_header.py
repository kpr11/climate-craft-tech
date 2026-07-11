#!/usr/bin/env python3
"""
Convert a WAV file into a C header with a raw int16 PCM array,
resampled/reformatted to match this project's audio playback format
(16kHz, stereo, 16-bit) as configured by esp_board_init(16000, 2, 16)
in main/main.c.

Usage:
    python3 wav_to_header.py input.wav output_name [output.h]

Produces a header defining:
    const unsigned int <output_name>_pcm_len = ...;
    const int16_t <output_name>_pcm[] = { ... };

matching the structure of main/welcome.h.
"""
import audioop
import sys
import wave

TARGET_RATE = 16000
TARGET_CHANNELS = 2
TARGET_SAMPWIDTH = 2  # 16-bit


def convert(input_path):
    with wave.open(input_path, "rb") as wf:
        nchannels = wf.getnchannels()
        sampwidth = wf.getsampwidth()
        framerate = wf.getframerate()
        frames = wf.readframes(wf.getnframes())

    if sampwidth != TARGET_SAMPWIDTH:
        frames = audioop.lin2lin(frames, sampwidth, TARGET_SAMPWIDTH)
        sampwidth = TARGET_SAMPWIDTH

    if framerate != TARGET_RATE:
        frames, _ = audioop.ratecv(
            frames, sampwidth, nchannels, framerate, TARGET_RATE, None
        )
        framerate = TARGET_RATE

    if nchannels == 1 and TARGET_CHANNELS == 2:
        frames = audioop.tostereo(frames, sampwidth, 1, 1)
    elif nchannels == 2 and TARGET_CHANNELS == 2:
        pass
    else:
        raise ValueError(f"Unsupported channel count: {nchannels}")

    return frames


def write_header(frames, name, out_path):
    # _pcm_len is a BYTE length (matches main/welcome.h's convention, which
    # esp_audio_play() expects), not a sample/element count.
    byte_len = len(frames)
    values = [
        str(int.from_bytes(frames[i : i + 2], "little", signed=True))
        for i in range(0, len(frames), 2)
    ]

    lines = []
    lines.append("#pragma once")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append(f"const unsigned int {name}_pcm_len = {byte_len};")
    lines.append(f"const int16_t {name}_pcm[] = {{")

    for i in range(0, len(values), 12):
        lines.append("    " + ", ".join(values[i : i + 12]) + ",")

    lines.append("};")
    lines.append("")

    with open(out_path, "w") as f:
        f.write("\n".join(lines))


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    input_path = sys.argv[1]
    name = sys.argv[2]
    out_path = sys.argv[3] if len(sys.argv) > 3 else f"{name}.h"

    frames = convert(input_path)
    write_header(frames, name, out_path)

    duration_s = (len(frames) // 2 // TARGET_CHANNELS) / TARGET_RATE
    print(f"Wrote {out_path}: {len(frames)//2} samples, {duration_s:.2f}s at 16kHz stereo")


if __name__ == "__main__":
    main()
