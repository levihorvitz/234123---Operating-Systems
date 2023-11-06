#include <cstdint>
#include <cstring>
#include <unistd.h>

#define SIZE_LIMIT (1e8)

typedef struct {
    size_t size;
    size_t is_free;
} head_metadata_t;

size_t free_blocks_num = 0;
size_t free_bytes_num = 0;
size_t allocated_blocks_num = 0;
size_t allocated_bytes_num = 0;

size_t _num_free_blocks()
{
    return free_blocks_num;
}
size_t _num_free_bytes()
{
    return free_bytes_num;
}
size_t _num_allocated_blocks()
{
    return allocated_blocks_num;
}
size_t _num_allocated_bytes()
{
    return allocated_bytes_num;
}
size_t _num_meta_data_bytes()
{
    return sizeof(head_metadata_t) * allocated_blocks_num;
}
size_t _size_meta_data()
{
    return sizeof(head_metadata_t);
}

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

head_metadata_t* global_head = nullptr;

// returns the first fit free block if not found returns the last searched block
static head_metadata_t* _find_free_block(size_t block_size)
{
    head_metadata_t* last_searched = global_head;
    void* program_break = _sbrk(0);
    if (program_break == (void*)(-1) || last_searched == nullptr) {
        return nullptr;
    }
    while ((void*)((uint8_t*)last_searched + last_searched->size) < program_break) {
        if (last_searched->is_free && last_searched->size >= block_size) {
            return last_searched;
        }
        last_searched = (head_metadata_t*)((uint8_t*)last_searched + last_searched->size);
    }
    return last_searched;
}

static head_metadata_t* _init_alloc_block(head_metadata_t* block, size_t block_size)
{
    if (_sbrk(block_size) == (void*)(-1)) {
        return nullptr;
    }
    block->size = block_size;
    block->is_free = false;
    return block;
}

void* smalloc(size_t size)
{
    head_metadata_t* last_block;
    if (size == 0 || size > SIZE_LIMIT) {
        return nullptr;
    }
    size_t block_size = size + _size_meta_data();
    if (global_head != nullptr) {
        head_metadata_t* last_searched = _find_free_block(block_size);
        if (last_searched == nullptr) {
            return nullptr;
        }
        if (last_searched->is_free) {
            last_searched->is_free = false;
            free_blocks_num--;
            free_bytes_num -= last_searched->size - _size_meta_data();
            return (void*)((uint8_t*)last_searched + sizeof(head_metadata_t));
        }
        // last search block is not free
        last_block = (head_metadata_t*)((uint8_t*)last_searched + last_searched->size);
        last_block = _init_alloc_block(last_block, block_size);
    } else {
        global_head = (head_metadata_t*)_sbrk(0);
        last_block = global_head;
        last_block = _init_alloc_block(last_block, block_size);
    }
    if (last_block == nullptr) {
        return nullptr;
    }
    allocated_blocks_num++;
    allocated_bytes_num += size;
    return (void*)((uint8_t*)last_block + sizeof(head_metadata_t));
}

void* scalloc(size_t num, size_t size)
{
    void* alloc = smalloc(num * size);
    if (alloc == nullptr) {
        return nullptr;
    }
    memset(alloc, 0, num * size);
    return alloc;
}

void sfree(void* p)
{
    if (p == nullptr) {
        return;
    }
    head_metadata_t* block_to_free = (head_metadata_t*)((uint8_t*)p - sizeof(head_metadata_t));
    if (block_to_free->is_free) {
        return;
    }
    block_to_free->is_free = true;
    free_blocks_num++;
    free_bytes_num += block_to_free->size - _size_meta_data();
}

void* srealloc(void* oldp, size_t size)
{
    if (oldp == nullptr) {
        return smalloc(size);
    }
    head_metadata_t* old_block = (head_metadata_t*)((uint8_t*)oldp - sizeof(head_metadata_t));
    if (size == 0 || size > SIZE_LIMIT) {
        return nullptr;
    }
    size_t block_size = size + _size_meta_data();
    if (old_block->size >= block_size) {
        return oldp;
    }
    void* newp = smalloc(size);
    if (newp == nullptr) {
        return nullptr;
    }
    memmove(newp, oldp, old_block->size - _size_meta_data());
    sfree(oldp);
    return newp;
}
