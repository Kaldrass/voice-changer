import argparse
import pathlib
import numpy as np
import soundfile as sf


def normalize(x: np.ndarray) -> np.ndarray:
    peak = np.max(np.abs(x))
    if peak < 1e-7:
        return x
    return x / peak * 0.95


def frame_rms(x: np.ndarray, hop: int, win: int) -> np.ndarray:
    if len(x) < win:
        return np.array([], dtype=np.float32)
    values = []
    for i in range(0, len(x) - win + 1, hop):
        f = x[i : i + win]
        values.append(np.sqrt(np.mean(f * f) + 1e-9))
    return np.asarray(values, dtype=np.float32)


def extract_voiced_segments(x: np.ndarray, sr: int, threshold: float, min_sec: float):
    hop = max(1, int(sr * 0.01))
    win = max(1, int(sr * 0.03))
    rms = frame_rms(x, hop, win)
    if rms.size == 0:
        return []

    voiced = rms > threshold
    segments = []
    start = None

    for i, v in enumerate(voiced):
        if v and start is None:
            start = i
        elif (not v) and start is not None:
            end = i
            s0 = start * hop
            s1 = min(len(x), end * hop + win)
            if (s1 - s0) / sr >= min_sec:
                segments.append((s0, s1))
            start = None

    if start is not None:
        s0 = start * hop
        s1 = len(x)
        if (s1 - s0) / sr >= min_sec:
            segments.append((s0, s1))

    return segments


def main() -> int:
    parser = argparse.ArgumentParser(description="Prepare voiced chunks for neural voice training.")
    parser.add_argument("--input", required=True, help="Input wav file")
    parser.add_argument("--output-dir", required=True, help="Directory for chunks")
    parser.add_argument("--sample-rate", type=int, default=24000, help="Target sample rate")
    parser.add_argument("--threshold", type=float, default=0.015, help="Energy threshold for voiced detection")
    parser.add_argument("--min-sec", type=float, default=2.0, help="Minimum segment duration")
    args = parser.parse_args()

    in_path = pathlib.Path(args.input)
    out_dir = pathlib.Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    x, sr = sf.read(str(in_path), always_2d=True)
    x = np.mean(x, axis=1).astype(np.float32)

    if sr != args.sample_rate:
        # Lightweight resample with linear interpolation.
        t_old = np.linspace(0.0, 1.0, num=len(x), endpoint=False)
        n_new = int(len(x) * args.sample_rate / sr)
        t_new = np.linspace(0.0, 1.0, num=n_new, endpoint=False)
        x = np.interp(t_new, t_old, x).astype(np.float32)
        sr = args.sample_rate

    x = normalize(x)
    segments = extract_voiced_segments(x, sr, args.threshold, args.min_sec)

    for i, (s0, s1) in enumerate(segments):
        seg = x[s0:s1]
        out_path = out_dir / f"chunk_{i:04d}.wav"
        sf.write(str(out_path), seg, sr)

    print(f"Input: {in_path}")
    print(f"Output chunks: {len(segments)} in {out_dir}")
    print("Next: prepare a source-speaker dataset with the same script, then train.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
