import argparse
import pathlib
import random
import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F
from contextlib import nullcontext
import torchaudio


class TinyVC(nn.Module):
    def __init__(self, n_mels: int = 80):
        super().__init__()
        hidden = 192
        self.in_proj = nn.Linear(n_mels, hidden)
        self.tconv = nn.Sequential(
            nn.Conv1d(hidden, hidden, kernel_size=5, padding=2),
            nn.GELU(),
            nn.Conv1d(hidden, hidden, kernel_size=5, padding=2),
            nn.GELU(),
        )
        self.out_proj = nn.Linear(hidden, n_mels)

    def forward(self, mel: torch.Tensor) -> torch.Tensor:
        x = self.in_proj(mel)  # [B, T, H]
        xt = x.transpose(1, 2)  # [B, H, T]
        xt = xt + self.tconv(xt)
        y = self.out_proj(xt.transpose(1, 2))
        return y


def set_seed(seed: int):
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)
    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(seed)


def load_wavs(folder: pathlib.Path):
    files = sorted(folder.glob("*.wav"))
    wavs = []
    for p in files:
        x, sr = torchaudio.load(str(p))
        x = x.mean(dim=0, keepdim=True)
        wavs.append((x, sr, p.name))
    return wavs


def pseudo_features(x: torch.Tensor, n_mels: int = 80):
    # Match the C++ runtime featureization used in ONNXVoiceConverter.
    # x: [1, T] waveform in [-1, 1]
    x = torch.clamp(x, -1.0, 1.0)
    ax = torch.abs(x).unsqueeze(-1)          # [1, T, 1]
    sq = (x * x).unsqueeze(-1)               # [1, T, 1]
    bands = torch.linspace(0.0, 1.0, n_mels, device=x.device).view(1, 1, n_mels)
    return (1.0 - bands) * ax + bands * sq   # [1, T, M]


def random_crop_wav(x: torch.Tensor, frames: int):
    if x.shape[1] <= frames:
        pad = frames - x.shape[1]
        if pad > 0:
            x = F.pad(x, (0, pad))
        return x[:, :frames]
    s = random.randint(0, x.shape[1] - frames)
    return x[:, s : s + frames]


def split_train_val(items, val_ratio: float):
    if len(items) <= 1:
        return items, items

    n_val = max(1, int(len(items) * val_ratio))
    idx = list(range(len(items)))
    random.shuffle(idx)
    val_idx = set(idx[:n_val])

    train_items = [x for i, x in enumerate(items) if i not in val_idx]
    val_items = [x for i, x in enumerate(items) if i in val_idx]
    if not train_items:
        train_items = val_items
    return train_items, val_items


def sample_batch(src_wavs, tgt_wavs, frames: int, device: torch.device):
    sx, _, _ = random.choice(src_wavs)
    tx, _, _ = random.choice(tgt_wavs)

    sx = random_crop_wav(sx, frames).to(device)
    tx = random_crop_wav(tx, frames).to(device)

    sm = pseudo_features(sx)
    tm = pseudo_features(tx)
    return sm, tm


def compute_loss(model: nn.Module, sm: torch.Tensor, tm: torch.Tensor):
    pred = model(sm)
    # Proxy objectives: timbre distribution + temporal content consistency.
    loss_recon = F.l1_loss(pred, tm)
    loss_content = F.l1_loss(pred[:, 1:, :] - pred[:, :-1, :], sm[:, 1:, :] - sm[:, :-1, :])
    pred_mean = pred.mean(dim=1)
    tgt_mean = tm.mean(dim=1)
    pred_std = pred.std(dim=1)
    tgt_std = tm.std(dim=1)
    loss_stats = F.l1_loss(pred_mean, tgt_mean) + 0.5 * F.l1_loss(pred_std, tgt_std)
    loss = loss_recon + 0.25 * loss_content + 0.30 * loss_stats
    return loss


def save_checkpoint(path: pathlib.Path, model: nn.Module, opt: torch.optim.Optimizer, scaler, epoch: int, best_val: float):
    path.parent.mkdir(parents=True, exist_ok=True)
    torch.save(
        {
            "epoch": epoch,
            "model": model.state_dict(),
            "opt": opt.state_dict(),
            "scaler": scaler.state_dict() if scaler is not None else None,
            "best_val": best_val,
        },
        str(path),
    )


def load_checkpoint(path: pathlib.Path, model: nn.Module, opt: torch.optim.Optimizer, scaler):
    ckpt = torch.load(str(path), map_location="cpu")
    model.load_state_dict(ckpt["model"])
    opt.load_state_dict(ckpt["opt"])
    if scaler is not None and ckpt.get("scaler") is not None:
        scaler.load_state_dict(ckpt["scaler"])
    start_epoch = int(ckpt.get("epoch", 0)) + 1
    best_val = float(ckpt.get("best_val", 1e9))
    return start_epoch, best_val


def main() -> int:
    parser = argparse.ArgumentParser(description="Train a tiny neural voice style mapper and export ONNX.")
    parser.add_argument("--source-dir", required=True)
    parser.add_argument("--target-dir", required=True)
    parser.add_argument("--epochs", type=int, default=10)
    parser.add_argument("--steps-per-epoch", type=int, default=100)
    parser.add_argument("--frames", type=int, default=96)
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--val-ratio", type=float, default=0.1)
    parser.add_argument("--val-steps", type=int, default=32)
    parser.add_argument("--checkpoint-dir", default="models/checkpoints")
    parser.add_argument("--resume", default="", help="Path to checkpoint to resume")
    parser.add_argument("--save-every", type=int, default=1)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--fp16", action="store_true", help="Enable mixed precision on CUDA")
    parser.add_argument("--out-model", required=True, help="Path to ONNX model")
    args = parser.parse_args()

    set_seed(args.seed)

    src = load_wavs(pathlib.Path(args.source_dir))
    tgt = load_wavs(pathlib.Path(args.target_dir))
    if not src or not tgt:
        raise RuntimeError("Source and target datasets must contain .wav files")

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"Device: {device}")
    if device.type == "cuda":
        print(f"CUDA device: {torch.cuda.get_device_name(0)}")

    model = TinyVC().to(device)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    use_amp = bool(args.fp16 and device.type == "cuda")
    scaler = torch.cuda.amp.GradScaler(enabled=use_amp)

    src_train, src_val = split_train_val(src, args.val_ratio)
    tgt_train, tgt_val = split_train_val(tgt, args.val_ratio)

    ckpt_dir = pathlib.Path(args.checkpoint_dir)
    ckpt_last = ckpt_dir / "last.pt"
    ckpt_best = ckpt_dir / "best.pt"

    start_epoch = 1
    best_val = 1e9
    if args.resume:
        start_epoch, best_val = load_checkpoint(pathlib.Path(args.resume), model, opt, scaler)
        print(f"Resumed from {args.resume} at epoch {start_epoch}")

    autocast_ctx = torch.cuda.amp.autocast if use_amp else nullcontext

    for epoch in range(start_epoch, args.epochs + 1):
        model.train()
        losses = []
        for _ in range(args.steps_per_epoch):
            sm, tm = sample_batch(src_train, tgt_train, args.frames, device)
            opt.zero_grad()

            with autocast_ctx():
                loss = compute_loss(model, sm, tm)

            scaler.scale(loss).backward()
            scaler.step(opt)
            scaler.update()
            losses.append(float(loss.item()))

        model.eval()
        val_losses = []
        with torch.no_grad():
            for _ in range(args.val_steps):
                sm, tm = sample_batch(src_val, tgt_val, args.frames, device)
                with autocast_ctx():
                    l = compute_loss(model, sm, tm)
                val_losses.append(float(l.item()))

        train_loss = float(np.mean(losses))
        val_loss = float(np.mean(val_losses)) if val_losses else train_loss

        if epoch % max(1, args.save_every) == 0:
            save_checkpoint(ckpt_last, model, opt, scaler if use_amp else None, epoch, best_val)

        if val_loss < best_val:
            best_val = val_loss
            save_checkpoint(ckpt_best, model, opt, scaler if use_amp else None, epoch, best_val)

        print(
            f"Epoch {epoch}/{args.epochs} "
            f"- train_loss={train_loss:.5f} val_loss={val_loss:.5f} best_val={best_val:.5f}"
        )

    # Load best checkpoint before ONNX export for better runtime quality.
    if ckpt_best.exists():
        ck = torch.load(str(ckpt_best), map_location=device)
        model.load_state_dict(ck["model"])
        print(f"Loaded best checkpoint: {ckpt_best}")

    out_path = pathlib.Path(args.out_model)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    model.eval()
    dummy = torch.randn(1, args.frames, 80, device=device)
    torch.onnx.export(
        model,
        dummy,
        str(out_path),
        input_names=["mel_in"],
        output_names=["mel_out"],
        dynamic_axes={"mel_in": {1: "T"}, "mel_out": {1: "T"}},
        opset_version=17,
    )

    print(f"Last checkpoint: {ckpt_last}")
    print(f"Best checkpoint: {ckpt_best}")
    print(f"ONNX exported to: {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
