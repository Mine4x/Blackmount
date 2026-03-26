#include "errno.h"

uint64_t serror(int err)
{
    return -(uint64_t)err;
}