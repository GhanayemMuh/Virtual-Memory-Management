#include <unistd.h>
#include <assert.h>
#include <cstring>
#include <cstdlib>
#include <math.h>
#include <sys/mman.h>

#define P3_MAX_ALLOC 100000000
#define BUFFER_OVERFLOW_EXIT_VAL 0xdeadbeef
#define MIN_ORDER 0
#define MAX_ORDER 10


struct MallocMetadata {
    int m_cookie;
    bool m_is_free;
    size_t m_size;
    int m_order;
    MallocMetadata* m_next;
    MallocMetadata* m_prev;
    MallocMetadata* m_next_free;
    MallocMetadata* m_prev_free;
};/* next_free and prev_free in struct mallocmetadata are uninitialized for used blocks */
MallocMetadata* start_meta_data = NULL;
MallocMetadata* start_mmap_list = NULL;
MallocMetadata* end_mmap_list = NULL;
size_t free_blocks = 0;
size_t free_bytes = 0;
size_t allocated_blocks = 0;
size_t allocated_bytes = 0;
int cookie_value = rand();
size_t init_block_size = 128*1024; // Size of each block (128 KB)
size_t total_size = init_block_size*32; // Total size to allocate initially
bool initialized = false;
MallocMetadata* freeLists[MAX_ORDER+1]; // array of free lists of different orders.

void _initialize_block(MallocMetadata* node, bool is_free, size_t size);
MallocMetadata* _find_buddy(MallocMetadata* block);

void initialize_list(){
    void* initial_block = sbrk(0); // Get the current program break
    size_t misalignment = (size_t)initial_block % init_block_size; // Calculate the misalignment

    if (misalignment != 0) {
        // If the current program break is not aligned, increment it to the next multiple of alignment_size
        sbrk(init_block_size - misalignment);
    }

    for (int i = 0; i <= MAX_ORDER; ++i) {
        freeLists[i] = NULL;
    }

    MallocMetadata* curr_node = NULL;
    MallocMetadata* prev_node = NULL;
    for (int i = 0; i < 32; ++i) {
        curr_node = (MallocMetadata*)sbrk(init_block_size);
        _initialize_block(curr_node, false, init_block_size - sizeof(MallocMetadata));
        curr_node->m_prev = prev_node;
        if(prev_node != NULL){
            prev_node->m_next = curr_node; // Link the previous node's next to the current node
        }
        prev_node = curr_node;

        if (i == 0) {
            freeLists[MAX_ORDER] = curr_node; // Initialize the head of the list
        }
    }
	allocated_blocks = 32;
	allocated_bytes = total_size - 32*sizeof(MallocMetadata);
    free_blocks = 32;
    free_bytes = total_size - 32*sizeof(MallocMetadata);
    initialized = true;
}




void* smalloc(size_t size);
void* scalloc(size_t num, size_t size);
void sfree(void* p);
void* srealloc(void* oldp, size_t size);
size_t _num_free_blocks();
size_t _num_free_bytes();
size_t _num_allocated_blocks();
size_t _num_allocated_bytes();
size_t _num_meta_data_bytes();
size_t _size_meta_data();


#define getDataAdress(meta) (meta?(void*)(((char*)meta)+sizeof(MallocMetadata)):NULL)
#define getDataAdressNOTNULL(meta) (void*)(((char*)meta)+sizeof(MallocMetadata))
#define getEndOfBlock(meta) ((void*)((char*)meta+meta->m_size+sizeof(MallocMetadata)))
#define isWilderness(meta) (sbrk(0)==getEndOfBlock(meta))
#define MINIMUM_SIZE_FOR_MMAP (128*1024)

/*** FUNCTIONS FOR SORTED LIST OPERATIONS. DO NOT UPDATE ANY GLOBAL COUNTERS. ***/
/* inserts an initilized not-free block to the sorted list.*/
void _insert_in_sorted_list(MallocMetadata* node, int order);
/* Helper function for _insert_in_sorted_list.
Inserts @param node in the sorted list after @param before. if @param before is NULL then inserts at the beginning.*/
void _insert_in_sorted_list_after(MallocMetadata* node, MallocMetadata* before, int order);
/* remove a non-free node from the sorted list.*/
void _remove_from_sorted_list(MallocMetadata* node, int order);
/* update size of block and puts it in the correct location in the sorted list.*/
void _update_block_size(MallocMetadata* node, size_t size);

/*** COMPLETE FUNCTIONS, update global counters as expected. ***/
/* downsizes @param node to @param size if the remainder is large enough, and adds the rest as a free block.
DOESN'T MERGE REMAINDER WITH FREE BLOCK TO THE RIGHT (if exists).
Updates global counters accordingly.*/
MallocMetadata* _block_split(MallocMetadata* block);
/* removes a block from free list and sets it as not free. Updates global counters accordingly.*/
void _unfree_block(MallocMetadata* node);
/* sets block as free and inserts it into free list. returns prev and next free blocks. Updates global counters accordingly.*/
void _free_block(MallocMetadata* node, MallocMetadata** r_prev = NULL, MallocMetadata** r_next = NULL);
/* merges left and right if they are adjacent and non-NULL. returns the right meta data or new merged meta data.
Updates Global Counters Accordingly.*/
MallocMetadata* _merge_two_frees(MallocMetadata* left, MallocMetadata* right);
/* takes a block (already in the list) and adds it to the free list - with coalescing. Updates global counters accordingly. */
MallocMetadata* _free_and_coalesce(MallocMetadata* node);
/* finds the free block in the list that is nearest but before node. returns NULL if doesn't exist*/
MallocMetadata* _find_prior_free(MallocMetadata* node);
/* finds the free block in the list that is nearest but after node. returns NULL if doesn't exist*/
MallocMetadata* _find_subsequent_free(MallocMetadata* node);

/* verifies all metas' cookies, assuming first onw isn't NULL. exits program if cookie is invalid.*/
bool _testCookies(MallocMetadata* meta1);
/* initializes a block with cookie, free status, and size*/
void _initialize_block(MallocMetadata* node, bool is_free, size_t size);
/* returns order of block by size*/
int _block_order(size_t size);

/* allocates a new block with mmap (including adding to list & updating counters) and returns its meta. NULL if failed*/
MallocMetadata* _mmap_allocate(size_t size);

void* smalloc(size_t size){
    if(!initialized)
    {   
        initialize_list();
    }
    if (size == 0 || size > P3_MAX_ALLOC){
        return NULL;
    }
    if (size >= MINIMUM_SIZE_FOR_MMAP) {
        MallocMetadata* new_block = _mmap_allocate(size);
        return getDataAdress(new_block);
    }

    // Search for a free block in the free lists of the appropriate order or higher
    for (int o = MAX_ORDER; o >= 0; --o) {
        if (freeLists[o] != NULL) {
            // Use the first block in the free list
            MallocMetadata* block = freeLists[o];

            // If the block is larger than needed, split it
            while((block -> m_size) / 2 >= sizeof(MallocMetadata) + size){
                block = _block_split(block);
            }

            // Unlink the block from its free list and return its data
            _unfree_block(block);
            block->m_size = size;
            block->m_is_free = false;
            block->m_order = _block_order(size);
            //allocated_blocks ++;
            allocated_bytes += size;
            return getDataAdress(block);
        }
    }

    // If no suitable block was found, return nullptr
    return nullptr;
}




void* scalloc(size_t num, size_t size){
    void* allocated = smalloc(num * size);
    if (allocated == NULL){
        return NULL;
    }
    std::memset(allocated, 0, num * size);
    return allocated;
}

void sfree(void* p) {
    if (p == NULL) {
        return;
    }

    MallocMetadata* block = (MallocMetadata*)((char*)p - sizeof(MallocMetadata));
    assert(block != NULL);
    _testCookies(block);
	if (block -> m_size >= MINIMUM_SIZE_FOR_MMAP) { // block is a mapped memory region
        allocated_blocks--;
        allocated_bytes -= block -> m_size;
        if (block -> m_prev != NULL && _testCookies(block -> m_prev)) {
            block -> m_prev -> m_next = block -> m_next;
        }
        if (block -> m_next != NULL && _testCookies(block -> m_next)) {
            block -> m_next -> m_prev = block -> m_prev;
        }
        if (start_mmap_list == block) {
            start_mmap_list = block -> m_next;
        }
        if (end_mmap_list == block) {
            end_mmap_list = block -> m_prev;
        }
		//allocated_blocks--;
		//free_blocks++;
        munmap((void*)block, sizeof(MallocMetadata) + block -> m_size);
        
    }   else    {
		// Merge with adjacent blocks if possible
		MallocMetadata* buddy = _find_buddy(block);
		while (buddy != NULL && buddy->m_is_free && buddy->m_order == block->m_order) {
			block = _merge_two_frees(block, buddy);
			buddy = _find_buddy(block);
		}
		if(buddy == NULL)
		{
			free_blocks++;
			free_bytes+= block->m_size;
		}
		// Add the block back to the free list
		_insert_in_sorted_list(block, block->m_order);    
 }

}


void* srealloc(void* oldp, size_t size){
    if (size == 0 || size > P3_MAX_ALLOC){
        return NULL;
    }
    if (oldp == NULL){
        return smalloc(size);
    }
    MallocMetadata* old_meta = (MallocMetadata *)((char*) oldp - sizeof(MallocMetadata));
    _testCookies(old_meta);
    assert(old_meta -> m_is_free == false);
    size_t original_size = old_meta -> m_size;
    size_t to_copy = (size < old_meta -> m_size)? size : old_meta -> m_size;

    if (((size >= MINIMUM_SIZE_FOR_MMAP) && (old_meta -> m_size < MINIMUM_SIZE_FOR_MMAP)) ||
    ((size < MINIMUM_SIZE_FOR_MMAP) && (old_meta -> m_size >= MINIMUM_SIZE_FOR_MMAP))){
        void* to_return = smalloc(size);
        if (to_return != NULL){
            std::memmove(to_return, oldp, to_copy);
            sfree(oldp);
        }
        return to_return; 
    }

    if (old_meta -> m_size >= MINIMUM_SIZE_FOR_MMAP){
        assert(size >=  MINIMUM_SIZE_FOR_MMAP);
        if (size == old_meta -> m_size){
            return oldp;
        }
        void* to_return = smalloc(size);
        if (to_return != NULL){
            memmove(to_return, oldp, to_copy);
            sfree(oldp);
        }
        return to_return;
    }

    if (old_meta -> m_size >= size){
        while((old_meta -> m_size) / 2 >= sizeof(MallocMetadata) + size){
            assert(old_meta -> m_size < MINIMUM_SIZE_FOR_MMAP);
            old_meta = _block_split(old_meta);
        }
        return getDataAdress(old_meta);
    }

    MallocMetadata *old_prev_free = _find_prior_free(old_meta), *old_next_free = _find_subsequent_free(old_meta);

    if ( (old_prev_free !=  NULL) && _testCookies(old_prev_free) && (getEndOfBlock(old_prev_free) == (void*)old_meta)  &&
    (( old_prev_free -> m_size + old_meta -> m_size + sizeof(MallocMetadata) >= size))){
        if (old_prev_free -> m_size + old_meta -> m_size + sizeof(MallocMetadata) < size){
            assert((sbrk(0) == getEndOfBlock(old_meta)));
            if (sbrk(size - (old_prev_free -> m_size + old_meta -> m_size + sizeof(MallocMetadata))) == (void*)-1){
                return NULL;
            }
            allocated_bytes += size - (old_prev_free -> m_size + old_meta -> m_size + sizeof(MallocMetadata));
            _update_block_size(old_meta, size - (old_prev_free -> m_size + sizeof(MallocMetadata)));
        }
        _free_block(old_meta);
        old_meta = _merge_two_frees(old_prev_free, old_meta);
        assert(old_meta == old_prev_free);
        memmove(getDataAdressNOTNULL(old_meta), oldp, original_size);
        _unfree_block(old_meta);
        while((old_meta -> m_size) / 2 >= sizeof(MallocMetadata) + size){
            old_meta = _block_split(old_meta);
        }
        return getDataAdress(old_meta);
    }// 1b. merge with left block and add rest if needed and block is wilderness


    if ((old_next_free != NULL) && _testCookies(old_next_free) && (getEndOfBlock(old_meta) == (void*)old_next_free)
    && (old_next_free -> m_size + old_meta -> m_size + sizeof(MallocMetadata) >= size)){
        _free_block(old_meta);
        old_meta = _merge_two_frees(old_meta, old_next_free);
        _unfree_block(old_meta);
        while((old_meta -> m_size) / 2 >= sizeof(MallocMetadata) + size){
            old_meta = _block_split(old_meta);
        }
        return getDataAdress(old_meta);
    }//1c. merging with adjacent right block

    if ((old_next_free != NULL) && (old_prev_free !=  NULL) && (getEndOfBlock(old_meta) == (void*)old_next_free)
    && (getEndOfBlock(old_prev_free) == (void*)old_meta) 
    && (old_next_free -> m_size + old_meta -> m_size + old_prev_free -> m_size + 2*sizeof(MallocMetadata) >= size)){
        old_meta = _free_and_coalesce(old_meta);
        memmove(getDataAdressNOTNULL(old_meta), oldp, original_size);
        _unfree_block(old_meta);
        while((old_meta -> m_size) / 2 >= sizeof(MallocMetadata) + size){
            old_meta = _block_split(old_meta);
        }
        return getDataAdress(old_meta);
    }// 1d. merge with both neighbors.

    //default case:
    void* to_return = smalloc(size);
    if (to_return != NULL){
        std::memmove(to_return, oldp, old_meta -> m_size);
        sfree(oldp);
    }
    return to_return; 
}


size_t _num_free_blocks(){
    return free_blocks;
}
size_t _num_free_bytes(){
    return free_bytes;
}
size_t _num_allocated_blocks(){
    return allocated_blocks;
}
size_t _num_allocated_bytes(){
    return allocated_bytes;
}
size_t _num_meta_data_bytes(){
    return allocated_blocks * sizeof(MallocMetadata);
}
size_t _size_meta_data(){
    return sizeof(MallocMetadata);
}

void _insert_in_sorted_list(MallocMetadata* node, int order){
    _testCookies(node);
    MallocMetadata* current = freeLists[order];
    // If the free list for the given order is empty, or the node's address is lower than the first block's
    if ((current == NULL) || (!_testCookies(current)) || (current > node)){
        // Insert at the beginning of the list
        node->m_next_free = current;
        node->m_prev_free = NULL;
        if (current != NULL) {
            current->m_prev_free = node;
        }
        freeLists[order] = node;
    } else {
        // Traverse the list to find the correct position
        while ((current->m_next_free != NULL) && (_testCookies(current->m_next_free)) && (current->m_next_free < node)) {
            current = current->m_next_free;
        }
        assert(current != NULL && _testCookies(current));

        // Insert after the current block
        node->m_next_free = current->m_next_free;
        node->m_prev_free = current;
        if (current->m_next_free != NULL) {
            current->m_next_free->m_prev_free = node;
        }
        current->m_next_free = node;
    }
}


void _insert_in_sorted_list_after(MallocMetadata* node, MallocMetadata* before, int order){
    if (before == NULL){
        MallocMetadata* start = freeLists[order];
        freeLists[order] = node;
        node -> m_prev_free = NULL;
        node -> m_next_free = start;
        if (start != NULL){
            start -> m_prev_free = node;
        }
    }  else    {
        node -> m_next_free = before -> m_next_free;
        node -> m_prev_free = before;
        if (before -> m_next_free != NULL){
            before -> m_next_free -> m_prev_free = node;
        }
        before -> m_next_free = node;
    }
}

void _remove_from_sorted_list(MallocMetadata* node, int order){
    _testCookies(node);
    assert(node != NULL);
    if (freeLists[order] == node){
        if(node->m_next_free)
            freeLists[order] = node -> m_next_free;
        else
            freeLists[order] = NULL;
    }
    if (node -> m_prev_free != NULL && _testCookies(node -> m_prev_free)){
        node -> m_prev_free -> m_next_free = node -> m_next_free;
    }
    if (node -> m_next_free != NULL && _testCookies(node -> m_next_free)){
        node -> m_next_free -> m_prev_free = node -> m_prev_free;
    }
}


void _update_block_size(MallocMetadata* node, size_t size){
    _remove_from_sorted_list(node, node->m_order);
    node -> m_size = size;
    node->m_order = _block_order(node->m_size);
    _insert_in_sorted_list(node, node->m_order);
}

MallocMetadata* _block_split(MallocMetadata* block){
    _testCookies(block);
    assert(block != NULL);

    size_t size = block->m_size / 2; // divide size by 2
    block->m_size = size;
    block->m_order = _block_order(size);  // Update the order

    // Create the buddy block
    MallocMetadata* buddy = (MallocMetadata*)((char*)block + sizeof(MallocMetadata) + size);

    // Initialize the buddy block and insert it into the free list of the new order
    _initialize_block(buddy, true, size);
    buddy->m_order = _block_order(size);  // Update the order
    buddy->m_next = block->m_next;
    buddy->m_prev = block;
    if (block->m_next != NULL) {
        block->m_next->m_prev = buddy;
    }
    block->m_next = buddy;

    _insert_in_sorted_list(buddy,buddy->m_order);

    block->m_is_free = false;
    free_blocks++;
    allocated_blocks++;
    allocated_bytes -= sizeof(MallocMetadata);

    return block; // Return the address of the resized block.
}




MallocMetadata* _find_prior_free(MallocMetadata* node){
    int order = node->m_order;
    MallocMetadata* iter = freeLists[order];
    if (iter == NULL || iter >= node || (!_testCookies(iter))){
        return NULL;
    }
    assert(iter -> m_is_free);
    while (iter -> m_next_free != NULL && _testCookies(iter -> m_next_free) && iter -> m_next_free < node){
        iter = iter -> m_next_free;
        assert(iter -> m_is_free);
    }
    return iter;
}

MallocMetadata* _find_subsequent_free(MallocMetadata* node){
    int order = node->m_order;
    MallocMetadata* iter = freeLists[order];
    while(iter != NULL && _testCookies(iter) && iter <= node){
        assert(iter -> m_is_free);
        iter = iter -> m_next_free;
    }
    return iter;
}


void _free_block(MallocMetadata* node, MallocMetadata** r_prev, MallocMetadata** r_next){
    _testCookies(node);
    assert(!(node -> m_is_free));
    MallocMetadata* previous = _find_prior_free(node);
    MallocMetadata* next = _find_subsequent_free(node);
    if (r_prev != NULL){
        *r_prev = previous;
    }
    if (r_next != NULL){
        *r_next = next;
    }
    node -> m_is_free = true;
    free_blocks ++;
    free_bytes += node -> m_size;
    //allocated_bytes -= node -> m_size;

    node -> m_next_free = next;
    node -> m_prev_free = previous;
    int order = _block_order(node->m_size);
    if (previous != NULL && _testCookies(previous) && _block_order(previous->m_size) == order){
        previous -> m_next_free = node;
    }   else    {
        freeLists[order] = node;
    }
    if (next != NULL && _testCookies(next) && _block_order(next->m_size) == order){
        next -> m_prev_free = node;
    }
}



MallocMetadata* _free_and_coalesce(MallocMetadata* node){
    MallocMetadata *prev, *next;
    _free_block(node, &prev, &next);
    node = _merge_two_frees(prev, node);
    return _merge_two_frees(node, next);
}

MallocMetadata* _find_buddy(MallocMetadata* block) {
    assert(block != NULL);

    size_t buddy_address = (size_t)block ^ (1 << block->m_order);
    MallocMetadata* buddy = (MallocMetadata*)buddy_address;

    return (block->m_order == buddy->m_order && buddy->m_is_free) ? buddy : NULL;
}


MallocMetadata* _merge_two_frees(MallocMetadata* block1, MallocMetadata* block2) {
    assert(block1 != NULL && block2 != NULL);
    free_blocks --;
    allocated_blocks--;
    free_bytes += sizeof(MallocMetadata);
    allocated_bytes += sizeof(MallocMetadata);
    
    // We are merging, so we should remove them from their original free list
    _remove_from_sorted_list(block1, block1->m_order);
    _remove_from_sorted_list(block2, block2->m_order);

    // Update the first block's size and order
    block1->m_size += block2->m_size + sizeof(MallocMetadata);
    block1->m_order = _block_order(block1->m_size);

    // Update the next and previous blocks
    block1->m_next = block2->m_next;
    if (block1->m_next != NULL) {
        block1->m_next->m_prev = block1;
    }

    // Insert the merged block into the free list of the new order
    _insert_in_sorted_list(block1, block1->m_order);


    return block1;
}




void _unfree_block(MallocMetadata* node){
    _testCookies(node);
    assert(node != NULL);
    int order = node->m_order;
    if (freeLists[order] == node){
        freeLists[order] = node->m_next_free;
    }
    if (node->m_prev_free != NULL && _testCookies(node->m_prev_free)){
        node->m_prev_free->m_next_free = node->m_next_free;
    }
    if (node->m_next_free != NULL && _testCookies(node->m_next_free)){
        node->m_next_free->m_prev_free = node->m_prev_free;
    }
    node->m_is_free = false;
    node->m_next_free = NULL;
    node->m_prev_free = NULL;
    free_blocks--;
    free_bytes  -= (node->m_size);
}


bool _testCookies(MallocMetadata* meta1){
    if (meta1 -> m_cookie != cookie_value){
        exit(BUFFER_OVERFLOW_EXIT_VAL);
    }
    return true;
}

void _initialize_block(MallocMetadata* node, bool is_free, size_t size){
    assert(node != NULL);
    node->m_is_free = is_free;
    node->m_size = size;
    node->m_order = _block_order(node->m_size);
    node->m_cookie = cookie_value;
    node->m_next = NULL;
    node->m_prev = NULL;
    node->m_next_free = NULL;
    node->m_prev_free = NULL;
}


MallocMetadata* _mmap_allocate(size_t size){
    MallocMetadata* new_block = (MallocMetadata*)mmap(NULL, size + sizeof(MallocMetadata),
    PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if ((void*)new_block == MAP_FAILED){
        return NULL;
    }
    _initialize_block(new_block, false, size);
    new_block -> m_prev = end_mmap_list;
    new_block -> m_next = NULL;
    if (end_mmap_list != NULL && _testCookies(end_mmap_list)) {
        end_mmap_list -> m_next = new_block;
    }   else    {
        start_mmap_list = new_block;
    }
    end_mmap_list = new_block;
    allocated_blocks++;
    allocated_bytes += size;
    return new_block;
}

int _block_order(size_t size){
    size_t total_size = size + sizeof(MallocMetadata); // Considering the size of metadata as well
    int order = 0;
    while(total_size > 128){  // the smallest block size
        total_size >>= 1;
        order++;
    }
    return order;
}
