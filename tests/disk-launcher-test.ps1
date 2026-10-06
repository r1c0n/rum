# Exercise the PowerShell wrapper with recorded WSL/QEMU calls.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$parent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$temporary = Join-Path $parent ('rum-launcher-' + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $temporary
$global:rumQemuArguments = @()
$global:rumWslCalls = @()
function wsl.exe {
    $global:rumWslCalls += ,@($args)
    $global:LASTEXITCODE = 0
    if ($args -contains 'wslpath') { '/mnt/test/new disk, image.raw' }
}
function Get-Command {
    param([string]$Name, $ErrorAction)
    if ($Name -eq 'qemu-system-i386.exe') {
        return [PSCustomObject]@{ Source = 'Invoke-RumTestQemu' }
    }
    throw "Unexpected command lookup: $Name"
}
function Invoke-RumTestQemu {
    $global:rumQemuArguments = @($args)
    $global:LASTEXITCODE = 0
}
function Check([bool]$condition, [string]$name) {
    if (-not $condition) { throw "Launcher check failed: $name" }
}
try {
    $disk = Join-Path $temporary 'disk with space,comma.raw'
    [IO.File]::WriteAllBytes($disk, [byte[]]::new(512))
    $launcher = Join-Path $root 'rum.ps1'
    & $launcher run -DiskImage $disk
    Check ($global:rumQemuArguments -contains '-nodefaults') 'explicit device layout'
    $drives = @($global:rumQemuArguments | Where-Object { $_ -like 'file=*' })
    Check ($drives.Count -eq 2 -and $drives[0] -like '*index=0,media=cdrom*') 'primary ISO'
    Check ($drives[1].Contains('space,,comma.raw') -and $drives[1].Contains('index=2,media=disk')) 'literal disk path and secondary slot'
    Check ($drives[1].Contains('format=raw') -and $drives[1].Contains('werror=report')) 'raw format and reported errors'
    & $launcher run-kernel -DiskImage $disk -DiskReadOnly
    Check ($global:rumQemuArguments -contains '-kernel') 'direct ELF'
    Check (@($global:rumQemuArguments | Where-Object { $_ -like '*readonly=on*' }).Count -eq 1) 'read-only flag'
    & $launcher debug
    Check ($global:rumQemuArguments -contains '-S' -and $global:rumQemuArguments -contains '-s') 'debug flags'
    Check (@($global:rumQemuArguments | Where-Object { $_ -like '*media=disk*' }).Count -eq 0) 'no implicit disk'
    $before = $global:rumWslCalls.Count
    $rejected = $false
    try { & $launcher run -DiskImage (Join-Path $temporary 'missing.raw') } catch { $rejected = $true }
    Check ($rejected -and $global:rumWslCalls.Count -eq $before) 'missing image rejected before build'
    $rejected = $false
    try { & $launcher run -DiskReadOnly } catch { $rejected = $true }
    Check $rejected 'read-only needs an explicit image'
    $before = $global:rumWslCalls.Count
    $rejected = $false
    try { & $launcher run -DiskImage (Join-Path $root 'build\rum.iso') } catch { $rejected = $true }
    Check ($rejected -and $global:rumWslCalls.Count -eq $before) 'boot output rejected before rebuilding'
    & $launcher create-disk -DiskImage (Join-Path $temporary 'new disk, image.raw') -DiskSizeMiB 1
    $creation = $global:rumWslCalls[-1]
    Check ($creation -contains 'create-disk' -and $creation -contains '/mnt/test/new disk, image.raw' -and $creation[-1] -eq 1) 'explicit image-creation arguments'
    Check ([IO.File]::ReadAllBytes($disk).Length -eq 512) 'existing image preserved'
    Write-Output 'PowerShell disk launcher tests passed'
} finally {
    $resolved = [IO.Path]::GetFullPath($temporary)
    if (-not $resolved.StartsWith($parent, [StringComparison]::OrdinalIgnoreCase) -or
        (Split-Path $resolved -Leaf) -notlike 'rum-launcher-*') { throw 'Unexpected cleanup path' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
