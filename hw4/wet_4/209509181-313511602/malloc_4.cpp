#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define SIZE_LIMIT (1e8)
#define SBRK_LIMIT (128 * 1024 + _size_meta_data()) // 128 KB
#define HUGE_PAGE_LIMIT (4 * 1024 * 1024) // 4MB
#define SCALLOC_HUGE_PAGE_LIMIT (2 * 1024 * 1024) // 2MB
#define REDUNDANT_SIZE (128 + _size_meta_data())
#define IS_REDUNDANT(block, block_size) ((block)->size - (block_size) >= REDUNDANT_SIZE)
#define TAIL_METADATA(block) ((tail_metadata_t*)((uint8_t*)(block) + ((block)->size - sizeof(tail_metadata_t))))
#define IS_SBRK_ALLOC(block) ((block)->size < SBRK_LIMIT)
#define ALLOC_SBRK(block_size) ((block_size) < SBRK_LIMIT)

// We use the next field in head_metadata as a flag to check if the block is inside huge page
typedef enum {
    REGULAR_PAGE,
    HUGE_PAGE
} mmap_page_type_e;

typedef struct head_metadata {
    size_t size;
    size_t is_free;
    struct head_metadata* next;
    struct head_metadata* prev;
} head_metadata_t;

typedef struct {
    uint32_t cookie;
    size_t size;
} tail_metadata_t;

uint32_t global_rand_cookie = 0;
head_metadata_t* sbrk_head = nullptr;
head_metadata_t* sbrk_free_head = nullptr;

size_t free_blocks_num = 0;
size_t free_bytes_num = 0;
size_t allocated_blocks_num = 0;
size_t allocated_bytes_num = 0;

// Challenge 7
size_t _8_bit_align(size_t size)
{
    return (size % 8 != 0) ? (size & (-8)) + 8 : size; // used to align the blocks
}

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
size_t _size_meta_data()
{
    // 48 bytes
    return sizeof(head_metadata_t) + sizeof(tail_metadata_t);
}
size_t _num_meta_data_bytes()
{
    return _size_meta_data() * allocated_blocks_num;
}

void* _sbrk(intptr_t delta)
{
    static void* program_break = sbrk(0);
    if ((size_t)program_break % 8 != 0) {
        sbrk(8 - (size_t)program_break % 8);
        program_break = sbrk(0);
    }
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

// has to be set after setting block head metadata
static void _set_tail(head_metadata_t* block)
{
    if (global_rand_cookie == 0) {
        srand(time(nullptr));
    }
    while (global_rand_cookie == 0) {
        global_rand_cookie = rand();
    }
    tail_metadata_t* tail = TAIL_METADATA(block);
    tail->cookie = global_rand_cookie;
    tail->size = block->size;
}

// Challenge 5
static void _check_cookie(head_metadata_t* block)
{
    tail_metadata_t* tail = TAIL_METADATA(block);
    if (global_rand_cookie != 0 && global_rand_cookie != tail->cookie) {
        exit(0xdeadbeef);
    }
}

static head_metadata_t* _init_sbrk_alloc_block(head_metadata_t* block, size_t block_size, bool alloc)
{
    if (alloc) {
        if (_sbrk(block_size) == (void*)(-1)) {
            return nullptr;
        }
    }
    block->size = block_size;
    block->is_free = false;
    block->next = nullptr;
    block->prev = nullptr;
    _set_tail(block);
    return block;
}

static head_metadata_t* _wilderness_sbrk_block_increase(head_metadata_t* wilderness, size_t block_size)
{
    size_t delta = block_size - wilderness->size;
    if (_sbrk(delta) == (void*)(-1)) {
        return nullptr;
    }
    wilderness->size = block_size;
    _set_tail(wilderness);
    return wilderness;
}

// returns the best fit free block if not found returns nullptr
static head_metadata_t* _find_sbrk_free_block(size_t block_size)
{
    head_metadata_t* min = nullptr;
    head_metadata_t* wilderness = nullptr;
    if (sbrk_free_head == nullptr) {
        return nullptr;
    }
    void* program_break = _sbrk(0);
    for (head_metadata_t* current = sbrk_free_head; current != nullptr; current = current->next) {
        _check_cookie(current);
        // Challenge 0
        if (current->size >= block_size && (min == nullptr || min->size > current->size)) {
            min = current;
        }
        // Challenge 3
        if ((void*)((uint8_t*)current + current->size) == program_break) {
            wilderness = current;
        }
    }
    if (min != nullptr) {
        return min;
    }
    if (wilderness != nullptr) {
        free_bytes_num += block_size - wilderness->size;
        allocated_bytes_num += block_size - wilderness->size;
        return _wilderness_sbrk_block_increase(wilderness, block_size);
    }
    return nullptr;
}

static void _add_sbrk_free_block(head_metadata_t* block)
{
    block->is_free = true;
    block->prev = nullptr;
    if (sbrk_free_head == nullptr || block->size < sbrk_free_head->size || (block->size == sbrk_free_head->size && block < sbrk_free_head)) {
        head_metadata_t* temp = sbrk_free_head;
        sbrk_free_head = block;
        block->next = temp;
        if (temp) {
            temp->prev = block;
        }
        return;
    }
    head_metadata_t* current;
    _check_cookie(sbrk_free_head);
    for (current = sbrk_free_head; current->next != nullptr; current = current->next) {
        head_metadata_t* next = current->next;
        _check_cookie(next);
        if (block->size < next->size || (block->size == next->size && block < next)) {
            block->prev = current;
            block->next = next;
            next->prev = block;
            current->next = block;
            return;
        }
    }
    block->prev = current;
    block->next = nullptr;
    current->next = block;
}

static void _remove_sbrk_free_block(head_metadata_t* block)
{
    block->is_free = false;
    if (sbrk_free_head == block) {
        sbrk_free_head = block->next;
        block->next = nullptr;
        block->prev = nullptr;
        return;
    }
    head_metadata_t* prev = block->prev;
    head_metadata_t* next = block->next;
    if (prev) {
        _check_cookie(prev);
        prev->next = next;
    }
    if (next) {
        _check_cookie(next);
        next->prev = prev;
    }
    block->next = nullptr;
    block->prev = nullptr;
}

static void _init_sbrk_free_block(head_metadata_t* block, size_t block_size)
{
    block->size = block_size;
    block->next = nullptr;
    block->prev = nullptr;
    _set_tail(block);
    _add_sbrk_free_block(block);
}

// Challenge 2
static head_metadata_t* _merge_sbrk_blocks(head_metadata_t* block, bool merge_left = true, bool merge_right = true, bool copy_data = false)
{
    size_t block_size_sum = block->size;
    head_metadata_t* returned_block = block;
    head_metadata_t* left_block = nullptr;
    head_metadata_t* right_block = nullptr;
    if (merge_left && sbrk_head != block) {
        size_t prev_block_size = ((tail_metadata_t*)((uint8_t*)block - sizeof(tail_metadata_t)))->size;
        left_block = (head_metadata_t*)((uint8_t*)block - prev_block_size);
        _check_cookie(left_block);
    }
    if (merge_right && (void*)((uint8_t*)block + block->size) != _sbrk(0)) {
        right_block = (head_metadata_t*)((uint8_t*)block + block->size);
        _check_cookie(right_block);
    }
    if (left_block && left_block->is_free) {
        returned_block = left_block;
        block_size_sum += left_block->size;
        free_blocks_num--;
        allocated_blocks_num--;
        free_bytes_num -= left_block->size - _size_meta_data();
        allocated_bytes_num += _size_meta_data();
        _remove_sbrk_free_block(left_block);
        if (copy_data) {
            memmove((void*)((uint8_t*)left_block + sizeof(head_metadata_t)), (void*)((uint8_t*)block + sizeof(head_metadata_t)), block->size - _size_meta_data());
        }
    }
    if (right_block && right_block->is_free) {
        block_size_sum += right_block->size;
        free_blocks_num--;
        allocated_blocks_num--;
        free_bytes_num -= right_block->size - _size_meta_data();
        allocated_bytes_num += _size_meta_data();
        _remove_sbrk_free_block(right_block);
    }
    return _init_sbrk_alloc_block(returned_block, block_size_sum, false);
}

static head_metadata_t* _sbrk_malloc(size_t block_size)
{
    head_metadata_t* last_block;
    if (sbrk_head) {
        head_metadata_t* last_searched = _find_sbrk_free_block(block_size);
        if (last_searched) {
            free_blocks_num--;
            free_bytes_num -= last_searched->size - _size_meta_data();
            _remove_sbrk_free_block(last_searched);
            // Challenge 1
            if (IS_REDUNDANT(last_searched, block_size)) {
                free_blocks_num++;
                free_bytes_num += last_searched->size - _size_meta_data() - block_size;
                allocated_blocks_num++;
                allocated_bytes_num -= _size_meta_data();
                size_t prev_size = last_searched->size;
                _init_sbrk_alloc_block(last_searched, block_size, false);
                _init_sbrk_free_block((head_metadata_t*)((uint8_t*)last_searched + block_size), prev_size - block_size);
            }
            return last_searched;
        }
    } else {
        sbrk_head = (head_metadata_t*)_sbrk(0);
        if (sbrk_head == (head_metadata_t*)(-1)) {
            sbrk_head = nullptr;
            return nullptr;
        }
    }
    last_block = (head_metadata_t*)_sbrk(0);
    last_block = _init_sbrk_alloc_block(last_block, block_size, true);
    allocated_blocks_num++;
    allocated_bytes_num += block_size - _size_meta_data();
    return last_block;
}

// Challenge 4
static head_metadata_t* _mmap_malloc(size_t block_size, bool force_hugepage = false)
{
    // Challenge 6
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    flags |= (force_hugepage || block_size >= HUGE_PAGE_LIMIT) ? MAP_HUGETLB : 0;
    void* mmap_addr = mmap(nullptr, block_size, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (mmap_addr == (void*)(-1)) {
        return nullptr;
    }
    head_metadata_t* block = (head_metadata_t*)mmap_addr;
    // We use the sbrk function because it fits our needs (we don't call sbrk of course)
    _init_sbrk_alloc_block(block, block_size, false);
    if (force_hugepage || block_size >= HUGE_PAGE_LIMIT) {
        block->next = (head_metadata_t*)HUGE_PAGE;
    } else {
        block->next = (head_metadata_t*)REGULAR_PAGE;
    }
    allocated_blocks_num++;
    allocated_bytes_num += block_size - _size_meta_data();
    return block;
}

void* smalloc(size_t size)
{
    head_metadata_t* block;
    size = _8_bit_align(size);
    if (size == 0 || size > SIZE_LIMIT) {
        return nullptr;
    }
    size_t block_size = size + _size_meta_data();
    if (ALLOC_SBRK(block_size)) {
        block = _sbrk_malloc(block_size);
    } else {
        block = _mmap_malloc(block_size);
    }
    return (block) ? (void*)((uint8_t*)block + sizeof(head_metadata_t)) : nullptr;
}

void* scalloc(size_t num, size_t size)
{
    void* alloc;
    size = _8_bit_align(num * size);
    if (size == 0 || size > SIZE_LIMIT) {
        return nullptr;
    }
    size_t block_size = size + _size_meta_data();
    if (block_size > SCALLOC_HUGE_PAGE_LIMIT + _size_meta_data()) {
        head_metadata_t* block = _mmap_malloc(block_size, true);
        if (block == nullptr) {
            return nullptr;
        }
        alloc = (void*)((uint8_t*)block + sizeof(head_metadata_t));
    } else {
        alloc = smalloc(size);
    }
    if (alloc == nullptr) {
        return nullptr;
    }
    memset(alloc, 0, size);
    return alloc;
}

void _sbrk_free(head_metadata_t* block)
{
    block = _merge_sbrk_blocks(block);
    free_blocks_num++;
    free_bytes_num += block->size - _size_meta_data();
    _add_sbrk_free_block(block);
}

void _mmap_free(head_metadata_t* block_to_free)
{
    allocated_blocks_num--;
    allocated_bytes_num -= block_to_free->size - _size_meta_data();
    munmap((void*)block_to_free, block_to_free->size);
}

void sfree(void* p)
{
    if (p == nullptr) {
        return;
    }
    head_metadata_t* block_to_free = (head_metadata_t*)((uint8_t*)p - sizeof(head_metadata_t));
    _check_cookie(block_to_free);
    if (block_to_free->is_free) {
        return;
    }
    if (IS_SBRK_ALLOC(block_to_free)) {
        _sbrk_free(block_to_free);
    } else {
        _mmap_free(block_to_free);
    }
}

static void* _sbrk_realloc(head_metadata_t* block, size_t block_size)
{
    void* program_break = _sbrk(0);
    // Try to reuse the same block
    if (block->size >= block_size) {
        goto split_block_if_needed;
    }
    // Try to merge with lower address
    block = _merge_sbrk_blocks(block, true, false, true);
    if (block->size >= block_size) {
        goto split_block_if_needed;
    }
    // Is wilderness block
    if ((void*)((uint8_t*)block + block->size) == program_break) {
        allocated_bytes_num += block_size - block->size;
        block = _wilderness_sbrk_block_increase(block, block_size);
        return (void*)((uint8_t*)block + sizeof(head_metadata_t));
    }
    // Try to merge with higher address
    block = _merge_sbrk_blocks(block, false, true, false);
    if (block->size >= block_size) {
        goto split_block_if_needed;
    }
    // Try to merge 3 block all toghether
    block = _merge_sbrk_blocks(block, true, true, true);
    if (block->size >= block_size) {
        return (void*)((uint8_t*)block + sizeof(head_metadata_t));
    }
    // Is wilderness block
    if ((void*)((uint8_t*)block + block->size) == program_break) {
        allocated_bytes_num += block_size - block->size;
        block = _wilderness_sbrk_block_increase(block, block_size);
        return (void*)((uint8_t*)block + sizeof(head_metadata_t));
    }
    // If non of the options worked just allocate and copy to new block
    return nullptr;

split_block_if_needed:
    if (IS_REDUNDANT(block, block_size)) {
        free_blocks_num++;
        free_bytes_num += block->size - block_size - _size_meta_data();
        allocated_blocks_num++;
        allocated_bytes_num -= _size_meta_data();
        size_t prev_size = block->size;
        _init_sbrk_alloc_block(block, block_size, false);
        _init_sbrk_free_block((head_metadata_t*)((uint8_t*)block + block_size), prev_size - block_size);
    }
    return (void*)((uint8_t*)block + sizeof(head_metadata_t));
}

void* srealloc(void* oldp, size_t size)
{
    void* newp;
    size = _8_bit_align(size);
    if (oldp == nullptr) {
        return smalloc(size);
    }
    head_metadata_t* old_block = (head_metadata_t*)((uint8_t*)oldp - sizeof(head_metadata_t));
    if (size == 0 || size > SIZE_LIMIT) {
        return nullptr;
    }
    size_t block_size = size + _size_meta_data();
    if (IS_SBRK_ALLOC(old_block)) {
        newp = _sbrk_realloc(old_block, block_size);
        if (newp) {
            return newp;
        }
    }
    if (old_block->size == block_size) {
        return oldp;
    }
    if (!IS_SBRK_ALLOC(old_block) && old_block->next == (head_metadata_t*)HUGE_PAGE) {
        head_metadata_t* block;
        block = _mmap_malloc(block_size, true);
        if (block == nullptr) {
            return nullptr;
        }
        newp = (block) ? (void*)((uint8_t*)block + sizeof(head_metadata_t)) : nullptr;
    } else {
        newp = smalloc(size);
    }
    if (newp == nullptr) {
        return nullptr;
    }
    memmove(newp, oldp, size);
    sfree(oldp);
    return newp;
}
