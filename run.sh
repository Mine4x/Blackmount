qemu-system-i386 \
  -debugcon stdio \
  -fda build/main_floppy.img \
  -hda build/harddisk.img \
  -boot a
