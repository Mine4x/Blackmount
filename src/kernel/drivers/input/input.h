#pragma once
#include <stddef.h>
#include <stdbool.h>

// Initialize the input manager with a file descriptor
bool Input_Init(int file_descriptor);

// Free the input manager
// Automatically clears buffer and file contents
void Input_Free(void);

// Add a character to the buffer
// Immediately writes the character to file
bool Input_AddChar(char c);

// Remove the highest (last) character
// Adjusts internal buffer and file position
bool Input_RmChar(void);

// Clear the buffer and reset file contents
void Input_Clear(void);

// Copy current buffer into 'out'
// out_size must include space for null terminator
// Returns number of bytes copied (excluding null terminator)
size_t Input_GetBuffer(char *out, size_t out_size);

// Get current buffer length
size_t Input_get_length(void);
