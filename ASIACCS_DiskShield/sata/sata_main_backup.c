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


sata_context_t		g_sata_context;
sata_ncq_t			g_sata_ncq;
volatile UINT32		g_sata_action_flags;

#define HW_EQ_SIZE		128
#define HW_EQ_MARGIN	4

static UINT32 num_ata_flush_cache=0;
static UINT32 num_ata_flush_cache_execute=0;
//sgx 
extern UINT32 g_ftl_write_buf_id;

extern EVENT_Q eve_q[Q_SIZE];
extern UINT32 eveq_front;
extern UINT32 eveq_rear;

//key event q SW version
static UINT32 queue_isEmpty()
{
	return (eveq_front==eveq_rear);
}

static UINT32 queue_pop(UINT32* lba, UINT32* sector_count, UINT32* cmd_type, UINT32 *key, SGX_PARAM *sgx_param)
{
	if(!queue_isEmpty())
	{
		*lba = eve_q[eveq_front].lba;
		*sector_count = eve_q[eveq_front].sector_count;
		*cmd_type = eve_q[eveq_front].cmd_type;
		*key = eve_q[eveq_front].key;
		*sgx_param = eve_q[eveq_front].sgx_param;
		
		eveq_front = (eveq_front+1) % Q_SIZE;
		SETREG(SATA_EQ_STATUS, ((eveq_rear-eveq_front) & 0xFF)<<16);
	}
	return;
}

static UINT32 eventq_get_count(void)
{
	//key eventq sw version
	return (GETREG(SATA_EQ_STATUS) >> 16) & 0xFF;
	//return eveq_rear-eveq_front;
}

static void eventq_get(CMD_T* cmd, UINT32* key, SGX_PARAM *sgx_param)
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
	queue_pop( &(cmd->lba), &(cmd->sector_count), &(cmd->cmd_type), key, sgx_param);
	//uart_printf("queue pop : lba : %d, sec_cnt: %d, type : %d, key : %d",cmd->lba, cmd->sector_count, cmd->cmd_type, *key);

	if(cmd->sector_count == 0)
		cmd->sector_count = 0x10000;
		
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

	while (1)
	{
		if (event_cnt=eventq_get_count())
		{
			CMD_T cmd;
			eventq_get(&cmd, &key, &sgx_param);
			//uart_printf("queue pop : lba : %d, sec_cnt: %d, type : %d, key : %d, q_len:%d",cmd.lba, cmd.sector_count, cmd.cmd_type, key, event_cnt);
		//	uart_printf("pop : queue front: %d rear : %d size : %d ",eveq_front, eveq_rear,event_cnt);
			//key=H2D_key;
//test code! - key 어떤 명령이오던, WRITE후, READ 를 테스트한다. 이게 성공으로 뜬다면, 맵핑테이블까지 잘 연결되었다볼수있겠지.
//근데 이게 실패할리는없다고봄. 의미가 있는 실험이니?
//
//			uart_printf("type : %d, lba : %d, sec_cnt : %d, key : %d",cmd.cmd_type, cmd.lba, cmd.sector_count, key);
//			ftl_write(cmd.lba, cmd.sector_count, key);
//			ftl_read(cmd.lba, cmd.sector_count, key);


			if (cmd.cmd_type == READ)
			{
				//uart_printf("type : %d, lba : %lu, %lx, sec_cnt : %d, key : %x, event count : %d",cmd.cmd_type, (unsigned long)cmd.lba, cmd.lba, cmd.sector_count, key, event_cnt);

				//uart_printf("type : %d, lba : %d, sec_cnt : %d, key : %d, event count: %d",cmd.cmd_type, cmd.lba, cmd.sector_count, key ,event_cnt);
				//uart_printf("read: event count: %d NCQ_BMP_1,2 :  %d %d NCQ_BASE : %d , NCQ_ORDER : %d , NCQ_CTRL: %d , NCQ_CMD_RECV : %d, NONE CMD_RECV : %d  ",event_cnt,GETREG(SATA_NCQ_BMP_1),GETREG(SATA_NCQ_BMP_2),GETREG(SATA_NCQ_BASE), GETREG(SATA_NCQ_ORDER), GETREG(SATA_NCQ_CTRL), GETREG(NCQ_CMD_RECV), GETREG(CMD_RECV));

					ftl_read(cmd.lba, cmd.sector_count);
					//ftl_read(cmd.lba, cmd.sector_count);
				}
			else
			{
				
				//if(key==0x11223344)
					//uart_printf("type : %d, lba : %lu, %lx, sec_cnt : %d, key : %x, event count : %d",cmd.cmd_type, (unsigned long)cmd.lba, cmd.lba, cmd.sector_count, key, event_cnt);
//				uart_printf("type : %d, lba : %d, sec_cnt : %d, key : %d, event count : %d",cmd.cmd_type, cmd.lba, cmd.sector_count, key, event_cnt);

				//uart_printf("write: event count: %d NCQ_BMP_1,2 :  %d %d NCQ_BASE : %d , NCQ_ORDER : %d , NCQ_CTRL: %d ",event_cnt,GETREG(SATA_NCQ_BMP_1),GETREG(SATA_NCQ_BMP_2),GETREG(SATA_NCQ_BASE), GETREG(SATA_NCQ_ORDER), GETREG(SATA_NCQ_CTRL));
				//sgx
				
				//sgx command

				//uart_printf("key : %x, flag : %x\n", key, sgx_param.flag );
				/*
				uart_printf("signature0 :\n");
				for(int i=0; i<64; i++)
				{
					uart_printf("%x, ",signature[i]);
				}
				*/
				if(sgx_param.flag==4)
				{
					UINT64 signature[128*4];
					memset(signature, 0x00, sizeof(UINT64)*128*4);
					mem_copy(signature, WR_BUF_PTR(g_ftl_write_buf_id), sizeof(UINT64)*128*4);

					uart_printf("sectorcount : %x", cmd.sector_count);
					uart_printf("key : %x, flag : %x", key, sgx_param.flag );
					uart_printf("signature1 :");
						
					uart_printf("wr buf : %llx", * (UINT64*)WR_BUF_PTR(g_ftl_write_buf_id));
				
					for(int i=0; i<128*4; i+=8)
					{
						
						uart_printf("%llx, %llx, %llx, %llx, %llx, %llx, %llx, %llx",\
						signature[i],signature[i+1],signature[i+2],signature[i+3],\
						signature[i+4],signature[i+5],signature[i+6],signature[i+7]);
					}
					uart_printf("sgx_param : %x %x %x %x %x %x", sgx_param.LBA[0], sgx_param.LBA[1], sgx_param.LBA[2], sgx_param.LBA[3], sgx_param.LBA[4], sgx_param.LBA[5]);
				
					//sgx_write here!
					//sgx_ftl_write(SGX_LBA sgx_lba, UINT32 encrypted_hash, UINT32 size);
				}
				else
					ftl_write(cmd.lba, cmd.sector_count);
					//ftl_write(cmd.lba, cmd.sector_count);
			}

		}
		else if (g_sata_context.slow_cmd.status == SLOW_CMD_STATUS_PENDING)
		{
			void (*ata_function)(UINT32 lba, UINT32 sector_count);

			slow_cmd_t* slow_cmd = &g_sata_context.slow_cmd;
			slow_cmd->status = SLOW_CMD_STATUS_BUSY;
/*
			if(slow_cmd->code == ATA_FLUSH_CACHE || slow_cmd->code == ATA_FLUSH_CACHE_EXT)
			{
				if(slow_cmd->code == ATA_FLUSH_CACHE)
				{
					num_ata_flush_cache++;
					uart_printf("ata_function : %x, %d", slow_cmd->code, num_ata_flush_cache); //key

				}
				if( slow_cmd->code == ATA_FLUSH_CACHE_EXT)
				{
					num_ata_flush_cache_execute++;
					uart_printf("ata_function : %x, %d", slow_cmd->code,num_ata_flush_cache_execute ); //key

				}
			}
		//	uart_printf("ata_function : %x, %d, %d", slow_cmd->code, num_ata_flush_cache,num_ata_flush_cache_execute ); //key
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
