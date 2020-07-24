// Copyright 2011 INDILINX Co., Ltd.
//
// This file is part of Jasmine.
//
// Jasmine is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Jasmine is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Jasmine. See the file COPYING.
// If not, see <http://www.gnu.org/licenses/>.
//
// GreedyFTL header file
//
// Author; Sang-Phil Lim (SKKU VLDB Lab.)
//

#ifndef FTL_H
#define FTL_H

/////////////////
// Define for PV-SSD
/////////////////
#define MAC_SIZE 32
#define MAX_FREE_BLKS 10
#define NUM_BUCKETS 5
#define MAX_CHAIN_LEN 5

#define PV_WRITE_NOR 0x48
#define PV_WRITE_EXT 0x49
#define PV_RECOVERY_NOR	0x4A
#define PV_RECOVERY_EXT 0x4B 
#define PV_RECOVERY_ALL_NOR	0x4C
#define PV_RECOVERY_ALL_EXT	0x4D 

#define PV_CREATE 0x65
#define PV_CHANGE (PV_CREATE+1)
#define PV_DELETE (PV_CHANGE+1)
#define PV_RECOVERY (PV_DELETE+1)

struct policy_metadata
{
    UINT32 pid; // Guess we don't need this, cause it'll be index
    UINT32 ret_time;
    UINT32 backup_cycle;
    UINT32 num_version;
};

struct file_metadata
{
    UINT32 pid;
    UINT32 fid;
    UINT32 offset;
};

#if OPTION_COMPRESSION_ON
#define MAX_PAGES_IN_DELTA 5
#else
#define MAX_PAGES_IN_DELTA 1
#endif

struct delta_header
{
    UINT32 prev_vpn;
    UINT32 lpn[5];
    UINT32 WT[5];
    UINT32 pid[5];
    UINT32 fid[5];
    UINT32 offset[5];
};

typedef struct _free_blk_pool 
{ 
    UINT32 front, rear, size; 
    UINT32 array[MAX_FREE_BLKS]; 
} free_blk_pool;

/////////////////
// DRAM buffers
/////////////////

#define NUM_RW_BUFFERS ((DRAM_SIZE - DRAM_BYTES_OTHER) / BYTES_PER_PAGE - 1)
#define NUM_RD_BUFFERS (((NUM_RW_BUFFERS / 8) + NUM_BANKS - 1) / NUM_BANKS * NUM_BANKS)
#define NUM_WR_BUFFERS (NUM_RW_BUFFERS - NUM_RD_BUFFERS)
#define NUM_COPY_BUFFERS NUM_BANKS_MAX
#define NUM_FTL_BUFFERS NUM_BANKS
#define NUM_HIL_BUFFERS 1
#define NUM_TEMP_BUFFERS 1

#define DRAM_BYTES_OTHER ((NUM_COPY_BUFFERS + NUM_FTL_BUFFERS + NUM_HIL_BUFFERS + NUM_TEMP_BUFFERS) * BYTES_PER_PAGE \
                         + BAD_BLK_BMP_BYTES + PAGE_MAP_BYTES + VCOUNT_BYTES + FLUSH_MAP_BYTES + RECLAIMABILITY_BMP_BYTES \
                         + BACKUP_ZONE_BMP_BYTES + BACKUP_PAGE_MAP_BYTES + BACKUP_DELTA_BUFFER_BYTES + BACKUP_CONTENT_TEMP_BYTES \
                         +BACKUP_POLICY_BMP_BYTES)

#define WR_BUF_PTR(BUF_ID) (WR_BUF_ADDR + ((UINT32)(BUF_ID)) * BYTES_PER_PAGE)
#define WR_BUF_ID(BUF_PTR) ((((UINT32)BUF_PTR) - WR_BUF_ADDR) / BYTES_PER_PAGE)
#define RD_BUF_PTR(BUF_ID) (RD_BUF_ADDR + ((UINT32)(BUF_ID)) * BYTES_PER_PAGE)
#define RD_BUF_ID(BUF_PTR) ((((UINT32)BUF_PTR) - RD_BUF_ADDR) / BYTES_PER_PAGE)

#define _COPY_BUF(RBANK) (COPY_BUF_ADDR + (RBANK)*BYTES_PER_PAGE)
#define COPY_BUF(BANK) _COPY_BUF(REAL_BANK(BANK))
#define FTL_BUF(BANK) (FTL_BUF_ADDR + ((BANK)*BYTES_PER_PAGE))

///////////////////////////////
// DRAM segmentation
///////////////////////////////

#define RD_BUF_ADDR DRAM_BASE // base address of SATA read buffers
#define RD_BUF_BYTES (NUM_RD_BUFFERS * BYTES_PER_PAGE)

#define WR_BUF_ADDR (RD_BUF_ADDR + RD_BUF_BYTES) // base address of SATA write buffers
#define WR_BUF_BYTES (NUM_WR_BUFFERS * BYTES_PER_PAGE)

#define COPY_BUF_ADDR (WR_BUF_ADDR + WR_BUF_BYTES) // base address of flash copy buffers
#define COPY_BUF_BYTES (NUM_COPY_BUFFERS * BYTES_PER_PAGE)

#define FTL_BUF_ADDR (COPY_BUF_ADDR + COPY_BUF_BYTES) // a buffer dedicated to FTL internal purpose
#define FTL_BUF_BYTES (NUM_FTL_BUFFERS * BYTES_PER_PAGE)

#define HIL_BUF_ADDR (FTL_BUF_ADDR + FTL_BUF_BYTES) // a buffer dedicated to HIL internal purpose
#define HIL_BUF_BYTES (NUM_HIL_BUFFERS * BYTES_PER_PAGE)

#define TEMP_BUF_ADDR (HIL_BUF_ADDR + HIL_BUF_BYTES) // general purpose buffer
#define TEMP_BUF_BYTES (NUM_TEMP_BUFFERS * BYTES_PER_PAGE)

#define BAD_BLK_BMP_ADDR (TEMP_BUF_ADDR + TEMP_BUF_BYTES) // bitmap of initial bad blocks
#define BAD_BLK_BMP_BYTES (((NUM_VBLKS / 8) + DRAM_ECC_UNIT - 1) / DRAM_ECC_UNIT * DRAM_ECC_UNIT)

#define PAGE_MAP_ADDR (BAD_BLK_BMP_ADDR + BAD_BLK_BMP_BYTES) // page mapping table
#define PAGE_MAP_BYTES ((NUM_LPAGES * sizeof(UINT32) + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)

#define VCOUNT_ADDR (PAGE_MAP_ADDR + PAGE_MAP_BYTES)
#define VCOUNT_BYTES ((NUM_BANKS * VBLKS_PER_BANK * sizeof(UINT16) + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)

// ****** TC-SSD Modification ****** //
// How many entries in a page? : Jasmine 32KB / 4B = 8K Entries in a page
// How many pages should be reserved for entire page map?
#define PAGES_FOR_PAGE_MAP (PAGE_MAP_BYTES + BYTES_PER_PAGE - 1) / BYTES_PER_PAGE

///////////////////////////////
// TC-SSD v1.0: Newly Defined DRAM Segmentation
///////////////////////////////
 
// Selective flush table
// Size of flush map = (the number of pages) bits
#define FLUSH_MAP_ADDR (VCOUNT_ADDR + VCOUNT_BYTES)
#define FLUSH_MAP_BYTES (((PAGES_FOR_PAGE_MAP / 8) + 1) + (DRAM_ECC_UNIT - 1)) / DRAM_ECC_UNIT * DRAM_ECC_UNIT

// Reclaimability
#define RECLAIMABILITY_BMP_ADDR (FLUSH_MAP_ADDR + FLUSH_MAP_BYTES)
#define RECLAIMABILITY_BMP_BYTES (((NUM_VPAGES / 8) + DRAM_ECC_UNIT - 1) / DRAM_ECC_UNIT * DRAM_ECC_UNIT)
// #define RECLAIMABILITY_BMP_BYTES (((NUM_VPAGES / 8) + 1) + (DRAM_ECC_UNIT - 1)) / DRAM_ECC_UNIT * DRAM_ECC_UNIT

// Backup zone block or valid zone block
#define BACKUP_ZONE_BMP_ADDR (RECLAIMABILITY_BMP_ADDR + RECLAIMABILITY_BMP_BYTES)
#define BACKUP_ZONE_BMP_BYTES (((NUM_VBLKS / 8) + DRAM_ECC_UNIT - 1) / DRAM_ECC_UNIT * DRAM_ECC_UNIT)

// Backup zone start address for each lpn
#define BACKUP_PAGE_MAP_ADDR (BACKUP_ZONE_BMP_ADDR + BACKUP_ZONE_BMP_BYTES)
#define BACKUP_PAGE_MAP_BYTES ((NUM_LPAGES * sizeof(UINT32) + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)

// Delta Buffer
#define BACKUP_DELTA_BUFFER_ADDR (BACKUP_PAGE_MAP_ADDR + BACKUP_PAGE_MAP_BYTES)
#define BACKUP_DELTA_BUFFER_BYTES (((NUM_BUCKETS * BYTES_PER_PAGE) + DRAM_ECC_UNIT - 1) / DRAM_ECC_UNIT * DRAM_ECC_UNIT) // Byte allignment

// Temporary Content Buffer
#define BACKUP_CONTENT_TEMP_ADDR (BACKUP_DELTA_BUFFER_ADDR + BACKUP_DELTA_BUFFER_BYTES)
#define BACKUP_CONTENT_TEMP_BYTES (((MAX_CHAIN_LEN * BYTES_PER_PAGE) + DRAM_ECC_UNIT - 1) / DRAM_ECC_UNIT * DRAM_ECC_UNIT) // Byte allignment

// Backup Policy Map if a given logical page has policy or not
#define BACKUP_POLICY_BMP_ADDR (BACKUP_CONTENT_TEMP_ADDR + BACKUP_CONTENT_TEMP_BYTES)
#define BACKUP_POLICY_BMP_BYTES (((NUM_LPAGES / 8) + DRAM_ECC_UNIT - 1) / DRAM_ECC_UNIT * DRAM_ECC_UNIT)



///////////////////////////////
// FTL public functions
///////////////////////////////

void ftl_open(void);
void ftl_read(UINT32 const lba, UINT32 const num_sectors);
void ftl_write(UINT32 const lba, UINT32 const num_sectors, struct file_metadata f);
void ftl_test_write(UINT32 const lba, UINT32 const num_sectors);
void ftl_flush(void);
void ftl_isr(void);

void ftl_policy_update (struct policy_metadata p_info, UINT32 const cmd_type);
void print_policy (struct policy_metadata policy);

#endif //FTL_H