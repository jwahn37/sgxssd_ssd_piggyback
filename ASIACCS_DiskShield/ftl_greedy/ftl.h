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
// DRAM buffers
/////////////////

#define NUM_RW_BUFFERS		((DRAM_SIZE - DRAM_BYTES_OTHER) / BYTES_PER_PAGE - 1)
#define NUM_RD_BUFFERS		(((NUM_RW_BUFFERS / 8) + NUM_BANKS - 1) / NUM_BANKS * NUM_BANKS)
#define NUM_WR_BUFFERS		(NUM_RW_BUFFERS - NUM_RD_BUFFERS)
#define NUM_COPY_BUFFERS	(NUM_BANKS_MAX)
#define NUM_FTL_BUFFERS		(NUM_BANKS)
#define NUM_HIL_BUFFERS		1
#define NUM_TEMP_BUFFERS	1

#define DRAM_BYTES_OTHER	((NUM_COPY_BUFFERS + NUM_FTL_BUFFERS + NUM_HIL_BUFFERS + NUM_TEMP_BUFFERS) * BYTES_PER_PAGE \
+ BAD_BLK_BMP_BYTES + PAGE_MAP_BYTES + VCOUNT_BYTES + FLUSH_MAP_BYTES  \
+ DS_INODE_BYTES + DS_FTL_BYTES + DS_NHASH_BYTES + EVENTQ_BYTES + RDAFWRQ_BYTES + HMAC_SIZE) //DiskShield

#define WR_BUF_PTR(BUF_ID)	(WR_BUF_ADDR + ((UINT32)(BUF_ID)) * BYTES_PER_PAGE)
#define WR_BUF_ID(BUF_PTR)	((((UINT32)BUF_PTR) - WR_BUF_ADDR) / BYTES_PER_PAGE)
#define RD_BUF_PTR(BUF_ID)	(RD_BUF_ADDR + ((UINT32)(BUF_ID)) * BYTES_PER_PAGE)
#define RD_BUF_ID(BUF_PTR)	((((UINT32)BUF_PTR) - RD_BUF_ADDR) / BYTES_PER_PAGE)

#define _COPY_BUF(RBANK)	(COPY_BUF_ADDR + (RBANK) * BYTES_PER_PAGE)
#define COPY_BUF(BANK)		(_COPY_BUF(REAL_BANK(BANK)))
#define FTL_BUF(BANK)       (FTL_BUF_ADDR + ((BANK) * BYTES_PER_PAGE))

///////////////////////////////
// DRAM segmentation
///////////////////////////////

//Virtual DRAM address라고 봐야함
//Real DRAM address는 ecc도 포함되어있다.

#define RD_BUF_ADDR			(DRAM_BASE)										// base address of SATA read buffers
#define RD_BUF_BYTES		(NUM_RD_BUFFERS * BYTES_PER_PAGE)

#define WR_BUF_ADDR			(RD_BUF_ADDR + RD_BUF_BYTES)					// base address of SATA write buffers
#define WR_BUF_BYTES		(NUM_WR_BUFFERS * BYTES_PER_PAGE)

#define COPY_BUF_ADDR		(WR_BUF_ADDR + WR_BUF_BYTES)					// base address of flash copy buffers
#define COPY_BUF_BYTES		(NUM_COPY_BUFFERS * BYTES_PER_PAGE)

#define FTL_BUF_ADDR		(COPY_BUF_ADDR + COPY_BUF_BYTES)				// a buffer dedicated to FTL internal purpose
#define FTL_BUF_BYTES		(NUM_FTL_BUFFERS * BYTES_PER_PAGE)

#define HIL_BUF_ADDR		(FTL_BUF_ADDR + FTL_BUF_BYTES)					// a buffer dedicated to HIL internal purpose
#define HIL_BUF_BYTES		(NUM_HIL_BUFFERS * BYTES_PER_PAGE)

#define TEMP_BUF_ADDR		(HIL_BUF_ADDR + HIL_BUF_BYTES)					// general purpose buffer
#define TEMP_BUF_BYTES		(NUM_TEMP_BUFFERS * BYTES_PER_PAGE)

#define BAD_BLK_BMP_ADDR	(TEMP_BUF_ADDR + TEMP_BUF_BYTES)				// bitmap of initial bad blocks
#define BAD_BLK_BMP_BYTES	(((NUM_VBLKS / 8) + DRAM_ECC_UNIT - 1) / DRAM_ECC_UNIT * DRAM_ECC_UNIT)

#define PAGE_MAP_ADDR		(BAD_BLK_BMP_ADDR + BAD_BLK_BMP_BYTES)			// page mapping table

#define PAGE_MAP_BYTES		((NUM_LPAGES * sizeof(UINT32) + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR) //no key
//#define PAGE_MAP_BYTES_WITH_KEY		((NUM_LPAGES * (2 * sizeof(UINT32)) + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR) //key 필드 추가

#define VCOUNT_ADDR			(PAGE_MAP_ADDR + PAGE_MAP_BYTES)  
#define VCOUNT_BYTES		((NUM_BANKS * VBLKS_PER_BANK * sizeof(UINT16) + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)

//key adding flush table
#define FLUSH_MAP_ADDR  (VCOUNT_ADDR + VCOUNT_BYTES)
//#define FLUSH_MAP_BYTES (((NUM_LPAGES / 8) + 1) + (DRAM_ECC_UNIT-1)) / DRAM_ECC_UNIT * DRAM_ECC_UNIT  //DRAM_ECC_UNIT의 배수여야함
//#define BYTES_PER_MAP_ENTRY_KEY 8 //key 4bytes, vpn 4bytes
//#define MAP_ENTRY_PER_PAGE (BYTES_PER_PAGE / BYTES_PER_MAP_ENTRY_KEY)   //64*64개

#define PAGES_PER_PAGE_MAP ((PAGE_MAP_BYTES + BYTES_PER_PAGE -1) / BYTES_PER_PAGE)
//#define FLUSH_MAP_BYTES ((((PAGES_PER_PAGE_MAP / 8) + 1 ) + (DRAM_ECC_UNIT - 1)) / DRAM_ECC_UNIT * DRAM_ECC_UNIT )//DRAM_ECC_UNIT 의 배수여야함
#define FLUSH_MAP_BYTES ((((PAGES_PER_PAGE_MAP / 4) + 1 ) + (DRAM_ECC_UNIT - 1)) / DRAM_ECC_UNIT * DRAM_ECC_UNIT )//DRAM_ECC_UNIT 의 배수여야함
//flush

//FTL-SGX memory allocation
#define DS_MAX_FILE (1024)
#define DS_INODE_SIZE (DRAM_ECC_UNIT)

#define DS_MAX_FTL (2048)
#define DS_FTL_SIZE (128)   //ECC는 128bytes여야함.

#define BUCKET_SIZE (256)
#define DS_MAX_NHASH (1024)
#define NAME_LEN 16
#define DS_NHASH_SIZE (4+4+NAME_LEN)

#define DS_INODE_ADDR (FLUSH_MAP_ADDR + FLUSH_MAP_BYTES)
#define DS_INODE_BYTES ((DS_MAX_FILE * DS_INODE_SIZE + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)

#define DS_FTL_ADDR (DS_INODE_ADDR + DS_INODE_BYTES)
#define DS_FTL_BYTES ((DS_MAX_FTL * DS_FTL_SIZE + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)   //1MB can 2048 chunks , deal with 2048*4MB = 8GB datas

//element만 저장
#define DS_NHASH_ADDR (DS_FTL_ADDR + DS_FTL_BYTES)
#define DS_NHASH_BYTES ((DS_MAX_NHASH * DS_NHASH_SIZE + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)   

//evnt queue를 DRAM으로 할당해야함 (sram overflow 문제해소)

#define EVEQ_SIZE 128
#define EVEQ_ELE 16
#define EVENTQ_ADDR (DS_NHASH_ADDR + DS_NHASH_BYTES)
#define EVENTQ_BYTES (EVEQ_SIZE*EVEQ_ELE)   //128 : QSIZE, 16 ; elements size // 그래서 총 2KB 필요.


#define RDAFWRQ_SIZE 128
#define RDAFWRQ_ELE 16
#define RDAFWRQ_ADDR (EVENTQ_ADDR + EVENTQ_BYTES)
#define RDAFWRQ_BYTES (RDAFWRQ_SIZE * RDAFWRQ_ELE)

#define HMAC_BUFF (RDAFWRQ_ADDR + RDAFWRQ_BYTES)
#define HMAC_SIZE (4096+512)

//비트맵은 SRAM에서 관리할래.

//Suppose : 
// MAX ENCLAVE is less than 256
// MAX FILES is less than 11915
// MAX FILE SIZE is less than 8GB
//then we need 1MB + 256KB + 2KB
//#define FILE_FTL_BITMAP_SIZE 250

//#define ENCLAVE_KEY_ADDR (FLUSH_MAP_ADDR + FLUSH_MAP_BYTES)
//#define ENCALVE_KEY_BYTES ((2*1024 + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)  //256(NUMOFENCLAVE) * 8 bytes

//#define ENCLAVE_FILES_ADDR (ENCLAVE_KEY_ADDR+ENCALVE_KEY_BYTES)
//#define ENCLAVE_FILES_BYTES ((256*1024 + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR) // 256KB / 24B = 10240 can be stored

//#define FILE_FTL_ADDR (ENCLAVE_FILES_ADDR + ENCLAVE_FILES_BYTES)
//#define FILE_FTL_BYTES ((1024*1024+ BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR) //1MB can 2048 chunks , deal with 2048*4MB = 8GB datas

//#define FILE_FTL_BITMAP_ADDR (FILE_FTL_ADDR + FILE_FTL_BYTES)   //this is to allocate and free FILE_FTLs
//#define FILE_FTL_BITMAP_BYTES (((FILE_FTL_BITMAP_SIZE) + (DRAM_ECC_UNIT - 1)) / DRAM_ECC_UNIT * DRAM_ECC_UNIT) //250bytes = 2000bit // there are 2000's file ftl. 

//#define BLKS_PER_BANK		VBLKS_PER_BANK



///////////////////////////////
// FTL public functions
///////////////////////////////

void ftl_open(void);
void ftl_read(UINT32 const lba, UINT32 const num_sectors); 
void ftl_write(UINT32 const lba, UINT32 const num_sectors); 

void DS_ftl_write(SGX_LBA sgx_lba, UINT32 size);
void DS_ftl_read(SGX_LBA sgx_lba,  UINT32 size);

void ftl_test_write(UINT32 const lba, UINT32 const num_sectors);
void ftl_flush(void);
void ftl_isr(void);

#endif //FTL_H
