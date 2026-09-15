param(
    [Parameter(Mandatory = $true, Position = 0)][string]$ServerRoot,
    [Parameter(Position = 1)][string]$BuildId = ""
)

$ErrorActionPreference = "Stop"
$BundleDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $BundleDir

foreach ($Line in [IO.File]::ReadAllLines((Join-Path $BundleDir "MANIFEST.txt"))) {
    if ($Line -notmatch '^([0-9a-f]{64})  (.+)$') { throw "invalid MANIFEST.txt entry" }
    $Path = Join-Path $BundleDir $Matches[2]
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { throw "bundle file missing: $($Matches[2])" }
    $Actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($Actual -ne $Matches[1]) { throw "bundle hash mismatch: $($Matches[2])" }
}

$ServerRoot = (Resolve-Path -LiteralPath $ServerRoot).Path
$ServerModule = Join-Path $ServerRoot "game\csgo\bin\win64\server.dll"
if (!(Test-Path -LiteralPath $ServerModule -PathType Leaf)) { throw "server module not found: $ServerModule" }

if (!$BuildId) {
    $Manifests = @(
        (Join-Path $ServerRoot "steamapps\appmanifest_730.acf"),
        (Join-Path (Split-Path -Parent $ServerRoot) "steamapps\appmanifest_730.acf")
    )
    foreach ($Manifest in $Manifests) {
        if (Test-Path -LiteralPath $Manifest -PathType Leaf) {
            $Text = [IO.File]::ReadAllText($Manifest)
            if ($Text -match '"buildid"\s+"([0-9]+)"') { $BuildId = $Matches[1]; break }
        }
    }
}
if ($BuildId -notmatch '^[0-9]+$') { throw "could not determine app 730 build ID; pass it as the second argument" }
$ExpectedBuild = [IO.File]::ReadAllText((Join-Path $BundleDir "EXPECTED_BUILD.txt")).Trim()
if ($BuildId -ne $ExpectedBuild) { throw "build mismatch: bundle=$ExpectedBuild server=$BuildId" }

$Timestamp = [DateTime]::UtcNow.ToString("yyyyMMdd-HHmmss")
$WorkDir = Join-Path ([IO.Path]::GetTempPath()) ("keels2-profile-" + [Guid]::NewGuid().ToString("N"))
$EvidenceName = "keels2-cs2-current-windows-profile-capture-$Timestamp-evidence"
$EvidenceDir = Join-Path $WorkDir $EvidenceName
[IO.Directory]::CreateDirectory($EvidenceDir) | Out-Null
$Request = Join-Path $WorkDir "request.tsv"
$Candidate = Join-Path $WorkDir "candidate.tsv"
$CaptureLog = Join-Path $WorkDir "capture.log"
$Utf8 = [Text.UTF8Encoding]::new($false)
$Records = @(
    "keels2-compatibility-request`t1",
    "game`tcs2",
    "version`t$BuildId",
    "platform`twin64",
    "module`tserver`t$ServerModule",
    "interface`tgame_clients`tSource2GameClients001`tserver",
    "interface`tgame_event_manager`tCGameEventManager`tserver",
    "interface`tserver`tSource2Server001`tserver",
    "interface`tserver_config`tSource2ServerConfig001`tserver",
    "slot`tgame_clients`tClientActive`t14",
    "slot`tgame_clients`tClientCommand`t17",
    "slot`tgame_clients`tClientConnect`t12",
    "slot`tgame_clients`tClientDisconnecting`t16",
    "slot`tgame_clients`tClientFullyConnected`t15",
    "slot`tgame_clients`tClientPutInServer`t13",
    "slot`tgame_clients`tClientSettingsChanged`t19",
    "slot`tgame_clients`tGameFrame`t19",
    "slot`tgame_clients`tOnClientConnected`t11",
    "slot`tgame_clients`tValidation`t0",
    "slot`tgame_event_manager`tAddListener`t3",
    "slot`tgame_event_manager`tLoadEventsFromFile`t1",
    "slot`tserver`tInit`t3",
    "slot`tserver_config`tConnect`t0",
    "slot`tserver_config`tDisconnect`t1",
    "pattern`tcs2.base_entity.take_damage`tserver`t40 55 53 56 57 41 54 48 8D 6C 24 ?? 48 81 EC ?? ?? ?? ?? 4D 8B E0`t0"
)
[IO.File]::WriteAllText($Request, (($Records -join "`n") + "`n"), $Utf8)

$Output = & (Join-Path $BundleDir "keels2_compatibility_review.exe") capture $Request $Candidate 2>&1
$CaptureStatus = $LASTEXITCODE
$Output | Tee-Object -FilePath $CaptureLog
Copy-Item -LiteralPath $Request -Destination (Join-Path $EvidenceDir "request.tsv")
Copy-Item -LiteralPath $CaptureLog -Destination (Join-Path $EvidenceDir "capture.log")
if (Test-Path -LiteralPath $Candidate -PathType Leaf) { Copy-Item -LiteralPath $Candidate -Destination (Join-Path $EvidenceDir "candidate.tsv") }
Copy-Item -LiteralPath (Join-Path $BundleDir "MANIFEST.txt") -Destination $EvidenceDir
Copy-Item -LiteralPath (Join-Path $BundleDir "EXPECTED_BUILD.txt") -Destination $EvidenceDir
$Run = "server_module=$ServerModule`nbuild_id=$BuildId`nutc=$Timestamp`nos=$([Environment]::OSVersion.VersionString)`nps=$($PSVersionTable.PSVersion)`n"
[IO.File]::WriteAllText((Join-Path $EvidenceDir "RUN.txt"), $Run, $Utf8)
$ServerSha = (Get-FileHash -LiteralPath $ServerModule -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $EvidenceDir "server.sha256"), "$ServerSha  $ServerModule`n", $Utf8)
$Archive = Join-Path $BundleDir "$EvidenceName.zip"
Compress-Archive -LiteralPath $EvidenceDir -DestinationPath $Archive -CompressionLevel Optimal
$Digest = (Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant()

if (Test-Path -LiteralPath $Candidate -PathType Leaf) {
    $CandidateLines = [IO.File]::ReadAllLines($Candidate)
    $Module = $CandidateLines | Where-Object { $_ -like "module`tserver`t*" } | Select-Object -First 1
    $Pattern = $CandidateLines | Where-Object { $_ -like "pattern`tcs2.base_entity.take_damage`t*" } | Select-Object -First 1
    $ModuleFields = $Module -split "`t"
    $PatternFields = $Pattern -split "`t"
    Write-Output "Compatibility candidate captured: cs2-$BuildId-win64-$($ModuleFields[3])-$($ModuleFields[4])"
    Write-Output ("TakeDamage signature matches: {0} at 0x{1:x}" -f [uint32]$PatternFields[5], [uint64]$PatternFields[6])
}
Write-Output "Evidence archive: $Archive"
Write-Output "SHA-256: $Digest"
Remove-Item -LiteralPath $WorkDir -Recurse -Force
if ($CaptureStatus -ne 0) { exit $CaptureStatus }
