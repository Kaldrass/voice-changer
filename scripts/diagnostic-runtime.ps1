param(
    [int]$DurationSec = 12,
    [string]$Preset = "girl",
    [int]$InIndex = 0,
    [int]$OutIndex = 0,
    [string]$BuildConfig = "Debug",
    [ValidateSet("dsp", "ai")]
    [string]$Mode = "dsp",
    [string]$AiProfile = "neutral",
    [double]$AiBlend = 0.65,
    [double]$AiIntensity = 0.4
)

$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repo ("build/{0}/voice_changer.exe" -f $BuildConfig)
if (-not (Test-Path $exe))
{
    $exe = Join-Path $repo "build/voice_changer.exe"
}

if (-not (Test-Path $exe))
{
    throw "Executable introuvable. Construis d'abord le projet (build/$BuildConfig/voice_changer.exe)."
}

$logsDir = Join-Path $repo "logs"
New-Item -ItemType Directory -Force -Path $logsDir | Out-Null

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$stdoutLog = Join-Path $logsDir ("diag-{0}.stdout.log" -f $stamp)
$stderrLog = Join-Path $logsDir ("diag-{0}.stderr.log" -f $stamp)

Write-Host "[diag] executable: $exe"
Write-Host "[diag] listing devices..."
& $exe --list-devices | Tee-Object -FilePath (Join-Path $logsDir ("diag-{0}.devices.log" -f $stamp))

$args = @(
    "--in-index", $InIndex,
    "--out-index", $OutIndex,
    "--preset", $Preset,
    "--mode", $Mode
)

if ($Mode -eq "ai")
{
    $args += @(
        "--ai-profile", $AiProfile,
        "--ai-blend", $AiBlend,
        "--ai-intensity", $AiIntensity
    )
}

Write-Host "[diag] start runtime for $DurationSec sec..."
$proc = Start-Process -FilePath $exe -ArgumentList $args -PassThru -NoNewWindow -RedirectStandardOutput $stdoutLog -RedirectStandardError $stderrLog

Start-Sleep -Seconds $DurationSec
if (-not $proc.HasExited)
{
    Stop-Process -Id $proc.Id -Force
    Start-Sleep -Milliseconds 200
}

$lines = @()
if (Test-Path $stdoutLog)
{
    $lines = Get-Content -Path $stdoutLog
}

$xruns = 0
$latencyMax = 0.0
foreach ($line in $lines)
{
    if ($line -match "xruns=(\d+)")
    {
        $x = [int]$Matches[1]
        if ($x -gt $xruns) { $xruns = $x }
    }
    if ($line -match "approxTotalLatencyMs=([0-9]+\.?[0-9]*)")
    {
        $l = [double]$Matches[1]
        if ($l -gt $latencyMax) { $latencyMax = $l }
    }
}

Write-Host "[diag] summary"
Write-Host ("  xruns_max={0}" -f $xruns)
Write-Host ("  latency_max_ms={0}" -f $latencyMax)
Write-Host ("  stdout_log={0}" -f $stdoutLog)
Write-Host ("  stderr_log={0}" -f $stderrLog)

if ((Test-Path $stderrLog) -and ((Get-Item $stderrLog).Length -gt 0))
{
    Write-Warning "Le log stderr contient des messages."
}
