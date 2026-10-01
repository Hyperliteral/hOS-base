# Building Documents

Compiling an operating system is not an easy task... The build environment I require for building cavOS is as simple as it gets, only needing a few UNIX utilities and obviously a cross compiler...

> **Warning!** If any step fails, do NOT proceed... Start over (double checking your dependencies) and if the issue persists, create an issue reporting it. Remember, this project is still in development!

## Requirements
- \[*C compiler*]: The kernel is written in C.
- nasm: The kernel includes some small assembly files ~~unfortunately~~.
- parted: For creating the boot disk.
- mkdosfs: For creating the FAT32 filesystem inside that disk.
- fatlabel: For labeling that disk as CAVOS.

Most required packages for compiling on specific distros (listed [below](#dependencies-gnulinux)) are specifically for building the cross-compiler... More info [here](https://wiki.osdev.org/GCC_Cross-Compiler).

## Dependencies: GNU/Linux
Compiling cavOS will work on all Linux distributions, but I've only **done testing** with the options that are **bold**, so pick wisely...

- **Ubuntu:** `sudo apt install -y nasm dosfstools parted bison flex libgmp3-dev libmpc-dev libmpfr-dev texinfo libisl-dev autoconf automake wget clang llvm lld`
- **Fedora:** `sudo dnf install -y nasm xorriso parted dosfstools bison flex gmp-devel libmpc-devel mpfr-devel texinfo autoconf automake patch isl-devel wget clang llvm lld`
- **Arch:** `sudo pacman -S --noconfirm nasm xorriso parted dosfstools gmp libmpc mpfr autoconf automake wget clang llvm lld`

If everything else fails, you got [distrobox](https://github.com/89luca89/distrobox).

## Dependencies: BSD
BSD should work, dependencies are cut out specifically for ease of use especially on BSD,
such as the migration to bmake which is natively used to ship and build most BSDs. (talking about netbsd's bmake, not fmake, not pmake.).

- **FreeBSD:** `sudo pkg install nasm dosfstools bison flex gmp mpc mpfr texinfo isl autoconf automake wget clang llvm lld`
- **DragonflyBSD:** `sudo pkg install nasm dosfstools bison flex gmp mpc mpfr texinfo isl autoconf automake wget clang llvm lld`
- **NetBSD:** `sudo pkgin install nasm dosfstools bison flex gmp mpc mpfr texinfo isl autoconf automake wget clang llvm lld`
- **OpenBSD:** `doas pkg_add nasm bison flex gmp mpc mpfr texinfo isl autoconf automake wget llvm`


## Dependencies: Microsoft Windows
Windows is objectively a shitshow when doing anything even slightly UNIX-related. If you want to get your hands dirty with Cygwin, then that's your choice. I'd suggest going with **WSL2, fully updated**. It's still going to be **extremely buggy**, at least from my experience. You have the option of creating a **headless Linux VM** inside software like VMware or VirtualBox, which might be your best option...

## Dependencies: macOS
I have not done any testing inside macOS. When anyone does so, please edit this file accordingly.

I can imagine that the cross compiler would work fine, but installing Limine by creating loopback devices wouldn't work. **The recommendation of Docker or even virtual machines persists to this OS too.**

## Get the compiler
for compiling, you just need clang. any version will do, you also need clangs lld, 
and the rest of the llvm suite.

## Building
This is an operating system, meaning that it has to be burnt into some sort of medium (disk, USB, floppy drive, somewhere!)

> **Warning!** In case you want to try cavOS on real hardware, while nobody is liable for any damage it might cause, many features might not be available due to a lack of drivers!

For building there are two options (although the second is mostly geared towards developers/tinkerers):
- **Build disk.img, a fully featured pack of the OS that includes Limine, the kernel and any software: `bmake disk`.**
- Build only the kernel.elf file, the HNU kernel: `bmake kernel`.

## Testing
- **QEMU:** `bmake qemu` will launch QEMU with the recommended options on the `disk.img` disk image.
- **VMware products:** `bmake vmware` will convert `disk.img` -> `disk.vmdk`.
- **VirtualBox:** can handle the `disk.vmdk` file, so you can use the same disk image as VMware.
