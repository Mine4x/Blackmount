#pragma once

#include <block/block.h>
#include <stdint.h>

block_device_t* image_create_blockdev(const char* name, const char* mod_name);