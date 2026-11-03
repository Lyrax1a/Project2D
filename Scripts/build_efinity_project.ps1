[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ProjectXml,
    [Parameter(Mandatory = $true)][string]$EfinityHome,
    [Parameter(Mandatory = $true)][string]$ResultDir
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$xml = [IO.Path]::GetFullPath($ProjectXml)
$efinityRoot = [IO.Path]::GetFullPath($EfinityHome.Replace('/', '\'))
if ($efinityRoot.Length -gt 3) { $efinityRoot = $efinityRoot.TrimEnd('\') }
$result = [IO.Path]::GetFullPath($ResultDir)
$setup = Join-Path $efinityRoot 'bin\setup.bat'
$runner = Join-Path $efinityRoot 'bin\efx_run.bat'
if (-not (Test-Path -LiteralPath $xml -PathType Leaf)) { throw "Project XML not found: $xml" }
if (-not (Test-Path -LiteralPath $setup -PathType Leaf)) { throw "setup.bat not found: $setup" }
New-Item -ItemType Directory -Force -Path $result | Out-Null
$log = Join-Path $result 'build.log'
$start = (Get-Date).ToUniversalTime()
$commit = 'UNVERSIONED_PACKAGE'
try {
    if (Get-Command git -ErrorAction SilentlyContinue) {
        $recordedCommit = & git -C $repo rev-parse HEAD 2>$null
        if ($LASTEXITCODE -eq 0 -and $recordedCommit) { $commit = $recordedCommit.Trim() }
    }
} catch {
    # A relocated source package need not be a Git checkout.
}
$hostNameValue = [Environment]::MachineName
$fullCommand = "`"$runner`" `"$xml`" --flow compile"
$pgmCommand = "`"$runner`" `"$xml`" --flow pgm"
$commandLine = "call `"$setup`" && $fullCommand && $pgmCommand"
$xmlOriginal = [IO.File]::ReadAllBytes($xml)

Push-Location (Split-Path $xml -Parent)
try {
    & cmd.exe /d /c "call `"$setup`" && $fullCommand > `"$log`" 2>&1 && $pgmCommand >> `"$log`" 2>&1"
    $exitCode = $LASTEXITCODE
} finally {
    Pop-Location
    [IO.File]::WriteAllBytes($xml, $xmlOriginal)
}
$end = (Get-Date).ToUniversalTime()
$freshImages = @(Get-ChildItem -LiteralPath (Split-Path $xml -Parent) -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Extension -in @('.hex', '.bit') -and $_.Length -gt 0 -and $_.LastWriteTimeUtc -ge $start })
if ($exitCode -eq 0 -and $freshImages.Count -eq 0) { $exitCode = 5 }

$manifest = [ordered]@{
    schema = 'tilestorm-efinity-build-v1'
    project_xml = $xml.Replace('\', '/')
    project_xml_sha256 = (Get-FileHash -LiteralPath $xml -Algorithm SHA256).Hash.ToLowerInvariant()
    efinity_home = $efinityRoot.Replace('\', '/')
    efinity_suite_version = if (Test-Path (Join-Path $efinityRoot 'scripts\sw_version.txt')) { (Get-Content (Join-Path $efinityRoot 'scripts\sw_version.txt') -Raw).Trim() } else { 'UNKNOWN' }
    command = $commandLine
    exit_code = $exitCode
    started_utc = $start.ToString('o')
    ended_utc = $end.ToString('o')
    host = $hostNameValue
    git_commit = $commit
    fresh_images = @($freshImages | ForEach-Object {
        [ordered]@{ path = $_.FullName.Replace('\', '/'); bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
}
$manifestJson = $manifest | ConvertTo-Json -Depth 4
[IO.File]::WriteAllText((Join-Path $result 'build_manifest.json'), $manifestJson, [Text.UTF8Encoding]::new($false))

$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) { throw 'Python is required to collect Efinity results.' }
& $python.Source (Join-Path $PSScriptRoot 'collect_efinity_results.py') (Split-Path $xml -Parent) $result

$projectLogs = @(Get-ChildItem -LiteralPath (Split-Path $xml -Parent) -Recurse -File -Filter '*.log' -ErrorAction SilentlyContinue)
$warningLines = @(
    if (Test-Path $log) { Select-String -LiteralPath $log -Pattern 'warning|critical warning|error' -CaseSensitive:$false }
    foreach ($projectLog in $projectLogs) {
        Select-String -LiteralPath $projectLog.FullName -Pattern 'warning|critical warning|error' -CaseSensitive:$false
    }
)
@('# Efinity warnings and errors', '', "Build exit code: $exitCode", '', '```text') +
    @($warningLines | ForEach-Object { $_.Line } | Select-Object -Unique) + @('```') |
    Set-Content -LiteralPath (Join-Path $result 'warnings.md') -Encoding utf8
Write-Output "BUILD_EXIT_CODE=$exitCode"
Write-Output "BUILD_LOG=$log"
exit $exitCode
