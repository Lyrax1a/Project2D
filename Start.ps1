[CmdletBinding()]
param([string]$Port='auto',[switch]$Load,[string]$EfinityHome=$env:EFINITY_HOME,[switch]$NoAudio)
$ErrorActionPreference='Stop'
$gameRoot=$PSScriptRoot
$python=Join-Path $gameRoot '.venv/Scripts/python.exe'
if (-not (Test-Path -LiteralPath $python)) { $python='python' }
& $python -c 'import serial, numpy, pygame'
if ($LASTEXITCODE -ne 0) { throw 'Install requirements.txt in this project virtual environment before starting.' }
if ($Load) {
    if (-not $EfinityHome) { throw 'Specify -EfinityHome or set EFINITY_HOME before loading.' }
    if (-not (Test-Path -LiteralPath (Join-Path $gameRoot 'Build/game.bin'))) { throw 'Run python build.py first.' }
    $saved=@{}
    foreach ($name in @('EFINITY_HOME','EFXPGM_HOME','EFXDBG_HOME','EFXPT_HOME','PYTHONHOME','PYTHONPATH','EFX_HIFTDI_ENABLE','EFINITY_USER_DIR_INI','PATH')) { $saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process') }
    try {
        $env:EFINITY_HOME=$EfinityHome
        $env:EFXPGM_HOME=Join-Path $EfinityHome 'pgm'
        $env:EFXDBG_HOME=Join-Path $EfinityHome 'debugger'
        $env:EFXPT_HOME=Join-Path $EfinityHome 'pt'
        $env:PYTHONHOME=Join-Path $EfinityHome 'python311'
        $env:PYTHONPATH="$(Join-Path $EfinityHome 'debugger/bin');$(Join-Path $EfinityHome 'pgm/bin')"
        $env:EFX_HIFTDI_ENABLE='1'
        $env:EFINITY_USER_DIR_INI=Join-Path $env:LOCALAPPDATA 'efinity/user_dir.ini'
        $env:PATH="$(Join-Path $EfinityHome 'python311/bin');$(Join-Path $EfinityHome 'pgm/bin');$(Join-Path $EfinityHome 'debugger/bin');$($saved.PATH)"
        & (Join-Path $EfinityHome 'python311/bin/python.exe') (Join-Path $gameRoot 'Scripts/efinity_d2xx_cli.py') (Join-Path $gameRoot 'Hardware/usurper_ts2d.bit') --mode jtag
        if ($LASTEXITCODE -ne 0) { throw 'JTAG configuration failed.' }
    } finally { foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name,$saved[$name],'Process') } }
}
$gameArgs=@((Join-Path $gameRoot 'Scripts/load_usurper.py'),'--port',$Port)
if ($Load) { $gameArgs+='--interactive' } else { $gameArgs+='--control-only' }
if ($NoAudio) { $gameArgs+='--no-audio' }
& $python @gameArgs
if ($LASTEXITCODE -ne 0) { throw 'Game loading/control failed. See Logs.' }
