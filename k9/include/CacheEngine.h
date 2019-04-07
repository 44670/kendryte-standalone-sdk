#pragma once

#define CE_USE_FATFS

#include <stdio.h>
#include <assert.h>
#include "bsp.h"
#include "printf.h"
#include "encoding.h"

#ifdef CE_USE_FATFS
#include "ff.h"
void* ceMapFileFatFs(FIL* fp);
#endif

#define CE_PAGE_SIZE (4096)
#define CE_BLOCK_SIZE_IN_PAGES (4)
#define CE_BLOCK_SIZE (CE_PAGE_SIZE * CE_BLOCK_SIZE_IN_PAGES)

#define CE_LV3_PAGE_TABLE_COUNT (32)

#define CE_CACHE_VA_SPACE_SIZE_IN_BLOCKS (512 * CE_LV3_PAGE_TABLE_COUNT)
#define CE_CACHE_VA_SPACE_SIZE_IN_PAGES (CE_CACHE_VA_SPACE_SIZE_IN_BLOCKS * CE_BLOCK_SIZE_IN_PAGES)
#define CE_CACHE_VA_SPACE_SIZE (CE_CACHE_VA_SPACE_SIZE_IN_PAGES * CE_PAGE_SIZE)

#define CE_CACHE_SIZE_IN_BLOCKS ((3072 / 4) / CE_BLOCK_SIZE_IN_PAGES)
#define CE_CACHE_SIZE_IN_PAGES (CE_CACHE_SIZE_IN_BLOCKS * CE_BLOCK_SIZE_IN_PAGES)


void ceResetCacheState();
void ceSetupMMU();
void ceUpdateBlockAge();

#define CE_DEBUG_PRINT printk
#define CE_ERROR_PRINT printk