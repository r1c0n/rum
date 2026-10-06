param(
    [ValidateSet('doctor', 'setup', 'build', 'user', 'run', 'run-kernel', 'test', 'debug', 'panic', 'clean', 'create-disk')]
    [string]$Action = 'run',
    [string]$Distro = 'Ubuntu',
    [string]$DiskImage,
    [switch]$DiskReadOnly,
    [ValidateRange(1, 131071)]
    [int]$DiskSizeMiB = 16
)

$ErrorActionPreference = 'Stop'
if ($DiskReadOnly -and -not $DiskImage) { throw '-DiskReadOnly requires -DiskImage.' }
if ($DiskImage -and $Action -notin @('run', 'run-kernel', 'debug', 'create-disk')) {
    throw '-DiskImage is supported by run, run-kernel, debug and create-disk.'
}
if ($Action -eq 'create-disk') {
    if (-not $DiskImage) { throw 'Supply -DiskImage with a NEW disposable image path.' }
    if ($DiskReadOnly) { throw '-DiskReadOnly applies only when booting.' }
    $imagePath = if ([IO.Path]::IsPathRooted($DiskImage)) { $DiskImage } else {
        Join-Path (Get-Location).Path $DiskImage
    }
    $linuxPath = & wsl.exe -d $Distro --exec wslpath -a -u $imagePath
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & wsl.exe -d $Distro --cd $PSScriptRoot --exec bash scripts/wsl-command.sh create-disk $linuxPath $DiskSizeMiB
    exit $LASTEXITCODE
}
if ($DiskImage) {
    $diskFile = Get-Item -LiteralPath $DiskImage
    if ($diskFile -isnot [IO.FileInfo] -or $diskFile.Length -eq 0 -or
        $diskFile.Length % 512 -ne 0 -or $diskFile.Length -gt 137438953472) {
        throw 'Raw disk must be a regular file with a nonzero multiple of 512 bytes, at most 128 GiB.'
    }
    foreach ($output in @('build\rum.iso', 'build\rum.elf', 'build\isodir\boot\rum.elf')) {
        if ($diskFile.FullName -eq (Join-Path $PSScriptRoot $output)) {
            throw 'A generated boot image cannot be attached as a writable disk.'
        }
    }
}
$buildAction = switch ($Action) {
    'run' { 'build' }
    'debug' { 'build' }
    default { $Action }
}
$buildArguments = @('-d', $Distro, '--cd', $PSScriptRoot, '--exec', 'bash', 'scripts/wsl-command.sh', $buildAction)
if ($DiskImage) {
    $linuxDiskPath = & wsl.exe -d $Distro --exec wslpath -a -u $diskFile.FullName
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $buildArguments += $linuxDiskPath
}
& wsl.exe @buildArguments
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($Action -eq 'test') {
    & (Join-Path $PSScriptRoot 'tests\disk-launcher-test.ps1')
    exit 0
}

if ($Action -in @('run', 'run-kernel', 'debug', 'panic')) {
    $qemuCommand = Get-Command qemu-system-i386.exe -ErrorAction SilentlyContinue
    if (-not $qemuCommand) {
        $qemuCommand = Get-Command qemu-system-x86_64.exe -ErrorAction SilentlyContinue
    }
    if (-not $qemuCommand) { throw 'QEMU is missing from Windows PATH. Use make run in WSL or add QEMU to PATH.' }
    $qemuArguments = @('-name', 'rum OS', '-m', '64M', '-nodefaults', '-vga', 'std', '-serial', 'stdio', '-no-reboot', '-no-shutdown')
    if ($Action -eq 'panic') {
        $qemuArguments += @('-kernel', (Join-Path $PSScriptRoot 'build\tests\fault-ud.elf'))
    } elseif ($Action -eq 'run-kernel') {
        $qemuArguments += @('-kernel', (Join-Path $PSScriptRoot 'build\rum.elf'))
    } else {
        $isoPath = (Join-Path $PSScriptRoot 'build\rum.iso').Replace(',', ',,')
        $qemuArguments += @('-boot', 'd', '-drive', "file=$isoPath,format=raw,if=ide,index=0,media=cdrom")
    }
    if ($DiskImage) {
        $bootPath = Join-Path $PSScriptRoot $(if ($Action -eq 'run-kernel') { 'build\rum.elf' } else { 'build\rum.iso' })
        if ($diskFile.FullName -eq $bootPath) { throw 'Boot image and writable disk must be different files.' }
        $diskPath = $diskFile.FullName.Replace(',', ',,')
        $diskOptions = "file=$diskPath,format=raw,if=ide,index=2,media=disk,cache=writeback,werror=report,rerror=report"
        if ($DiskReadOnly) { $diskOptions += ',readonly=on' }
        $qemuArguments += @('-drive', $diskOptions)
    }
    if ($Action -eq 'debug') { $qemuArguments += @('-S', '-s') }
    & $qemuCommand.Source @qemuArguments
    exit $LASTEXITCODE
}
