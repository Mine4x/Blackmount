include build_scripts/config.mk

.PHONY: all kernel clean always tools_fat iso_image harddisk_image apps

all: apps iso_image tools_fat harddisk_image

include build_scripts/toolchain.mk

#
# ISO image
#
iso_image: $(BUILD_DIR)/bmos.iso

$(BUILD_DIR)/bmos.iso: kernel harddisk_image
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
#
harddisk_image: $(BUILD_DIR)/harddisk.img
$(BUILD_DIR)/harddisk.img: always
	@dd if=/dev/zero of=$@ bs=512 count=133120 status=none
	@parted -s $@ mklabel gpt mkpart primary ext2 2048s 133086s
	@mkfs.ext2 -L BMOSHDD -E offset=$$((2048 * 512)) $@ >/dev/null
	@mkdir -p /tmp/bmosmnt
	@sudo mount -o loop,offset=$$((2048 * 512)) $@ /tmp/bmosmnt
	@sudo mkdir -p /tmp/bmosmnt/bin
	@sudo cp -r target/* /tmp/bmosmnt
	@for f in $(BUILD_DIR)/apps/*.bin; do \
		sudo cp $$f /tmp/bmosmnt/bin/$$(basename $${f%.bin}); \
	done
	@sudo umount /tmp/bmosmnt
	@echo "--> Created: $@"


#
# Kernel
#
kernel: $(BUILD_DIR)/kernel.bin

$(BUILD_DIR)/kernel.bin: always
	@$(MAKE) -C src/kernel BUILD_DIR=$(abspath $(BUILD_DIR))

#
# Apps / Userspace
#
apps: $(BUILD_DIR)/apps

$(BUILD_DIR)/apps: always
	@$(MAKE) -C src/apps BUILD_DIR=$(abspath $(BUILD_DIR))

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