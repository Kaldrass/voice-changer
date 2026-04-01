param(
    [int]$Device = -1,
    [double]$Seconds = 180,
    [int]$SampleRate = 48000,
    [string]$OutputWav = ".\audio_samples\source-voice.wav",
    [string]$ChunksDir = ".\data\source_chunks",
    [switch]$ListDevices
)

$ErrorActionPreference = "Stop"
$python = Join-Path (Get-Location).Path ".venv\Scripts\python.exe"
if (-not (Test-Path $python)) {
    throw "Python venv introuvable: $python"
}

$argsList = @(".\scripts\neural\capture_source_voice.py")
if ($ListDevices) {
    $argsList += "--list-devices"
} else {
    if ($Device -ge 0) {
        $argsList += @("--device", "$Device")
    }
    $argsList += @(
        "--seconds", "$Seconds",
        "--sample-rate", "$SampleRate",
        "--output-wav", $OutputWav,
        "--chunks-dir", $ChunksDir
    )
}

Write-Host "Running with venv Python: $python"
Write-Host (("Args: " + ($argsList -join " ")))
& $python @argsList
if ($LASTEXITCODE -ne 0) {
    throw "Capture script failed with exit code $LASTEXITCODE"
}
