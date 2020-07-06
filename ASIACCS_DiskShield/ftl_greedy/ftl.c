// Copyright 2011 INDILINX Co., Ltd.
//
// This file is part of Jasmine.
//
// Jasmine is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Jasmine is distributed in
// the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Jasmine. See the file COPYING.
// If not, see <http://www.gnu.org/licenses/>.
//
// GreedyFTL source file
//
// Author; Sang-Phil Lim (SKKU VLDB Lab.)
//
// - support POR
//  + fixed metadata area (Misc. block/Map block)
//  + logging entire FTL metadata when each ATA commands(idle/ready/standby) was issued
//
#include "jasmine.h"
//#include "ecdsa_sgx.h"
#include "ftl_sgx.h"
//----------------------------------
// macro
//----------------------------------
#define VC_MAX              0xCDCD
#define MISCBLK_VBN         0x1 // vblock #1 <- misc metadata
#define MAPBLKS_PER_BANK    (((PAGE_MAP_BYTES / NUM_BANKS) + BYTES_PER_PAGE - 1) / BYTES_PER_PAGE)    //RAM을 통해 각 뱅크에 맵핑 페이지 수
//#define MAPBLKS_PER_BANK_WITH_KEY    (((PAGE_MAP_BYTES_WITH_KEY / NUM_BANKS) + BYTES_PER_PAGE - 1) / BYTES_PER_PAGE)  //logging / loading pmap tbale
#define DS_INODE_BLKS_PER_BANK (DS_MAX_FILE / NUM_BANKS) //1024/8=128개의 block할당
#define DS_NHASH_BLKS_PER_BANK 1
#define DS_META_BLKS_PER_BANK DS_INODE_BLKS_PER_BANK

//#define META_BLKS_PER_BANK  (1 + 1 + MAPBLKS_PER_BANK) // include block #0, misc block
#define META_BLKS_PER_BANK  (1 + 1 + MAPBLKS_PER_BANK + DS_META_BLKS_PER_BANK) // include block #0, misc block
// the number of sectors of misc. metadata info.
#define NUM_MISC_META_SECT  ((sizeof(misc_metadata) + BYTES_PER_SECTOR - 1)/ BYTES_PER_SECTOR)
#define NUM_VCOUNT_SECT     ((VBLKS_PER_BANK * sizeof(UINT16) + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR)

//----------------------------------
// metadata structure
//----------------------------------
typedef struct _ftl_statistics
{
    UINT32 gc_cnt;
    UINT32 page_wcount; // page write count
}ftl_statistics;

//DiskShield codes

typedef struct _misc_metadata
{
    UINT32 cur_write_vpn; // physical page for new write
    UINT32 cur_miscblk_vpn; // current write vpn for logging the misc. metadata
    UINT32 cur_mapblk_vpn[MAPBLKS_PER_BANK]; // current write vpn for logging the age mapping info.
    UINT32 DS_cur_metablk_vpn[DS_META_BLKS_PER_BANK]; //current DiskShield inode, name hash block.

    UINT32 gc_vblock; // vblock number for garbage collection
    UINT32 free_blk_cnt; // total number of free block count
    
    UINT32 lpn_list_of_cur_vblock[PAGES_PER_BLK]; // logging lpn list of current write vblock for GC
    //DiskShield 이부분 수정해야함.
    SGX_LBA sgx_lpn_list_of_cur_vblock[PAGES_PER_BLK]; //logging sgx lpn list of current write vblock for GC
    //UINT8 is_sgx_lpn[PAGES_PER_BLK];    //lpn이 SGX인지 아닌지 알려주는 맵.
                                        //비트맵이 맞지만, 공간적으로 넉넉해서 바이트단위로 체크
}misc_metadata; // per bank

//----------------------------------
// FTL metadata (maintain in SRAM)
//----------------------------------
static misc_metadata  g_misc_meta[NUM_BANKS];
static ftl_statistics g_ftl_statistics[NUM_BANKS];
static UINT32		  g_bad_blk_count[NUM_BANKS];

// SATA read/write buffer pointer id
UINT32 				  g_ftl_read_buf_id;
UINT32 				  g_ftl_write_buf_id; //page단위의 id. 밑단에서 write끝냐면 +1. WRITE_BUFFER위치.

//----------------------------------
// NAND layout
//----------------------------------
// block #0: scan list, firmware binary image, etc.
// block #1: FTL misc. metadata
// block #2 ~ #31: page mapping table
// block #32: a free block for gc
// block #33~: user data blocks


//----------------------------------
// Diskshield NAND layout 
//----------------------------------
// block #0: scan list, firmware binary image, etc.
// block #1: FTL misc. metadata
// block #2 ~ #31: page mapping table
// block #32: a free block for gc
// if (BANK#0)  then block #33 : DS_NAMEHASH, block #34 ~ #160 : DS_INODE
// else         then block #33 ~ #160 : DS_INODE
// block #161~ : user data blocks



//----------------------------------
// macro functions
//----------------------------------
#define is_full_all_blks(bank)  (g_misc_meta[bank].free_blk_cnt == 1)
#define inc_full_blk_cnt(bank)  (g_misc_meta[bank].free_blk_cnt--)
#define dec_full_blk_cnt(bank)  (g_misc_meta[bank].free_blk_cnt++)
#define inc_mapblk_vpn(bank, mapblk_lbn)    (g_misc_meta[bank].cur_mapblk_vpn[mapblk_lbn]++)
#define inc_miscblk_vpn(bank)               (g_misc_meta[bank].cur_miscblk_vpn++)

// page-level striping technique (I/O parallelism)
#define get_num_bank(lpn)             ((lpn) % NUM_BANKS)
#define get_bad_blk_cnt(bank)         (g_bad_blk_count[bank])
#define get_cur_write_vpn(bank)       (g_misc_meta[bank].cur_write_vpn)
#define set_new_write_vpn(bank, vpn)  (g_misc_meta[bank].cur_write_vpn = vpn)
#define get_gc_vblock(bank)           (g_misc_meta[bank].gc_vblock)
#define set_gc_vblock(bank, vblock)   (g_misc_meta[bank].gc_vblock = vblock)
#define set_lpn(bank, page_num, lpn)  (g_misc_meta[bank].lpn_list_of_cur_vblock[page_num] = lpn)
#define get_lpn(bank, page_num)       (g_misc_meta[bank].lpn_list_of_cur_vblock[page_num])

#define sgx_set_lpn(bacnk, page_num, lpn)   (g_misc_meta[bank].sgx_lpn_list_of_cur_vblock[page_num] = lpn)
#define sgx_get_lpn(bank, page_num)   (g_misc_meta[bank].sgx_lpn_list_of_cur_vblock[page_num])

#define lpn_is_sgx(bank, page_num) (g_misc_meta[bank].sgx_lpn_list_of_cur_vblock[page_num].fid>0)   //fid==0이면 존재하지않는애임.
/*
#define lpn_is_sgx(bank, page_num)    (g_misc_meta[bank].is_sgx_lpn[page_num])
#define set_is_lpn(bank, page_num, is_sgx)   (g_misc_meta[bank].is_sgx_lpn[page_num] = is_sgx)
*/
#define get_miscblk_vpn(bank)         (g_misc_meta[bank].cur_miscblk_vpn)
#define set_miscblk_vpn(bank, vpn)    (g_misc_meta[bank].cur_miscblk_vpn = vpn)
#define get_mapblk_vpn(bank, mapblk_lbn)      (g_misc_meta[bank].cur_mapblk_vpn[mapblk_lbn])
#define set_mapblk_vpn(bank, mapblk_lbn, vpn) (g_misc_meta[bank].cur_mapblk_vpn[mapblk_lbn] = vpn)

//DiskShield block 관리 함수
#define DS_get_metablk_vpn(bank, DS_metablk_lbn)   (g_misc_meta[bank].DS_cur_metablk_vpn[DS_metablk_lbn])
#define DS_set_metablk_vpn(bank, DS_metablk_lbn, vpn)   (g_misc_meta[bank].DS_cur_metablk_vpn[DS_metablk_lbn] = vpn)
#define DS_inc_metablk_vpn(bank, DS_metablk_lbn) (g_misc_meta[bank].DS_cur_metablk_vpn[DS_metablk_lbn]++)

#define CHECK_LPAGE(lpn)              ASSERT((lpn) < NUM_LPAGES)
#define CHECK_VPAGE(vpn)              ASSERT((vpn) < (VBLKS_PER_BANK * PAGES_PER_BLK))


//----------------------------------
// FTL internal function prototype
//----------------------------------
static void   format(void);
static void   write_format_mark(void);
static void   sanity_check(void);
static void   load_pmap_table(void);
static void   load_misc_metadata(void);
static void   init_metadata_sram(void);
static void   load_metadata(void);
static void   logging_pmap_table(void);
static void   logging_misc_metadata(void);
static void   write_page(UINT32 const lpn, UINT32 const sect_offset, UINT32 const num_sectors); 
static void DS_write_page(SGX_LBA sgx_lba, UINT32 const sect_offset, UINT32 const num_sectors);
//static void   write_page(UINT32 const lpn, UINT32 const sect_offset, UINT32 const num_sectors, UINT32 const key); //key
static void   set_vpn(UINT32 const lpn, UINT32 const vpn); //no key
//static void   set_vpn(UINT32 const lpn, UINT32 const vpn, UINT32 const key); //key
//static UINT32 get_vpn_key(UINT32 const lpn); //key

static void   garbage_collection(UINT32 const bank);
static void   set_vcount(UINT32 const bank, UINT32 const vblock, UINT32 const vcount);
static BOOL32 is_bad_block(UINT32 const bank, UINT32 const vblock);
static BOOL32 check_format_mark(void);
static UINT32 get_vcount(UINT32 const bank, UINT32 const vblock);
static UINT32 get_vpn(UINT32 const lpn);
static UINT32 get_vt_vblock(UINT32 const bank);
static UINT32 assign_new_write_vpn(UINT32 const bank);

//key flush num_sectors
//static UINT32 flush_num=0;
//static UINT32 write_num=0;

//static UINT32 num_logging=0;
static UINT32 num_flush=0;
static UINT32 num_entry=0;
//
//static UINT32 cnt_ds_ftl_write =0 ;
//static UINT32 cnt_ftl_write =0 ;
//static UINT32 cnt_flush=0;

//SGX function


UINT32 cur_lba;

static void sanity_check(void)
{
    UINT32 dram_requirement = RD_BUF_BYTES + WR_BUF_BYTES + COPY_BUF_BYTES + FTL_BUF_BYTES
        + HIL_BUF_BYTES + TEMP_BUF_BYTES + BAD_BLK_BMP_BYTES + PAGE_MAP_BYTES + VCOUNT_BYTES;  //20170731

    if ((dram_requirement > DRAM_SIZE) || // DRAM metadata size check
        (sizeof(misc_metadata) > BYTES_PER_PAGE)) // misc metadata size check
    {
        led_blink();
        while (1);
    }
}
static void build_bad_blk_list(void)
{
	UINT32 bank, num_entries, result, vblk_offset;
	scan_list_t* scan_list = (scan_list_t*) TEMP_BUF_ADDR;

	mem_set_dram(BAD_BLK_BMP_ADDR, NULL, BAD_BLK_BMP_BYTES);

	disable_irq();

	flash_clear_irq();

	for (bank = 0; bank < NUM_BANKS; bank++)
	{
		SETREG(FCP_CMD, FC_COL_ROW_READ_OUT);
		SETREG(FCP_BANK, REAL_BANK(bank));
		SETREG(FCP_OPTION, FO_E);
		SETREG(FCP_DMA_ADDR, (UINT32) scan_list);
		SETREG(FCP_DMA_CNT, SCAN_LIST_SIZE);
		SETREG(FCP_COL, 0);
		SETREG(FCP_ROW_L(bank), SCAN_LIST_PAGE_OFFSET);
		SETREG(FCP_ROW_H(bank), SCAN_LIST_PAGE_OFFSET);

		SETREG(FCP_ISSUE, NULL);
		while ((GETREG(WR_STAT) & 0x00000001) != 0);
		while (BSP_FSM(bank) != BANK_IDLE);

		num_entries = NULL;
		result = OK;

		if (BSP_INTR(bank) & FIRQ_DATA_CORRUPT)
		{
			result = FAIL;
		}
		else
		{
			UINT32 i;

			num_entries = read_dram_16(&(scan_list->num_entries));

			if (num_entries > SCAN_LIST_ITEMS)
			{
				result = FAIL;
			}
			else
			{
				for (i = 0; i < num_entries; i++)
				{
					UINT16 entry = read_dram_16(scan_list->list + i);
					UINT16 pblk_offset = entry & 0x7FFF;

					if (pblk_offset == 0 || pblk_offset >= PBLKS_PER_BANK)
					{
						#if OPTION_REDUCED_CAPACITY == FALSE
						result = FAIL;
						#endif
					}
					else
					{
						write_dram_16(scan_list->list + i, pblk_offset);
					}
				}
			}
		}

		if (result == FAIL)
		{
			num_entries = 0;  // We cannot trust this scan list. Perhaps a software bug.
		}
		else
		{
			write_dram_16(&(scan_list->num_entries), 0);
		}

		g_bad_blk_count[bank] = 0;

		for (vblk_offset = 1; vblk_offset < VBLKS_PER_BANK; vblk_offset++)
		{
			BOOL32 bad = FALSE;

			#if OPTION_2_PLANE
			{
				UINT32 pblk_offset;

				pblk_offset = vblk_offset * NUM_PLANES;

                // fix bug@jasmine v.1.1.0
				if (mem_search_equ_dram(scan_list, sizeof(UINT16), num_entries + 1, pblk_offset) < num_entries + 1)
				{
					bad = TRUE;
				}

				pblk_offset = vblk_offset * NUM_PLANES + 1;

                // fix bug@jasmine v.1.1.0
				if (mem_search_equ_dram(scan_list, sizeof(UINT16), num_entries + 1, pblk_offset) < num_entries + 1)
				{
					bad = TRUE;
				}
			}
			#else
			{
                // fix bug@jasmine v.1.1.0
				if (mem_search_equ_dram(scan_list, sizeof(UINT16), num_entries + 1, vblk_offset) < num_entries + 1)
				{
					bad = TRUE;
				}
			}
			#endif

			if (bad)
			{
				g_bad_blk_count[bank]++;
				set_bit_dram(BAD_BLK_BMP_ADDR + bank*(VBLKS_PER_BANK/8 + 1), vblk_offset);
			}
		}
	}
}

void ftl_open(void)
{
    // debugging example 1 - use breakpoint statement!
    /* *(UINT32*)0xFFFFFFFE = 10; */

    /* UINT32 volatile g_break = 0; */
    /* while (g_break == 0); */

	led(0);
    sanity_check();
    //----------------------------------------
    // read scan lists from NAND flash
    // and build bitmap of bad blocks
    //----------------------------------------
	build_bad_blk_list();


 //   uart_printf("%d %d %d", NUM_RW_BUFFERS, NUM_RD_BUFFERS, NUM_WR_BUFFERS);
 //   uart_printf("%d %d", DRAM_SIZE, DRAM_BYTES_OTHER);
//uart_printf("DRAM : RD : %d WR :  %d COPY :  %d", RD_BUF_BYTES, WR_BUF_BYTES, COPY_BUF_BYTES);
//uart_printf("DRAM : FTL : %d HIL :  %d BADBLK :  %d", FTL_BUF_BYTES, HIL_BUF_BYTES, BAD_BLK_BMP_BYTES);
//uart_printf("DRAM : PAGEMAP : %d VCOUNT :  %d", PAGE_MAP_BYTES, VCOUNT_BYTES);
//uart_printf("FTL_BUF_BYTES : %d, NUM_BANKS : %d, BYTES_PER_PAGE: %d ",FTL_BUF_BYTES, NUM_BANKS, BYTES_PER_PAGE);
//uart_printf("DS_INODE_ADDR : %x, DS_FTL_ADDR : %x, DS_NHASH_ADDR : %x", DS_INODE_ADDR, DS_FTL_ADDR, DS_NHASH_ADDR);
//ecdsa code
//ecdsa_test();
 

  //finish
//sds
    //----------------------------------------
	// If necessary, do low-level format
	// format() should be called after loading scan lists, because format() calls is_bad_block().
    //----------------------------------------
 //	if (check_format_mark() == FALSE) //
	if (TRUE) //key
	{
        uart_print("do format");
		format();
        uart_print("end format");
	}
    // load FTL metadata
    else
    {
        uart_print("load metadata");
        load_metadata();
    }
	g_ftl_read_buf_id = 0;
	g_ftl_write_buf_id = 0;

    //SGX test!
   // SHA_test();
   // uart_printf("test v4.0!");
    //sgx_test();
    //uart_printf("")
   
   // hmac_test();
    //uart_printf("Hello This is DiskShield SSD\n");
    DS_init_superblock();

  //  DS_test(); 
    // /sgx_init_create_enclave_file();

    // This example FTL can handle runtime bad block interrupts and read fail (uncorrectable bit errors) interrupts
    flash_clear_irq();

    SETREG(INTR_MASK, FIRQ_DATA_CORRUPT | FIRQ_BADBLK_L | FIRQ_BADBLK_H);
	SETREG(FCONF_PAUSE, FIRQ_DATA_CORRUPT | FIRQ_BADBLK_L | FIRQ_BADBLK_H);

	enable_irq();

}
void ftl_flush(void)
{
    /* ptimer_start(); */
    //key
 //   flush_num++;
 //   uart_print("flush");
 //   uart_print("flush..");
//    uart_printf("flush num : %d write num : %d, mapping table pages: %d",flush_num,write_num, PAGE_MAP_BYTES_WITH_KEY / BYTES_PER_PAGE);
/*
    if(flush_num%4==0)
    {
      logging_pmap_table();
      logging_misc_metadata();

    }d
    */
   // logging_pmap_table();
   // logging_misc_metadata();
    /* ptimer_stop_and_uart_print(); */
}
// Testing FTL protocol APIs
void ftl_test_write(UINT32 const lba, UINT32 const num_sectors)
{
    ASSERT(lba + num_sectors <= NUM_LSECTORS);
    ASSERT(num_sectors > 0);

   // ftl_write(lba, num_sectors, 0); //key
    ftl_write(lba, num_sectors); //no key
}
void ftl_read(UINT32 const lba, UINT32 const num_sectors)
{
    UINT32 remain_sects, num_sectors_to_read;
    UINT32 lpn, sect_offset;
    UINT32 bank, vpn;
    //UINT32 key;

    lpn          = lba / SECTORS_PER_PAGE;
    sect_offset  = lba % SECTORS_PER_PAGE;
    remain_sects = num_sectors;

//key
  //  key = GETREG(SATA_FIS_H2D_4) & 0xFFFFFFFF;    //load key
    //key = 0x11111111;
   // uart_printf("[ftl_read} lba, sec : %x %d", lba, num_sectors);
    //uart_printf("ftl_read, lba: %x, lpn : %x, rec key val : %d, mapping table key : %d",lba,lpn, key, get_vpn_key(lpn));
/*
    if(get_vpn_key(lpn) == key)
    {
  //    uart_printf("READ : Key access succeeded. lba: %x", lba);
      ;
        //uart_printf("Success! rec key val : %d, mapping table key : %d",key, get_vpn_key(lpn));
    }
    else if (get_vpn_key(lpn) != key && get_vpn_key(lpn)!=0){
   //     uart_printf("READ : Key access failed. lba: %x", lba);
   //   return; //access contorl, no read !
        //uart_printf("Fail! rec key val : %d, mapping table key : %d",key, get_vpn_key(lpn));
    }
*/

    while (remain_sects != 0)
    {
        if ((sect_offset + remain_sects) < SECTORS_PER_PAGE)
        {
            num_sectors_to_read = remain_sects;
        }
        else
        {
            num_sectors_to_read = SECTORS_PER_PAGE - sect_offset;
        }
        bank = get_num_bank(lpn); // page striping
        vpn  = get_vpn(lpn);
        CHECK_VPAGE(vpn);

        if (vpn != NULL)
        {
            nand_page_ptread_to_host(bank,
                                     vpn / PAGES_PER_BLK,
                                     vpn % PAGES_PER_BLK,
                                     sect_offset,
                                     num_sectors_to_read);
        }
        // The host is requesting to read a logical page that has never been written to.
        else
        {
			UINT32 next_read_buf_id = (g_ftl_read_buf_id + 1) % NUM_RD_BUFFERS;

			#if OPTION_FTL_TEST == 0
			while (next_read_buf_id == GETREG(SATA_RBUF_PTR));	// wait if the read buffer is full (slow host)
			#endif

            // fix bug @ v.1.0.6
            // Send 0xFF...FF to host when the host request to read the sector that has never been written.
            // In old version, for example, if the host request to read unwritten sector 0 after programming in sector 1, Jasmine would send 0x00...00 to host.
            // However, if the host already wrote to sector 1, Jasmine would send 0xFF...FF to host when host request to read sector 0. (ftl_read() in ftl_xxx/ftl.c)
			mem_set_dram(RD_BUF_PTR(g_ftl_read_buf_id) + sect_offset*BYTES_PER_SECTOR,
                         0xFFFFFFFF, num_sectors_to_read*BYTES_PER_SECTOR);

            flash_finish();

			SETREG(BM_STACK_RDSET, next_read_buf_id);	// change bm_read_limit
			SETREG(BM_STACK_RESET, 0x02);				// change bm_read_limit

			g_ftl_read_buf_id = next_read_buf_id;
        }
        sect_offset   = 0;
        remain_sects -= num_sectors_to_read;
        lpn++;
    }
   // uart_printf("read finish");
}

//no key
void ftl_write(UINT32 const lba, UINT32 const num_sectors)
{
    UINT32 remain_sects, num_sectors_to_write;
    UINT32 lpn, sect_offset;

    lpn          = lba / SECTORS_PER_PAGE;
    sect_offset  = lba % SECTORS_PER_PAGE;
    remain_sects = num_sectors;

    //uart_printf("[ftl_write] lba, num_sectors  %x %d", lba, num_sectors);
/*
    cnt_ftl_write++;
    if((cnt_ftl_write % 1000)==0)
    {
        uart_print("ftlwrite");
       
        if(cnt_ftl_write>20000)
       {
           if((cnt_ftl_write%100)==0)
            uart_print("100");
       }
       
    }
    */
    while (remain_sects != 0)
    {
        if ((sect_offset + remain_sects) < SECTORS_PER_PAGE)
        {
            num_sectors_to_write = remain_sects;
        }
        else
        {
            num_sectors_to_write = SECTORS_PER_PAGE - sect_offset;
        }
        // single page write individually
        write_page(lpn, sect_offset, num_sectors_to_write);

        sect_offset   = 0;
        remain_sects -= num_sectors_to_write;
        lpn++;
    }
}


//no key
static void write_page(UINT32 const lpn, UINT32 const sect_offset, UINT32 const num_sectors)
{
    CHECK_LPAGE(lpn);
    ASSERT(sect_offset < SECTORS_PER_PAGE);
    ASSERT(num_sectors > 0 && num_sectors <= SECTORS_PER_PAGE);

    UINT32 bank, old_vpn, new_vpn;
    UINT32 vblock, page_num, page_offset, column_cnt;

    bank        = get_num_bank(lpn); // page striping
    page_offset = sect_offset;
    column_cnt  = num_sectors;

    new_vpn  = assign_new_write_vpn(bank);
    old_vpn  = get_vpn(lpn);

    CHECK_VPAGE (old_vpn);
    CHECK_VPAGE (new_vpn);
    ASSERT(old_vpn != new_vpn);

    g_ftl_statistics[bank].page_wcount++;

    // if old data already exist,
    if (old_vpn != NULL)
    {
        vblock   = old_vpn / PAGES_PER_BLK;
        page_num = old_vpn % PAGES_PER_BLK;

        //--------------------------------------------------------------------------------------
        // `Partial programming'
        // we could not determine whether the new data is loaded in the SATA write buffer.
        // Thus, read the left/right hole sectors of a valid page and copy into the write buffer.
        // And then, program whole valid data
        //--------------------------------------------------------------------------------------
        if (num_sectors != SECTORS_PER_PAGE)
        {
            // Performance optimization (but, not proved)
            // To reduce flash memory access, valid hole copy into SATA write buffer after reading whole page
            // Thus, in this case, we need just one full page read + one or two mem_copy
            if ((num_sectors <= 8) && (page_offset != 0))
            {
                // one page async read
                nand_page_read(bank,
                               vblock,
                               page_num,
                               FTL_BUF(bank));
                // copy `left hole sectors' into SATA write buffer
                if (page_offset != 0)
                {
                    mem_copy(WR_BUF_PTR(g_ftl_write_buf_id),
                             FTL_BUF(bank),
                             page_offset * BYTES_PER_SECTOR);
                }
                // copy `right hole sectors' into SATA write buffer
                if ((page_offset + column_cnt) < SECTORS_PER_PAGE)
                {
                    UINT32 const rhole_base = (page_offset + column_cnt) * BYTES_PER_SECTOR;

                    mem_copy(WR_BUF_PTR(g_ftl_write_buf_id) + rhole_base,
                             FTL_BUF(bank) + rhole_base,
                             BYTES_PER_PAGE - rhole_base);
                }
            }
            // left/right hole async read operation (two partial page read)
            else
            {
                // read `left hole sectors'
                if (page_offset != 0)
                {
                    nand_page_ptread(bank,
                                     vblock,
                                     page_num,
                                     0,
                                     page_offset,
                                     WR_BUF_PTR(g_ftl_write_buf_id),
                                     RETURN_ON_ISSUE);
                }
                // read `right hole sectors'
                if ((page_offset + column_cnt) < SECTORS_PER_PAGE)
                {
                    nand_page_ptread(bank,
                                     vblock,
                                     page_num,
                                     page_offset + column_cnt,
                                     SECTORS_PER_PAGE - (page_offset + column_cnt),
                                     WR_BUF_PTR(g_ftl_write_buf_id),
                                     RETURN_ON_ISSUE);
                }
            }
        }
        // full page write
        page_offset = 0;
        column_cnt  = SECTORS_PER_PAGE;
        // invalid old page (decrease vcount)
        set_vcount(bank, vblock, get_vcount(bank, vblock) - 1);
    }
    vblock   = new_vpn / PAGES_PER_BLK;
    page_num = new_vpn % PAGES_PER_BLK;
    ASSERT(get_vcount(bank,vblock) < (PAGES_PER_BLK - 1));

    // write new data (make sure that the new data is ready in the write buffer frame)
    // (c.f FO_B_SATA_W flag in flash.h)
    nand_page_ptprogram_from_host(bank,
                                  vblock,
                                  page_num,
                                  page_offset,
                                  column_cnt);
    // update metadata
    set_lpn(bank, page_num, lpn);
 //   set_is_lpn(bank,  page_num, 0);
    set_vpn(lpn, new_vpn);
    set_vcount(bank, vblock, get_vcount(bank, vblock) + 1);
}

//read
//void sgx_ftl_write(SGX_LBA sgx_lba, UINT32 encrypted_hash, UINT32 size)
void DS_ftl_read(SGX_LBA sgx_lba,  UINT32 size)
{
    UINT32 remain_sects, num_sectors_to_read;
    UINT32 lpn, sect_offset;
    UINT32 bank, vpn;
    UINT32 lba;
    //UINT32 key;
  //  int tp;
    //lpn          = lba / SECTORS_PER_PAGE;
    //sect_offset  = lba % SECTORS_PER_PAGE;
    //remain_sects = num_sectors;
    
    lba = sgx_lba.addr.offset / BYTES_PER_SECTOR;
    lpn          = lba / SECTORS_PER_PAGE;
    sect_offset  = lba % SECTORS_PER_PAGE;  
    remain_sects = size / BYTES_PER_SECTOR;
    
    sgx_lba.addr.lpn = lpn;

    while (remain_sects != 0)
    {
        if ((sect_offset + remain_sects) < SECTORS_PER_PAGE)
        {
            num_sectors_to_read = remain_sects;
        }
        else
        {
            num_sectors_to_read = SECTORS_PER_PAGE - sect_offset;
        }
        bank = get_num_bank(lpn); // page striping
        vpn  = DS_get_vpn(sgx_lba);
      

        //CHECK_VPAGE(vpn);

        if (vpn != NULL)
        {
            nand_page_ptread_to_host(bank,
                                     vpn / PAGES_PER_BLK,
                                     vpn % PAGES_PER_BLK,
                                     sect_offset,
                                     num_sectors_to_read);
        }
        // The host is requesting to read a logical page that has never been written to.
        else
        {
			UINT32 next_read_buf_id = (g_ftl_read_buf_id + 1) % NUM_RD_BUFFERS;

			#if OPTION_FTL_TEST == 0
			while (next_read_buf_id == GETREG(SATA_RBUF_PTR));	// wait if the read buffer is full (slow host)
			#endif

            // fix bug @ v.1.0.6
            // Send 0xFF...FF to host when the host request to read the sector that has never been written.
            // In old version, for example, if the host request to read unwritten sector 0 after programming in sector 1, Jasmine would send 0x00...00 to host.
            // However, if the host already wrote to sector 1, Jasmine would send 0xFF...FF to host when host request to read sector 0. (ftl_read() in ftl_xxx/ftl.c)
			mem_set_dram(RD_BUF_PTR(g_ftl_read_buf_id) + sect_offset*BYTES_PER_SECTOR,
                         0xFFFFFFFF, num_sectors_to_read*BYTES_PER_SECTOR);

            flash_finish();

			SETREG(BM_STACK_RDSET, next_read_buf_id);	// change bm_read_limit
			SETREG(BM_STACK_RESET, 0x02);				// change bm_read_limit

			g_ftl_read_buf_id = next_read_buf_id;
        }
        sect_offset   = 0;
        remain_sects -= num_sectors_to_read;
        lpn++;
        sgx_lba.addr.lpn++;
    }
}


void DS_ftl_write(SGX_LBA sgx_lba, UINT32 size)
//size는 바이트단위임
{
    UINT32 remain_sects, num_sectors_to_write;
    UINT32 lpn, sect_offset, lba;
    UINT32 offset;
   // UINT32 public_key;
   //인증되었다 가정하고 쭉 진행해보자.
   /*
   cnt_ds_ftl_write++;
   if((cnt_ds_ftl_write%1000)==0)
   {
       uart_print("dsftlwrite");
       if(cnt_ds_ftl_write>18000)
       {
           if((cnt_ds_ftl_write%100)==0)
            uart_print("100");
       }
   }
   */
   //size, sgx_lba.position은 4KB의 배수로 온다. 따라서 sector size로 나뉘어 떨어짐.
    ASSERT(sgx_lba.addr.offset % BYTES_PER_SECTOR == 0)
    
    lba = sgx_lba.addr.offset / BYTES_PER_SECTOR;
    lpn          = lba / SECTORS_PER_PAGE;
    sect_offset  = lba % SECTORS_PER_PAGE;  
    remain_sects = size / BYTES_PER_SECTOR;

    offset = sgx_lba.addr.offset;
    sgx_lba.addr.lpn = lpn; //여기서 position은 logical page number됨.
   
    //ftl을 골라잡아야합니다. file_ftl 잘 골라서 write해야 합니다. 그러기 위한 flow를공부합시다. 

    while (remain_sects != 0)
    {
        if ((sect_offset + remain_sects) < SECTORS_PER_PAGE)
        {
            num_sectors_to_write = remain_sects;
        }
        else
        {
            num_sectors_to_write = SECTORS_PER_PAGE - sect_offset;
        }
        // single page write individually
        DS_write_page(sgx_lba, sect_offset, num_sectors_to_write);
        sect_offset   = 0;
        remain_sects -= num_sectors_to_write;
        lpn++;
        sgx_lba.addr.lpn++;
    }

    //여기서 inode size업데이트해주는게옳다.
    DS_inode_set_size(sgx_lba.fid, offset, size);
}

//key

static void DS_write_page(SGX_LBA sgx_lba, UINT32 const sect_offset, UINT32 const num_sectors)
{
	ASSERT(sect_offset < SECTORS_PER_PAGE);
    ASSERT(num_sectors > 0 && num_sectors <= SECTORS_PER_PAGE);

    UINT32 bank, old_vpn, new_vpn;
    UINT32 vblock, page_num, page_offset, column_cnt;
   
    bank        = get_num_bank(sgx_lba.addr.lpn); // page striping   //같은 규칙 허용.
    page_offset = sect_offset;
    column_cnt  = num_sectors;
   
    new_vpn  = assign_new_write_vpn(bank);
	old_vpn = DS_get_vpn(sgx_lba);
 
    CHECK_VPAGE (old_vpn);
    CHECK_VPAGE (new_vpn);
    ASSERT(old_vpn != new_vpn);
    g_ftl_statistics[bank].page_wcount++;

    // if old data already exist,
    if (old_vpn != NULL)
    {
        vblock   = old_vpn / PAGES_PER_BLK;
        page_num = old_vpn % PAGES_PER_BLK;
        //--------------------------------------------------------------------------------------
        // `Partial programming'
        // we could not determine whether the new data is loaded in the SATA write buffer.
        // Thus, read the left/right hole sectors of a valid page and copy into the write buffer.
        // And then, program whole valid data
        //--------------------------------------------------------------------------------------
        if (num_sectors != SECTORS_PER_PAGE)
        {
            // Performance optimization (but, not proved)
            // To reduce flash memory access, valid hole copy into SATA write buffer after reading whole page
            // Thus, in this case, we need just one full page read + one or two mem_copy
            if ((num_sectors <= 8) && (page_offset != 0))
            {
              //  uart_printf("1");
                // one page async read
                nand_page_read(bank,
                               vblock,
                               page_num,
                               FTL_BUF(bank));
              
                // copy `left hole sectors' into SATA write buffer
                if (page_offset != 0)
                {
               //                     uart_printf("1-1");

                    mem_copy(WR_BUF_PTR(g_ftl_write_buf_id),
                             FTL_BUF(bank),
                             page_offset * BYTES_PER_SECTOR);
                }
                // copy `right hole sectors' into SATA write buffer
                if ((page_offset + column_cnt) < SECTORS_PER_PAGE)
                {
                    UINT32 const rhole_base = (page_offset + column_cnt) * BYTES_PER_SECTOR;
               // uart_printf("1-2");

                    mem_copy(WR_BUF_PTR(g_ftl_write_buf_id) + rhole_base,
                             FTL_BUF(bank) + rhole_base,
                             BYTES_PER_PAGE - rhole_base);
                }
            }
            // left/right hole async read operation (two partial page read)
            else
            {
            //    uart_printf("2");
                // read `left hole sectors'
                if (page_offset != 0)
                {
              //           uart_printf("2-1");

                    nand_page_ptread(bank,
                                     vblock,
                                     page_num,
                                     0,
                                     page_offset,
                                     WR_BUF_PTR(g_ftl_write_buf_id),
                                     RETURN_ON_ISSUE);
                }
                // read `right hole sectors'
                if ((page_offset + column_cnt) < SECTORS_PER_PAGE)
                {                                    

                    nand_page_ptread(bank,
                                     vblock,
                                     page_num,
                                     page_offset + column_cnt,
                                     SECTORS_PER_PAGE - (page_offset + column_cnt),
                                     WR_BUF_PTR(g_ftl_write_buf_id),
                                     RETURN_ON_ISSUE);
                }
            }
        }
        // full page write
        page_offset = 0;
        column_cnt  = SECTORS_PER_PAGE;
        // invalid old page (decrease vcount)
        set_vcount(bank, vblock, get_vcount(bank, vblock) - 1);
    }
    
    vblock   = new_vpn / PAGES_PER_BLK;
    page_num = new_vpn % PAGES_PER_BLK;
    ASSERT(get_vcount(bank,vblock) < (PAGES_PER_BLK - 1));
    // write new data (make sure that the new data is ready in the write buffer frame)
    // (c.f FO_B_SATA_W flag in flash.h)
    nand_page_ptprogram_from_host(bank,
                                  vblock,
                                  page_num,
                                  page_offset,
                                  column_cnt);
    // update metadata
    	
	sgx_set_lpn(bank, page_num, sgx_lba);   //에러 있을수있지만 지금은 없을거라 믿음
    DS_set_vpn(sgx_lba, new_vpn);  //key
    set_vcount(bank, vblock, get_vcount(bank, vblock) + 1);
}







// get vpn from PAGE_MAP

//no key
static UINT32 get_vpn(UINT32 const lpn)
{
    //uart_printf("PAGE_MAP_boundary : %d, cur : %d\n", PAGE_MAP_ADDR+PAGE_MAP_BYTES, PAGE_MAP_ADDR +lpn * sizeof(UINT32) );
    CHECK_LPAGE(lpn);
    return read_dram_32(PAGE_MAP_ADDR + lpn * sizeof(UINT32));
}

// set vpn to PAGE_MAP


//no key

// set vpn to PAGE_MAP
static void set_vpn(UINT32 const lpn, UINT32 const vpn)
{
    //uart_printf("PAGE_MAP_boundary : %d, cur : %d\n", PAGE_MAP_ADDR+PAGE_MAP_BYTES, PAGE_MAP_ADDR +lpn * sizeof(UINT32) );
    CHECK_LPAGE(lpn);
    ASSERT(vpn >= (META_BLKS_PER_BANK * PAGES_PER_BLK) && vpn < (VBLKS_PER_BANK * PAGES_PER_BLK));
    
    write_dram_32(PAGE_MAP_ADDR + lpn * sizeof(UINT32), vpn);
}

// get valid page count of vblock
static UINT32 get_vcount(UINT32 const bank, UINT32 const vblock)
{
    UINT32 vcount;

    ASSERT(bank < NUM_BANKS);
    ASSERT((vblock >= META_BLKS_PER_BANK) && (vblock < VBLKS_PER_BANK));

    vcount = read_dram_16(VCOUNT_ADDR + (((bank * VBLKS_PER_BANK) + vblock) * sizeof(UINT16)));
    ASSERT((vcount < PAGES_PER_BLK) || (vcount == VC_MAX));

    return vcount;
}
// set valid page count of vblock
static void set_vcount(UINT32 const bank, UINT32 const vblock, UINT32 const vcount)
{
    ASSERT(bank < NUM_BANKS);
    ASSERT((vblock >= META_BLKS_PER_BANK) && (vblock < VBLKS_PER_BANK));
    ASSERT((vcount < PAGES_PER_BLK) || (vcount == VC_MAX));

    write_dram_16(VCOUNT_ADDR + (((bank * VBLKS_PER_BANK) + vblock) * sizeof(UINT16)), vcount);
}
static UINT32 assign_new_write_vpn(UINT32 const bank)
{
    ASSERT(bank < NUM_BANKS);

    UINT32 write_vpn;
    UINT32 vblock;

    write_vpn = get_cur_write_vpn(bank);
    //uart_printf("[assign_new_write_vpn] : write_vpn: %x bank = %d\n", write_vpn, bank);
    vblock    = write_vpn / PAGES_PER_BLK;

    // NOTE: if next new write page's offset is
    // the last page offset of vblock (i.e. PAGES_PER_BLK - 1),
    //block이 꽉찼어 -> //마지막 페이지에 reverse index(garbage collection위해서) 넣자
    if ((write_vpn % PAGES_PER_BLK) == (PAGES_PER_BLK - 2))
    {
      //  uart_printf("[assign_new_write_vpn] here?\n");
        // then, because of the flash controller limitation
        // (prohibit accessing a spare area (i.e. OOB)),
        // thus, we persistenly write a lpn list into last page of vblock.
        //마지막 페이지에 reverse index(garbage collection위해서) 넣자
     //   uart_printf("block full!");
 // uart_printf("block full!");
 // uart_printf("block full!");
 // uart_printf("block full!");
 // uart_printf("block full!");
 // uart_printf("block full!");
//  uart_printf("block full!");
//  uart_printf("block full!");
  //uart_printf("*******************************block full!***********************************");


        mem_copy(FTL_BUF(bank), g_misc_meta[bank].lpn_list_of_cur_vblock, sizeof(UINT32) * PAGES_PER_BLK);
        mem_copy(FTL_BUF(bank)+ sizeof(UINT32) * PAGES_PER_BLK, g_misc_meta[bank].sgx_lpn_list_of_cur_vblock,\
        sizeof(SGX_LBA) * PAGES_PER_BLK);  // (6bytes * 128 = 4bytes의 배수이므로 괜찮다. 지금은.)
        
        //mem_copy(FTL_BUF(bank)+ sizeof(UINT32) * PAGES_PER_BLK + sizeof(SGX_LBA) * PAGES_PER_BLK, g_misc_meta[bank].is_sgx_lpn, sizeof(UINT8) * PAGES_PER_BLK);
       
        // fix minor bug
        nand_page_ptprogram(bank, vblock, PAGES_PER_BLK - 1, 0,
                            (((sizeof(UINT32) * PAGES_PER_BLK + sizeof(SGX_LBA) * PAGES_PER_BLK) + BYTES_PER_SECTOR - 1 ) / BYTES_PER_SECTOR), FTL_BUF(bank));

        mem_set_sram(g_misc_meta[bank].lpn_list_of_cur_vblock, 0x00000000, sizeof(UINT32) * PAGES_PER_BLK);
        mem_set_sram(g_misc_meta[bank].sgx_lpn_list_of_cur_vblock, 0x00000000, sizeof(SGX_LBA) * PAGES_PER_BLK);
        //mem_set_sram(g_misc_meta[bank].is_sgx_lpn, 0x00000000, sizeof(UINT8) * PAGES_PER_BLK);
        //다음 블락
        inc_full_blk_cnt(bank);

        // do garbage collection if necessary
        //블락도 꽉찼으면 garbage collection
        if (is_full_all_blks(bank))
        {
            garbage_collection(bank);
            return get_cur_write_vpn(bank);
        }
        do
        {
            vblock++;

            ASSERT(vblock != VBLKS_PER_BANK);
        }while (get_vcount(bank, vblock) == VC_MAX);
    }
    // write page -> next block
    if (vblock != (write_vpn / PAGES_PER_BLK))
    {
        write_vpn = vblock * PAGES_PER_BLK;
    }
    else
    {
       // uart_printf("[assign_new_write_vpn] here?\n");
        write_vpn++;
    }
   // uart_printf("[assign_new_write_vpn] : set_new_write_vpn: %x\n", write_vpn);
    set_new_write_vpn(bank, write_vpn);

    return write_vpn;
}

static BOOL32 is_bad_block(UINT32 const bank, UINT32 const vblk_offset)
{
    if (tst_bit_dram(BAD_BLK_BMP_ADDR + bank*(VBLKS_PER_BANK/8 + 1), vblk_offset) == FALSE)
    {
        return FALSE;
    }
    return TRUE;
}
//------------------------------------------------------------
// if all blocks except one free block are full,
// do garbage collection for making at least one free page
//-------------------------------------------------------------
static void garbage_collection(UINT32 const bank)
{
    //uart_printf("gc");
    ASSERT(bank < NUM_BANKS);
    g_ftl_statistics[bank].gc_cnt++;

    UINT32 src_lpn;
    SGX_LBA sgx_src_lpn;
    UINT32 vt_vblock;
    UINT32 free_vpn;
    UINT32 vcount; // valid page count in victim block
    UINT32 src_page;
    UINT32 gc_vblock;
    UINT8 lpn_from_sgx=0;

    g_ftl_statistics[bank].gc_cnt++;


uart_print("garbage collection....\n");


    vt_vblock = get_vt_vblock(bank);   // get victim block
    vcount    = get_vcount(bank, vt_vblock);  //victim block의 valid 갯수
    gc_vblock = get_gc_vblock(bank);
    free_vpn  = gc_vblock * PAGES_PER_BLK;

/*     uart_printf("garbage_collection bank %d, vblock %d",bank, vt_vblock); */

    ASSERT(vt_vblock != gc_vblock);
    ASSERT(vt_vblock >= META_BLKS_PER_BANK && vt_vblock < VBLKS_PER_BANK);
    ASSERT(vcount < (PAGES_PER_BLK - 1));
    ASSERT(get_vcount(bank, gc_vblock) == VC_MAX);
    ASSERT(!is_bad_block(bank, gc_vblock));

    // 1. load p2l list from last page offset of victim block (4B x PAGES_PER_BLK) 은 lpn을 찾기 위해 사용됨
    // fix minor bug
    nand_page_ptread(bank, vt_vblock, PAGES_PER_BLK - 1, 0,
                     ((sizeof(UINT32) * PAGES_PER_BLK + BYTES_PER_SECTOR - 1 ) / BYTES_PER_SECTOR), FTL_BUF(bank), RETURN_WHEN_DONE);
    mem_copy(g_misc_meta[bank].lpn_list_of_cur_vblock, FTL_BUF(bank), sizeof(UINT32) * PAGES_PER_BLK);  //메타데이터에 넣엇네

    mem_copy(g_misc_meta[bank].sgx_lpn_list_of_cur_vblock, FTL_BUF(bank)+ sizeof(UINT32) * PAGES_PER_BLK, \
    sizeof(SGX_LBA) * PAGES_PER_BLK);  // (6bytes * 128 = 4bytes의 배수이므로 괜찮다. 지금은.)
  
    //mem_copy(g_misc_meta[bank].is_sgx_lpn, FTL_BUF(bank)+ sizeof(UINT32) * PAGES_PER_BLK + sizeof(SGX_LBA) * PAGES_PER_BLK, sizeof(UINT8) * PAGES_PER_BLK);


    // 2. copy-back all valid pages to free space
    for (src_page = 0; src_page < (PAGES_PER_BLK - 1); src_page++)
    {
        // get lpn of victim block from a read lpn list
        //SGX에서
        //여기서 가져올 lpn이 SGX용인지 아닌지 확인해야함.

        lpn_from_sgx = lpn_is_sgx(bank, src_page);

        if(lpn_from_sgx)
        {
          //  uart_printf("sgx");
            sgx_src_lpn = sgx_get_lpn(bank, src_page);

            if (DS_get_vpn(sgx_src_lpn) !=
                ((vt_vblock * PAGES_PER_BLK) + src_page))
            {
                // invalid page
                continue;
            }

                    //CHECK_LPAGE(src_lpn);

                // if the page is valid,
                // then do copy-back op. to free space
            nand_page_copyback(bank,
                           vt_vblock,
                           src_page,
                           free_vpn / PAGES_PER_BLK,
                           free_vpn % PAGES_PER_BLK);
            ASSERT((free_vpn / PAGES_PER_BLK) == gc_vblock);
                // update metadata
            //  set_vpn(src_lpn, free_vpn); //no key
            DS_set_vpn(sgx_src_lpn, free_vpn);
            sgx_set_lpn(bank, (free_vpn % PAGES_PER_BLK), sgx_src_lpn);
        //    set_is_lpn(bank,  (free_vpn % PAGES_PER_BLK), lpn_from_sgx);
        }
        else{
            src_lpn = get_lpn(bank, src_page);  //여기서 lpn가져오는데 쓰겠네
            CHECK_VPAGE(get_vpn(src_lpn));
            
            //key = get_vpn_key(src_lpn); //key

            // determine whether the page is valid or not
            if (get_vpn(src_lpn) !=
                ((vt_vblock * PAGES_PER_BLK) + src_page))
            {
                // invalid page
                continue;
            }
            ASSERT(get_lpn(bank, src_page) != INVALID);

                    //CHECK_LPAGE(src_lpn);

            // if the page is valid,
            // then do copy-back op. to free space
            nand_page_copyback(bank,
                           vt_vblock,
                           src_page,
                           free_vpn / PAGES_PER_BLK,
                           free_vpn % PAGES_PER_BLK);
            ASSERT((free_vpn / PAGES_PER_BLK) == gc_vblock);
            // update metadata
        //  set_vpn(src_lpn, free_vpn); //no key

            set_vpn(src_lpn, free_vpn);
            set_lpn(bank, (free_vpn % PAGES_PER_BLK), src_lpn);
          //  set_is_lpn(bank, (free_vpn % PAGES_PER_BLK), lpn_from_sgx);
        }

        free_vpn++;
    }
#if OPTION_ENABLE_ASSERT
    if (vcount == 0)
    {
        ASSERT(free_vpn == (gc_vblock * PAGES_PER_BLK));
    }
#endif
    // 3. erase victim block
    nand_block_erase(bank, vt_vblock);
    ASSERT((free_vpn % PAGES_PER_BLK) < (PAGES_PER_BLK - 2));
    ASSERT((free_vpn % PAGES_PER_BLK == vcount));

/*     uart_printf("gc page count : %d", vcount); */

    // 4. update metadata
    set_vcount(bank, vt_vblock, VC_MAX);
    set_vcount(bank, gc_vblock, vcount);
    set_new_write_vpn(bank, free_vpn); // set a free page for new write
    set_gc_vblock(bank, vt_vblock); // next free block (reserve for GC)
    dec_full_blk_cnt(bank); // decrease full block count
    /* uart_print("garbage_collection end"); */
}
//-------------------------------------------------------------
// Victim selection policy: Greedy
//
// Select the block which contain minumum valid pages
//-------------------------------------------------------------
static UINT32 get_vt_vblock(UINT32 const bank)
{
    ASSERT(bank < NUM_BANKS);

    UINT32 vblock;

    // search the block which has mininum valid pages
    vblock = mem_search_min_max(VCOUNT_ADDR + (bank * VBLKS_PER_BANK * sizeof(UINT16)),
                                sizeof(UINT16),
                                VBLKS_PER_BANK,
                                MU_CMD_SEARCH_MIN_DRAM);

    ASSERT(is_bad_block(bank, vblock) == FALSE);
    ASSERT(vblock >= META_BLKS_PER_BANK && vblock < VBLKS_PER_BANK);
    ASSERT(get_vcount(bank, vblock) < (PAGES_PER_BLK - 1));

    return vblock;
}
static void format(void)
{
    UINT32 bank, vblock, vcount_val;

    ASSERT(NUM_MISC_META_SECT > 0);
    ASSERT(NUM_VCOUNT_SECT > 0);

    //uart_printf("Total FTL DRAM metadata size: %d KB", DRAM_BYTES_OTHER / 1024);
    //uart_printf("LAST1 : %d, LAST2 : %d, FIRST : %d, DRAM SIZE:%d",VCOUNT_ADDR+VCOUNT_BYTES, FLUSH_MAP_ADDR+FLUSH_MAP_BYTES, RD_BUF_ADDR, DRAM_SIZE);

    //uart_printf("VBLKS_PER_BANK: %d", VBLKS_PER_BANK);
    //uart_printf("LBLKS_PER_BANK: %d", NUM_LPAGES / PAGES_PER_BLK / NUM_BANKS);
    //overflose uart_printf 버그나옴.
    //uart_printf("META_BLKS_PER_BANK: %d, MAPBLKS_PER_BANK : %d", META_BLKS_PER_BANK, MAPBLKS_PER_BANK);
   // uart_printf("DS_META_BLKS_PER_BANK : %d", DS_META_BLKS_PER_BANK);
    //uart_printf("PAGE_MAP_BYTES : %d, NUM_BANK: %d, ")
 // #define MAPBLKS_PER_BANK    (((PAGE_MAP_BYTES / NUM_BANKS) + BYTES_PER_PAGE - 1) / BYTES_PER_PAGE)    //RAM을 통해 각 뱅크에 맵핑 페이지 수

   /// uart_printf("DRAM_ECC_UNIT: %d, FLUSH_MAP_BYTES : %d",DRAM_ECC_UNIT, FLUSH_MAP_BYTES);
    //----------------------------------------
    // initialize DRAM metadata
    //----------------------------------------
    mem_set_dram(PAGE_MAP_ADDR, NULL, PAGE_MAP_BYTES); //20170731
    mem_set_dram(VCOUNT_ADDR, NULL, VCOUNT_BYTES);
    mem_set_dram(FLUSH_MAP_ADDR, NULL, FLUSH_MAP_BYTES);
    
    //uart_printf("DS_INODE_ADDR : %x %x\n", DS_INODE_ADDR, DS_INODE_BYTES);
    mem_set_dram(DS_INODE_ADDR, NULL, DS_INODE_BYTES);
   // uart_printf("DS_FTL_ADDR : %x %x\n", DS_FTL_ADDR, DS_FTL_BYTES);

    mem_set_dram(DS_FTL_ADDR, NULL, DS_FTL_BYTES);
   // uart_printf("DS_NHASH_ADDR : %x %x\n", DS_NHASH_ADDR, DS_NHASH_BYTES);
    mem_set_dram(DS_NHASH_ADDR, NULL, DS_NHASH_BYTES);
    mem_set_dram(EVENTQ_ADDR, NULL, EVENTQ_BYTES);
    mem_set_dram(RDAFWRQ_ADDR, NULL, RDAFWRQ_BYTES);
    mem_set_dram(HMAC_BUFF, NULL, HMAC_SIZE);

    //mem_set_dram(ENCLAVE_KEY_ADDR, NULL, ENCALVE_KEY_BYTES);
    //mem_set_dram(ENCLAVE_FILES_ADDR, NULL, ENCLAVE_FILES_BYTES);
    //mem_set_dram(FILE_FTL_ADDR, NULL, FILE_FTL_BYTES);
    //mem_set_dram(FILE_FTL_BITMAP_ADDR, NULL, FILE_FTL_BITMAP_BYTES);
    //----------------------------------------
    // erase all blocks except vblock #0
    //----------------------------------------
	for (vblock = MISCBLK_VBN; vblock < VBLKS_PER_BANK; vblock++)
	{
		for (bank = 0; bank < NUM_BANKS; bank++)
		{
            vcount_val = VC_MAX;
            if (is_bad_block(bank, vblock) == FALSE)
			{
				nand_block_erase(bank, vblock);
                vcount_val = 0;
            }
            write_dram_16(VCOUNT_ADDR + ((bank * VBLKS_PER_BANK) + vblock) * sizeof(UINT16),
                          vcount_val);
        }
    }
    //----------------------------------------
    // initialize SRAM metadata
    //----------------------------------------
    init_metadata_sram();
    // flush metadata to NAND
    logging_pmap_table();
    logging_misc_metadata();
    write_format_mark();
	led(1);
    uart_print("format complete");
}
static void init_metadata_sram(void)
{
    UINT32 bank;
    UINT32 vblock;
    UINT32 mapblk_lbn;
    UINT32 DS_metablk_lbn;
    //----------------------------------------
    // initialize misc. metadata
    //----------------------------------------
    for (bank = 0; bank < NUM_BANKS; bank++)
    {
        
        g_misc_meta[bank].free_blk_cnt = VBLKS_PER_BANK - META_BLKS_PER_BANK;
        g_misc_meta[bank].free_blk_cnt -= get_bad_blk_cnt(bank);
        //uart_printf("[init_metadata_sram] : freeblk cnt : %d", g_misc_meta[bank].free_blk_cnt);
        // NOTE: vblock #0,1 don't use for user space
        write_dram_16(VCOUNT_ADDR + ((bank * VBLKS_PER_BANK) + 0) * sizeof(UINT16), VC_MAX);
        write_dram_16(VCOUNT_ADDR + ((bank * VBLKS_PER_BANK) + 1) * sizeof(UINT16), VC_MAX);

        //----------------------------------------
        // assign misc. block
        //----------------------------------------
        // assumption: vblock #1 = fixed location.
        // Thus if vblock #1 is a bad block, it should be allocate another block.
        set_miscblk_vpn(bank, MISCBLK_VBN * PAGES_PER_BLK - 1);
        ASSERT(is_bad_block(bank, MISCBLK_VBN) == FALSE);

        vblock = MISCBLK_VBN;

        //----------------------------------------
        // assign map block
        //----------------------------------------
        mapblk_lbn = 0;
        while (mapblk_lbn < MAPBLKS_PER_BANK)
        {
            vblock++;
            ASSERT(vblock < VBLKS_PER_BANK);
            if (is_bad_block(bank, vblock) == FALSE)
            {
                set_mapblk_vpn(bank, mapblk_lbn, vblock * PAGES_PER_BLK);
                write_dram_16(VCOUNT_ADDR + ((bank * VBLKS_PER_BANK) + vblock) * sizeof(UINT16), VC_MAX);
                mapblk_lbn++;
            }
        }

        //----------------------------------------
        // assign free block for gc
        //----------------------------------------
        do
        {
            vblock++;
            // NOTE: free block should not be secleted as a victim @ first GC
            write_dram_16(VCOUNT_ADDR + ((bank * VBLKS_PER_BANK) + vblock) * sizeof(UINT16), VC_MAX);
            // set free block
            set_gc_vblock(bank, vblock);

            ASSERT(vblock < VBLKS_PER_BANK);
        }while(is_bad_block(bank, vblock) == TRUE);

        //----------------------------------------
        //assign free block for DiskShield metadata
        //----------------------------------------
        //mapblk_lbn = 0;
        //while (mapblk_lbn < MAPBLKS_PER_BANK)
        //DiskShield block allocation
        //uart_printf("[init_metadata_sram] DS block allocation");
        DS_metablk_lbn = 0;
        while (DS_metablk_lbn < DS_META_BLKS_PER_BANK)
        {
           // uart_printf("%d %d", DS_metablk_lbn, DS_META_BLKS_PER_BANK);
            vblock++;
            ASSERT(vblock < VBLKS_PER_BANK);
            if (is_bad_block(bank, vblock) == FALSE)
            {

                DS_set_metablk_vpn(bank, DS_metablk_lbn, vblock * PAGES_PER_BLK);
               // uart_printf("VCOUNT bytes : %x, cur : %x", VCOUNT_BYTES, ((bank * VBLKS_PER_BANK) + vblock));
                write_dram_16(VCOUNT_ADDR + ((bank * VBLKS_PER_BANK) + vblock) * sizeof(UINT16), VC_MAX);
                DS_metablk_lbn++;
                //set_mapblk_vpn(bank, mapblk_lbn, vblock * PAGES_PER_BLK);
               // write_dram_16(VCOUNT_ADDR + ((bank * VBLKS_PER_BANK) + vblock) * sizeof(UINT16), VC_MAX);
               // mapblk_lbn++;
            }
        }
        //uart_printf("//");
        //----------------------------------------
        // assign free vpn for first new write
        //----------------------------------------
        do
        {
            vblock++;
            // 현재 next vblock부터 새로운 데이터를 저장을 시작
            set_new_write_vpn(bank, vblock * PAGES_PER_BLK);
            ASSERT(vblock < VBLKS_PER_BANK);
        }while(is_bad_block(bank, vblock) == TRUE);
    }
}
// logging misc + vcount metadata
static void logging_misc_metadata(void)
{
    UINT32 misc_meta_bytes = NUM_MISC_META_SECT * BYTES_PER_SECTOR; // per bank
    UINT32 vcount_addr     = VCOUNT_ADDR;
    UINT32 vcount_bytes    = NUM_VCOUNT_SECT * BYTES_PER_SECTOR; // per bank
    UINT32 vcount_boundary = VCOUNT_ADDR + VCOUNT_BYTES; // entire vcount data
    UINT32 bank;

    flash_finish();

    for (bank = 0; bank < NUM_BANKS; bank++)
    {
        inc_miscblk_vpn(bank);

        // note: if misc. meta block is full, just erase old block & write offset #0
        if ((get_miscblk_vpn(bank) / PAGES_PER_BLK) != MISCBLK_VBN)
        {
            nand_block_erase(bank, MISCBLK_VBN);
            set_miscblk_vpn(bank, MISCBLK_VBN * PAGES_PER_BLK); // vpn = 128
        }
        // copy misc. metadata to FTL buffer
        mem_copy(FTL_BUF(bank), &g_misc_meta[bank], misc_meta_bytes);

        // copy vcount metadata to FTL buffer
        if (vcount_addr <= vcount_boundary)
        {
            mem_copy(FTL_BUF(bank) + misc_meta_bytes, vcount_addr, vcount_bytes);
            vcount_addr += vcount_bytes;
        }
    }
    // logging the misc. metadata to nand flash
    for (bank = 0; bank < NUM_BANKS; bank++)
    {
        nand_page_ptprogram(bank,
                            get_miscblk_vpn(bank) / PAGES_PER_BLK,
                            get_miscblk_vpn(bank) % PAGES_PER_BLK,
                            0,
                            NUM_MISC_META_SECT + NUM_VCOUNT_SECT,
                            FTL_BUF(bank));
    }
    flash_finish();
}
static void logging_pmap_table(void)
{
    UINT32 pmap_addr  = PAGE_MAP_ADDR;
    UINT32 pmap_bytes = BYTES_PER_PAGE; // per bank
    UINT32 mapblk_vpn;
    UINT32 bank;
    UINT32 pmap_boundary = PAGE_MAP_ADDR + PAGE_MAP_BYTES;  //이게 맞음 load_pmap_table이 좀 잘못짰네보니  20170731
  //  UINT32 i,j,k;
 //   UINT32 cur_sector;
    BOOL32 finished = FALSE;
	int num_logging = 0;

//key flush

    num_flush++;

    UINT32 fmap_next = 0;
    UINT32 fmap_addr = FLUSH_MAP_ADDR;  //flush mapping table 비트단위로 맵핑이다.
    UINT8 cur_fmap;


//flush
  //  uart_printf("logging_pmap_table12");
//key필드 추가됐으므로 MAPBLKS_PER_BANK가 두배사이즈로 변함
    for (UINT32 mapblk_lbn = 0; mapblk_lbn < MAPBLKS_PER_BANK; mapblk_lbn++) //key
    {
        flash_finish();
        for (bank = 0; bank < NUM_BANKS; bank++)
        {
            if (finished)
            {
                break;
            }
            else if (pmap_addr >= pmap_boundary)
            {
                finished = TRUE;
                break;
            }
            else if (pmap_addr + BYTES_PER_PAGE >= pmap_boundary)
            {
                finished = TRUE;
                pmap_bytes = (pmap_boundary - pmap_addr + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR ;
            }
//flush
            if(fmap_next%8 == 0)
            {
                if(fmap_next != 0)  //처음에는 update하면 안된다.
                  fmap_addr += 1;
                if(fmap_addr >= FLUSH_MAP_ADDR + FLUSH_MAP_BYTES)
                {;
                  //  uart_printf("flush oops!!!\n");
                }
                cur_fmap = read_dram_8(fmap_addr);
              //  mem_copy(&cur_fmap, fmap_addr, 1);  //1바이트씩 가져온다.(8개의 페이지정보)
            }
            fmap_next += 1;

            if(((cur_fmap >> (8 - fmap_next%8 )) & 0x01) != 0) //flush 해야하는 페이지의 경우
            {
                num_logging++;
              //여기서부터가 실제 logging하는 부분임
              //안쓴녀석한테 써야하므로 순차적으로 내려가는 모양이지.
                inc_mapblk_vpn(bank, mapblk_lbn);

                mapblk_vpn = get_mapblk_vpn(bank, mapblk_lbn);

                //내려가다가 블락 바닥가면, 블락을 통째로 지워버리면 되겠지.
                // note: if there is no free page, then erase old map block first.
                if ((mapblk_vpn % PAGES_PER_BLK) == 0)
                {
                  // erase full map block
                  nand_block_erase(bank, (mapblk_vpn - 1) / PAGES_PER_BLK);

                  // next vpn of mapblk is offset #0
                  set_mapblk_vpn(bank, mapblk_lbn, ((mapblk_vpn - 1) / PAGES_PER_BLK) * PAGES_PER_BLK);
                  mapblk_vpn = get_mapblk_vpn(bank, mapblk_lbn);
                }
                // copy the page mapping table to FTL buffer
                mem_copy(FTL_BUF(bank), pmap_addr, pmap_bytes); //64*64개의 맵핑정보
//flush

//          uart_printf("SECTORS_PER_PAGE %d ,BYTES_PER_SECTOR %d,BYTES_PER_MAP_ENTRY_KEY %d,NUM_LPAGES %d", SECTORS_PER_PAGE, BYTES_PER_SECTOR, BYTES_PER_MAP_ENTRY_KEY, NUM_LPAGES);
/*
            for(cur_sector=0; cur_sector<(pmap_bytes / BYTES_PER_SECTOR); cur_sector++) //page 32KB, sector 512bytes 기준 64개
            {
                //각 sector에 대해 update해야하는지 결정해야함.
                for(j=0; j<(BYTES_PER_SECTOR / BYTES_PER_MAP_ENTRY_KEY) / 32; j++ ) //512 / 8 = 64 이제 맵핑정보별로 체크해야함
                {
                    mem_copy(&cur_flush_tb, fmap_addr, 4);  //4바이트씩 가져온다.(32개의 맵핑정보)
                //    uart_printf("cur_flush_tb : %d",cur_flush_tb);
                    if(cur_flush_tb!=0)
                    {
                      //logging 해야함 현재 섹터자리에 해당하는애들말이죠.
                      logging_flag=1;
                    }
                    fmap_addr += 4;
                }

                if(logging_flag==1)
                {
                  nand_page_ptprogram(bank,
                                    mapblk_vpn / PAGES_PER_BLK,
                                    mapblk_vpn % PAGES_PER_BLK,
                                    cur_sector,
                                    //0,
                                    1,
                                    FTL_BUF(bank));

                  logging_flag=0;
                }
            }

*/
            //무사히 빠져나오면 logging안해도됨.

//flush

            // logging update page mapping table into map_block
              nand_page_ptprogram(bank,
                                mapblk_vpn / PAGES_PER_BLK,
                                mapblk_vpn % PAGES_PER_BLK,
                                0,
                                pmap_bytes / BYTES_PER_SECTOR,
                                FTL_BUF(bank));


			//  uart_printf("logging_pmap_table) %x %x	curfmap : %d",  pmap_addr, pmap_bytes, cur_fmap);
			}
		



                                pmap_addr += pmap_bytes;
        }
      //  uart_printf("fmap_addr : %d, , init : %d, boundary : %d",fmap_addr, FLUSH_MAP_ADDR,FLUSH_MAP_ADDR+FLUSH_MAP_BYTES);
      //  uart_printf("map_addr : %d, , init : %d, boundary : %d",pmap_addr, PAGE_MAP_ADDR, PAGE_MAP_ADDR+PAGE_MAP_BYTES_WITH_KEY);

        if (finished)
        {
            break;
        }
    }
    num_entry=0;
    mem_set_dram(FLUSH_MAP_ADDR, 0,FLUSH_MAP_BYTES); //key flush
	//uart_printf("num_flush : %d, num_logging : %d", num_flush, num_logging);
    flash_finish();
}

void DS_store_inode(INODE* inode)  //ino가 block주소를 상징한다 생각하면 된다.
{
    UINT32 bank = inode->no % 8;
    UINT32 DS_metablk_lbn = inode->no / 8;
    UINT32 DS_metablk_vpn;
    int i;
  
   //FTL chunk는 128bytes의 각 FTL 덩어리를 의미함.
    UINT32 DS_filesize_in_FTLchunk = BYTES_PER_PAGE * (DS_FTL_SIZE/4); //32KB*32 = 1MB
    //우리는 FTL의 청크 단위로 복사를할것임.
    UINT32 num_FTLchunk = (inode->size + DS_filesize_in_FTLchunk -1) / (DS_filesize_in_FTLchunk);
    int ftl_chunk;
    UINT32 ftl_chunk_addr;
    UINT32 copy_bytes=0;
    INODE* v_inode = (INODE*)DS_PA_to_VA((UINT32)inode);
    //inode의 시작 vpn을 저장한다.
    UINT8 page_to_store = ((DS_INODE_SIZE + num_FTLchunk * DS_FTL_SIZE)+ BYTES_PER_PAGE - 1 ) / BYTES_PER_PAGE;

    //nand erase가 발생할것으로 예상되면 반드시 미리 지워야한다.
    //중간 과정에서 지우면 데이터가 날라가기때문.
    DS_metablk_vpn = DS_get_metablk_vpn(bank, DS_metablk_lbn) ;
    if(((DS_metablk_vpn % PAGES_PER_BLK) + page_to_store) > PAGES_PER_BLK)
    {
        uart_print("not here");
        nand_block_erase(bank, (DS_metablk_vpn - 1) / PAGES_PER_BLK);
        DS_set_metablk_vpn(bank, DS_metablk_lbn, ((DS_metablk_vpn - 1) / PAGES_PER_BLK) * PAGES_PER_BLK);
        //DS_metablk_vpn = DS_get_metablk_vpn(bank, DS_metablk_lbn);
    }

    //블록의 저장되는 첫번째 페이지를 아이노드에 등록해야 나중에 오픈할때 읽을 지점을 정한다.
    //현재 페이지 "다음"부터 저장되므로 1을 더해서 미리 저장한다.
    //inode->vpn = (DS_get_metablk_vpn(bank, DS_metablk_lbn) + 1 )% PAGE_PER_BLK;
    int vpn = DS_get_metablk_vpn(bank, DS_metablk_lbn);

//128bytes씩 복사하겠다.

   //inode 복사하겠다.
    //아이노드 복사
    flash_finish();
    //뒤에서 inode업데이트후 복사하는게 맞는데..
  
    //mem_copy(FTL_BUF(bank), v_inode, DS_INODE_SIZE);//inode=128bytes(DRAM_ECC_UNIT)
    //copy_bytes += DS_INODE_SIZE;
    //2b80만 DMA가 안되는거같아서..
    //mem_copy가 작동안하는 경우가많어...왠지 알야아하지싶긴한데..
    //이런방식이 얼마나 오버헤드를 많이 발생할지 모르겠음....미지수.
    if(1)//vpn==0x2b80)
    {
       // uart_print("2b80..");
        for(i=0; i<DS_FTL_SIZE; i+=4)
        {
            //DS_PA_to_VA(ftl_chunk_addr+i)
           // UINT32 tp = read_dram_32(v_inode+i);
            UINT32 tp = read_dram_32((UINT32)v_inode+i);
            write_dram_32(FTL_BUF(bank)+i, tp);
        }
    }
    else
    {
       // uart_printf("%x %x %x", FTL_BUF(bank), v_inode, DS_INODE_SIZE);
        mem_copy(FTL_BUF(bank), v_inode, DS_INODE_SIZE);//inode=128bytes(DRAM_ECC_UNIT)
  
    }
    
    copy_bytes += DS_INODE_SIZE;

    
    for(ftl_chunk=0; ftl_chunk<num_FTLchunk; ftl_chunk++)
    {
        //매번 페이지만큼 복사되고 나면 nand에 써야함.
        if(copy_bytes % BYTES_PER_PAGE == 0 )
        {
            DS_metablk_vpn = DS_get_metablk_vpn(bank, DS_metablk_lbn);

            DS_inc_metablk_vpn(bank, DS_metablk_lbn);

            nand_page_ptprogram(bank,
                                DS_metablk_vpn / PAGES_PER_BLK,
                                DS_metablk_vpn % PAGES_PER_BLK,
                                0,
                                copy_bytes / BYTES_PER_SECTOR,
                                FTL_BUF(bank));
            
            copy_bytes=0;
            
            flash_finish();
        }

        ftl_chunk_addr = DS_get_FTLchunk_addr(inode, ftl_chunk);
        //여기까진 올바름
       
        ftl_chunk_addr = DS_PA_to_VA(ftl_chunk_addr);
     
        for(i=0; i<DS_FTL_SIZE; i+=4)
        {
            UINT32 tp = read_dram_32((UINT32)ftl_chunk_addr+i);
            write_dram_32(FTL_BUF(bank)+copy_bytes+i, tp);
        }
        copy_bytes += DS_FTL_SIZE;
     }
    
    //남은 쩌리짱도 처리하기.
    if(copy_bytes > 0)
    {
        //DS_inc_metablk_vpn(bank, DS_metablk_lbn);
        DS_metablk_vpn = DS_get_metablk_vpn(bank, DS_metablk_lbn);

        nand_page_ptprogram(bank,
                            DS_metablk_vpn / PAGES_PER_BLK,
                            DS_metablk_vpn % PAGES_PER_BLK,
                            0,
                            (copy_bytes+BYTES_PER_SECTOR-1) / BYTES_PER_SECTOR,
                            FTL_BUF(bank));

        copy_bytes=0;
        flash_finish();   
    }
    //close되기 직전 vpn을 재설정한다.
    //이렇게 하면 open시 시작점 페이지부터 읽을 수 있다.
    DS_set_metablk_vpn(bank, DS_metablk_lbn, vpn);
}
 
void DS_load_inode(INODE* inode, UINT32 ino)
{
    UINT32 bank = ino % 8;
    UINT32 DS_metablk_lbn = ino / 8;
    //UINT32 DS_metablk_pn;
    UINT32 DS_metablk_vpn;
    //UINT32 load_bytes;
    int chunk_idx;
   // UINT32 ftl_chunk_addr;
    UINT32 load_bytes=0;
    UINT32 tvpn;
 
   
   // int i=1;

    flash_finish();
  //  UINT32 vpn = DS_get_metablk_vpn(bank, DS_metablk_lbn);

    //첫 페이지 통째로 read
    nand_page_ptread(bank,
                    DS_get_metablk_vpn(bank, DS_metablk_lbn) / PAGES_PER_BLK,
                    DS_get_metablk_vpn(bank, DS_metablk_lbn) % PAGES_PER_BLK,
                    0,
                    BYTES_PER_PAGE / BYTES_PER_SECTOR,   //1페이지만큼의 섹터수(섹터수가 단위이므로)
                    FTL_BUF(bank),
                    RETURN_ON_ISSUE);
    flash_finish();
    tvpn = DS_get_metablk_vpn(bank, DS_metablk_lbn);
   
    DS_inc_metablk_vpn(bank, DS_metablk_lbn);

    INODE* v_inode = (INODE*)DS_PA_to_VA((UINT32)inode);
  
    mem_copy(v_inode, FTL_BUF(bank), DS_INODE_SIZE);
    DS_inode_set_FTL_NULL(inode);

    load_bytes += DS_INODE_SIZE;

      //FTL chunk는 128bytes의 각 FTL 덩어리를 의미함.
    UINT32 DS_filesize_in_FTLchunk = BYTES_PER_PAGE * (DS_FTL_SIZE/4); //32KB*32 = 1MB
    //우리는 FTL의 청크 단위로 복사를할것임.
    UINT32 num_FTLchunk = (inode->size + DS_filesize_in_FTLchunk -1) / (DS_filesize_in_FTLchunk);
    //UINT32 ftl_chunk[FILE_FTL_NUM];
    FILE_FTL ftl_chunk;
    
   // uart_printf("num_FTLchunk : %d", num_FTLchunk);

    //chunk단위로 읽어온다. ftl_chunk는 128bytes짜리 ftl 청크.
    for(chunk_idx=0; chunk_idx<num_FTLchunk; chunk_idx++)
    {
        //uart_printf("?");
        //page단위로 읽어와야한다.
        if(load_bytes % BYTES_PER_PAGE == 0 )
        {
            tvpn = DS_get_metablk_vpn(bank, DS_metablk_lbn);
     
            flash_finish();
            nand_page_ptread(bank,
                            DS_get_metablk_vpn(bank, DS_metablk_lbn) / PAGES_PER_BLK,
                            DS_get_metablk_vpn(bank, DS_metablk_lbn) % PAGES_PER_BLK,
                            0,
                            load_bytes / BYTES_PER_SECTOR,   //1페이지만큼의 섹터수(섹터수가 단위이므로)
                            FTL_BUF(bank),
                            RETURN_ON_ISSUE);
            flash_finish();
            DS_inc_metablk_vpn(bank, DS_metablk_lbn);

            load_bytes=0;
        }

        mem_copy(&ftl_chunk, FTL_BUF(bank)+load_bytes, DS_FTL_SIZE);
        load_bytes += DS_FTL_SIZE;
        DS_set_FTLChunk(inode, &ftl_chunk, chunk_idx);
    }
    
    //load한 다음 포인터가 꽉 찼을 경우가 있으면, 다 지워줘야한다.
    DS_metablk_vpn = DS_get_metablk_vpn(bank, DS_metablk_lbn);

    if((DS_metablk_vpn % PAGES_PER_BLK) == 0)
    {
       // uart_printf(" not here");
        nand_block_erase(bank, (DS_metablk_vpn - 1 ) / PAGES_PER_BLK);
        DS_set_metablk_vpn(bank, DS_metablk_lbn, ((DS_metablk_vpn - 1) / PAGES_PER_BLK) * PAGES_PER_BLK);
        //DS_metablk_vpn = DS_get_metablk_vpn(bank, DS_metablk_lbn);
    }

    flash_finish();
}



// load flushed FTL metadta
static void load_metadata(void)
{
    load_misc_metadata();
    load_pmap_table();
}
// misc + VCOUNT
static void load_misc_metadata(void)
{
    UINT32 misc_meta_bytes = NUM_MISC_META_SECT * BYTES_PER_SECTOR;
    UINT32 vcount_bytes    = NUM_VCOUNT_SECT * BYTES_PER_SECTOR;
    UINT32 vcount_addr     = VCOUNT_ADDR;
    UINT32 vcount_boundary = VCOUNT_ADDR + VCOUNT_BYTES;

    UINT32 load_flag = 0;
    UINT32 bank, page_num;
    UINT32 load_cnt = 0;

    flash_finish();

	disable_irq();
	flash_clear_irq();	// clear any flash interrupt flags that might have been set

    // scan valid metadata in descending order from last page offset
    for (page_num = PAGES_PER_BLK - 1; page_num != ((UINT32) -1); page_num--)
    {
        for (bank = 0; bank < NUM_BANKS; bank++)
        {
            if (load_flag & (0x1 << bank))
            {
                continue;
            }
            // read valid metadata from misc. metadata area
            nand_page_ptread(bank,
                             MISCBLK_VBN,
                             page_num,
                             0,
                             NUM_MISC_META_SECT + NUM_VCOUNT_SECT,
                             FTL_BUF(bank),
                             RETURN_ON_ISSUE);
        }
        flash_finish();

        for (bank = 0; bank < NUM_BANKS; bank++)
        {
            if (!(load_flag & (0x1 << bank)) && !(BSP_INTR(bank) & FIRQ_ALL_FF))
            {
                load_flag = load_flag | (0x1 << bank);
                load_cnt++;
            }
            CLR_BSP_INTR(bank, 0xFF);
        }
    }
    ASSERT(load_cnt == NUM_BANKS);

    for (bank = 0; bank < NUM_BANKS; bank++)
    {
        // misc. metadata
        mem_copy(&g_misc_meta[bank], FTL_BUF(bank), sizeof(misc_metadata));

        // vcount metadata
        if (vcount_addr <= vcount_boundary)
        {
            mem_copy(vcount_addr, FTL_BUF(bank) + misc_meta_bytes, vcount_bytes);
            vcount_addr += vcount_bytes;

        }
    }
	enable_irq();
}
static void load_pmap_table(void)
{
    UINT32 pmap_addr = PAGE_MAP_ADDR;
    UINT32 temp_page_addr;

    UINT32 pmap_bytes = BYTES_PER_PAGE; // per bank

    UINT32 pmap_boundary = PAGE_MAP_ADDR + (NUM_LPAGES * sizeof(UINT32)); // no key

    //key : 바운더리를 두배로 키운다. 이래도 되는지는 잘 몰라.
    //UINT32 pmap_boundary = PAGE_MAP_ADDR + (NUM_LPAGES * sizeof(UINT32) * 2); //key
    //uart_printf("[load_pmap_table]\n");
    //uart_printf("NUM_LPAGES : %d, MAPBLKS_PER_BANK : %d\n", NUM_LPAGES, MAPBLKS_PER_BANK);

    UINT32 mapblk_lbn, bank;
    BOOL32 finished = FALSE;
    flash_finish();

  //  uart_printf("load pmap table! key");

//page단위임을 명심하자!
//key field추가했으므로 MAPBLKS_PER_BANK 가 두배 증가하였다. 과연 이래도 잘 작동하는지가 포인트!
    for (mapblk_lbn = 0; mapblk_lbn < MAPBLKS_PER_BANK; mapblk_lbn++) //with key
    {
        temp_page_addr = pmap_addr; // backup page mapping addr

        for (bank = 0; bank < NUM_BANKS; bank++)
        {
            if (finished)
            {
                break;
            }
            else if (pmap_addr >= pmap_boundary)
            {
                finished = TRUE;
                break;
            }
            else if (pmap_addr + BYTES_PER_PAGE >= pmap_boundary)
            {
                finished = TRUE;
                pmap_bytes = (pmap_boundary - pmap_addr + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR;
            }
            // read page mapping table from map_block
            //buffer로 flash의 맵핑테이블 페이지크기만큼 로드해온다
            nand_page_ptread(bank,
                             get_mapblk_vpn(bank, mapblk_lbn) / PAGES_PER_BLK,
                             get_mapblk_vpn(bank, mapblk_lbn) % PAGES_PER_BLK,
                             0,
                             pmap_bytes / BYTES_PER_SECTOR,   //1페이지만큼의 섹터수(섹터수가 단위이므로)
                             FTL_BUF(bank),
                             RETURN_ON_ISSUE);
            pmap_addr += pmap_bytes;
        }

        flash_finish();

        pmap_bytes = BYTES_PER_PAGE;
        for (bank = 0; bank < NUM_BANKS; bank++)
        {
            if (temp_page_addr >= pmap_boundary)
            {
                break;
            }
            else if (temp_page_addr + BYTES_PER_PAGE >= pmap_boundary)
            {
                pmap_bytes = (pmap_boundary - temp_page_addr + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR;
            }
            // copy page mapping table to PMAP_ADDR from FTL buffer
            mem_copy(temp_page_addr, FTL_BUF(bank), pmap_bytes);

            temp_page_addr += pmap_bytes;
        }
        if (finished)
        {
            break;
        }

        //이슈1. 현재 flash로부터 얼마나 가져오는지 체크하고 실제 테이블크기를 구해보자.
    }

      ///실험결과 flash 에 할당 무리없이 되는듯함. flash 가 필요한 맵핑테이블보다 크므로 okay 통과
    //  uart_printf("flash mapping table size : %d , real mapping table size : %d", pmap_addr - PAGE_MAP_ADDR, (NUM_LPAGES * sizeof(UINT32) )); //no key
    //    uart_printf("flash mapping table size : %d , real mapping table size : %d", pmap_addr - PAGE_MAP_ADDR, (NUM_LPAGES * sizeof(UINT32) * 2)); //key
    //  uart_printf("NUM_BANK : %d, MAPBLKS_PER_BANK : %d", NUM_BANKS, MAPBLKS_PER_BANK);

}


static void write_format_mark(void)
{
	// This function writes a format mark to a page at (bank #0, block #0).

	#ifdef __GNUC__
	extern UINT32 size_of_firmware_image;
	UINT32 firmware_image_pages = (((UINT32) (&size_of_firmware_image)) + BYTES_PER_FW_PAGE - 1) / BYTES_PER_FW_PAGE;
	#else
	extern UINT32 Image$$ER_CODE$$RO$$Length;
	extern UINT32 Image$$ER_RW$$RW$$Length;
	UINT32 firmware_image_bytes = ((UINT32) &Image$$ER_CODE$$RO$$Length) + ((UINT32) &Image$$ER_RW$$RW$$Length);
	UINT32 firmware_image_pages = (firmware_image_bytes + BYTES_PER_FW_PAGE - 1) / BYTES_PER_FW_PAGE;
	#endif

	UINT32 format_mark_page_offset = FW_PAGE_OFFSET + firmware_image_pages;

	mem_set_dram(FTL_BUF_ADDR, 0, BYTES_PER_SECTOR);

	SETREG(FCP_CMD, FC_COL_ROW_IN_PROG);
	SETREG(FCP_BANK, REAL_BANK(0));
	SETREG(FCP_OPTION, FO_E | FO_B_W_DRDY);
	SETREG(FCP_DMA_ADDR, FTL_BUF_ADDR); 	// DRAM -> flash
	SETREG(FCP_DMA_CNT, BYTES_PER_SECTOR);
	SETREG(FCP_COL, 0);
	SETREG(FCP_ROW_L(0), format_mark_page_offset);
	SETREG(FCP_ROW_H(0), format_mark_page_offset);

	// At this point, we do not have to check Waiting Room status before issuing a command,
	// because we have waited for all the banks to become idle before returning from format().
	SETREG(FCP_ISSUE, NULL);

	// wait for the FC_COL_ROW_IN_PROG command to be accepted by bank #0
	while ((GETREG(WR_STAT) & 0x00000001) != 0);

	// wait until bank #0 finishes the write operation
	while (BSP_FSM(0) != BANK_IDLE);
}
static BOOL32 check_format_mark(void)
{
	// This function reads a flash page from (bank #0, block #0) in order to check whether the SSD is formatted or not.

	#ifdef __GNUC__
	extern UINT32 size_of_firmware_image;
	UINT32 firmware_image_pages = (((UINT32) (&size_of_firmware_image)) + BYTES_PER_FW_PAGE - 1) / BYTES_PER_FW_PAGE;
	#else
	extern UINT32 Image$$ER_CODE$$RO$$Length;
	extern UINT32 Image$$ER_RW$$RW$$Length;
	UINT32 firmware_image_bytes = ((UINT32) &Image$$ER_CODE$$RO$$Length) + ((UINT32) &Image$$ER_RW$$RW$$Length);
	UINT32 firmware_image_pages = (firmware_image_bytes + BYTES_PER_FW_PAGE - 1) / BYTES_PER_FW_PAGE;
	#endif

	UINT32 format_mark_page_offset = FW_PAGE_OFFSET + firmware_image_pages;
	UINT32 temp;

	flash_clear_irq();	// clear any flash interrupt flags that might have been set

	SETREG(FCP_CMD, FC_COL_ROW_READ_OUT);
	SETREG(FCP_BANK, REAL_BANK(0));
	SETREG(FCP_OPTION, FO_E);
	SETREG(FCP_DMA_ADDR, FTL_BUF_ADDR); 	// flash -> DRAM
	SETREG(FCP_DMA_CNT, BYTES_PER_SECTOR);
	SETREG(FCP_COL, 0);
	SETREG(FCP_ROW_L(0), format_mark_page_offset);
	SETREG(FCP_ROW_H(0), format_mark_page_offset);

	// At this point, we do not have to check Waiting Room status before issuing a command,
	// because scan list loading has been completed just before this function is called.
	SETREG(FCP_ISSUE, NULL);

	// wait for the FC_COL_ROW_READ_OUT command to be accepted by bank #0
	while ((GETREG(WR_STAT) & 0x00000001) != 0);

	// wait until bank #0 finishes the read operation
	while (BSP_FSM(0) != BANK_IDLE);

	// Now that the read operation is complete, we can check interrupt flags.
	temp = BSP_INTR(0) & FIRQ_ALL_FF;

	// clear interrupt flags
	CLR_BSP_INTR(0, 0xFF);

	if (temp != 0)
	{
		return FALSE;	// the page contains all-0xFF (the format mark does not exist.)
	}
	else
	{
		return TRUE;	// the page contains something other than 0xFF (it must be the format mark)
	}
}

// BSP interrupt service routine
void ftl_isr(void)
{
    UINT32 bank;
    UINT32 bsp_intr_flag;

    uart_print("BSP interrupt occured...");
    // interrupt pending clear (ICU)
    SETREG(APB_INT_STS, INTR_FLASH);

    for (bank = 0; bank < NUM_BANKS; bank++) {
        while (BSP_FSM(bank) != BANK_IDLE);
        // get interrupt flag from BSP
        bsp_intr_flag = BSP_INTR(bank);

        if (bsp_intr_flag == 0) {
            continue;
        }
        UINT32 fc = GETREG(BSP_CMD(bank));
        // BSP clear
        CLR_BSP_INTR(bank, bsp_intr_flag);

        // interrupt handling
		if (bsp_intr_flag & FIRQ_DATA_CORRUPT) {
           // uart_printf("BSP interrupt at bank: 0x%x", bank);
            uart_print("BSP interrupt at bank:..");

            uart_print("FIRQ_DATA_CORRUPT occured...");
		}
		if (bsp_intr_flag & (FIRQ_BADBLK_H | FIRQ_BADBLK_L)) {
            uart_print("BSP interrupt at bank: ");
			if (fc == FC_COL_ROW_IN_PROG || fc == FC_IN_PROG || fc == FC_PROG) {
                uart_print("find runtime bad block when block program...");
			}
			else {
               // uart_printf("find runtime bad block when block erase...vblock #: %d", GETREG(BSP_ROW_H(bank)) / PAGES_PER_BLK);
               uart_print("find runtime bad block when block erase...vblock #:")
				ASSERT(fc == FC_ERASE);
			}
		}
    }
}
