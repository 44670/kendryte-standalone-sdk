#include "CacheEngine.h"

uint64_t volatile __attribute__((aligned(CE_PAGE_SIZE)))  ceLv1PageTable[(CE_PAGE_SIZE / 8) * (1)];
uint64_t volatile __attribute__((aligned(CE_PAGE_SIZE)))  ceLv2PageTables[(CE_PAGE_SIZE / 8) * (1)];
uint64_t volatile __attribute__((aligned(CE_PAGE_SIZE)))  ceLv3PageTables[(CE_PAGE_SIZE / 8) * (CE_LV3_PAGE_TABLE_COUNT)];
uint64_t  __attribute__((aligned(CE_PAGE_SIZE)))  ceCacheMemory[(CE_PAGE_SIZE / 8) * CE_CACHE_SIZE_IN_PAGES];

uint16_t ceCacheMemoryBlockAge[CE_CACHE_SIZE_IN_BLOCKS];
uint16_t ceCacheMemoryBlockToVBlockId[CE_CACHE_SIZE_IN_BLOCKS];

static const uint64_t ceVABase = 0x100000000ULL;
static int ceIsMapWritable;


#ifdef CE_USE_FATFS
static FIL* ceFatFsFp;
static DWORD ceFatFsFileClusterTable[1024];

void* ceMapFileFatFs(FIL* fp) {
    ceResetCacheState();
    memset(ceFatFsFileClusterTable, 0, sizeof(ceFatFsFileClusterTable));
    ceFatFsFileClusterTable[0] = sizeof(ceFatFsFileClusterTable) / sizeof(DWORD);
    // Cache the cluster table to make random access effecient, FF_USE_FASTSEEK must be set in ffconf.h
    fp->cltbl = ceFatFsFileClusterTable;
    FRESULT ret = f_lseek(fp, CREATE_LINKMAP);
    if (ret != FR_OK) {
        CE_ERROR_PRINT("ceMapFileFatFs: init cluster table failed: %d (maybe the file is too fragmented?)\n", ret);
        return 0;
    }
    ceFatFsFp = fp;
    return (void*)ceVABase;
}

int ceFileReadCallback(uint32_t fileOffset, uint64_t* buf, uint32_t len) {

    //CE_DEBUG_PRINT("ceFileReadCallback(fatfs): %d, %p, %d\n", fileOffset, buf, len);
    UINT bytesRead = 0;
    if (f_lseek(ceFatFsFp, fileOffset) != FR_OK) {
        CE_ERROR_PRINT("ceFileReadCallback(fatfs) f_lseek failed: %d, %p, %d\n", fileOffset, buf, len);
        return -1;
    }
    if (f_read(ceFatFsFp, buf, len, &bytesRead) != FR_OK) {
        CE_ERROR_PRINT("ceFileReadCallback(fatfs) f_read failed: %d, %p, %d\n", fileOffset, buf, len);
        return -1;
    }
    if (bytesRead != len) {
        CE_ERROR_PRINT("ceFileReadCallback(fatfs) bytesRead != len: %d, %p, %d, %d\n", fileOffset, buf, (int) bytesRead,  len);
        return 0;
    }
    return 0;
}
#endif



void ceResetCacheState() {
    memset(ceCacheMemoryBlockAge, 0, sizeof(ceCacheMemoryBlockAge));
    memset((void*)ceLv3PageTables, 0, sizeof(ceLv3PageTables));
    ceIsMapWritable = 0;
    asm volatile ("sfence.vm");
}

static uint64_t ceEncodePTE(uint32_t physAddr, uint32_t flags) {
    assert((physAddr % CE_PAGE_SIZE) == 0);
    return (((uint64_t)physAddr >> 12) << 10) | flags;
}

void ceSetupMMU() {

    CE_DEBUG_PRINT("setup mmu...\n");

    //0 - 0xFFFFFFFF -> mirror to phys
    for (uint32_t i = 0; i < 4; i++) {
        ceLv1PageTable[i] = ceEncodePTE((0x40000000U) * i,  PTE_V | PTE_R | PTE_W | PTE_X | PTE_G | PTE_U);
    }

    //0x100000000 (1GiB) -> lv2
    ceLv1PageTable[4] = ceEncodePTE((uint32_t)ceLv2PageTables, PTE_V | PTE_G | PTE_U  );

    //0x100000000 (2MiB * CE_LV3_PAGE_TABLE_COUNT) -> lv3
    for (uint32_t i = 0; i < CE_LV3_PAGE_TABLE_COUNT; i++) {
        ceLv2PageTables[i] = ceEncodePTE(((uint32_t)ceLv3PageTables) + i * CE_PAGE_SIZE,  PTE_V | PTE_G | PTE_U);
    }

    write_csr(sptbr, (uint64_t)ceLv1PageTable >> 12);

    uint64_t msValue = read_csr(mstatus);
    msValue |= MSTATUS_MPRV | ((uint64_t)VM_SV39 << 24);
    write_csr(mstatus, msValue);

    ceResetCacheState();
}

static inline uint32_t ceVAddrToVBlockId(uintptr_t vaddr) {
    return (vaddr - ceVABase) / (CE_BLOCK_SIZE);
}

static void ceMapVBlockToPhysAddr(uint32_t vBlockId, uint32_t physAddr) {
    uint32_t basePageId = vBlockId * CE_BLOCK_SIZE_IN_PAGES;
    uintptr_t vaddr = 0;
    for (uint32_t i = 0 ; i < CE_BLOCK_SIZE_IN_PAGES; i++) {
        ceLv3PageTables[basePageId + i] = physAddr ? ceEncodePTE(physAddr + i * CE_PAGE_SIZE, PTE_V | PTE_R | PTE_X | PTE_G | PTE_U) : 0;
        vaddr = ceVABase + ((uintptr_t)(basePageId + i) * CE_PAGE_SIZE);
        asm volatile("sfence.vm %0" : "=r"(vaddr));
    }
}

static inline int ceCheckAndSetVBlockAccessFlag(uint32_t vBlockId) {
    int hasAccessed = 0;

    uint32_t basePageId = vBlockId * CE_BLOCK_SIZE_IN_PAGES;
    for (uint32_t i = 0; i < CE_BLOCK_SIZE_IN_PAGES; i++) {
        uint64_t pte = ceLv3PageTables[basePageId + i];
        assert(pte & PTE_V);
        if (pte & PTE_A) {
            // TODO: ensure this operation is atomic
            ceLv3PageTables[basePageId + i] &= (~((uint64_t)PTE_A));
            hasAccessed = 1;
        }
    }
    return hasAccessed;
}


static uint32_t ceFindBlockToRetire() {

    uint16_t maxAge = 0;
    uint32_t maxAgeAt = 0;

    for (uint32_t i = 0; i < CE_CACHE_SIZE_IN_BLOCKS; i++) {
        uint16_t age = ceCacheMemoryBlockAge[i];
        if (age == 0) {
            // an empty block!
            return i;
        }
        if (age >= maxAge) {
            maxAge = age;
            maxAgeAt = i;
        }
    }
    return maxAgeAt;
}


int ceHandlePageFault(uintptr_t vaddr, int isWrite) {
    if (isWrite) {
        if (!ceIsMapWritable) {
            return -1;
        }
    }

    uint32_t cacheBlockId = ceFindBlockToRetire();
    //CE_DEBUG_PRINT("ceHandlePageFault: %p, %d\n", (void*)vaddr, cacheBlockId);
    if (ceCacheMemoryBlockAge[cacheBlockId]) {
        CE_DEBUG_PRINT("retire block: %d\n", (int) ceCacheMemoryBlockToVBlockId[cacheBlockId]);
        // an used block, free it
        ceMapVBlockToPhysAddr(ceCacheMemoryBlockToVBlockId[cacheBlockId], 0);
    }
    ceCacheMemoryBlockAge[cacheBlockId] = 0;

    uint32_t vBlockId = ceVAddrToVBlockId(vaddr);
    uint32_t physAddr = ((uint32_t) ceCacheMemory) + (CE_BLOCK_SIZE * cacheBlockId);
    
    int ret = ceFileReadCallback(vBlockId * CE_BLOCK_SIZE, (uint64_t*)physAddr, CE_BLOCK_SIZE);
    if (ret != 0) {
        CE_ERROR_PRINT("ceHandlePageFault: file read failed, %p, %d\n", (void*)vaddr, ret);
        return -1;
    }

    ceCacheMemoryBlockAge[cacheBlockId] = 1;
    ceCacheMemoryBlockToVBlockId[cacheBlockId] = (uint16_t) vBlockId;
    ceMapVBlockToPhysAddr(vBlockId, physAddr);
    return 0;
}



void ceUpdateBlockAge() {
    for (uint32_t i = 0; i < CE_CACHE_SIZE_IN_BLOCKS; i++) {
        uint16_t age = ceCacheMemoryBlockAge[i];
        if (age == 0) {
            // an empty block!
            continue;
        }
        int hasAccessed = ceCheckAndSetVBlockAccessFlag(ceCacheMemoryBlockToVBlockId[i]);
        if (!hasAccessed) {
            if (age < UINT16_MAX) {
                age ++;
                ceCacheMemoryBlockAge[i] = age;
            }
        } else {
            age = 1;
            ceCacheMemoryBlockAge[i] = age;
        }
        //CE_DEBUG_PRINT("ceUpdateBlockAge: %d, %d\n", i, age);
    }
}

uintptr_t handle_fault_load(uintptr_t cause, uintptr_t epc, uintptr_t regs[32], uintptr_t fregs[32]) {
    uintptr_t badAddr = read_csr(mbadaddr);
    CE_DEBUG_PRINT("handle_fault_load: %p, %p\n", (void*) badAddr, (void*) epc);

    if ((badAddr >= ceVABase) && (badAddr < (ceVABase + CE_CACHE_VA_SPACE_SIZE))) {
        if (ceHandlePageFault(badAddr, 0) == 0) {
            return epc;
        }
    }
    CE_ERROR_PRINT("fault load could not be handled, badAddr: %p, epc: %p\n", (void*) badAddr, (void*) epc);
    sys_exit(1337);
    return epc;
}
