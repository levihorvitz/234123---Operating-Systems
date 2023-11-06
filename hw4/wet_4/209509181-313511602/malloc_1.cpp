#include <unistd.h>

#define SIZE_LIMIT (1e8)

void* _sbrk(intptr_t delta)
{
    static void* program_break = sbrk(0);
    if (delta == 0) {
        return program_break;
    }
    void* sbrk_break = sbrk(0);
    if (sbrk_break == (void*)(-1)) {
        return sbrk_break;
    }
    if ((intptr_t)program_break + delta > (intptr_t)sbrk_break) {
        sbrk_break = sbrk((intptr_t)program_break + delta - (intptr_t)sbrk_break);
        if (sbrk_break == (void*)(-1)) {
            return sbrk_break;
        }
    }
    void* prev_break = program_break;
    program_break = (void*)((intptr_t)program_break + delta);
    return prev_break;
}

void* smalloc(size_t size)
{
    if (size == 0 || size > SIZE_LIMIT) {
        return NULL;
    }
    void* prev_program_break = _sbrk(size);
    if (prev_program_break == (void*)(-1)) {
        return NULL;
    }
    return prev_program_break;
}
