param(
    [Parameter(Mandatory = $true, Position = 0)][string]$ServerRoot,
    [string]$BuildId = "",
    [int]$ClientSlot = 0,
    [int]$Port = 27035,
    [string]$Map = "de_dust2",
    [switch]$SkipGameplay,
    [switch]$VerboseServerOutput
)

$ErrorActionPreference = "Stop"
$BundleDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Arguments = @(
    (Join-Path $BundleDir "run-keels2-09-live-gate.py"),
    $ServerRoot,
    "--client-slot", $ClientSlot,
    "--port", $Port,
    "--map", $Map
)
if ($BuildId) { $Arguments += @("--build-id", $BuildId) }
if ($SkipGameplay) { $Arguments += "--skip-gameplay" }
if ($VerboseServerOutput) { $Arguments += "--verbose-server-output" }
& python @Arguments
exit $LASTEXITCODE
