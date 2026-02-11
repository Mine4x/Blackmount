include build_scripts/config.mk

.PHONY: all kernel clean always tools_fat iso_image

all: iso_image tools_fat

include build_scripts/toolchain.mk

#
# ISO image
#
iso_image: $(BUILD_DIR)/bmos.iso

$(BUILD_DIR)/bmos.iso: kernel
	@mkdir -p $(BUILD_DIR)/iso
	@echo "--> Copying target files (including Limine)..."
	@cp -r target/* $(BUILD_DIR)/iso/
	@cp $(BUILD_DIR)/kernel.bin $(BUILD_DIR)/iso/boot
	@echo "--> Creating ISO image..."
	@xorriso -as mkisofs \
		-b boot/limine-bios-cd.bin \
		-no-emul-boot \
		-boot-load-size 4 \
		-boot-info-table \
		--protective-msdos-label \
		$(BUILD_DIR)/iso \
		-o $@
	@echo "--> ISO created: $@"

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