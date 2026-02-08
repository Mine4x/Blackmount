include build_scripts/config.mk

.PHONY: all floppy_image kernel bootloader clean always tools_fat iso harddisk_image

all: floppy_image harddisk_image tools_fat

include build_scripts/toolchain.mk

#
# Floppy image
#
floppy_image: $(BUILD_DIR)/main_floppy.img

$(BUILD_DIR)/main_floppy.img: bootloader kernel
	@dd if=/dev/zero of=$@ bs=512 count=2880 >/dev/null
	@mkfs.fat -F 12 -n "NBOS" $@ >/dev/null
	@dd if=$(BUILD_DIR)/stage1.bin of=$@ conv=notrunc >/dev/null
	@mcopy -i $@ $(BUILD_DIR)/stage2.bin "::stage2.bin"
	@mcopy -i $@ $(BUILD_DIR)/kernel.bin "::kernel.bin"
	@mcopy -i $@ test.txt "::test.txt"
	@mmd -i $@ "::mydir"
	@mcopy -i $@ test.txt "::mydir/test.txt"
	@mmd -i $@ "::config"
	@mcopy -i $@ config "::config/config"
	@echo "--> Created: " $@

#
# Hard disk image
#
harddisk_image: $(BUILD_DIR)/harddisk.img

$(BUILD_DIR)/harddisk.img: always
	@dd if=/dev/zero of=$@ bs=512 count=131072 >/dev/null
	@mkfs.fat -F 32 -n "NBOSHDD" $@ >/dev/null
	@mcopy -i $@ test.txt "::test.txt"
	@mmd -i $@ "::mydir"
	@mcopy -i $@ test.txt "::mydir/test.txt"
	@echo "--> Created: " $@

#
# ISO image
#
iso: $(BUILD_DIR)/os.iso

$(BUILD_DIR)/os.iso: bootloader kernel
	@mkdir -p $(BUILD_DIR)/iso
	@cp $(BUILD_DIR)/main_floppy.img $(BUILD_DIR)/iso/boot.img
	@genisoimage -quiet -V "NBOS" -input-charset iso8859-1 -o $@ -b boot.img -hide boot.img $(BUILD_DIR)/iso
	@echo "--> Created: " $@

#
# ISO Image
#
iso_image: $(BUILD_DIR)/main_cd.iso

$(BUILD_DIR)/main_cd.iso: bootloader kernel
	@mkdir -p $(BUILD_DIR)/iso_root
	@cp $(BUILD_DIR)/stage2.bin $(BUILD_DIR)/iso_root/
	@cp $(BUILD_DIR)/kernel.bin $(BUILD_DIR)/iso_root/
	@cp test.txt $(BUILD_DIR)/iso_root/
	@mkdir -p $(BUILD_DIR)/iso_root/mydir
	@cp test.txt $(BUILD_DIR)/iso_root/mydir/

	@cp $(BUILD_DIR)/stage1.bin $(BUILD_DIR)/iso_root/stage1.bin

	@xorriso -as mkisofs \
		-quiet \
		-R \
		-o $@ \
		-isohybrid-mbr $(BUILD_DIR)/stage1.bin \
		-b stage1.bin \
		-no-emul-boot \
		-boot-load-size 1 \
		-boot-info-table \
		$(BUILD_DIR)/iso_root



#
# Bootloader
#
bootloader: stage1 stage2

stage1: $(BUILD_DIR)/stage1.bin

$(BUILD_DIR)/stage1.bin: always
	@$(MAKE) -C src/bootloader/stage1 BUILD_DIR=$(abspath $(BUILD_DIR))

stage2: $(BUILD_DIR)/stage2.bin

$(BUILD_DIR)/stage2.bin: always
	@$(MAKE) -C src/bootloader/stage2 BUILD_DIR=$(abspath $(BUILD_DIR))

#
# Kernel
#
kernel: $(BUILD_DIR)/kernel.bin

$(BUILD_DIR)/kernel.bin: always
	@$(MAKE) -C src/kernel BUILD_DIR=$(abspath $(BUILD_DIR))

#
# Tools
#
tools_fat: $(BUILD_DIR)/tools/fat

$(BUILD_DIR)/tools/fat: always tools/fat/fat.c
	@mkdir -p $(BUILD_DIR)/tools
	@$(MAKE) -C tools/fat BUILD_DIR=$(abspath $(BUILD_DIR))

#
# Always
#
always:
	@mkdir -p $(BUILD_DIR)

#
# Clean
#
clean:
	@$(MAKE) -C src/bootloader/stage1 BUILD_DIR=$(abspath $(BUILD_DIR)) clean
	@$(MAKE) -C src/bootloader/stage2 BUILD_DIR=$(abspath $(BUILD_DIR)) clean
	@$(MAKE) -C src/kernel BUILD_DIR=$(abspath $(BUILD_DIR)) clean
	@rm -rf $(BUILD_DIR)/*