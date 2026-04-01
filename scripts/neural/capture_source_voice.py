import argparse
import pathlib
import subprocess
import sys

import numpy as np
import sounddevice as sd
import soundfile as sf


def trim_silence(x: np.ndarray, threshold: float, sr: int) -> np.ndarray:
    if x.size == 0:
        return x

    abs_x = np.abs(x)
    idx = np.where(abs_x > threshold)[0]
    if idx.size == 0:
        return x

    start = int(idx[0])
    end = int(idx[-1]) + 1

    pad = int(0.12 * sr)
    start = max(0, start - pad)
    end = min(len(x), end + pad)
    return x[start:end]


def normalize(x: np.ndarray, target_peak: float = 0.95) -> np.ndarray:
    peak = float(np.max(np.abs(x))) if x.size else 0.0
    if peak < 1e-7:
        return x
    return x * (target_peak / peak)


def list_input_devices() -> int:
    devices = sd.query_devices()
    print("Input devices:")
    for i in range(len(devices)):
        info = sd.query_devices(i)
        max_in = int(info.get("max_input_channels", 0))
        if max_in > 0:
            name = str(info.get("name", f"device_{i}"))
            sr = info.get("default_samplerate", "?")
            print(f"[{i}] {name} (in={max_in}, sr={sr})")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Capture source voice from microphone and build source_chunks.")
    parser.add_argument("--list-devices", action="store_true")
    parser.add_argument("--device", type=int, default=None, help="Input device index")
    parser.add_argument("--seconds", type=float, default=180.0, help="Recording duration in seconds")
    parser.add_argument("--sample-rate", type=int, default=48000)
    parser.add_argument("--output-wav", default="audio_samples/source-voice.wav")
    parser.add_argument("--chunks-dir", default="data/source_chunks")
    parser.add_argument("--silence-threshold", type=float, default=0.01)
    parser.add_argument("--prepare-threshold", type=float, default=0.015)
    parser.add_argument("--prepare-min-sec", type=float, default=2.0)
    args = parser.parse_args()

    if args.list_devices:
        return list_input_devices()

    out_wav = pathlib.Path(args.output_wav)
    out_wav.parent.mkdir(parents=True, exist_ok=True)

    frames = int(max(1.0, args.seconds) * args.sample_rate)
    print(f"Recording {args.seconds:.1f}s from device={args.device} at {args.sample_rate} Hz...")
    rec = sd.rec(frames, samplerate=args.sample_rate, channels=1, dtype="float32", device=args.device)
    sd.wait()

    x = rec.reshape(-1).astype(np.float32)
    x = trim_silence(x, args.silence_threshold, args.sample_rate)
    x = normalize(x, 0.95)

    sf.write(str(out_wav), x, args.sample_rate)
    print(f"Saved raw source wav: {out_wav}")

    prep_cmd = [
        sys.executable,
        "scripts/neural/prepare_dataset.py",
        "--input",
        str(out_wav),
        "--output-dir",
        args.chunks_dir,
        "--sample-rate",
        "24000",
        "--threshold",
        str(args.prepare_threshold),
        "--min-sec",
        str(args.prepare_min_sec),
    ]
    print("Preparing source chunks...")
    subprocess.run(prep_cmd, check=True)
    print(f"Source chunks ready in: {args.chunks_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
