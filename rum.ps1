[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('help', 'doctor', 'setup', 'toolchain', 'build', 'user', 'run', 'run-kernel', 'test', 'debug', 'panic', 'clean', 'create-disk', 'prepare-disk', 'package', 'vm-images', 'release-check')]
    [string]$Action = 'run',
    [string]$Distro = 'Ubuntu',
    [string]$DiskImage,
    [switch]$DiskReadOnly,
    [ValidateRange(1, 131071)][int]$DiskSizeMiB = 16,
    [ValidateRange(1, 64)][int]$Jobs = 4,
    [Alias('TestJobs')][ValidateRange(1, 4)][int]$Workers = 4,
    [ValidateSet('all', 'host', 'user', 'package', 'shell', 'storage', 'block', 'fat16', 'fat16-write', 'filesystem', 'preemption', 'lifecycle', 'sleep', 'console', 'integration', 'handles', 'pipes', 'workflows', 'jobs', 'terminal', 'editing', 'file-tools', 'vm', 'runtime')]
    [string]$Suite = 'all',
    [ValidateRange(16, 4096)][int]$MemoryMiB = 64,
    [string]$Qemu,
    [string]$CrossPrefix,
    [string]$Output,
    [switch]$NoBuild,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
function ConvertTo-RumLinuxPath([string]$Path) {
    $absolute = if ([IO.Path]::IsPathRooted($Path)) { $Path } else { Join-Path (Get-Location).Path $Path }
    $converted = & wsl.exe -d $Distro --exec wslpath -a -u $absolute
    if ($LASTEXITCODE -ne 0) { throw "Cannot translate path for WSL: $Path" }
    return $converted
}

# WSL supplies Python and all build tools. No Windows Python installation is required.
$linuxRoot = ConvertTo-RumLinuxPath $PSScriptRoot
$arguments = @($Action, '--jobs', $Jobs, '--workers', $Workers, '--memory', $MemoryMiB,
               '--size-mib', $DiskSizeMiB, '--suite', $Suite)
if ($DiskImage) { $arguments += @('--disk', (ConvertTo-RumLinuxPath $DiskImage)) }
if ($DiskReadOnly) { $arguments += '--read-only' }
if ($Output) { $arguments += @('--output', (ConvertTo-RumLinuxPath $Output)) }
if ($CrossPrefix) { $arguments += @('--cross-prefix', $CrossPrefix) }
if ($NoBuild) { $arguments += '--no-build' }
if ($DryRun) { $arguments += '--dry-run' }

if ($Action -in @('run', 'run-kernel', 'debug', 'panic', 'doctor')) {
    if ($Qemu) {
        $qemuCommand = Get-Command $Qemu -ErrorAction Stop
    } else {
        $qemuCommand = Get-Command qemu-system-i386.exe -ErrorAction SilentlyContinue
        if (-not $qemuCommand) { $qemuCommand = Get-Command qemu-system-x86_64.exe -ErrorAction SilentlyContinue }
    }
    if ($qemuCommand) {
        $arguments += @('--windows-qemu', (ConvertTo-RumLinuxPath $qemuCommand.Source))
    } elseif ($Action -ne 'doctor') {
        throw 'Windows QEMU is missing. Add it to PATH or supply -Qemu with its executable path.'
    } else {
        Write-Warning 'Windows QEMU is missing; builds and WSL tests can still run.'
    }
} elseif ($Qemu) {
    # Noninteractive tests and release checks use the Linux executable inside WSL.
    $arguments += @('--qemu', $Qemu)
}

& wsl.exe -d $Distro --cd (Get-Location).Path --exec python3 "$linuxRoot/scripts/rum.py" @arguments
exit $LASTEXITCODE
