#include <stddef.h>
#include <stdbool.h>
#include <heap.h>
#include <hal/vfs.h>

#define INPUT_BUFFER_SIZE 128

typedef struct {
    char *buffer;
    size_t length;
    size_t capacity;
    int fd;
} InputManager;

static InputManager *input = NULL;

// Initialize input manager
bool Input_Init(int file_descriptor)
{
    if (input != NULL)
        return false;

    input = kmalloc(sizeof(InputManager));
    if (!input)
        return false;

    input->buffer = kmalloc(INPUT_BUFFER_SIZE);
    if (!input->buffer) {
        kfree(input);
        input = NULL;
        return false;
    }

    input->length = 0;
    input->capacity = INPUT_BUFFER_SIZE;
    input->fd = file_descriptor;

    // Reset file position to start
    VFS_Set_Pos(input->fd, 0);

    return true;
}

// Free input manager
void Input_Free(void)
{
    if (!input)
        return;

    Input_Clear(); // reset buffer + file

    kfree(input->buffer);
    kfree(input);
    input = NULL;
}

// Add character (writes ONLY the new char immediately)
bool Input_AddChar(char c)
{
    if (!input || input->length >= input->capacity)
        return false;

    input->buffer[input->length++] = c;

    // Write only the new character
    if (input->fd >= 0) {
        int written = VFS_Write(input->fd, 1, &c);
        if (written != 1)
            return false;
    }

    return true;
}

// Remove last character (does NOT modify file automatically)
bool Input_RmChar(void)
{
    if (!input || input->length == 0)
        return false;

    input->length--;

    // Move file position back by 1
    if (input->fd >= 0) {
        size_t new_pos = input->length;
        VFS_Set_Pos(input->fd, new_pos);
    }

    return true;
}

// Clear buffer AND reset file contents
void Input_Clear(void)
{
    if (!input)
        return;

    input->length = 0;

    if (input->fd >= 0) {
        // Reset file to beginning
        VFS_Set_Pos(input->fd, 0);

        // Overwrite entire previous buffer with zeros
        char zero = 0;
        for (size_t i = 0; i < input->capacity; i++)
            VFS_Write(input->fd, 1, &zero);

        // Reset back to beginning again
        VFS_Set_Pos(input->fd, 0);
    }
}

// Copy buffer safely
size_t Input_GetBuffer(char *out, size_t out_size)
{
    if (!input || !out || out_size == 0)
        return -1;

    size_t n = (input->length < out_size - 1) ? input->length : out_size - 1;

    for (size_t i = 0; i < n; i++)
        out[i] = input->buffer[i];

    out[n] = '\0';
    return n;
}

size_t Input_get_length(void)
{
    if (!input)
        return -1;

    return input->length;
}
