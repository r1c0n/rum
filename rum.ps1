param(
    [ValidateSet('doctor', 'setup', 'build', 'run', 'run-kernel', 'test', 'debug', 'panic', 'clean')]
    [string]$Action = 'run',
    [string]$Distro = 'Ubuntu'
)

$ErrorActionPreference = 'Stop'
$buildAction = switch ($Action) {
    'run' { 'build' }
    'debug' { 'build' }
    default { $Action }
}
& wsl.exe -d $Distro --cd $PSScriptRoot --exec bash scripts/wsl-command.sh $buildAction
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($Action -in @('run', 'run-kernel', 'debug', 'panic')) {
    $qemuCommand = Get-Command qemu-system-i386.exe -ErrorAction SilentlyContinue
    if (-not $qemuCommand) {
        $qemuCommand = Get-Command qemu-system-x86_64.exe -ErrorAction SilentlyContinue
    }
    if (-not $qemuCommand) { throw 'QEMU is missing from Windows PATH. Use make run in WSL or add QEMU to PATH.' }
    $qemuArguments = @('-name', 'rum OS', '-m', '64M', '-serial', 'stdio', '-no-reboot', '-no-shutdown')
    if ($Action -eq 'panic') {
        $qemuArguments += @('-kernel', (Join-Path $PSScriptRoot 'build\tests\fault-ud.elf'))
    } elseif ($Action -eq 'run-kernel') {
        $qemuArguments += @('-kernel', (Join-Path $PSScriptRoot 'build\rum.elf'))
    } else {
        $qemuArguments += @('-boot', 'd', '-cdrom', (Join-Path $PSScriptRoot 'build\rum.iso'))
    }
    if ($Action -eq 'debug') { $qemuArguments += @('-S', '-s') }
    & $qemuCommand.Source @qemuArguments
    exit $LASTEXITCODE
}
