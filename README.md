# Sagittarius SGS-1

Independent OS built from scratch. No Linux. No Windows. No compromises.

## Status
- [x] MyFS — custom filesystem with CoW + zstd compression
- [x] Kernel — bare metal x86, boots via GRUB2/Multiboot2
- [x] VGA driver — direct 0xB8000 access
- [x] Shell — main + emergency
- [ ] GDT/IDT
- [ ] Paging
- [ ] Userspace
- [ ] dinit

## Build
```bash
mkdir cmake-build-debug && cd cmake-build-debug
cmake .. && ninja sagittarius.elf
qemu-system-x86_64 -cdrom sagittarius.iso -m 256M
```

## Stack
- C++26, NASM, clang
- MyFS: CoW, zstd level 1, extent allocator
- Boot: Multiboot2 + GRUB2
