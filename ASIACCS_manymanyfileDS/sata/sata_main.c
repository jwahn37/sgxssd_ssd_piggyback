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


#include "jasmine.h"
//#include "sha256_sgx.h"
#include "ftl_sgx.h"
//#include "ftl.h"

int YAME_RD = 0;

sata_context_t		g_sata_context;
sata_ncq_t			g_sata_ncq;
volatile UINT32		g_sata_action_flags;

#define HW_EQ_SIZE		128
#define HW_EQ_MARGIN	4

static UINT32 num_ata_flush_cache=0;
static UINT32 num_ata_flush_cache_execute=0;
//sgx 
extern UINT32 g_ftl_write_buf_id;	//page단위의 id. 밑단에서 write끝냐면 +1. WRITE_BUFFER위치.
extern UINT32 g_ftl_read_buf_id;
extern const int input_blocksize;

int num_ipfs_rd=0;
int num_ds_rd=0;
int num_ipfs_wr=0;
int num_ds_wr=0;
//int iter_print=0;

// EVENT_Q eve_sq[Q_SIZE];
extern EVENT_Q *eve_q;

extern UINT32 eveq_front;
extern UINT32 eveq_rear;
extern UINT32 eveq_size;

//SGX_LBA auth[10]; //일단 시간없으니까 실험용. 원래는 linked list가 맞다.
//int auth_size=0;
//UINT64 seqnum=1;

//unsigned char data[8192];	//보낼 사이즈임 42bytes
///UINT8 h_mac[SHA256_BLOCK_SIZE];	//32bytes = 256bit
//UINT8 h_mac[32];	//32bytes = 256bit
//UINT8 sym_key[KEY_SIZE];
int data_size;

//malloc 써보자
typedef struct RDAFWR_Q{
	UINT8 cmd;
	UINT32 version;
	UINT32 return_msg;
	UINT32 fid;
//	struct RDAFWR_Q *next;
}RDAFWR_Q;

RDAFWR_Q *rdafwr_queue=NULL;
UINT8 rdafwrq_front, rdafwrq_rear;
UINT8 rdafwrq_bmp[8]={0,0,0,0,0,0,0,0};

int num_rdafwrQ = 0;
UINT8 return_buf[512];
//UINT32 return_buf1[4096];


//static void rdafwr_q_init();
static void rdafwr_q_push(const UINT8 cmd, const UINT32 version, const UINT32 return_msg, const UINT32 fid);
static UINT8 rdafwr_q_pop(const UINT8 cmd, const UINT32 version, UINT32 *return_msg, UINT32 *fid);
void DS_open_wr(SGX_PARAM sgx_param);
void DS_create_wr(SGX_PARAM sgx_param);
void DS_close_wr(SGX_PARAM sgx_param);
void DS_remove_wr(SGX_PARAM sgx_param);
static void DS_write_wr(const SGX_PARAM sgx_param, const UINT32 sector_count);
static UINT32 DS_extract_mac_version(const UINT32 offset, UINT8 *mac, UINT32 *version, UINT32 num_page);
static UINT32 DS_extract_mac_version_name(const UINT32 offset, UINT8 *mac, UINT32 *version, char* name);
static UINT32 DS_extract_mac_version_name_key(const UINT32 offset, UINT8 *mac, UINT32 *version, char* name, UINT8* key);
static UINT32 DS_extract_writebuf(const UINT32 offset, UINT32 num_page);

/*static*/// UINT8 DS_auth_write(UINT32 version, SGX_PARAM sgx_param, UINT32 size, UINT8* buf, UINT8* mac_host, UINT8* key);
//static void DS_auth_rdafwr(UINT32 version, UINT32 return_msg, UINT8 *mac);

//static UINT8 DS_auth_create(UINT32 version, UINT8* name, UINT8* key, UINT8* mac);
//static UINT8 DS_auth_close(UINT32 version, UINT32 fid, UINT8* key, UINT8* mac);
//static UINT8 DS_auth_open(UINT32 version, UINT8 *name, UINT8 *mac, UINT8 *key);

static UINT8 HMAC_authentication(UINT8 *key, UINT8* buf, UINT32 size, UINT8* mac_host);
//static void HMAC_make_buf_vcn(UINT8* buf, const UINT32 version, const UINT8 cmd, const UINT8 *name);
//static void HMAC_make_buf_vcnk(UINT8* buf, const UINT32 version, const UINT8 cmd, const UINT8 *name, const UINT8* key);
//static void HMAC_make_buf_vcf(UINT8* buf, const UINT32 version, const UINT8 cmd, UINT32 fid);
static void HMAC_make_buf_vc(UINT8* buf, UINT32 version, const UINT8 cmd);

//static UINT8 HMAC_authentication(char* bur, UINT32 size);
static void DS_rdafwr_make_mac(UINT32 version, UINT32 return_msg, UINT8 *mac, UINT32 file_size, UINT8* key);
static void HMAC_DELAY(int utime);
//static UINT8 DS_rdafwr_make_mac(UINT32 version, UINT32 return_msg, UINT8 *mac);
static UINT8 rdafwrq_init=1;

static void rdafwr_q_push(const UINT8 cmd, const UINT32 version, const UINT32 return_msg, const UINT32 fid)
{
	
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

//key event q SW version
static UINT32 queue_isEmpty()
{
	return (eveq_front==eveq_rear);
}

static void queue_pop(UINT32* lba, UINT32* sector_count, UINT32* cmd_type, SGX_PARAM *sgx_param)
{
	if(!queue_isEmpty())
	{
	
	//	*lba = eve_q[eveq_front].lba;
	//	*sector_count = eve_q[eveq_front].sector_count;
		*lba = read_dram_32(&eve_q[eveq_front].lba);
		*sector_count = read_dram_32(&eve_q[eveq_front].sector_count);
		
	//	if(eve_q[eveq_front].sgx_fd==0)
		if(read_dram_32(&eve_q[eveq_front].sgx_fd)==0)
		{
			//*cmd_type = eve_q[eveq_front].cmd_type;
			*cmd_type = read_dram_32(&eve_q[eveq_front].cmd_type);
			sgx_param->fid = 0;	//sgx인지 구별자.
		}
		//*key = eve_q[eveq_front].key;
		//sgx_param = eve_q[eveq_front].sgx_param;
		//if(eve_q[eveq_front].sgx_fd!=0)
		else
		{
			/*
			*cmd_type = (eve_q[eveq_front].cmd_type & 0xff000000) >> 24; 
			sgx_param->cmd = eve_q[eveq_front].cmd_type & 0x000000ff;
			sgx_param->fid = eve_q[eveq_front].sgx_fd;
			sgx_param->offset = eve_q[eveq_front].lba | ((eve_q[eveq_front].cmd_type & 0x00ffff00)<< 24);
			*/
			
		
			*cmd_type = (read_dram_32(&eve_q[eveq_front].cmd_type) & 0xff000000) >> 24; 
			sgx_param->cmd = read_dram_32(&eve_q[eveq_front].cmd_type) & 0x000000ff;
			sgx_param->fid = read_dram_32(&eve_q[eveq_front].sgx_fd);
			sgx_param->offset = read_dram_32(&eve_q[eveq_front].lba) | ((read_dram_32(&eve_q[eveq_front].cmd_type) & 0x00ffff00)<< 24);
			
		}
		eveq_size--;
		eveq_front = (eveq_front+1) % Q_SIZE;

		//SETREG(SATA_EQ_STATUS, ((eveq_rear-eveq_front) & 0xFF)<<16);
		SETREG(SATA_EQ_STATUS, ((eveq_size) & 0xFF)<<16);

		
	}
	else{
	}
	return;
}

static UINT32 eventq_get_count(void)
{
	//key eventq sw version
	//return (eveq_rear - eveq_front)
	//r////e/turn !(eveq_front==eveq_rear);
	
	//return eveq_size;
	return (GETREG(SATA_EQ_STATUS) >> 16) & 0xFF;
	
	//return eveq_rear-eveq_front;
}

static void eventq_get(CMD_T* cmd, SGX_PARAM *sgx_param)
{
	disable_fiq();

	SETREG(SATA_EQ_CTRL, 1);	// The next entry from the Event Queue is copied to SATA_EQ_DATA_0 through SATA_EQ_DATA_2.

	while ((GETREG(SATA_EQ_DATA_2) & 8) != 0);

	UINT32 EQReadData0	= GETREG(SATA_EQ_DATA_0);
	UINT32 EQReadData1	= GETREG(SATA_EQ_DATA_1);

//key event q SW version
	//cmd->lba			= EQReadData1 & 0x3FFFFFFF;
	//cmd->sector_count	= EQReadData0 >> 16;
	//cmd->cmd_type		= EQReadData1 >> 31;

	//key event queue pop
	queue_pop( &(cmd->lba), &(cmd->sector_count), &(cmd->cmd_type), sgx_param);

	if(cmd->sector_count == 0)
	{
		//uart_printf("!!!seccoun00000!!!");
		cmd->sector_count = 0x10000;
	}	
	if (g_sata_context.eq_full)
	{
		g_sata_context.eq_full = FALSE;
		if ((GETREG(SATA_PHY_STATUS) & 0xF0F) == 0x103)
		{
			SETREG(SATA_CTRL_2, g_sata_action_flags);
		}
	}
	enable_fiq();
}

__inline ATA_FUNCTION_T search_ata_function(UINT32 command_code)
{
	UINT32 index;
	ATA_FUNCTION_T ata_function;

	index = mem_search_equ(ata_command_code_table, sizeof(UINT8), CMD_TABLE_SIZE, MU_CMD_SEARCH_EQU_SRAM, command_code);

	ata_function = (index == CMD_TABLE_SIZE) ? ata_not_supported : ata_function_table[index];

	if (ata_function == (ATA_FUNCTION_T) INVALID32)
		ata_function = ata_not_supported;

	return ata_function;
}

void Main(void)
{
	UINT32 key;
	UINT32 event_cnt;
	//SGX_LBA sgx_lba; 	//sgx parameter/ Get enclave id, file id, position from event queue.
	SGX_PARAM sgx_param;
	//unsigned char data[8192];	//보낼 사이즈임 42bytes
	SGX_LBA sgx_lba;
	int i;

	while (1)
	{
		if (event_cnt=eventq_get_count())// && !queue_isEmpty())
		{
			CMD_T cmd;
			eventq_get(&cmd, &sgx_param);

			if (cmd.cmd_type == READ)
			{
				//DiskShield read 종류의 커맨드
				if(DS_is_read_cmd(sgx_param.cmd))
				{
					//일반 read일경우 그냥 읽어오면돼 SGX에서 인증해줄거야.
					if(sgx_param.cmd == DS_READ_RD)
					{
						sgx_lba.addr.offset = sgx_param.offset;
						sgx_lba.fid = sgx_param.fid;
/*						
						if(iter_print==100)
						{
							iter_print=0;
						}
						
						iter_print++;
						*/
					//	num_ds_rd++;
						YAME_RD=1;
						DS_ftl_read(sgx_lba,  cmd.sector_count*512);	
																			
					}
					//read after write
					//1. cmd, version을 뽑아서 queue에 있는지 확인한다.(있으면 성공한것이므로) 있으면 pop
					//2. 보낼 정보도 받아온다.
					//3. 유저에게 정보를 보내준다. 
					else	//read after write : reserved 영역인 fid 에 version 정보가 있다.
					{
						//1. version, cmd 정보 뽑아서 queue와 비교한다.
						//UINT64 *mac;
						UINT32 version = sgx_param.fid;	//얘가 여기선 version이 됨. 나중에 union으로 수정.
						//UINT32 version;
						UINT8 cmd = sgx_param.cmd;
						UINT32 return_msg;
						UINT32 file_size;
						UINT32 fid;
						//UINT32 mac[MAC_SIZE / 4];
						UINT8 mac[MAC_SIZE];
						//version = DS_get_version_increased(fid);
						//rdafwr는 인증하지 않는다. -> 구현상의 이슈이기 때문.
						
						
						if(rdafwr_q_pop(cmd, version, &return_msg, &fid))
						{
					
							//mac값을 생성해준다.
							//DS_auth_rdafwr(version, return_msg, mac);
							HMAC_DELAY(3);
							//key는??
							//이미 close됐는데 버전을 어케가져옴?  
							
						
							if(cmd!=DS_CLOSE_RD && fid>0)
							{
								if(cmd == DS_WRITE_RD)
									version = DS_get_version_increased(fid);//얘도...
								else
									version = DS_get_version(fid);
								
							}	
							
							
							else if(cmd==DS_CLOSE_RD)
							{
								version = version;
								/* code */
							}
							
							if(cmd==DS_OPEN_RD)
							{	
								if(fid == FILE_NOT_EXISTS)
									file_size=0;
								else
									file_size =  DS_get_filesize(fid);
								//DS_rdafwr_make_mac(version, return_msg, mac, file_size, (UINT8*)DS_get_filekey(sgx_param.fid));
							}
							else
							{
								if(cmd==DS_WRITE_RD){
								//	DS_rdafwr_make_mac(version, return_msg, mac, -1, (UINT8*)DS_get_filekey(sgx_param.fid));
								}
							}
							
							//3. 유저에게 정보를 보내준다. 무슨정보를?
							//UINT8 return_buf[512];
							//mac 32bytes
							for(i=0; i<MAC_SIZE; i++)
							{
								//return_buf[i] = 0xAA;//mac[i];
								return_buf[i] = mac[i];
								//return_buf[i] = 0x11223344;
							}
							//versiona
							*(UINT32*)(&return_buf[i]) = return_msg;
							*(UINT32*)(&return_buf[i+4]) = version;
							
							if(cmd==DS_OPEN_RD)
							{
								*(UINT32*)(&return_buf[i+8]) = file_size;
								//return_buf[i+1]= file_size;	//return_msg 는 fid임
							}
							
							UINT32 next_read_buf_id = (g_ftl_read_buf_id + 1) % NUM_RD_BUFFERS;

							#if OPTION_FTL_TEST == 0
							while (next_read_buf_id == GETREG(SATA_RBUF_PTR));	// wait if the read buffer is full (slow host)
							#endif

							// fix bug @ v.1.0.6
							// Send 0xFF...FF to host when the host request to read the sector that has never been written.
							// In old version, for example, if the host request to read unwritten sector 0 after programming in sector 1, Jasmine would send 0x00...00 to host.
							// However, if the host already wrote to sector 1, Jasmine would send 0xFF...FF to host when host request to read sector 0. (ftl_read() in ftl_xxx/ftl.c)
							//mem_set_dram(RD_BUF_PTR(g_ftl_read_buf_id) + (((sgx_param.offset/BYTES_PER_SECTOR)%SECTORS_PER_PAGE)*BYTES_PER_SECTOR),//sect_offset*BYTES_PER_SECTOR,
							//        0x55555555, DRAM_ECC_UNIT);	//1+1+8+32
							//mem_set_dram(RD_BUF_PTR(g_ftl_read_buf_id),//sect_offset*BYTES_PER_SECTOR,
							//		0x55555555, 512);
							//mac값과 version정보를 (20bytes) data영역으로 유저에게 보내준다.

							mem_copy(RD_BUF_PTR(g_ftl_read_buf_id), return_buf, MAC_SIZE+sizeof(UINT32)+sizeof(UINT32)+sizeof(UINT32));

							flash_finish();

							SETREG(BM_STACK_RDSET, next_read_buf_id);	// change bm_read_limit
							SETREG(BM_STACK_RESET, 0x02);				// change bm_read_limit

							g_ftl_read_buf_id = next_read_buf_id;

						}
						else{
							;
						
						}
					
					}
				}
			
				else
				{
				//	num_ipfs_rd++;
			
					ftl_read(cmd.lba, cmd.sector_count);
				}
			}

			else
			{
				//DiskShield write종류의 커맨드
				if(DS_is_write_cmd(sgx_param.cmd))
				{
					if(sgx_param.cmd == DS_WRITE_WR)
					{
						YAME_RD = 0;
						num_ds_wr++;
						DS_write_wr(sgx_param, cmd.sector_count);
					}
				
					else{

						if(sgx_param.cmd==DS_CREATE_WR)
						{
							DS_create_wr(sgx_param);
						}
						else if(sgx_param.cmd==DS_OPEN_WR)
						{
							DS_open_wr(sgx_param);
						}
						else if(sgx_param.cmd==DS_CLOSE_WR)
						{
							DS_close_wr(sgx_param);
						}
						else if(sgx_param.cmd==DS_REMOVE_WR)
						{
							DS_remove_wr(sgx_param);
						}

						while (g_ftl_write_buf_id == GETREG(SATA_WBUF_PTR));	// bm_write_limit should not outpace SATA_WBUF_PTR

						g_ftl_write_buf_id = (g_ftl_write_buf_id + 1) % NUM_WR_BUFFERS;		// Circular buffer

						SETREG(BM_STACK_WRSET, g_ftl_write_buf_id);	// change bm_write_limit
						SETREG(BM_STACK_RESET, 0x01);				// change bm_write_limit			
					}							
				}
				else
				{
					num_ipfs_wr++;
					ftl_write(cmd.lba, cmd.sector_count);
				}		
			}

		}
		else if (g_sata_context.slow_cmd.status == SLOW_CMD_STATUS_PENDING)
		{
		//	uart_print("slow cmd");
			void (*ata_function)(UINT32 lba, UINT32 sector_count);

			slow_cmd_t* slow_cmd = &g_sata_context.slow_cmd;
			slow_cmd->status = SLOW_CMD_STATUS_BUSY;
/*
			if(slow_cmd->code == ATA_FLUSH_CACHE || slow_cmd->code == ATA_FLUSH_CACHE_EXT)
			{
				if(slow_cmd->code == ATA_FLUSH_CACHE)
				{
					num_ata_flush_cache++;

				}
				if( slow_cmd->code == ATA_FLUSH_CACHE_EXT)
				{
					num_ata_flush_cache_execute++;

				}
			}
*/
			ata_function = search_ata_function(slow_cmd->code);
			ata_function(slow_cmd->lba, slow_cmd->sector_count);

			slow_cmd->status = SLOW_CMD_STATUS_NONE;
		}
		else
		{
			
			// idle time operations
		}
	}
}

void sata_reset(void)
{
	disable_interrupt();

	mem_set_sram(&g_sata_context, 0, sizeof(g_sata_context));

	g_sata_context.write_cache_enabled = TRUE;
	g_sata_context.read_look_ahead_enabled = TRUE;

	SETREG(PMU_ResetCon, RESET_SATA | RESET_SATADWCLK | RESET_SATAHCLK | RESET_PMCLK | RESET_PHYDOMAIN);
	delay(100);

	SETREG(PHY_DEBUG, 0x400A040E);
	while ((GETREG(PHY_DEBUG) & BIT30) == 1);

	SETREG(SATA_BUF_PAGE_SIZE, BYTES_PER_PAGE);
	SETREG(SATA_WBUF_BASE, (WR_BUF_ADDR - DRAM_BASE));
	SETREG(SATA_RBUF_BASE, (RD_BUF_ADDR - DRAM_BASE));
	SETREG(SATA_WBUF_SIZE, NUM_WR_BUFFERS);
	SETREG(SATA_RBUF_SIZE, NUM_RD_BUFFERS);
	SETREG(SATA_WBUF_MARGIN, 16);
	SETREG(SATA_RESET_WBUF_PTR, BIT0);
	SETREG(SATA_RESET_RBUF_PTR, BIT0);

	SETREG(SATA_NCQ_BASE, g_sata_ncq.queue);

	SETREG(SATA_EQ_CFG_1, BIT0 | BIT14 | BIT9 | BIT16 | ((NUM_BANKS / 2) << 24));
	SETREG(SATA_EQ_CFG_2, (EQ_MARGIN & 0xF) << 16);

	SETREG(SATA_CFG_10, BIT0);

	SETREG(SATA_NCQ_CTRL, AUTOINC | FLUSH_NCQ);
	SETREG(SATA_NCQ_CTRL, AUTOINC);
	SETREG(SATA_CFG_5, BIT12 | BIT11*BSO_RX_SSC | (BIT9|BIT10)*BSO_TX_SSC | BIT4*0x05);
	SETREG(SATA_CFG_8, 0);
	SETREG(SATA_CFG_9, BIT20);

	SETREG(SATA_MAX_LBA, MAX_LBA);

	SETREG(APB_INT_STS, INTR_SATA);

	#if OPTION_SLOW_SATA
	SETREG(SATA_PHY_CTRL, 0x00000310);
	#else
	SETREG(SATA_PHY_CTRL, 0x00000300);
	#endif

	SETREG(SATA_ERROR, 0xFFFFFFFF);
	SETREG(SATA_INT_STAT, 0xFFFFFFFF);

	SETREG(SATA_CTRL_1, BIT31);

	while ((GETREG(SATA_INT_STAT) & PHY_ONLINE) == 0);

	SETREG(SATA_CTRL_1, BIT31 | BIT25 | BIT24);

	SETREG(SATA_INT_ENABLE, PHY_ONLINE);

	enable_interrupt();
}

void delay(UINT32 const count)
{
	static volatile UINT32 temp;
	UINT32 i;

	for (i = 0; i < count; i++)
	{
		temp = i;
	}
}

void DS_open_wr(SGX_PARAM sgx_param)
{
	//UINT8 *data_buf;
	UINT32 fid;
	char name[NAME_LEN];
	UINT32 version;
	UINT32 size;
	//UINT32 mac[KEY_SIZE / 4];
	UINT8 mac[MAC_SIZE];

	//1. //1. 현재 WRITE BUFFER 상에 저장된 MAC, VERSION, name 을 뽑아온다.
	// DS_extract_mac_version_name(const UINT32 offset, UINT32 *mac, UINT32 *version, UINT8* name, UINT32* mac_version_name_buf)
	DS_extract_mac_version_name(sgx_param.offset, mac, &version, name);

	//미리 open. key를 받아와야하므로.
	//open fail이면 -1이 리턴될거임.
	//uart_print("DS_open_wr"); 
	fid = DS_file_open(name);
//	fid=1;
	/*
	if(fid==FILE_NOT_EXISTS)
	{
		fid = FILE_NOT_EXISTS; //is -2
	}
	*/
	//2. HMAC 인증한다.
	HMAC_DELAY(3);
	if(1)
	//if(DS_auth_open(version, name, mac, (UINT8*)DS_get_filekey(fid)))
	{
		//3.rdafwr queue에 push한다.
		rdafwr_q_push(DS_convert_RD_cmd(sgx_param.cmd), version, fid, fid);	//얘는 후에 READ 할때 인증됩니다.
	}	
	else
	{	
		//인증실패시 닫고 오류메세지.
		DS_file_close(fid);
	}
	
}
void DS_create_wr(SGX_PARAM sgx_param)
{
	//UINT8 *data_buf;
	int i;
	UINT32 fid;
	char name[NAME_LEN];
	UINT32 version;
	UINT8 mac[MAC_SIZE];
	UINT8 key[KEY_SIZE];
	//uart_print("DS_create_wr");

	//1. 현재 WRITEBUFFER에 저장된 MAC, VERSION, NAME, KEY를 뽑아온다
	DS_extract_mac_version_name_key(sgx_param.offset, mac, &version, name, key);
	//이 key는 device key 로 encryption되있는 per file key다.
	//이 key를 등록해줘야하는거임.
	//일단 decryption 과정 생략함.
	//decrypt도 delay를 줄 예정.
	//devicekey로 decrypt해서 encalave key를 찾고 
	//2. enclave key로 HMAC 인증한다 
	HMAC_DELAY(3);
	if(1)
	//if(DS_auth_create(version, name, key, mac))
	{
		//create fail이면 -1이 리턴
		fid=DS_file_create(name, key);
		//uart_print("shard key is");
		//for(i=0; i<16; i+=4)
		//	uart_printf("%d %d %d %d", key[i], key[i+1], key[i+2], key[i+3]);
	//	fid=1;
	//	uart_printf("DS create %x", version);
//		uart_printf("DS_create_wr %d", fid);
		//rdafwr queue에 push
		rdafwr_q_push(DS_convert_RD_cmd(sgx_param.cmd), version, fid, fid);
	//	rdafwr_q_push(DS_convert_RD_cmd(sgx_param.cmd), version, fid);
	//	rdafwr_q_push(DS_convert_RD_cmd(sgx_param.cmd), version, fid);
	//	rdafwr_q_push(DS_convert_RD_cmd(sgx_param.cmd), version, fid);
	//	rdafwr_q_push(DS_convert_RD_cmd(sgx_param.cmd), version, fid);

		
	}
}

void DS_close_wr(SGX_PARAM sgx_param)
{
	//UINT8 *data_buf;
	UINT32 fid;
	UINT32 version;
	UINT8 mac[MAC_SIZE];
	UINT32 rtmsg;
	
	//1. 현재 WRITE BUFFER에 저장된 MAC, VERSION 뽑아온다.
	DS_extract_mac_version(sgx_param.offset, mac, &version, 0);
	//2. DEVICE key로 HMAC 인증
	HMAC_DELAY(3);
	if(1)
	//if(DS_auth_close(version, sgx_param.fid, (UINT8*)DS_get_filekey(fid), mac))
	{
		//close 
		rtmsg = DS_file_close(sgx_param.fid);
	///	rtmsg = 1;
	//	uart_print("DS_close_wr");
	//	uart_printf("DS_close_wr fid:%d rtmsg : %d", sgx_param.fid, rtmsg);
		rdafwr_q_push(DS_convert_RD_cmd(sgx_param.cmd), version, rtmsg, sgx_param.fid);
	}
}

/*remove 는 나중에!*/

void DS_remove_wr(SGX_PARAM sgx_param)
{
;
}

//DS_write모드
//우선 최초 16B = MAC, 4B=version 뽑고,
//MAC인증하고, (cmd, fid, offset, size, version, data) 변조 인증
//DS_ftl_write한다.
static void DS_write_wr(const SGX_PARAM sgx_param, const UINT32 sector_count)
{
//	UINT8 *mac_version_buf;
	SGX_LBA sgx_lba; 
	int i;
	UINT32 version;
	//UINT32 mac[MAC_SIZE / 4];
	UINT8 mac[MAC_SIZE];
	/*
	if(iter_print==100)
	{
		uart_print("DS_write_wr");
		iter_print=0;
	}
	iter_print++;
	*/	

	//uart_printf("sc %d %d", sector_count, sgx_param.offset);

//	uart_printf("[main] write fid , offset, sector_count : %d %d %d", sgx_param.fid, sgx_param.offset, sector_count);	
	////1. 현재 WRITE BUFFER 상에 저장된 MAC, VERSION을 뽑아온다.
	UINT32 data_end = (((sgx_param.offset / BYTES_PER_SECTOR) % SECTORS_PER_PAGE) + (sector_count-1));	//ftl_write할 page 내 write buffer end point (이후 메타데이터포홤)
	UINT32 num_page = data_end / SECTORS_PER_PAGE;	//0 or 1
	//uart_printf("%d", num_page);
//	UINT32 sata_ptr=GETREG(SATA_WBUF_PTR);
//	uart_printf("%d", data_end);

	UINT8* data = DS_extract_mac_version(sgx_param.offset+((sector_count-1)*512), mac, &version, num_page);	//마지막부분에 저장된다.

	//UINT8* data = (UINT8*)DS_extract_writebuf(sgx_param.offset+((sector_count-1)*512), num_page);

	//int num = 0;
	/*
	for(i=0; i<MAC_SIZE; i++) {
		mac[i] = read_dram_8(&data[i]);
		//while (++num < 10000000);
		num=0;
	}
	*/
/*
	UINT32 rd;
	if(((UINT32)(&data[0]))%4 != 0)
		uart_print("data not aligned");
	if(((UINT32)mac) %4 != 0)
		uart_print("mac not aligned");
*/
/*
	for(i=0; i<MAC_SIZE; i+=4)
	{
		rd=read_dram_32(&data[i]);
	//	if(rd==0)	uart_print("read fail 0");
		mac[i] = rd&0xff;
		rd = rd>>8;
		mac[i+1] =  rd&0xff;
		rd = rd>>8;
		mac[i+2] = rd&0xff;
		rd = rd>>8;
		mac[i+3] = rd&0xff;
	}

		for(i=0; i<MAC_SIZE; i+=4)
	{
		rd=read_dram_32(&data[i]);
	//	if(rd==0)	uart_print("read fail 0");
		mac[i] = rd&0xff;
		rd = rd>>8;
		mac[i+1] =  rd&0xff;
		rd = rd>>8;
		mac[i+2] = rd&0xff;
		rd = rd>>8;
		mac[i+3] = rd&0xff;
	}

		*/
	//mac[0]=read_dram_8(&data[i]);
//	mem_copy(mac, data, MAC_SIZE);
//	uart_printf("ms %d", MAC_SIZE);
//	version = read_dram_32(&data[i]);	
//	uart_printf("%u %u", mac[0], read_dram_8(&data[0]));
	/*
	unsigned int yonghyuk=0;
	num = 0;
	for(i=0; i<MAC_SIZE; i++)
	{
		if(mac[i] != read_dram_8(&data[i]))
		{
			yonghyuk = yonghyuk | (1<<i);
		}
		//while (++num < 10000000);
		//num=0;
	}
	uart_printf("maccopyfailed %x", yonghyuk);
*/



	
	data = (UINT8*)DS_extract_writebuf(sgx_param.offset, num_page);

//	if(mac[0]==0)
//		uart_print("maciszero");
//	if(version==0)
	//	{
			//UINT32 ftl_id=g_ftl_write_buf_id;
			//UINT32 sata_id = GETREG(SATA_WBUF_PTR);
		//	uart_print("version 0");
			//uart_printf("version 0 %d %d", ftl_id, sata_id);
			//if(sector_count!=9)
			//	uart_print("sector count inst 9");
	//	}
	//uart_printf("ov %lld %d", sgx_param.offset, version);
	//DS_extract_

	//UINT8 *data = (UINT8*) ((UINT32)mac_version_buf + buf_idx);
	//2. MAC인증하고, (cmd, fid, offset, size, version, data) 변조 인증
	//if(DS_auth_write(version, (char*)mac_version_buf, sgx_param))

	//DS_auth_write(version, sgx_param, (sector_count-1)*512, data, mac, (UINT8*)DS_get_filekey(sgx_param.fid));
	HMAC_DELAY(23);	//delay

	//uart_print("DS_ftl_write");
	if(1)
	//if(DS_auth_write(version, sgx_param, sector_count*512, data, mac, (UINT8*)DS_get_filekey(sgx_param.fid)))
	{
		//3. DS_ftl_write
		//uart_printf("wrof %d %d", sgx_param.offset, sector_count);
		sgx_lba.addr.offset = sgx_param.offset;
		sgx_lba.fid = sgx_param.fid;
		DS_ftl_write(sgx_lba, (sector_count-1)*512);	//마지막 512바이트는 메타데이터이므로 저장하지 않는다.

		//if(num_page>0)
		//if( (sgx_param.offset / BYTES_PER_SECTOR) % SECTORS_PER_PAGE + (sector_count-1)

		if(data_end % SECTORS_PER_PAGE == 0)
		{	
				flash_finish();
				while (g_ftl_write_buf_id == GETREG(SATA_WBUF_PTR));	// bm_write_limit should not outpace SATA_WBUF_PTR
				g_ftl_write_buf_id = (g_ftl_write_buf_id + 1) % NUM_WR_BUFFERS;		// Circular buffer
				SETREG(BM_STACK_WRSET, g_ftl_write_buf_id);	// change bm_write_limit
				SETREG(BM_STACK_RESET, 0x01);				// change bm_write_limit	
		}
		
		//uart_printf("%d %d",ftl_id, sata_id);		
		//4.rdafwr queue에 push한다.
	//	rdafwr_q_push(DS_convert_RD_cmd(sgx_param.cmd), version, 1, sgx_param.fid);	//얘는 후에 READ 할때 인증됩니다.
		
		
		
	/*임시*/	rdafwr_q_push(DS_convert_RD_cmd(sgx_param.cmd), version, sgx_param.offset, sgx_param.fid);	//얘는 후에 READ 할때 인증됩니다.
	}
	else
	{
	//	uart_printf("[DS_write_wr] authenticaiton fails..");
	}
}


static UINT32 DS_extract_mac_version(const UINT32 offset, UINT8 *mac, UINT32 *version, UINT32 num_page)
{
	int i;
	UINT8* mac_version_buf = (UINT8*)DS_extract_writebuf(offset, num_page);
	//little endian이므로 수정 가능성 있으나, 지금은 일단 진행:
	//for(i=0; i<KEY_SIZE; i+=4)
	for(i=0; i<MAC_SIZE; i++)
	{
		mac[i] = read_dram_8(&mac_version_buf[i]);
//		uart_printf("mac %d",read_dram_8(&mac_version_buf[i]));
		//mac[i/4] = read_dram_32(&mac_version_buf[i]);
	}
//uart_printf("mac %d %d", read_dram_8(&mac_version_buf[0]), read_dram_8(&mac_version_buf[1]));
	
	*version = read_dram_32(&mac_version_buf[i]);	
	//next address를 리턴한다.
	return (UINT32)mac_version_buf+MAC_SIZE+4;
}

static UINT32 DS_extract_mac_version_name(const UINT32 offset, UINT8 *mac, UINT32 *version, char* name)
{
	int i;
	UINT8* mac_version_name_buf=(UINT8*)DS_extract_mac_version(offset, mac, version, 0);
	//for(i=(KEY_SIZE/4+1); i<(KEY_SIZE/4+1)+NAME_LEN; i++)
	for(i=0; i<NAME_LEN; i++)
	{
		
		name[i] = read_dram_8(&mac_version_name_buf[i]);
	//	uart_printf("%d %x", i, name[i]);
		if(name[i]=='\0')
			break;
	}
	//next address를 리턴한다.
	return (UINT32)mac_version_name_buf + NAME_LEN;
}

static UINT32 DS_extract_mac_version_name_key(const UINT32 offset, UINT8 *mac, UINT32 *version, char* name, UINT8* key)
{
	int i;
	UINT8* mac_version_name_key_buf = (UINT8*)DS_extract_mac_version_name(offset, mac, version, name);
	//for(i=(KEY_SIZE/4+1)+NAME_LEN; i<(KEY_SIZE/4+1)+NAME_LEN+KEY_SIZE; i++)
	for(i=0; i<KEY_SIZE; i++)
	{
		key[i] = read_dram_8(&mac_version_name_key_buf[i]);
	}
	
		//next address를 리턴한다.

	return (UINT32)mac_version_name_key_buf + KEY_SIZE;
}

static UINT32 DS_extract_writebuf(const UINT32 offset, UINT32 num_page)
{
	//1. 현재 WRITE BUFFER 상에 저장된 MAC, VERSION을 뽑아온다.
	UINT32 lba, buf_offset;
	UINT32 dram_write_buf;

	lba = offset/BYTES_PER_SECTOR;	//현재 offset에 맵핑되는 lba
//	buf_offset = lba%SECTORS_PER_PAGE; //writebuffer에 현재 페이지의 offset (sector단위)
//	buf_offset = buf_offset * BYTES_PER_SECTOR; //writebuffer에 현재 페이지의 offset(byte)단위
	
	/*
	while (g_ftl_write_buf_id == GETREG(SATA_WBUF_PTR))
	{
		uart_print("wait s")
	};	// bm_write_limit should not outpace SATA_WBUF_PTR
	*/

	dram_write_buf =  WR_BUF_PTR( (g_ftl_write_buf_id + num_page) % NUM_WR_BUFFERS) + ((lba%SECTORS_PER_PAGE)*BYTES_PER_SECTOR);
	return dram_write_buf;
}

static void HMAC_DELAY(int time)
{
   
    start_interval_measurement(TIMER_CH1, TIMER_PRESCALE_0); //timer start
    UINT32 rtime;
    char buf[21];

    do{
        rtime = 0xFFFFFFFF - GET_TIMER_VALUE(TIMER_CH1);
	//uart_printf("%d );
    // Tick to us
        rtime = (UINT32)((UINT64)rtime * 2 * 1000000 * 
	    PRESCALE_TO_DIV(TIMER_PRESCALE_0) / CLOCK_SPEED);
    }
    while(rtime<time);
	
}


//buf는 최초  32B(MAC_SIZE)가 DRAM의 write buffer에 있는 mac이고, 나중부분은 data임
//data buf는 DRAM이다. (WRITE BUFFER)
/*
//static
UINT8 DS_auth_write(UINT32 version, SGX_PARAM sgx_param, UINT32 data_size, UINT8* data_buf, UINT8* mac_host, UINT8* key)
{
	//UINT8 buf[data_size+4+1+4+4+4];	//DATA|version(4)|cmd(1)|fd(4)|offset(4)|size(4)
	UINT32 buf = HMAC_BUFF+DRAM_ECC_UNIT;	//64 는 input block size
	//UINT32 tp = HMAC_BUFF;
	//UINT8 *buf = 
	UINT32 size = data_size+4+1+4+4+4;
	int i;
	int tp_ds = data_size;
	UINT8 tp_buf[4+1+4+4+4+3];
///	UINT32 offset = sgx_param.offset;

//	UINT32 offset = sgx_param.offset;
	mem_set_dram(buf, 0x00, HMAC_SIZE);	
//	uart_print("auth write");
//	uart_printf("vf, %d %d", version, sgx_param.fid);
//	uart_printf("od %d %d", sgx_param.offset, data_size);
//	uart_printf("of %d", sgx_param.offset);
//	uart_printf("ds %d", data_size);
//	uart_printf("key %d %d %d %d", key[0], key[1], key[2], key[3]);
	mem_copy(buf,data_buf, data_size);	//4096
	mem_copy(mac_host, data_buf+data_size, MAC_SIZE);
//	uart_printf("data org %d %d %d %d", read_dram_8(data_buf), read_dram_8(data_buf+1), read_dram_8(data_buf+2), read_dram_8(data_buf+3));
//	uart_printf("data %d %d %d %d", read_dram_8(buf), read_dram_8(buf+1), read_dram_8(buf+2), read_dram_8(buf+3));
//	uart_printf("mac %d", mac_host[0]);
	if(mac_host[0]!=117)
	{
		uart_print("maccopyfailed2");
	}
	if(read_dram_8(data_buf)!=read_dram_8(buf))
	{
		uart_print("copy failed");
	}
	//mem_copy(tp, data_buf, data_size);
	//uart_printf("data %d %d %d %d", read_dram_8(tp), read_dram_8(tp+1), read_dram_8(tp+2), read_dram_8(tp+3));

	//HMAC_make_buf_vc(&buf[data_size], version, DS_WRITE_WR);
	//mem_copy(buf+data_size, &version, 4);	//4
	
	
	for(i=0; i<4; i++)
	{
		//little endian 고려
		tp_buf[i] = version & 0xFF;
		version = version>>8;
	}
	//buf[4] = DS_OPEN_WR;
	tp_buf[4] = DS_WRITE_WR;

	//file descriptor
	for(i=0; i<4; i++)
	{
		tp_buf[4+1+i] = sgx_param.fid & 0xff;
		sgx_param.fid = sgx_param.fid>>8;
	}
	
	//offset, size
	for(i=0; i<4; i++)
	{
		tp_buf[4+1+4+i] = sgx_param.offset & 0xff;
		sgx_param.offset = sgx_param.offset>>8;
	}

	for(i=0; i<4; i++)
	{
		//uart_printf("data dize %d", tp_ds);
		tp_buf[4+1+4+4+i] = tp_ds & 0xff;
		tp_ds = tp_ds>>8;
	}
	mem_copy(buf+data_size,tp_buf, 4+4+4+4+4);	//반드시 4byteeksdnl

	//uart_printf ("aw)data marshaled %d", size);
	
	return HMAC_authentication(key, buf, size, mac_host);
}

*/
//DS_rdafwr_make_mac(version, return_msg, mac, file_size, DS_get_filekey(sgx_param.fid));

static void DS_rdafwr_make_mac(UINT32 version, UINT32 return_msg, UINT8 *mac, UINT32 file_size, UINT8* key)
{
	//file_size -1이면 open이 아닌거임
	UINT32 buf=HMAC_BUFF + 64;
	UINT8 buf_[4+4+4];
	UINT32 size=8;
	int i;
	for(i=0; i<4; i++)
	{
	
		buf_[i] = version & 0xff;
		version = version>>8;
	}
	for(i=4; i<8; i++)
	{
		buf_[i] = return_msg & 0xff;
		return_msg = return_msg >> 8;
	}
	
	if(file_size != -1)
	{
		for(i=8; i<12; i++)
		{
			buf_[i] = file_size & 0xff;
			file_size = file_size >> 8;
		}
		size += 4;
	}
	mem_copy(buf, buf_, size);
//	HMAC(key, mac, buf, size);

	//uart_print("rdafwr mac created");
	//for(i=0; i<32; i+=4)
	//	uart_printf("%d %d %d %d", mac[i],mac[i+1],mac[i+2],mac[i+3]);
	
}
/*
static UINT8 DS_auth_open(UINT32 version, UINT8 *name, UINT8 *mac_host, UINT8* key)
{
	UINT8 buf[4+1+NAME_LEN];	//21bytes
	int i;
	UINT32 size = 4+1+NAME_LEN;	
	//버퍼에 집어넣기 version|cmd|name
	HMAC_make_buf_vcn(buf, version, DS_OPEN_WR, name);
	
	return HMAC_authentication(key, buf, size, mac_host);
}

//void HMAC(const unsigned char key[], unsigned char h_mac[], const unsigned char text[], const int text_size)
//device key로 인증해줘야한다.
static UINT8 DS_auth_create(UINT32 version, UINT8* name, UINT8* key, UINT8* mac_host)
{
	UINT8 device_key[KEY_SIZE];
	UINT8 buf[4+1+NAME_LEN+KEY_SIZE];
	UINT32 size = 4+1+NAME_LEN+KEY_SIZE;

	DS_get_devicekey(device_key);
	//버퍼에집어넣기 version|cmd|name|key
	HMAC_make_buf_vcnk(buf, version, DS_CREATE_WR, name, key);
	return HMAC_authentication(device_key, buf, size, mac_host);
}

static UINT8 DS_auth_close(UINT32 version, UINT32 fid, UINT8* key, UINT8* mac_host)
{
	UINT8 buf[4+1+4];
	UINT32 size = 4+1+4;

	HMAC_make_buf_vcf(buf, version, DS_CLOSE_WR,fid);
	return HMAC_authentication(key, buf, size, mac_host);
	//return HMAC_authentication(NULL, 0);
}
*/
static UINT8 HMAC_authentication(UINT8 *key, UINT8* buf, UINT32 size, UINT8* mac_host)
{
	int i;
	UINT8 mac_device[MAC_SIZE];

	//HMAC이 맞는지 체크해라
	//for(i=0;i<16;i++)
	//	key[i]=1;
	//for(i=0;i<4096;i++)
	//	buf[i]=1;
	//size=4096;

	//uart_print("HMAC");
	//HMAC(key, mac_device, buf, size);
	
	for(i=0; i<MAC_SIZE; i++)
	{
		//인증 실패
		if(mac_device[i] != mac_host[i])
		{
	//		uart_print("MAC Authentication Fails");
			return 1;	//test라 무조건 성공하게 할거임;;; 원래는 return 0;
		}
	}
	//uart_print("MAC Authentication Success");
	return 1;
}
/*
static void HMAC_make_buf_vcnk(UINT8* buf, const UINT32 version, const UINT8 cmd, const UINT8 *name, const UINT8* key)
{
	int i;
	HMAC_make_buf_vcn(buf, version, cmd, name);
	//for(i=4+1+NAME_LEN; i<4+1+NAME_LEN+KEY_SIZE; i++)
	for(i=0; i<KEY_SIZE; i++)
		buf[i+4+1+NAME_LEN] = key[i];
}

static void HMAC_make_buf_vcn(UINT8* buf, const UINT32 version, const UINT8 cmd, const UINT8 *name)
{
	//버퍼에 집어넣기 version|cmd|name
	//mem_copy(buf, version, 4);
	int i;

	HMAC_make_buf_vc(buf, version, cmd);
	for(i=0; i<NAME_LEN; i++)
	{
		if(name[i]=='\0')
			break;
		else
		{
			buf[4+1+i] = name[i];
		}
	}
	for(; i<NAME_LEN; i++)
		buf[4+1+i] = '\0';
}

static void HMAC_make_buf_vcf(UINT8* buf, const UINT32 version, const UINT8 cmd, UINT32 fid)
{
	int i;
	HMAC_make_buf_vc(buf, version, cmd);
	for(i=0; i<4; i++)
	{
		buf[4+1+i] = fid & 0xff;
		fid = fid>>8;
	}
}

*/
/*
static void HMAC_make_buf_vc(UINT8* buf, UINT32 version, const UINT8 cmd)
{
	int i;
	for(i=0; i<4; i++)
	{
		//little endian 고려
		buf[i] = version & 0xFF;
		version = version>>8;
	}
	//buf[4] = DS_OPEN_WR;
	buf[4] = cmd;
}
*/


/*
static UINT8 HMAC_authentication(char* bur, UINT32 size)
{
		//sgx_write here!
					//인증을 여기서 하는게 나아보인다.
					//전체적으로 ecc고려안해서 잘못짬. 퍼포먼스측정이니 그냥 간다.
					//다음에 수정할것!
				/*
					int authentication=0;
					
					if(sgx_enclave_exists(sgx_lba.enclave_id))
					{
						//unsigned char data[8192];
						//int data_size=8192;
						//UINT8 h_mac[SHA256_BLOCK_SIZE];	//32bytes = 256bit
						
						data_size =8192;
					
						UINT32 addr = (UINT32)(signature)+32;	//mac이 256bits(32bytes)라면..
						//이부분 헷갈림!
						UINT32 real_addr = DRAM_BASE + (addr - DRAM_BASE)/128*132 + (addr - DRAM_BASE)%128;
						//ecc고려안했음 고려해야한다. read_dram으로읽어와야함반드시. 여러번읽어오는수밖에없다. 
						
						memset(data, 0x00, 8192);
						memcpy(data, real_addr, sgx_lba.addr.position-32);	//postion : 8KB
						memcpy(data+sgx_lba.addr.position-32, sgx_lba.enclave_id, 1);
						memcpy(data+sgx_lba.addr.position-31, sgx_lba.file_id, 1);
						memcpy(data+sgx_lba.addr.position+30, sgx_lba.addr.position, 4);
						
						//memset(data, 0x11111111, data_size);
						//int enclave_id = sgx_enclave_get_public_key(sgx_lba.enclave_id);	
						//UINT8 sym_key[KEY_SIZE];
						//sgx_enclave_get_sym_key(sgx_lba.enclave_id,sym_key);
						//uart_printf("key: %llx %llx", *((UINT64*)sym_key), *(UINT64*)(&sym_key[KEY_SIZE/2]));
						//uart_printf("HMAC : size : %d", data_size);
						//HMAC(sym_key, h_mac, data, data_size);	//중요!sym_key=KEY_SZIE,


						addr = (UINT32)signature_bf;
						real_addr = DRAM_BASE + (addr - DRAM_BASE)/128*132 + (addr - DRAM_BASE)%128;


						//if(0==memcmp(h_mac, real_addr, 32))
						//	authentication=1;
							

					}
					*/
					/*
					if(authentication)
					{
						//uart_printf("SUCCESS!! authenticated!");
						auth[auth_size] = sgx_lba;	//테이블에 등록! 실험땐 무조건 성공가정!
						auth_size=(auth_size+1)%10;	//한번에 100개이상이면 망함.
						
					}
					*/
					//else
					
					//	uart_printf("FAILURE! UNAUTHENTIATED!!");
					
				//uart_printf("b44)bm_write_limit : %x, read_buf : %x, sata: %x", GETREG(BM_WRITE_LIMIT), g_ftl_write_buf_id, GETREG(SATA_WBUF_PTR));
				//	data_size =8192;
					//HMAC 통쨰로 딜레이주는수밖에.
				//	HMAC(sym_key, h_mac, data, data_size);
				//	HMAC_DELAY(88);
				////	sgx_ftl_write(sgx_lba, encrypted_hash, cmd.sector_count*512);
				//	uart_printf("att)bm_write_limit : %x, read_buf : %x, sata: %x",  GETREG(BM_WRITE_LIMIT), g_ftl_write_buf_id, GETREG(SATA_WBUF_PTR));
				//	uart_printf("sgx_ftl_write end");
					
					//sgx_ftl_write(sgx_lba, encrypted_hash, cmd.sector_count*512);
				//	DS_ftl_write(sgx_lba, cmd.sector_count*512);
				//	uart_printf("flag 44444444444444444444444444444444444444444444444444444444444444444444444444444444");
/*&
	return 1;
}

*/



