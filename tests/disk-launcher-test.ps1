# Check the thin PowerShell adapter without starting WSL or QEMU.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$global:rumWslCalls = @()
$global:rumWslStatus = 0
$global:rumCommandStatus = 0
function wsl.exe {
    $global:rumWslCalls += ,@($args)
    $global:LASTEXITCODE = $global:rumWslStatus
    if ($args -contains 'wslpath') {
        '/translated/' + ([string]$args[-1]).Replace('\', '/')
    } else {
        $global:LASTEXITCODE = $global:rumCommandStatus
    }
}
function Get-Command {
    param([string]$Name, $ErrorAction)
    if ($Name -eq 'qemu-system-i386.exe') {
        return [PSCustomObject]@{ Source = 'E:\qemu\qemu-system-i386.exe' }
    }
    throw "Unexpected command lookup: $Name"
}
function Check([bool]$condition, [string]$name) {
    if (-not $condition) { throw "Launcher check failed: $name" }
}
$launcher = Join-Path $root 'rum.ps1'
& $launcher run -DiskImage 'build\disk with space,comma.raw' -Distro 'Ubuntu Test' -Jobs 3 -MemoryMiB 32 -DryRun
$call = $global:rumWslCalls[-1]
Check ($call[1] -eq 'Ubuntu Test' -and $call -contains 'python3') 'selected distro and WSL Python'
Check ($call -contains '--windows-qemu') 'native Windows QEMU selected for interactive boot'
Check ($call -contains '--disk' -and @($call | Where-Object { $_ -like '*disk with space,comma.raw' }).Count -eq 1) 'literal disk path stays one argument'
Check ($call -contains '--dry-run' -and $call -contains '--memory' -and $call -contains 32) 'preview and guest memory'
Check ($call -contains '--cd' -and $call -contains (Get-Location).Path) 'caller working directory preserved'
& $launcher test -Jobs 4 -Workers 2 -Suite host
$call = $global:rumWslCalls[-1]
Check ($call -contains 'host' -and $call -contains 2 -and -not ($call -contains '--windows-qemu')) 'Linux test backend and bounded workers'
& $launcher prepare-disk -DiskImage 'build\new disk.raw' -DiskSizeMiB 24
$call = $global:rumWslCalls[-1]
Check ($call -contains 'prepare-disk' -and $call -contains 24) 'explicit preparation and size'
& $launcher package -Output 'build\package with spaces.zip' -NoBuild
$call = $global:rumWslCalls[-1]
Check ($call -contains '--output' -and $call -contains '--no-build') 'package options'
$global:rumWslStatus = 7
$failed = $false
try { & $launcher build } catch { $failed = $true }
Check $failed 'WSL path conversion failures stop dispatch'
$global:rumWslStatus = 0
$global:rumCommandStatus = 9
& $launcher build
Check ($LASTEXITCODE -eq 9) 'build failures propagate to the calling shell'
Write-Output 'PowerShell build adapter tests passed'
