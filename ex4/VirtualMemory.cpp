#include "VirtualMemory.h"
#include "PhysicalMemory.h"
#include <algorithm>

typedef struct PagesData
{
    int maxCyclical = 0;
    uint64_t cyclicFrame = 0;
    uint64_t cyclicRowToDelete = 0;
    uint64_t evictedPageIndex = 0;
    uint64_t maxFrameIndex = 0;
    uint64_t emptyFrame = -1;
    uint64_t emptyFrameToDelete = -1;
} PagesData;


void clearTable(uint64_t frameIndex)
{
    for (uint64_t i = 0; i < PAGE_SIZE; ++i)
    {
        PMwrite(frameIndex * PAGE_SIZE + i, 0);
    }
}

/**
 * a search function that runs dfs and finds emptyFrames, maxFrameIndex and Cyclic frame
 * @param depth the depth in the tree
 * @param frameIndex the current frame index
 * @param page a PagesData struct object that saves all the
 * @param currFrame the current frame
 * @param pageSwappedIn the page to be swapped in, in case 3.
 * @param currP the currP for the case 3
 * @param frameParent the frameParent of thecurrent frame
 * @param parentRow the parent's row.
 */
void dfs(int depth, uint64_t frameIndex, PagesData &page, uint64_t currFrame, uint64_t pageSwappedIn, uint64_t currP,
         uint64_t frameParent, uint64_t parentRow)
{
    if (frameIndex > page.maxFrameIndex)
    {
        page.maxFrameIndex = frameIndex;
    }

    if (depth > TABLES_DEPTH)
    {
        uint64_t pageDiff = abs((long) (pageSwappedIn - currP));
        int cyclic = std::min((uint64_t)(((uint64_t) NUM_PAGES) - pageDiff), pageDiff);
        if (cyclic > page.maxCyclical)
        {
            page.maxCyclical = cyclic;
            page.cyclicFrame = frameIndex;
            page.cyclicRowToDelete = frameParent * PAGE_SIZE + parentRow;
            page.evictedPageIndex = currP;
        }
        return;
    }
    int numOfEmpty = 0;
    for (int i = 0; i < PAGE_SIZE; i++)
    {
        word_t child;
        PMread(frameIndex * PAGE_SIZE + i, &child);
        if (child == 0)
        {
            numOfEmpty++;
            continue;
        }
        uint64_t nextP = (currP << OFFSET_WIDTH) + i;
        dfs(depth + 1, (uint64_t) child, page, currFrame, pageSwappedIn, nextP, frameIndex, i);
    }
    if (numOfEmpty == PAGE_SIZE && depth > 1 && currFrame != frameIndex)
    {
        page.emptyFrame = frameIndex;
        page.emptyFrameToDelete = frameParent * PAGE_SIZE + parentRow;
        return;
    }
}

/**
 * a funtion that finds an unused frame to write in.
 * @param currFrame the currentframe
 * @param virtualAddress the virtual address given
 * @return the unused frame index.
 */
uint64_t findUnusedFrame(uint64_t currFrame, uint64_t virtualAddress)
{
    PagesData page;
    uint64_t pageSwappedIn = virtualAddress >> OFFSET_WIDTH;
    dfs(1, 0, page, currFrame, pageSwappedIn, 0, 0, 0);
    if ((int)page.emptyFrame != -1 && (int)page.emptyFrameToDelete != -1)
    {
        PMwrite(page.emptyFrameToDelete, 0);
        return page.emptyFrame;
    }
    if (page.maxFrameIndex + 1 < NUM_FRAMES)
    {
        clearTable(page.maxFrameIndex + 1);
        return page.maxFrameIndex + 1;
    }
    PMevict(page.cyclicFrame, page.evictedPageIndex);
    clearTable(page.cyclicFrame);
    PMwrite(page.cyclicRowToDelete, 0);
    return page.cyclicFrame;
}

/**
 * a funtion that extracts the bit in the given position.
 * @param address the address to give the position bit in.
 * @param position the position of the bit in address.
 * @return the bit in the given position on address.
 */
uint64_t extractBit(uint64_t address, int position)
{
    return (((1 << OFFSET_WIDTH) - 1) & (address >> (position)));
}

/**
 * a funtion that finds the correct physical address to write or read in.
 * @param virtualAddress the virtual address
 * @return the physical address.
 */
uint64_t findAddress(uint64_t virtualAddress)
{
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE || virtualAddress < 0)
    {
        return -1;
    }
    uint64_t baseFrameIndex = 0;
    word_t currAddr;
    for (int i = 0; i < TABLES_DEPTH; ++i)
    {
        uint64_t currBit = extractBit(virtualAddress, VIRTUAL_ADDRESS_WIDTH - ((i + 1) * OFFSET_WIDTH));
        PMread(baseFrameIndex * PAGE_SIZE + currBit, &currAddr);
        if (currAddr == 0)
        {
            uint64_t f1 = findUnusedFrame(baseFrameIndex, virtualAddress);
            PMwrite(baseFrameIndex * PAGE_SIZE + currBit, (word_t) f1);
            baseFrameIndex = f1;
            if (i == TABLES_DEPTH - 1)
            {
                PMrestore(f1, virtualAddress >> OFFSET_WIDTH);
            }
            continue;
        }
        baseFrameIndex = currAddr;
    }
    return baseFrameIndex * PAGE_SIZE
           + extractBit(virtualAddress, VIRTUAL_ADDRESS_WIDTH - (TABLES_DEPTH + 1) * OFFSET_WIDTH);
}


void VMinitialize()
{
    clearTable(0);
}


int VMread(uint64_t virtualAddress, word_t *value)
{
    int64_t address = findAddress(virtualAddress);
    if (address == -1)
    {
        return 0;
    }
    PMread(address, value);
    return 1;
}


int VMwrite(uint64_t virtualAddress, word_t value)
{
    int64_t address = findAddress(virtualAddress);
    if (address == -1)
    {
        return 0;
    }
    PMwrite(address, value);
    return 1;
}


