import argparse
import pathlib
import numpy as np
import soundfile as sf
import onnxruntime as ort
import torch
import torchaudio


def pseudo_features(x: torch.Tensor, n_mels: int = 80):
    x = torch.clamp(x, -1.0, 1.0)
    ax = torch.abs(x).unsqueeze(-1)
    sq = (x * x).unsqueeze(-1)
    bands = torch.linspace(0.0, 1.0, n_mels, device=x.device).view(1, 1, n_mels)
    return (1.0 - bands) * ax + bands * sq


def main() -> int:
    parser = argparse.ArgumentParser(description="Run offline tiny VC inference on a wav file.")
    parser.add_argument("--model", required=True)
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    sess = ort.InferenceSession(args.model, providers=["CPUExecutionProvider"])

    wav, sr = torchaudio.load(args.input)
    wav = wav.mean(dim=0, keepdim=True)
    mel = pseudo_features(wav).numpy().astype(np.float32)

    out = sess.run(["mel_out"], {"mel_in": mel})[0]

    # Placeholder vocoder-less rendering: reuse original waveform but scale by predicted mel energy envelope.
    env = np.exp(out).mean(axis=2).reshape(-1)
    env = env / (np.max(env) + 1e-9)
    env = np.repeat(env, 256)[: wav.shape[1]]

    y = wav.numpy().reshape(-1)
    y = y * (0.4 + 0.9 * env)
    y = np.clip(y, -1.0, 1.0)

    out_path = pathlib.Path(args.output)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    sf.write(str(out_path), y, sr)
    print(f"Wrote: {out_path}")
    print("Note: this is an offline neural starter pipeline, not final real-time VC quality.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
