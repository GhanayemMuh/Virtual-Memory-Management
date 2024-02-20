#include <stdio.h>
#include <unistd.h>
#include <string.h>

#define maxAlloc 100000000

typedef struct MallocMetaData{
    size_t size;
    bool is_free;
    MallocMetaData* prev;
    MallocMetaData* next;

} MMD;

class BlockList{
    private:
        MMD* head;
        MMD* tail;
    
    public:
        //meta data.
        size_t num_free_blocks;
        size_t num_free_bytes;
        size_t num_allocated_blocks;
        size_t num_allocated_bytes;

        // main class methods.
        BlockList();
        MMD* findAvailableBlock(size_t wanted_size);
        void* allocateBlockInList(size_t size);
        void insertBlockToList(MMD* mmd);
        void removeBlockFromList(MMD* mmd);


};

BlockList::BlockList():head(nullptr),tail(nullptr),num_free_blocks(0),num_free_bytes(0),num_allocated_blocks(0),num_allocated_bytes(0){}

MMD* BlockList::findAvailableBlock(size_t wanted_size)
{
    MMD* currBlock = head;
    while(currBlock)
    {
        if(currBlock->is_free && currBlock->size >= wanted_size)
            return currBlock;
        currBlock = currBlock->next;
    }
    return nullptr;
}

void* BlockList::allocateBlockInList(size_t size)
{
    MMD* toAllocate  = findAvailableBlock(size);
    if(toAllocate)
    {
        toAllocate->is_free = false;
        //toAllocate->size = size;
        this->num_free_blocks--;
        this->num_free_bytes -= toAllocate->size;
        return toAllocate;
    }
    else
    {
        size_t overall_size = size + sizeof(MMD);
        void* allocateBlock = sbrk(overall_size);
        if(allocateBlock == (void*) -1)
            return nullptr;
        MMD* newBlock = (MMD*)allocateBlock;
        newBlock->is_free = false;
        newBlock->size = size;
        newBlock->prev = nullptr;
        newBlock->next = nullptr;
        insertBlockToList(newBlock);
        this->num_allocated_blocks++;
        this->num_allocated_bytes +=size;
        return allocateBlock;
    }
}

void BlockList::insertBlockToList(MMD* mmd)
{
    if(!head)
    {
        head = mmd;
        tail = mmd;
    }
    else
    {
        tail->next = mmd;
        mmd->prev = tail;
        tail = mmd;
        tail->next = nullptr;
    }
}

void BlockList::removeBlockFromList(MMD* mmd)
{
    if(!mmd->is_free)
    {
        mmd->is_free = true;
        this->num_free_blocks++;
        this->num_free_bytes+=mmd->size;
    }
}



/// Required allocation functions for part2

BlockList blockList = BlockList();
void* smalloc(size_t size)
{
    if(size > maxAlloc || size==0)
        return nullptr;
    void* newBlock = blockList.allocateBlockInList(size);
    return (char*)newBlock+sizeof(MMD); // point to first byte of actual allocated block.
}

void* scalloc(size_t num, size_t size)
{
    int overall_size = num*size;
    if(overall_size ==0 || num*size > maxAlloc)
        return nullptr;
    void* p = smalloc(num*size);
    if(p == nullptr)
        return nullptr;
    memset(p,0,overall_size);
    return p;
}

void sfree(void* p)
{
    if(p==nullptr)
    {
		return;
    }
	MMD* toRemove = (MMD*)((char*)p - sizeof(MMD));
	blockList.removeBlockFromList(toRemove);
}

void* srealloc(void* oldp, size_t size)
{
    if(size == 0 || size > maxAlloc)
        return nullptr;
    if(oldp == nullptr)
        return smalloc(size);
    
    MMD* toReAlloc = (MMD*)((char*)oldp - sizeof(MMD));
    if (toReAlloc->size>= size)
        return oldp;

    void* newp = smalloc(size);
    if(newp == nullptr)
        return nullptr;

    memmove(newp,oldp,size);
    sfree(oldp);
    return newp;
}



// meta data methods.
size_t _num_free_blocks(){return blockList.num_free_blocks;}
size_t _num_free_bytes(){return blockList.num_free_bytes;}
size_t _num_allocated_blocks(){return blockList.num_allocated_blocks;}
size_t _num_allocated_bytes(){return blockList.num_allocated_bytes;}
size_t _num_meta_data_bytes(){return blockList.num_allocated_blocks*sizeof(MMD);}
size_t _size_meta_data(){return sizeof(MMD);}
