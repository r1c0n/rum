#!/usr/bin/env python3
"""Create new FAT16 data images and a BIOS VMware preset beside a copied ISO."""
import argparse
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
from zipfile import ZIP_DEFLATED, ZipFile

ROOT = Path(__file__).resolve().parents[1]
SIZE = 16 * 1024 * 1024
SEED = b"Hello from rum's persistent data disk!\nThis file survives VM restarts.\n"
VMX = '''.encoding = "UTF-8"
config.version = "8"
virtualHW.version = "14"
displayName = "rum"
guestOS = "other"
firmware = "bios"
memsize = "64"
numvcpus = "1"
ide0:0.present = "TRUE"
ide0:0.deviceType = "cdrom-image"
ide0:0.fileName = "rum.iso"
ide0:0.startConnected = "TRUE"
ide1:0.present = "TRUE"
ide1:0.fileName = "rum-data.vmdk"
floppy0.present = "FALSE"
usb.present = "FALSE"
ehci.present = "FALSE"
sound.present = "FALSE"
ethernet0.present = "FALSE"
mks.enable3d = "FALSE"
serial0.present = "TRUE"
serial0.fileType = "file"
serial0.fileName = "vmware-serial.log"
serial0.startConnected = "TRUE"
bios.bootOrder = "cdrom"
'''
README = '''# rum virtual-machine files

Boot from rum.iso. The 16 MiB data images are optional persistent storage,
already formatted as unpartitioned FAT16 with README.TXT inside. They are not
bootable installation disks. Keep the ISO attached for every boot.

Use legacy BIOS, one CPU, 64 MiB RAM, VGA text, and a PS/2 keyboard.

## VMware Workstation

Open rum.vmx. It attaches the ISO at IDE 0:0 and rum-data.vmdk at IDE 1:0.
If asked whether you moved or copied the VM, choose "I copied it".
The preset writes diagnostic serial output to vmware-serial.log.

## VirtualBox

Create an Other/Unknown (32-bit) VM with 64 MiB RAM and one CPU. Disable UEFI
and I/O APIC, use the PIIX3 chipset and VBoxVGA display with 3D disabled.
Skip unattended installation. Do not create a new disk.

Under Storage, add an IDE controller (PIIX4). Attach rum.iso as IDE Primary
Master (port 0, device 0), and rum-data.vdi as IDE Secondary Master (port 1,
device 0). Remove unused SATA/SCSI/NVMe controllers. Boot from Optical first.
Set keyboard and pointing device to PS/2. Start the VM and click its display.

## Try the disk

    ls /disk
    cat /disk/README.TXT
    write /disk/NOTE.TXT hello from rum
    cat /disk/NOTE.TXT

Wait for the prompt before powering off. Boot the same ISO and disk again and
read NOTE.TXT. Writes flush before success, but FAT16 has no journal or general
crash recovery. VMware and VirtualBox use separate images; their changes are
independent. rum-data.raw is the initial QEMU/host-tool copy.

Disk files change when used. SHA256SUMS records their initial contents only.
This helper refuses an existing output directory. Choose another directory
when creating a new set; it never replaces a disk from an earlier run.
'''


def run(*arguments):
    subprocess.run([str(argument) for argument in arguments], check=True, timeout=120)


def prepare(iso, output):
    if output.exists() or output.is_symlink():
        raise FileExistsError(f"output already exists: {output}; choose a new directory")
    if not iso.is_file() or iso.stat().st_size == 0:
        raise ValueError(f"missing ISO: {iso}; build rum first")
    for tool in ("qemu-img", "mformat", "mcopy"):
        if not shutil.which(tool):
            raise ValueError(f"missing {tool}; run under Ubuntu/WSL with qemu-utils and mtools installed")
    # Exclusively claim the whole destination before any formatter or converter
    # runs. All mutations below operate on newly created files inside it.
    output.mkdir(parents=True, exist_ok=False)
    raw = output / "rum-data.raw"
    with raw.open("xb") as image:
        image.truncate(SIZE)
    run("mformat", "-i", raw, "-T", SIZE // 512, "-c", 2, "-v", "RUMDATA", "::")
    with raw.open("rb") as image:
        bpb = image.read(512)
    sector_size = struct.unpack_from("<H", bpb, 11)[0]
    sectors = struct.unpack_from("<H", bpb, 19)[0] or struct.unpack_from("<I", bpb, 32)[0]
    fat_size = struct.unpack_from("<H", bpb, 22)[0]
    root_sectors = (struct.unpack_from("<H", bpb, 17)[0] * 32 + 511) // 512
    start = struct.unpack_from("<H", bpb, 14)[0] + bpb[16] * fat_size + root_sectors
    clusters = (sectors - start) // bpb[13] if bpb[13] else 0
    if (sector_size != 512 or bpb[16] != 2 or sectors * 512 != SIZE or
            not 4085 <= clusters < 65525 or bpb[510:512] != b"\x55\xaa"):
        raise ValueError("formatter did not produce the expected bounded FAT16 volume")
    with tempfile.TemporaryDirectory(prefix="rum-vm-seed-") as temporary:
        note = Path(temporary) / "README.TXT"
        note.write_bytes(SEED)
        run("mcopy", "-i", raw, note, "::/README.TXT")
    for kind in ("vmdk", "vdi"):
        target = output / f"rum-data.{kind}"
        arguments = ["qemu-img", "convert", "-f", "raw", "-O", kind]
        if kind == "vmdk":
            arguments += ["-o", "adapter_type=ide,subformat=monolithicSparse"]
        run(*arguments, raw, target)
        run("qemu-img", "compare", "-f", "raw", "-F", kind, raw, target)
    shutil.copyfile(iso, output / "rum.iso")
    (output / "rum.vmx").write_text(VMX, encoding="utf-8")
    (output / "README.md").write_text(README, encoding="utf-8")
    files = sorted(output.iterdir())
    sums = "".join(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n" for path in files)
    (output / "SHA256SUMS").write_text(sums, encoding="ascii")
    with ZipFile(output / "rum-vm-images.zip", "x", compression=ZIP_DEFLATED) as archive:
        for path in files + [output / "SHA256SUMS"]:
            archive.write(path, path.name)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, default=ROOT / "build/rum.iso")
    parser.add_argument("--output", type=Path, default=ROOT / "build/vm-images")
    args = parser.parse_args()
    try:
        output = prepare(args.iso.resolve(), args.output.absolute())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"rum VM images: {error}", file=sys.stderr)
        return 1
    print(f"Created {output}; open rum.vmx in VMware or attach rum-data.vdi in VirtualBox.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
