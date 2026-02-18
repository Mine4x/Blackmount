include build_scripts/config.mk

.PHONY: all kernel clean always tools_fat iso_image harddisk_image

all: iso_image tools_fat harddisk_image

include build_scripts/toolchain.mk

#
# ISO image
#
iso_image: $(BUILD_DIR)/nbos.iso

$(BUILD_DIR)/nbos.iso: kernel harddisk_image
	@mkdir -p $(BUILD_DIR)/iso
	@echo "--> Copying target files (including Limine)..."
	@cp -r target/* $(BUILD_DIR)/iso/
	@cp $(BUILD_DIR)/harddisk.img $(BUILD_DIR)/iso/hdd.img
	@cp $(BUILD_DIR)/kernel.bin $(BUILD_DIR)/iso/boot
	@echo "--> Creating hybrid BIOS+UEFI ISO image..."
	@xorriso -as mkisofs \
		-R -r -J \
		-b boot/limine-bios-cd.bin \
		-no-emul-boot \
		-boot-load-size 4 \
		-boot-info-table \
		--protective-msdos-label \
		--efi-boot boot/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image \
		$(BUILD_DIR)/iso \
		-o $@
	@echo "--> ISO created: $@"

#
# Hard disk image
#
# Hard disk pretends to be a root drive to test systems.
harddisk_image: $(BUILD_DIR)/harddisk.img

$(BUILD_DIR)/harddisk.img: always
	@dd if=/dev/zero of=$@ bs=512 count=131072 status=none
	@mkfs.ext2 -L NBOSHDD $@ >/dev/null
	@mkdir -p /tmp/nbosmnt
	@sudo mount -o loop $@ /tmp/nbosmnt
	@sudo mkdir -p /tmp/nbosmnt/mydir
	@sudo mkdir -p /tmp/nbosmnt/dev
	@sudo cp test.txt /tmp/nbosmnt/test.txt
	@sudo cp test.txt /tmp/nbosmnt/mydir/test.txt
	@sudo umount /tmp/nbosmnt
	@echo "--> Created: $@"


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
	@$(MAKE) -C src/kernel BUILD_DIR=$(abspath $(BUILD_DIR)) clean
	@rm -rf $(BUILD_DIR)/*