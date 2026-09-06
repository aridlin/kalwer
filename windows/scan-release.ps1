param(
    [Parameter(Mandatory = $true)][string]$ReleaseTag,
    [string]$Repository = 'aridlin/kalwer',
    [switch]$RequireRealtime
)
$ErrorActionPreference = 'Stop'
if ($ReleaseTag -notmatch '^v\d+\.\d+\.\d+$') { throw 'Expected a desktop release tag such as v0.5.1' }
if ($Repository -ne 'aridlin/kalwer') { throw 'Only the official Kalwer repository is supported' }

# Scan exact published bytes, not a separate rebuild. Never execute the sample.
$sampleDirectory = Join-Path ([IO.Path]::GetTempPath()) ('kalwer-scan-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path $sampleDirectory | Out-Null
$status = Get-MpComputerStatus
if (-not $status.AMServiceEnabled -or -not $status.AntivirusEnabled) {
    throw 'Defender is unavailable on this runner; this is NOT a clean scan.'
}
function Get-ScannerPath {
    $platformRoot = Join-Path $env:ProgramData 'Microsoft\Windows Defender\Platform'
    $path = Get-ChildItem "$platformRoot\*\MpCmdRun.exe" -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
    if (-not $path) { $path = Join-Path $env:ProgramFiles 'Windows Defender\MpCmdRun.exe' }
    if (-not (Test-Path $path)) { throw 'Defender command-line scanner not found' }
    return $path
}
# Fresh hosted images can restart the Defender service while updating its engine.
# Use the supported CLI with bounded retries; never silently use stale signatures.
$updated = $false
for ($attempt = 0; $attempt -lt 3; $attempt++) {
    & (Get-ScannerPath) -SignatureUpdate -MMPC
    if ($LASTEXITCODE -eq 0) { $updated = $true; break }
    if ($attempt -lt 2) { Start-Sleep -Seconds 10 }
}
if (-not $updated) { throw 'Defender intelligence update failed; scan results are unavailable.' }
if ($RequireRealtime) {
    # Opt-in for disposable CI runners; only enable protection, never disable it.
    Set-MpPreference -DisableRealtimeMonitoring $false
    for ($attempt = 0; $attempt -lt 6; $attempt++) {
        if ((Get-MpComputerStatus).RealTimeProtectionEnabled) { break }
        Start-Sleep -Seconds 5
    }
    if (-not (Get-MpComputerStatus).RealTimeProtectionEnabled) {
        throw 'Real-time protection could not be enabled; download check is unavailable.'
    }
}
Get-MpComputerStatus | Select-Object AMEngineVersion, AMProductVersion, AntivirusSignatureVersion,
    AntivirusSignatureLastUpdated, AMRunningMode, RealTimeProtectionEnabled | Format-List
Get-MpPreference | Select-Object MAPSReporting, SubmitSamplesConsent, DisableBlockAtFirstSeen | Format-List

$baseUrl = "https://github.com/$Repository/releases/download/$ReleaseTag"
$sample = Join-Path $sampleDirectory 'kalwer.exe'
Invoke-WebRequest "$baseUrl/kalwer.exe.sha256" -OutFile "$sample.sha256"
Invoke-WebRequest "$baseUrl/kalwer.exe" -OutFile $sample
$expected = ((Get-Content "$sample.sha256" -Raw) -split '\s+')[0].ToLowerInvariant()
$actual = (Get-FileHash $sample -Algorithm SHA256).Hash.ToLowerInvariant()
if ($expected -notmatch '^[a-f0-9]{64}$' -or $expected -ne $actual) { throw 'Release checksum mismatch' }
Write-Output "Release: $ReleaseTag`nSHA256: $actual"
Get-AuthenticodeSignature $sample | Select-Object Status, StatusMessage | Format-List

$scanner = Get-ScannerPath
# This per-scan option ignores exclusions and reports detections without modifying
# the sample. It does not disable antivirus or change system protection settings.
& $scanner -Scan -ScanType 3 -File $sample -DisableRemediation
$scanExit = $LASTEXITCODE
if ($scanExit -ne 0) { throw "Defender detected a threat or could not scan (exit $scanExit). See output above." }
if (-not (Test-Path $sample) -or (Get-FileHash $sample -Algorithm SHA256).Hash.ToLowerInvariant() -ne $actual) {
    throw 'Sample was removed or modified during scanning; not a clean result.'
}
if ($RequireRealtime -and -not (Get-MpComputerStatus).RealTimeProtectionEnabled) {
    throw 'Real-time protection became unavailable during the check.'
}
Write-Output 'On-demand scan passed. This does not certify SmartScreen, cloud reputation, or runtime behavior.'
