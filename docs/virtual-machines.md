# VMware and VirtualBox

rum boots from `rum.iso` in both VMware Workstation and VirtualBox. Keep the ISO attached when you restart the VM. A prepared FAT16 system disk
is required for the normal shell: it supplies `/RUM` programs and persistent
root files. Without it, the ISO provides the recovery console.

## Settings

| Setting | Value |
| --- | --- |
| Guest type | Other / Unknown, 32-bit |
| Firmware | Legacy BIOS; UEFI off |
| CPUs | 1 |
| RAM | 64 MiB |
| Display | VGA-compatible text display; 3D off |
| Keyboard | PS/2 |
| Boot medium | `rum.iso`, optical drive first |
| System disk | 16 MiB unpartitioned FAT16, secondary IDE master |

The current ATA driver probes only the legacy secondary IDE master at ports
`0x170` / `0x376`. A disk connected through SATA, SCSI, NVMe, primary IDE, or the
secondary slave will not appear at `/`. The disk format alone does not
select its controller.

## Prepare the files

From PowerShell in the repository:

```powershell
.\rum.ps1 build
wsl.exe -d Ubuntu --cd $PWD.Path --exec python3 scripts/prepare-vm-images.py
```

Or from Ubuntu/Linux:

```sh
make
python3 scripts/prepare-vm-images.py
```

The helper needs `qemu-img` and Mtools in Ubuntu/WSL. If either is missing:

```sh
sudo apt install qemu-utils mtools
```

It creates a new `build/vm-images/` directory:

| File | Purpose |
| --- | --- |
| `rum.iso` | Copy of the built boot ISO |
| `rum.vmx` | VMware BIOS/IDE preset using relative paths |
| `rum-data.vmdk` | VMware installed system disk |
| `rum-data.vdi` | VirtualBox installed system disk |
| `rum-data.raw` | Equivalent raw disk for QEMU and host FAT tools |
| `README.md` | Short setup instructions suitable for the extracted bundle |
| `SHA256SUMS` | Initial file checksums |
| `rum-vm-images.zip` | Portable bundle of these initial files |

Both converted disks contain FAT16 directly at sector zero and include
`README.TXT`. They contain `/RUM` programs but do not contain an HDD bootloader; keep the ISO attached. The helper compares
each converted image against the raw image before producing the bundle.
[QEMU documents these conversion formats and options](https://www.qemu.org/docs/master/system/images.html).

An existing output directory is refused before anything is formatted. To make
another independent set, choose a new directory:

```powershell
wsl.exe -d Ubuntu --cd $PWD.Path --exec python3 scripts/prepare-vm-images.py --output build/vm-images-new
```

The standard `rum.zip` release archive stays unchanged: ISO, root README, and
documentation. These fresh system disks are a separate bundle. The VMDK,
VDI, and raw images start with the same bytes, but subsequent edits to one are
independent of the others.

## VMware Workstation

The quickest setup is the generated preset:

1. Extract `rum-vm-images.zip` into a writable directory, or open
   `build/vm-images/` directly.
2. In Workstation, choose **Open a Virtual Machine** and select `rum.vmx`.
3. If asked whether you moved or copied it, choose **I copied it**.
4. Check that the CD/DVD drive is connected, then power on the VM.
5. Click the display to type. Press **Ctrl+Alt** to release captured input.

Keep `rum.vmx`, `rum.iso`, and `rum-data.vmdk` in the same directory. The preset
uses one CPU, 64 MiB RAM, BIOS firmware, an IDE optical drive at **IDE 0:0**,
and the writable data disk at **IDE 1:0**. It also writes COM1 output to
`vmware-serial.log` for troubleshooting.

For a manually created VM, select **Other / Other (32-bit)** and install the
operating system later. Use the settings above, choose an **IDE** hard disk,
and select **Use an existing virtual disk** for `rum-data.vmdk`. In the disk's
advanced settings, select **IDE 1:0**. Keep the optical drive on **IDE 0:0** so
it does not occupy the disk's slot. Remove any unused blank disk created by
the wizard from this VM's configuration.

If the GUI does not expose a device-node setting, power off the VM and close
its tab before editing its `.vmx`. The relevant entries are:

```ini
firmware = "bios"
ide0:0.present = "TRUE"
ide0:0.deviceType = "cdrom-image"
ide0:0.fileName = "rum.iso"
ide0:0.startConnected = "TRUE"
ide1:0.present = "TRUE"
ide1:0.fileName = "rum-data.vmdk"
```

These filenames assume all three files are together. Use the generated preset
if you do not already have a VM configuration to preserve. See VMware's
[VMX editing instructions](https://knowledge.broadcom.com/external/article/311480/editing-the-vmx-file-of-a-vmware-worksta.html)
for how configuration files are managed.

## VirtualBox

1. Choose **New**, name the VM `rum`, and select **Other / Other/Unknown
   (32-bit)**. Skip unattended installation.
2. Set memory to **64 MiB**, CPUs to **1**, and choose not to create a hard disk.
3. Under **Settings → System**, use the **PIIX3** chipset, disable **UEFI** and
   **I/O APIC**, and put **Optical** first in the boot order.
4. Under **Display**, select **VBoxVGA**, set video memory to **8 MiB**, and
   disable **3D acceleration**.
5. Under **Storage**, add an **IDE controller** with type **PIIX4**. Remove any
   unused SATA controller from this new VM.
6. Add an optical drive containing `rum.iso` at **IDE Primary Master**.
7. Add an existing hard disk, select `rum-data.vdi`, and set it to
   **IDE Secondary Master**.
8. Keep the pointing device as **PS/2 Mouse**. The normal legacy PS/2 keyboard
   works without Guest Additions.
9. Start the VM and click inside the display. On Windows/Linux, the default
   **Host key is Right Ctrl**, which releases captured input.

Use the GUI's Expert settings view if a controller or firmware option is hidden.
The exact IDE addresses are:

| Medium | IDE port | Device | GUI label |
| --- | --- | --- | --- |
| `rum.iso` | 0 | 0 | Primary Master |
| `rum-data.vdi` | 1 | 0 | Secondary Master |

For command-line configuration, these are the corresponding
[`VBoxManage` storage attachments](https://docs.oracle.com/en/virtualization/virtualbox/7.2/user/vboxmanage.html):

```powershell
$vbox = 'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe'
& $vbox storagectl rum --name IDE --add ide --controller PIIX4
& $vbox storageattach rum --storagectl IDE --port 0 --device 0 --type dvddrive --medium "$PWD\build\vm-images\rum.iso"
& $vbox storageattach rum --storagectl IDE --port 1 --device 0 --type hdd --medium "$PWD\build\vm-images\rum-data.vdi"
```

Run these only for an already created, powered-off `rum` VM that does not yet
have those devices configured. If its IDE controller has another name, use that
name and omit the `storagectl` command. The GUI steps above also configure BIOS,
memory, CPU, and display settings; the attachment commands alone do not.

## Check the disk and restart

At rum's prompt:

```text
/> ls /
README.TXT
/> cat /README.TXT
Hello from rum's installed system disk!
This file survives VM restarts.
/> write /NOTE.TXT hello from rum
/> cat /NOTE.TXT
hello from rum
```

Wait for the prompt after the write, power off the VM, and start it again with
the same ISO and data disk attached. `cat /NOTE.TXT` should recover the
same text. rum has no guest shutdown command yet; a completed filesystem write
flushes before returning success. FAT16 is not journaled, so avoid powering off
while a file operation is active. See [FAT16 write guarantees](fat16.md).

Do not install VMware Tools or VirtualBox Guest Additions: rum does not provide
the interfaces needed by those guest packages. Its console, keyboard, and disk
access use the emulated legacy hardware directly.

## Troubleshooting

- **No bootable medium:** attach the ISO, enable connection at power-on, use
  BIOS, and put the optical drive first. The system disk requires the ISO to start the kernel.
- **The shell works but `/` is unavailable:** verify IDE **1:0 / Secondary
  Master** and use the prepared FAT16 image. A blank disk or an MBR-partitioned
  volume is not the supported disk layout.
- **Keyboard input is missing:** click the display and use the legacy PS/2
  keyboard. Disable a USB-only keyboard configuration if you selected one.
- **No files survive reboot:** use the same disk and normal writable mode.
  Immutable/nonpersistent disks or restoring a snapshot can discard changes.
- **Only the recovery shell starts:** check that you attached the complete
  current ISO and a prepared disk containing `/RUM/SHELL.ELF`.

The settings above were boot-tested on Windows with VMware Workstation
17.5.2 and VirtualBox 7.2.4. VirtualBox additionally passed keyboard-driven
listing, file creation/readback, and readback after a separate boot. Image
conversion was checked byte for byte with `qemu-img`; data-disk tests used
disposable copies, leaving the supplied images unchanged.
