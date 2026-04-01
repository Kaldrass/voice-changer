param(
    [string]$SourceDir = ".\data\source_chunks",
    [string]$TargetDir = ".\data\target_chunks",
    [string]$OutModel = ".\models\tiny_vc.onnx",
    [string]$CheckpointDir = ".\models\checkpoints",
    [int]$Epochs = 40,
    [int]$StepsPerEpoch = 300,
    [switch]$ResumeLast
)

$ErrorActionPreference = "Stop"

$repoRoot = (Get-Location).Path
$venvPython = Join-Path $repoRoot ".venv\Scripts\python.exe"

if (-not (Test-Path $venvPython)) {
    throw "Python venv introuvable: $venvPython`nCree le venv puis installe les dependances: .venv\\Scripts\\python.exe -m pip install -r .\\scripts\\neural\\requirements.txt"
}

if (-not (Test-Path $SourceDir)) {
    throw "SourceDir introuvable: $SourceDir"
}

if (-not (Test-Path $TargetDir)) {
    throw "TargetDir introuvable: $TargetDir"
}

$outDir = Split-Path -Parent $OutModel
if ($outDir -and -not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
}

$ckptDirFull = Resolve-Path -Path (Join-Path $repoRoot $CheckpointDir) -ErrorAction SilentlyContinue
if (-not $ckptDirFull) {
    New-Item -ItemType Directory -Path $CheckpointDir -Force | Out-Null
}

$resumeArgs = @()
if ($ResumeLast) {
    $last = Join-Path $CheckpointDir "last.pt"
    if (Test-Path $last) {
        $resumeArgs = @("--resume", $last)
        Write-Host "Resuming from checkpoint: $last"
    } else {
        Write-Host "No last checkpoint found at $last, starting fresh."
    }
}

$argsList = @(
    ".\scripts\neural\train_tiny_vc.py",
    "--source-dir", $SourceDir,
    "--target-dir", $TargetDir,
    "--epochs", "$Epochs",
    "--steps-per-epoch", "$StepsPerEpoch",
    "--val-ratio", "0.1",
    "--val-steps", "64",
    "--frames", "96",
    "--lr", "1e-3",
    "--fp16",
    "--save-every", "1",
    "--checkpoint-dir", $CheckpointDir,
    "--out-model", $OutModel
)

if ($resumeArgs.Count -gt 0) {
    $argsList += $resumeArgs
}

Write-Host "Running with venv Python: $venvPython"
Write-Host (("Args: " + ($argsList -join " ")))
& $venvPython @argsList
if ($LASTEXITCODE -ne 0) {
    throw "Training process failed with exit code $LASTEXITCODE"
}

if (-not (Test-Path $OutModel)) {
    throw "Entrainement termine mais modele ONNX absent: $OutModel"
}

Write-Host "Model generated: $OutModel"
