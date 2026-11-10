[CmdletBinding()]
param(
    [string]$Sdk = $env:RUM_SDK,
    [string]$CrossPrefix = 'i686-elf-',
    [string]$Distro = 'Ubuntu'
)
$ErrorActionPreference = 'Stop'
if (-not $Sdk) { throw 'Supply -Sdk with the unpacked rum-sdk directory.' }
$projectPath = & wsl.exe -d $Distro --exec wslpath -a -u $PSScriptRoot
if ($LASTEXITCODE -ne 0) { throw 'Cannot translate project path.' }
$sdkPath = & wsl.exe -d $Distro --exec wslpath -a -u (Resolve-Path -LiteralPath $Sdk).Path
if ($LASTEXITCODE -ne 0) { throw 'Cannot translate SDK path.' }
& wsl.exe -d $Distro --cd $projectPath --exec python3 "$sdkPath/tools/build.py" --cross-prefix $CrossPrefix --output build/example.elf main.c
exit $LASTEXITCODE
