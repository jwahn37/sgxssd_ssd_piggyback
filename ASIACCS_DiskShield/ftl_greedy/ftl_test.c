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
// Synthetic test cases for verifying FTL logic
//
#if OPTION_FTL_TEST == TRUE

#include "jasmine.h"
#include "ftl_sgx.h"
#include "sha256_sgx.h"

#include <stdlib.h>

#define IO_LIMIT           (NUM_LSECTORS)
#define RANDOM_SEED        (IO_LIMIT)
#define NUM_PSECTORS_4KB   ((4 * 1024) / 512)
#define NUM_PSECTORS_8KB   (NUM_PSECTORS_4KB << 1)
#define NUM_PSECTORS_16KB  (NUM_PSECTORS_8KB << 1)
#define NUM_PSECTORS_32KB  (NUM_PSECTORS_16KB << 1)
#define NUM_PSECTORS_64KB  (NUM_PSECTORS_32KB << 1)
#define NUM_PSECTORS_128KB ((128 * 1024) / 512)
#define NUM_PSECTORS_256KB ((256 * 1024) / 512)

extern UINT32 g_ftl_read_buf_id;
extern UINT32 g_ftl_write_buf_id;

UINT8 DS_auth_write(UINT32 version, SGX_PARAM sgx_param, UINT32 size, UINT8* buf, UINT8* mac_host, UINT8* key);

static void tc_write_seq(const UINT32 start_lsn, const UINT32 io_num, const UINT32 sector_size);
static void tc_write_rand(const UINT32 start_lsn, const UINT32 io_num, const UINT32 sector_size);


//malloc 써보자
typedef struct RDAFWR_Q{
	UINT8 cmd;
	UINT32 version;
	UINT32 return_msg;
	UINT32 fid;
//	struct RDAFWR_Q *next;
}RDAFWR_Q;

static RDAFWR_Q *rdafwr_queue=NULL;
static UINT8 rdafwrq_front, rdafwrq_rear;
static UINT8 rdafwrq_bmp[8]={0,0,0,0,0,0,0,0};
static UINT8 rdafwrq_init=1;

static int num_rdafwrQ = 0;


static void rdafwr_q_push(const UINT8 cmd, const UINT32 version, const UINT32 return_msg, const UINT32 fid)
{
	uart_printf("push v %d", version);
	if(rdafwrq_init)
	{
		rdafwrq_front=0;
		rdafwrq_rear=0;
		rdafwrq_init=0;
		rdafwr_queue = (RDAFWR_Q*)(RDAFWRQ_ADDR);
	}

	//빈 index를 찾아라.
	//ring queue이니까
	UINT8 i;
	UINT8 full=1;
	for(i=rdafwrq_rear; ; i++)
	{
		if(i==RDAFWRQ_SIZE)	//ring queue
		{
			i=0;
		}

		UINT8 cur_byte = rdafwrq_bmp[i/8];	//current byte of bmp.
		if((cur_byte & (0x01<<(7-i%8))) == 0)	//the index(i) is empty
		{
			rdafwrq_rear = i;
			//push to i-idx
			*((UINT8*)DS_VA_to_PA(&rdafwr_queue[rdafwrq_rear].cmd)) = cmd;
			*((UINT32*)DS_VA_to_PA(&rdafwr_queue[rdafwrq_rear].version)) = version;
			*((UINT32*)DS_VA_to_PA(&rdafwr_queue[rdafwrq_rear].return_msg)) = return_msg;
			*((UINT32*)DS_VA_to_PA(&rdafwr_queue[rdafwrq_rear].fid)) = fid;

			rdafwrq_bmp[i/8] = cur_byte | (((UINT8)0x01)<<(7-i%8));
			full=0;
			num_rdafwrQ++;

			break;
		}
		if(i==rdafwrq_front)
		{
			full=1;
			break;
		}
	}
	if(full==1)
	{
		;
		//uart_print("rdafwrq full");

		//uart_printf("rdafwrq full %x", rdafwrq_bmp[i/8]);
	}

	/*
	RDAFWR_Q* cur = (RDAFWR_Q*) malloc (sizeof(RDAFWR_Q));
	cur->cmd = cmd;
	cur->version = version;
	cur->return_msg = return_msg;
	cur->next = NULL;
	cur->fid = fid;
	
	if(rdafwr_queue)
	{
		//rdafwr_queue->next = cur;
		cur->next = rdafwr_queue;
		rdafwr_queue = cur;
	}
	else
		rdafwr_queue = cur;
	
/*
	RDAFWR_Q cur;
	cur.cmd = cmd;
	cur.version = version;
	cur.return_msg = return_msg;
	cur.next = NULL;
	cur.fid = fid;
	
	if(0)
	{
		//rdafwr_queue->next = cur;
		//cur->next = rdafwr_queue;
		rdafwr_queue = cur;
	}
	else
		rdafwr_queue = cur;

*/
	/*
	if(num_rdafwrQ>=2)
		uart_printf("rdafwrq push >=2");
	*/
}
static UINT8 rdafwr_q_pop(const UINT8 cmd, const UINT32 version, UINT32 *return_msg, UINT32 *fid)
{
//빈 index를 찾아라.
	//ring queue이니까
	
	//uart_printf("pop v %d", version);
	UINT8 i;
	UINT8 empty=1;
	UINT8 front_flag=1;
	for(i=rdafwrq_front; ; i++)
	{
		if(i==RDAFWRQ_SIZE)	//ring queue
		{
			i=0;
		}

		UINT8 cur_byte = rdafwrq_bmp[i/8];	//current byte of bmp.
		if((cur_byte & (0x01<<(7-i%8))) != 0)	//the index(i) is fulll
		{
			//rdafwrq_front = i;
			//push to i-idx
			

			//if(read_dram_8(&rdafwr_queue[i].cmd)==cmd && read_dram_32(&rdafwr_queue[i].version)==version)
			if(1)
			{
				*return_msg = read_dram_32(&rdafwr_queue[i].return_msg);
				*fid = read_dram_32(&rdafwr_queue[i].fid);

				rdafwrq_bmp[i/8] = cur_byte & (~(((UINT8)0x01)<<(7-i%8)));
				empty=0;
				rdafwrq_front = i;
				num_rdafwrQ--;
			//	if(num_rdafwrQ>=1)
			//		uart_print("num_rdafwrQ >= 1");
			//	else if (num_rdafwrQ==2)
					
				return 1;
			}
			front_flag=0;
			/*
			else if(front_flag==1)
			{
				front_flag=0;
				rdafwrq_front = i;
			}
			*/
		}
		else if(front_flag==1)
		{
			//front_flag=0;
			rdafwrq_front = i;
		}
		
		
		if(i==rdafwrq_rear)
		{
			empty=1;
			break;
		}
	}
	if(empty==1)
	{
		//UINT8 tp_cmd = read_dram_8(&rdafwr_queue[0].cmd);
		//UINT32 tp_v = read_dram_32(&rdafwr_queue[0].version);
		//if(tp_v==0)
	//	uart_print("rdafwr not found");
	//	uart_printf("rdafwrq not found %d %d\n", rdafwrq_front, rdafwrq_rear);
		return 0;
	}
	return 0;
	
/*	


	RDAFWR_Q *cur, *prev;
	cur = rdafwr_queue;
//	prev = cur;

	if(cur->cmd == cmd && cur->version == version)
	{
		*return_msg = cur->return_msg;
		*fid = cur->fid;
		rdafwr_queue = cur->next;

		free(cur);
		num_rdafwrQ--;
			/*
			if(num_rdafwrQ==0)
			{
			}
*/
/*
		return 1;
	}
	
	prev = cur;
	cur = cur->next;
	do{
		if(cur->cmd == cmd && cur->version == version)
		{
			*return_msg = cur->return_msg;
			*fid = cur->fid;

			prev->next = cur->next;
			free(cur);
			num_rdafwrQ--;
			/*
			if(num_rdafwrQ==0)
			{
			}
			*/
		/*
			return 1;
		}
		prev = cur;
		cur = cur->next;
	}while(cur);
*/
/*


	RDAFWR_Q cur;
	cur = rdafwr_queue;
//	prev = cur;

	if(cur.cmd == cmd && cur.version == version)
	{
		*return_msg = cur.return_msg;
		*fid = cur.fid;
	//	rdafwr_queue = cur.next;

	//	free(cur);
		num_rdafwrQ--;
		return 1;
	}
	else
	{
		;
			*return_msg = cur.return_msg;
		*fid = cur.fid;
	//	rdafwr_queue = cur.next;

	//	free(cur);
		num_rdafwrQ--;
		return 1;
	}
	*/
	/*
	prev = cur;
	cur = cur->next;
	do{
		if(cur->cmd == cmd && cur->version == version)
		{
			*return_msg = cur->return_msg;
			*fid = cur->fid;

			prev->next = cur->next;
			free(cur);
			num_rdafwrQ--;
			return 1;
		}
		prev = cur;
		cur = cur->next;
	}while(cur);
	*/
//	return 1; //for evaluation
//	return 0;
}



//static void fillup_dataspace(void);
//static void aging_with_rw(UINT32 io_cnt);

/* void ftl_test(void) */
/* { */
/*     UINT32 i, j, wr_buf_addr, rd_buf_addr, data; */
/*     UINT32 lba, num_sectors = 128; */
/*     /\* UINT32 io_cnt = 1000; *\/ */
/*     /\* UINT32 const start_lba = 328064; *\/ */
/*     UINT32 io_cnt = 100000; */
/*     UINT32 r_data; */
/*     UINT32 const start_lba = 0; */

/* 	/\* g_barrier = 0; *\/ */
/* 	/\* while (g_barrier == 0); *\/ */
/*     /\* led(0); *\/ */

/*     // STEP 1 - write */
/*     for (UINT32 loop = 0; loop < 1; loop++) { */
/*         wr_buf_addr = WR_BUF_ADDR; */
/*         data = 0; */
/*         r_data = 0; */

/*         lba  = start_lba; */

/*         rd_buf_addr = RD_BUF_ADDR; */

/*         uart_print_32(loop); uart_print(""); */

/*         for (i = 0; i < io_cnt; i++) { */
/*             wr_buf_addr = WR_BUF_PTR(g_ftl_write_buf_id) + ((lba % SECTORS_PER_PAGE) * BYTES_PER_SECTOR); */
/*             r_data = data; */

/*             for (j = 0; j < num_sectors; j++) { */
/*                 mem_set_dram(wr_buf_addr, data, BYTES_PER_SECTOR); */

/*                 wr_buf_addr += BYTES_PER_SECTOR; */

/*                 if (wr_buf_addr >= WR_BUF_ADDR + WR_BUF_BYTES) { */
/*                     wr_buf_addr = WR_BUF_ADDR; */
/*                 } */
/*                 data++; */
/*             } */
/*             ftl_write(lba, num_sectors); */

/*             rd_buf_addr = RD_BUF_PTR(g_ftl_read_buf_id) + ((lba % SECTORS_PER_PAGE) * BYTES_PER_SECTOR); */
/*             ftl_read(lba, num_sectors); */

/*             flash_finish(); */

/*             for (j = 0; j < num_sectors; j++) { */
/*                 UINT32 sample = read_dram_32(rd_buf_addr); */

/*                 if (sample != r_data) { */
/*                     uart_printf("ftl test fail...io#: %d, %d", lba, num_sectors); */
/*                     uart_printf("sample data %d should be %d", sample, r_data); */
/*                     led_blink(); */
/*                 } */
/*                 rd_buf_addr += BYTES_PER_SECTOR; */

/*                 if (rd_buf_addr >= RD_BUF_ADDR + RD_BUF_BYTES) { */
/*                     rd_buf_addr = RD_BUF_ADDR; */
/*                 } */
/*                 r_data++; */
/*             } */
/*             lba += (num_sectors * 9); */

/*             if (lba >= (UINT32)NUM_LSECTORS) { */
/*                 lba = 0; */
/*             } */
/*         } */
/*     } */
/*     ftl_flush(); */
/* } */
#define WRITE_TEST 0
#define READ_TEST 1
void ftl_test(void)
{
    uart_print("DiskShield test!\n");
    
    //HMAC TEST
    int version=1;
    int sector_count = 8;
    SGX_PARAM sgx_param;
    sgx_param.cmd = DS_WRITE_WR;
    sgx_param.fid = 1;
    sgx_param.offset = 0;
    char data[4096];
    char mac[32];
    char key[16];
    int i;
    char file_name[8][16] = {"foo1.txt", "foo2.txt","foo3.txt","foo4.txt","foo5.txt","foo6.txt","foo7.txt","foo8.txt"};
    int fid;
    for(i=0; i<8; i++)
    {
        fid=DS_file_create(file_name[i], key);
        rdafwr_q_push(DS_CREATE_RD, version++, fid, fid);
        uart_printf("file created %d", fid);
    }   

    
/*
    for(i=0; i<4096; i++)
    {
        data[i]=1;
    }
    data[0]=3;
    data[1]=2;

    for(i=0; i<16; i++)
        key[i]=1;

    DS_auth_write(version, sgx_param, sector_count*512, data, mac, key);
    
*/

    /*
    uart_printf(" %d %d", NUM_WR_BUFFERS, NUM_RD_BUFFERS);
   // hmac_test();
    //tc_write_rand(const UINT32 start_lsn, const UINT32 io_num, const UINT32 sector_size)
  //  tc_write_seq(0, 1, NUM_PSECTORS_8KB);
  //  tc_write_rand(0, 1, NUM_PSECTORS_8KB);
   // DS_test();
   
    int fid;
    int i;
    
    char name[NAME_LEN]="foo0000.txt";
    UINT8 key[KEY_SIZE];

    DS_init_superblock();


    for(i=0; i<KEY_SIZE; i++)
    {
        key[i]=0xff;
    }
    */
/*
    //test 1 1023개의 4KB 파일 I/O 
    for(i=0; i<1023; i++)
    {
        name[6]++;
        if(name[6]>'9')
        {
            name[5]++;
            name[6]='0';
        }
        if(name[5]>'9')
        {
            name[4]++;
            name[5]='0';
        }
         if(name[4]>'9')
        {
            name[3]++;
            name[4]='0';
        }
          //if(i%10==0)
        //{
            uart_printf("%s",name);
        //}
        //uart_printf("%s", name);
        fid = DS_file_create(name, key);
        DS_file_close(fid);

    }
    
    memcpy(name, "foo0000.txt", 12);
    
    for(i=0; i<1023; i++)
    {
        //if(i%10==0)
        //{
        //    uart_printf("test..w.%d ",i);
        //}
        //name[0]=i;
        //memcpy(&name[7], &i, sizeof(int));
        //name[7+sizeof(int)]='\0';
        //memcpy(name, "foo.txt", 8);
        name[6]++;
        if(name[6]>'9')
        {
            name[5]++;
            name[6]='0';
        }
        if(name[5]>'9')
        {
            name[4]++;
            name[5]='0';
        }
         if(name[4]>'9')
        {
            name[3]++;
            name[4]='0';
        }
       

        fid = DS_file_open(name);   
        SGX_LBA sgx_lba;
        sgx_lba.fid = fid;
        sgx_lba.addr.offset = 0;
        sgx_tc_write_seq(sgx_lba, 1, 8, WRITE_TEST);
        DS_file_close(fid);

      //  fid = DS_file_open(name);   
       // sgx_tc_write_seq(sgx_lba, 1, 8, READ_TEST);
       // DS_file_close(fid);
    }

    memcpy(name, "foo0000.txt", 12);
    
    for(i=0; i<1023; i++)
    {
        //if(i%10==0)
        //{
     //       uart_printf("test..r.%d ",i);
        //}
        //name[0]=i;
        //memcpy(&name[7], &i, sizeof(int));
        //name[7+sizeof(int)]='\0';
        //memcpy(name, "foo.txt", 8);
        name[6]++;
        if(name[6]>'9')
        {
            name[5]++;
            name[6]='0';
        }
        if(name[5]>'9')
        {
            name[4]++;
            name[5]='0';
        }
         if(name[4]>'9')
        {
            name[3]++;
            name[4]='0';
        }
       

        fid = DS_file_open(name);   
        SGX_LBA sgx_lba;
        sgx_lba.fid = fid;
        sgx_lba.addr.offset = 0;
        sgx_tc_write_seq(sgx_lba, 1, 8, READ_TEST);
        DS_file_close(fid);

    }
*/

    //test2 : 단일 파일에 대해 512MB I/O
/*
 for(i=0; i<1; i++)
    {
        //if(i%10==0)
        //{
            uart_printf("test...%d ",i);
        //}
        //name[0]=i;
        //memcpy(&name[7], &i, sizeof(int));
        //name[7+sizeof(int)]='\0';
        //memcpy(name, "foo.txt", 8);
        name[6]++;
        if(name[6]>'9')
        {
            name[5]++;
            name[6]='0';
        }
        if(name[5]>'9')
        {
            name[4]++;
            name[5]='0';
        }
         if(name[4]>'9')
        {
            name[3]++;
            name[4]='0';
        }
        //uart_printf("%s", name);
        fid = DS_file_create(name, key);
        DS_file_close(fid);

    }
    memcpy(name, "foo0000.txt", 12);
    
    for(i=0; i<1; i++)
    {
        //if(i%10==0)
        //{
            uart_printf("test...%d ",i);
        //}
        //name[0]=i;
        //memcpy(&name[7], &i, sizeof(int));
        //name[7+sizeof(int)]='\0';
        //memcpy(name, "foo.txt", 8);
        name[6]++;
        if(name[6]>'9')
        {
            name[5]++;
            name[6]='0';
        }
        if(name[5]>'9')
        {
            name[4]++;
            name[5]='0';
        }
         if(name[4]>'9')
        {
            name[3]++;
            name[4]='0';
        }
       
    //현재 46MB성공, 47MB 실패
        fid = DS_file_open(name);   
        SGX_LBA sgx_lba;
        sgx_lba.fid = fid;
        sgx_lba.addr.offset = 0;
        sgx_tc_write_seq(sgx_lba, 128*1024, 8, WRITE_TEST);
      //  DS_file_close(fid);

       // fid = DS_file_open(name);   
        sgx_tc_write_seq(sgx_lba, 128*1024, 8, READ_TEST);
        DS_file_close(fid);
    }

*/
/*
    memcpy(name, "foo0000.txt", 12);
    for(i=0; i<512; i++)
    {
        name[6]++;
        if(name[6]>'9')
        {
            name[5]++;
            name[6]='0';
        }
        if(name[5]>'9')
        {
            name[4]++;
            name[5]='0';
        }
         if(name[4]>'9')
        {
            name[3]++;
            name[4]='0';
        }

        //fid = DS_file_create(name, key);
        SGX_LBA sgx_lba;
        sgx_lba.fid = fid;
        sgx_lba.addr.offset = 0;
      // sgx_tc_write_seq(sgx_lba, 1, 1, WRITE_TEST);
        //DS_file_close(fid);

        fid = DS_file_open(name);  
        uart_printf("%s %d",name, fid); 
        //sgx_tc_write_seq(sgx_lba, 1, 1, READ_TEST);
        DS_file_close(fid);

        //fid = DS_file_open(name);   
        //DS_file_close(fid);
    }
*/

/*
     //uart_printf("create) fid : %d", fid);
   
   
   
  
    uart_printf("opend) fid : %d", fid);

    SGX_LBA sgx_lba;
    sgx_lba.fid = fid;
    sgx_lba.addr.offset = 0;
    sgx_tc_write_seq(sgx_lba, 20000, NUM_PSECTORS_64KB, WRITE_TEST);
   // DS_print_all();
    uart_printf("////");
  
    DS_file_close(fid);
    fid = DS_file_open(name);
    uart_printf("////");

    uart_printf("reopend)fid : %d", fid);
    //DS_print_all();


    sgx_lba.fid = fid;
    sgx_lba.addr.offset = 0;
    sgx_tc_write_seq(sgx_lba, 20000, NUM_PSECTORS_64KB, READ_TEST);
    
    DS_file_close(fid);
    

   //uart_printf("seq  20000!");
  // sgx_tc_write_seq()
     //tc_write_seq(0,  20000, NUM_PSECTORS_64KB);
     //uart_printf("rand 20000!");
  //  tc_write_rand(0, 200000, NUM_PSECTORS_4KB);
     //tc_write_rand(0, 200000, NUM_PSECTORS_4KB);
    */ 
    uart_print("ftl test passed!");

}
/*
static void aging_with_rw(UINT32 io_cnt)
{
    UINT32 lba;
    UINT32 const num_sectors = SECTORS_PER_PAGE * NUM_BANKS;
    srand(RANDOM_SEED);

    uart_printf("start aging with random writes");
    while (io_cnt > 0) {
        do {
            lba = rand() % IO_LIMIT;
        }while (lba >= (NUM_LSECTORS - num_sectors));
        // page alignment
        lba = lba / SECTORS_PER_PAGE * SECTORS_PER_PAGE;
        ftl_write(lba, num_sectors);

        io_cnt--;
    }
    uart_printf("complete!");
}
*/
// fill entire dataspace
/*
static void fillup_dataspace(void)
{
    UINT32 lba = 0;
    UINT32 const num_sectors = SECTORS_PER_PAGE * NUM_BANKS;

    uart_printf("start fill entire data space");
    while (lba < (NUM_LSECTORS - num_sectors))
    {
        ftl_write(lba, num_sectors);
        lba += num_sectors;
    }
    uart_printf("complete!");
}
*/


static void tc_write_seq(const UINT32 start_lsn, const UINT32 io_num, const UINT32 sector_size)
{
    UINT32 i, j, wr_buf_addr, rd_buf_addr, data;
    UINT32 lba, num_sectors = sector_size;
    UINT32 io_cnt = io_num;
    UINT32 const start_lba = start_lsn;
    static UINT32 key=0;

    // UINT32 volatile g_barrier = 0; while (g_barrier == 0); 
    led(0);

    // STEP 1 - write
    //uart_print("w");
    for (UINT32 loop = 0; loop < 1; loop++)
    {
        wr_buf_addr = WR_BUF_ADDR;
        data = 0;
        lba  = start_lba;
        key = key+1; //매번 key 증가한다. 이래도 success뜨니??
        uart_print_32(loop); 
        //uart_printf("z %x", WR_BUF_ADDR);

        for (i = 0; i < io_cnt; i++)
        {
            wr_buf_addr = WR_BUF_PTR(g_ftl_write_buf_id) + ((lba % SECTORS_PER_PAGE) * BYTES_PER_SECTOR);
            for (j = 0; j < num_sectors; j++)
            {   
               // uart_printf("f %x %d", WR_BUF_PTR(g_ftl_write_buf_id), g_ftl_write_buf_id);
                mem_set_dram(wr_buf_addr, data, BYTES_PER_SECTOR);
               // uart_printf("k");
                wr_buf_addr += BYTES_PER_SECTOR;

                if (wr_buf_addr >= WR_BUF_ADDR + WR_BUF_BYTES)
                {
                    wr_buf_addr = WR_BUF_ADDR;
                }
                data++;
            }
         //   uart_printf("%x %d", lba, num_sectors);
            ptimer_start();
           // ftl_write(lba, num_sectors, key);
            ftl_write(lba, num_sectors);
            ptimer_stop_and_uart_print();

            lba += num_sectors;

            if (lba >= (UINT32)NUM_LSECTORS)
            {
                uart_print("adjust lba because of out of lba");
                lba = 0;
            }
        }

        // STEP 2 - read and verify
        rd_buf_addr = RD_BUF_ADDR;
        data = 0;
        lba  = start_lba;
        num_sectors = MIN(num_sectors, NUM_RD_BUFFERS * SECTORS_PER_PAGE);

        for (i = 0; i < io_cnt; i++)
        {
            rd_buf_addr = RD_BUF_PTR(g_ftl_read_buf_id) + ((lba % SECTORS_PER_PAGE) * BYTES_PER_SECTOR);
            // ptimer_start(); 
            //ftl_read(lba, num_sectors, key);
            ptimer_start();
            ftl_read(lba, num_sectors);
            flash_finish();        
            ptimer_stop_and_uart_print();
  
      
            // ptimer_stop_and_uart_print(); 

            for (j = 0; j < num_sectors; j++)
            {
                UINT32 sample = read_dram_32(rd_buf_addr);

                if (sample != data)
                {
                    uart_print("ftl test fail");
                 //   uart_printf("ftl test fail...io#: %d, %d", lba, num_sectors);
                 //   uart_printf("sample data %d should be %d", sample, data);
                    led_blink();
                }

                rd_buf_addr += BYTES_PER_SECTOR;

                if (rd_buf_addr >= RD_BUF_ADDR + RD_BUF_BYTES)
                {
                    rd_buf_addr = RD_BUF_ADDR;
                }
                data++;
            }

            lba += num_sectors;

            if (lba >= IO_LIMIT + num_sectors)
            {
                lba = 0;
            }
        }
    }
    ftl_flush();
}


void sgx_tc_write_seq(const SGX_LBA sgx_lba, const UINT32 io_num, const UINT32 sector_size, char TEST_IO)
//static void sgx_tc_write_seq(const UINT32 start_lsn, const UINT32 io_num, const UINT32 sector_size)
{
    UINT32 i, j, wr_buf_addr, rd_buf_addr, data;
    UINT32 lba, num_sectors = sector_size;
    UINT32 io_cnt = io_num;
    UINT32 const start_lba = sgx_lba.addr.offset / BYTES_PER_SECTOR;
    SGX_LBA cur_sgx_lba;
    static UINT32 key=0;
    //UINT32 start_lba;
   // uart_printf("[sgx_tc_write_seq] %d", TEST_IO);
    // UINT32 volatile g_barrier = 0; while (g_barrier == 0); 
    led(0);

    // STEP 1 - write
    for (UINT32 loop = 0; loop < 1; loop++)
    {
       if(TEST_IO == WRITE_TEST)
       {

        wr_buf_addr = WR_BUF_ADDR;
        data = 0;
        lba  = start_lba;
        cur_sgx_lba = sgx_lba;
       // key = key+1; //매번 key 증가한다. 이래도 success뜨니??
       // uart_print_32(loop); uart_print("");

        for (i = 0; i < io_cnt; i++)
        {
             
            wr_buf_addr = WR_BUF_PTR(g_ftl_write_buf_id) + ((lba % SECTORS_PER_PAGE) * BYTES_PER_SECTOR);
            //sector단위로 data 를 쓴다. 일단 write buffer에 쓴다.
            for (j = 0; j < num_sectors; j++)
            {
                mem_set_dram(wr_buf_addr, data, BYTES_PER_SECTOR);

                wr_buf_addr += BYTES_PER_SECTOR;

                if (wr_buf_addr >= WR_BUF_ADDR + WR_BUF_BYTES)
                {
                    wr_buf_addr = WR_BUF_ADDR;
                }
                data++;
            }
            //ptimer_start();
            //ftl_write(lba, num_sectors, key);
            //sgx_lba.addr.position = lba*BYTES_PER_SECTOR;
            //uart_printf("ftl_write : eid %d, fid %d, pos : %d, size : %d\n",\
            cur_sgx_lba.enclave_id, cur_sgx_lba.file_id, cur_sgx_lba.addr.position\
            , num_sectors*BYTES_PER_SECTOR);
       //     uart_printf("test sgx ftl write!");
           // sgx_ftl_write(cur_sgx_lba, 1, num_sectors*BYTES_PER_SECTOR);
           //uart_pritnf("DS_ftl_write call!\n");
            //uart_printf("[sgx_tw_write_sq] lba : %d", lba);
            //uart_printf("offset : %d", cur_sgx_lba.addr.offset);
            //uart_print("DS_ftl_write");
          
            DS_ftl_write(cur_sgx_lba, num_sectors*BYTES_PER_SECTOR);
           // ftl_write(lba, num_sectors);
          
            //uart_print("DS_ftl_finish");
            //ptimer_stop_and_uart_print();

            lba += num_sectors;
            cur_sgx_lba.addr.offset = lba*BYTES_PER_SECTOR;
            if((lba-start_lba) % (1024*2)==0)
                uart_printf("%d", lba-start_lba);    
            

            if (lba >= (UINT32)NUM_LSECTORS)
            {
                uart_print("adjust lba because of out of lba");
                lba = 0;
                cur_sgx_lba.addr.offset = lba*BYTES_PER_SECTOR;
            }
        }
            uart_print("write test finish!");
        }
        
        else if(TEST_IO == READ_TEST){
        // STEP 2 - read and verify
        rd_buf_addr = RD_BUF_ADDR;
        data = 0;
        lba  = start_lba;
        cur_sgx_lba = sgx_lba;
        //cur_sgx_lba.addr.offset = lba*BYTES_PER_SECTOR;
        num_sectors = MIN(num_sectors, NUM_RD_BUFFERS * SECTORS_PER_PAGE);
         
        for (i = 0; i < io_cnt; i++)
        {
            rd_buf_addr = RD_BUF_PTR(g_ftl_read_buf_id) + ((lba % SECTORS_PER_PAGE) * BYTES_PER_SECTOR);
            // ptimer_start(); 
            //uart_printf("ftl_read : eid %d, fid %d, pos : %d, size : %d\n",\
            cur_sgx_lba.enclave_id, cur_sgx_lba.file_id, cur_sgx_lba.addr.position\
            , num_sectors*BYTES_PER_SECTOR);
           // uart_print("DS_ftl_read");
            DS_ftl_read(cur_sgx_lba, num_sectors*BYTES_PER_SECTOR);
          // ftl_read(lba, num_sectors);
           // uart_print("DS_ftl_finish");
           // sgx_ftl_read(cur_sgx_lba, num_sectors*BYTES_PER_SECTOR);
            //ftl_read(lba, num_sectors, key);
             //uart_printf("test sgx read write!");
            flash_finish();
            //ptimer_stop_and_uart_print(); 

            for (j = 0; j < num_sectors; j++)
            {
                UINT32 sample = read_dram_32(rd_buf_addr);

                if (sample != data)
                {
                   // uart_print("test fail...");
                   uart_printf("ftl test fail...io#: %d, %d %d %d", lba, num_sectors, sample, data);
                    //uart_printf("sample data %d should be %d", sample, data);
                    led_blink();
                }

                rd_buf_addr += BYTES_PER_SECTOR;

                if (rd_buf_addr >= RD_BUF_ADDR + RD_BUF_BYTES)
                {
                    rd_buf_addr = RD_BUF_ADDR;
                }
                data++;
            }

            lba += num_sectors;
           // if(lba-start_lba % (1024*1024)==0)
           if((lba-start_lba) % (1024*2)==0)
                uart_printf("%d", lba-start_lba);    
            cur_sgx_lba.addr.offset = lba*BYTES_PER_SECTOR;
            if (lba >= IO_LIMIT + num_sectors)
            {
                lba = 0;
                cur_sgx_lba.addr.offset = lba*BYTES_PER_SECTOR;
            }
        }

        }


    }
    ftl_flush();
}

static void tc_write_rand(const UINT32 start_lsn, const UINT32 io_num, const UINT32 sector_size)
{
    UINT32 i, j, wr_buf_addr, rd_buf_addr, data, r_data;
    UINT32 lba, num_sectors = sector_size;
    UINT32 io_cnt = io_num;
   // static UINT32 key=0;
    // UINT32 volatile g_barrier = 0; while (g_barrier == 0); 
    led(0);
    srand(RANDOM_SEED);

    for (UINT32 loop = 0; loop < 1; loop++) {
        wr_buf_addr = WR_BUF_ADDR;
        data = 0;
      //  uart_printf("test loop cnt: %d", loop);

        for (i = 0; i < io_cnt; i++) {
            do {
                lba = rand() % IO_LIMIT;
                
            }while(lba + num_sectors >= IO_LIMIT);

            wr_buf_addr = WR_BUF_PTR(g_ftl_write_buf_id) + ((lba % SECTORS_PER_PAGE) * BYTES_PER_SECTOR);
            r_data = data;

            for (j = 0; j < num_sectors; j++) {
                mem_set_dram(wr_buf_addr, data, BYTES_PER_SECTOR);

                wr_buf_addr += BYTES_PER_SECTOR;

                if (wr_buf_addr >= WR_BUF_ADDR + WR_BUF_BYTES) {
                    wr_buf_addr = WR_BUF_ADDR;
                }
                data++;
            }
          //  key=key+1;
        
          //  ftl_write(lba, num_sectors,key);
         // uart_printf("write test %d %d\n", lba, num_sectors);
          ptimer_start(); 
            ftl_write(lba, num_sectors);
             ptimer_stop_and_uart_print(); 
            rd_buf_addr = RD_BUF_PTR(g_ftl_read_buf_id) + ((lba % SECTORS_PER_PAGE) * BYTES_PER_SECTOR);
          //  ftl_read(lba, num_sectors,key);
        //  uart_printf("read test\n");
            ptimer_start(); 
            ftl_read(lba, num_sectors);
            ptimer_stop_and_uart_print(); 

            flash_finish();

            for (j = 0; j < num_sectors; j++) {
                UINT32 sample = read_dram_32(rd_buf_addr);

                if (sample != r_data) {
                    uart_print ("test fail..");
                  //  uart_printf("ftl test fail...io#: %d, %d", lba, num_sectors);
                  //  uart_printf("sample data %d should be %d", sample, r_data);
                    led_blink();
                }
                rd_buf_addr += BYTES_PER_SECTOR;

                if (rd_buf_addr >= RD_BUF_ADDR + RD_BUF_BYTES) {
                    rd_buf_addr = RD_BUF_ADDR;
                }
                r_data++;
            }
        } // end for
    }
    ftl_flush();
}
#endif // OPTION_FTL_TEST
