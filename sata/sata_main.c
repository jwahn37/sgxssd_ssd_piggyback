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

sata_context_t g_sata_context;
sata_ncq_t g_sata_ncq;
volatile UINT32 g_sata_action_flags;

#define HW_EQ_SIZE 128
#define HW_EQ_MARGIN 4

extern UINT32 g_ftl_write_buf_id;
extern UINT32 g_ftl_read_buf_id;
int write_cnt = 0;
int read_cnt = 0;
int sgxssd = 0;
static UINT32 ptr_diff(UINT32 ftl_id, UINT32 sata_id);

UINT32 isRecoveryCmd(UINT32 cmd_code);

static UINT32 queue_isEmpty()
{
	return (eveq_front == eveq_rear);
}

static UINT32 queue_pop(UINT32 *lba, UINT32 *sector_count, UINT32 *cmd_type, UINT32 *recv_meta)
{
	if (!queue_isEmpty())
	{
		*lba = eve_q[eveq_front].lba;
		*sector_count = eve_q[eveq_front].sector_count;
		*cmd_type = eve_q[eveq_front].cmd_type;
		*recv_meta = eve_q[eveq_front].sgx_fd;
		//	*key = eve_q[eveq_front].key;

		eveq_front = (eveq_front + 1) % Q_SIZE;
		SETREG(SATA_EQ_STATUS, ((eveq_rear - eveq_front) & 0xFF) << 16);
	}
	return;
}

static UINT32 eventq_get_count(void)
{
	return (GETREG(SATA_EQ_STATUS) >> 16) & 0xFF;
}

static void eventq_get(CMD_T *cmd, UINT32 *recv_meta)
{
	disable_fiq();

	SETREG(SATA_EQ_CTRL, 1); // The next entry from the Event Queue is copied to SATA_EQ_DATA_0 through SATA_EQ_DATA_2.

	while ((GETREG(SATA_EQ_DATA_2) & 8) != 0)
		;

	UINT32 EQReadData0 = GETREG(SATA_EQ_DATA_0);
	UINT32 EQReadData1 = GETREG(SATA_EQ_DATA_1);

	//cmd->lba			= EQReadData1 & 0x3FFFFFFF;
	//cmd->sector_count	= EQReadData0 >> 16;
	//cmd->cmd_type		= EQReadData1 >> 31;
	queue_pop(&(cmd->lba), &(cmd->sector_count), &(cmd->cmd_type), recv_meta);
	if (cmd->sector_count == 0)
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

	if (ata_function == (ATA_FUNCTION_T)INVALID32)
		ata_function = ata_not_supported;

	return ata_function;
}

void Main(void)
{
	UINT32 recovery_time;
	UINT32 fid;
	static UINT32 offset = 0;
	while (1)
	{
		if (eventq_get_count())
		{
			CMD_T cmd;
			UINT32 recv_meta;

			eventq_get(&cmd, &recv_meta);
			//uart_printf("pop Q cmd_type: %d lba %d size %d", cmd.cmd_type, cmd.lba, cmd.sector_count);
			sgxssd = 0;
			if (cmd.cmd_type == READ)
			{
				//	read_cnt++;
				//	if(read_cnt%20==0)
				//		uart_printf("rd%d", read_cnt);
				ftl_read(cmd.lba, cmd.sector_count);
			}

			else if (isRecoveryCmd(cmd.cmd_type))
			{

				recovery_time = recv_meta & 0x0000FFFF;
				fid = (recv_meta & 0XFFFF0000) >> 16;

				uart_printf("recv time/fd %x %x", recovery_time, fid);

				UINT32 head_lba = cmd.lba;
				UINT8 *offset_pointer = RD_BUF_PTR((g_ftl_read_buf_id) % NUM_RD_BUFFERS) + ((head_lba % SECTORS_PER_PAGE) * BYTES_PER_SECTOR);

				uart_printf("lba/size/offset 0x%x/%u/%d", cmd.lba, cmd.sector_count, offset);
				//offset = 0x11111111;
				write_dram_32((UINT32)offset_pointer, offset);
				offset += 32768;
				//uart_printf("bf ftl/sata/bm %d %d %d",  g_ftl_write_buf_id, GETREG(SATA_WBUF_PTR), GETREG(BM_WRITE_LIMIT));
				
				//첫번째 페이지는 offset이 저장되었으므로 두번째 페이지로 이동.
				UINT32 next_read_buf_id = (g_ftl_read_buf_id + 1) % NUM_RD_BUFFERS;
				while (next_read_buf_id == GETREG(SATA_RBUF_PTR)); // wait if the read buffer is full (slow host)
				SETREG(BM_STACK_RDSET, next_read_buf_id); // change bm_read_limit
				SETREG(BM_STACK_RESET, 0x02);			  // change bm_read_limit

				g_ftl_read_buf_id = next_read_buf_id;

				ftl_read(cmd.lba, cmd.sector_count - SECTORS_PER_PAGE);
				//offset = 0x37373737;
				write_dram_32((UINT32)offset_pointer, offset);
			}

			else if (cmd.cmd_type == CMD_SGXSSD_WRITE_NOR || cmd.cmd_type == CMD_SGXSSD_WRITE_EXT)
			{
				//tail sector
				// UINT32 tail_lba = cmd.lba + cmd.sector_count - 1;
				// UINT32 tail_pageidx = ((cmd.lba % SECTORS_PER_PAGE) + (cmd.sector_count - 1)) / SECTORS_PER_PAGE;
				// UINT8 *piggyback_pointer = WR_BUF_PTR((g_ftl_write_buf_id + tail_pageidx) % NUM_WR_BUFFERS) + ((tail_lba % SECTORS_PER_PAGE) * BYTES_PER_SECTOR);
				// UINT32 fid, offset, pid;
				UINT32 pid2, fid2, offset2;

				//tail page
				// UINT32 tail_lba = cmd.lba + cmd.sector_count - 8; //4096/512
				// UINT32 tail_pageidx = ((cmd.lba % SECTORS_PER_PAGE) + (cmd.sector_count - 8)) / SECTORS_PER_PAGE;
				// UINT8 *piggyback_pointer = WR_BUF_PTR((g_ftl_write_buf_id + tail_pageidx) % NUM_WR_BUFFERS) + ((tail_lba % SECTORS_PER_PAGE) * BYTES_PER_SECTOR);

				//예외 처리.
				//만일 SATA id가 FTL id을 초과하기 위해 접근하는 경우라면, 에러 발생함.
				//그런일이 없길 바람.
				//기본적으로 sata id 가 ftl id보다 선행되어야 하나, ftl id가 1만큼 더 큰 예외케이스가 발생함.

				//uart_printf("bf ftl/sata/bm %d %d %d",  g_ftl_write_buf_id, GETREG(SATA_WBUF_PTR), tail_pageidx);

				// while (((g_ftl_write_buf_id+NUM_WR_BUFFERS-1)%NUM_WR_BUFFERS) == GETREG(SATA_WBUF_PTR));
				// //DMA로부터 모든 값을 write buffer에 읽어올떄까지 기다린다.
				// while( ptr_diff(g_ftl_write_buf_id, GETREG(SATA_WBUF_PTR)) <= tail_pageidx );
				// //uart_printf("af ftl/sata/bm %d %d %d",  g_ftl_write_buf_id, GETREG(SATA_WBUF_PTR), tail_pageidx);

				// //HMAC_DELAY(1000);

				// pid = read_dram_32((UINT32)piggyback_pointer);
				// fid = read_dram_32((UINT32)(&piggyback_pointer[4]));
				// offset = read_dram_32((UINT32)(&piggyback_pointer[8]));

				//header
				//	wait until SATA transfer the data.
				//	만일 SATA가 FTL을 초과하기 위해 접근하는 경우라면, 에러 발생함.
				//	그런일이 없길 바람.
				//	uart_print("start");
				while (((g_ftl_write_buf_id + NUM_WR_BUFFERS - 1) % NUM_WR_BUFFERS) == GETREG(SATA_WBUF_PTR))
					;

				UINT32 head_lba = cmd.lba;
				//UINT32 head_pageidx = ((cmd.lba % SECTORS_PER_PAGE) + (cmd.sector_count - 8)) / SECTORS_PER_PAGE;
				UINT8 *piggyback_pointer = WR_BUF_PTR((g_ftl_write_buf_id) % NUM_WR_BUFFERS) + ((head_lba % SECTORS_PER_PAGE) * BYTES_PER_SECTOR);

				UINT32 fid, offset, pid;
				pid = read_dram_32((UINT32)piggyback_pointer);
				fid = read_dram_32((UINT32)(&piggyback_pointer[4]));
				offset = read_dram_32((UINT32)(&piggyback_pointer[8]));

				//	uart_printf("bf ftl/sata/bm %d %d %d",  g_ftl_write_buf_id, GETREG(SATA_WBUF_PTR), GETREG(BM_WRITE_LIMIT));

				//첫번째 page는 piggyback set이므로 두번째 page부터 실제 데이터 존재.
				while (g_ftl_write_buf_id == GETREG(SATA_WBUF_PTR))
					;
				g_ftl_write_buf_id = (g_ftl_write_buf_id + 1) % NUM_WR_BUFFERS; // Circular buffer
				//SETREG(BM_STACK_WRSET, g_ftl_write_buf_id);	// change bm_write_limit
				//SETREG(BM_STACK_RESET, 0x01);				// change bm_write_limit

				//SETREG(BM_STACK_WRSET, GETREG(BM_WRITE_LIMIT)+1);	// change bm_write_limit
				//SETREG(BM_STACK_RESET, 0x01);				// change bm_write_limit

				//	uart_printf("af ftl/sata/bm %d %d %d",  g_ftl_write_buf_id, GETREG(SATA_WBUF_PTR), GETREG(BM_WRITE_LIMIT));
				sgxssd = 1;
				/*
				UINT32 data_end = (((sgx_param.offset / BYTES_PER_SECTOR) % SECTORS_PER_PAGE) + (sector_count-1));	//ftl_write할 page 내 write buffer end point (이후 메타데이터포홤)
				UINT32 num_page = data_end / SECTORS_PER_PAGE;	//0 or 1
				UINT8* data = DS_extract_mac_version(sgx_param.offset+((sector_count-1)*512), mac, &version, num_page);	//마지막부분에 저장된다.
				dram_write_buf =  WR_BUF_PTR( (g_ftl_write_buf_id + num_page) % NUM_WR_BUFFERS) + ((lba%SECTORS_PER_PAGE)*BYTES_PER_SECTOR);
				*/
				//	int tp = (g_ftl_write_buf_id + tail_pageidx) % NUM_WR_BUFFERS;
				//	uart_printf("P_Write : 0x%lx, %d", cmd.lba, cmd.sector_count);
				//	uart_printf("tail lba 0x%x, page idx: %d, pg %d", tail_lba, tail_pageidx,tp);
				//	uart_printf("1 0x%x 0x%x %d", pid, fid, offset);

			
				if (pid == 0x11223344)
					;
				//	uart_print("1good");
				else
				{
					uart_print("1bad");
				}

				//	uart_printf("%x %x %x %x", read_dram_8((UINT32)piggyback_pointer), read_dram_8((UINT32)(&piggyback_pointer[1])), read_dram_8((UINT32)(&piggyback_pointer[2])), read_dram_8((UINT32)(&piggyback_pointer[3])));
				//	ftl_write(cmd.lba, cmd.sector_count - 1);	//tail (sector)
				//	ftl_write(cmd.lba, cmd.sector_count - 8);	//tail
				//	ftl_write(cmd.lba+8, cmd.sector_count - 8);

				//uart_printf("lba: 0x%x, size: 0x%d %d", cmd.lba, cmd.sector_count, cmd.sector_count - SECTORS_PER_PAGE);
				ftl_write(cmd.lba, cmd.sector_count - SECTORS_PER_PAGE); //head

				//	uart_printf("last ftl/sata/bm %d %d %d",  g_ftl_write_buf_id, GETREG(SATA_WBUF_PTR), GETREG(BM_WRITE_LIMIT));

				//	uart_printf("%d",g_ftl_write_buf_id);

				//추가적인 piggyback 섹터로 인한 페이지 초과시 g_ftl_write_id를 인위적으로 조정해주어야함.
				//if (tail_lba % SECTORS_PER_PAGE == 0)

				//	if (((tail_lba % SECTORS_PER_PAGE) == 0) || ((tail_lba % SECTORS_PER_PAGE) > (SECTORS_PER_PAGE - 8)))	//1-56까지의 범위는 page boundary를 초과하지 않음.
				// if (((tail_lba % SECTORS_PER_PAGE) == 0))	//1-56까지의 범위는 page boundary를 초과하지 않음.
				// {
				// 	//uart_print("pgboundary");
				// 	flash_finish();
				// 	while (g_ftl_write_buf_id == GETREG(SATA_WBUF_PTR))
				// 		;															// bm_write_limit should not outpace SATA_WBUF_PTR
				// 	g_ftl_write_buf_id = (g_ftl_write_buf_id + 1) % NUM_WR_BUFFERS; // Circular buffer
				// 	SETREG(BM_STACK_WRSET, g_ftl_write_buf_id);						// change bm_write_limit
				// 	SETREG(BM_STACK_RESET, 0x01);									// change bm_write_limit
				// }
				// else{
				// 	;//uart_print("Not pgboundary");
				// }
				/*
					pid2 = read_dram_32((UINT32)piggyback_pointer);
					fid2 = read_dram_32((UINT32)(&piggyback_pointer[4]));
					offset2 = read_dram_32((UINT32)(&piggyback_pointer[8]));
					
					
						if(pid2==0x11223344);
						//uart_print("2good");
					else
					{
						uart_print("2bad");
					}
*/
			}

			else
			{
				ftl_write(cmd.lba, cmd.sector_count);
			}
		}
		else if (g_sata_context.slow_cmd.status == SLOW_CMD_STATUS_PENDING)
		{
			void (*ata_function)(UINT32 lba, UINT32 sector_count);

			slow_cmd_t *slow_cmd = &g_sata_context.slow_cmd;
			slow_cmd->status = SLOW_CMD_STATUS_BUSY;

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

void HMAC_DELAY(int time) //us
{

	start_interval_measurement(TIMER_CH1, TIMER_PRESCALE_0); //timer start
	UINT32 rtime;
	char buf[21];

	do
	{
		rtime = 0xFFFFFFFF - GET_TIMER_VALUE(TIMER_CH1);
		//uart_printf("%d );
		// Tick to us
		rtime = (UINT32)((UINT64)rtime * 2 * 1000000 *
						 PRESCALE_TO_DIV(TIMER_PRESCALE_0) / CLOCK_SPEED);
	} while (rtime < time);
}

UINT32 ptr_diff(UINT32 ftl_id, UINT32 sata_id)
{
	if (sata_id >= ftl_id)
		return sata_id - ftl_id;
	else
		return NUM_WR_BUFFERS - (ftl_id - sata_id);
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
	while ((GETREG(PHY_DEBUG) & BIT30) == 1)
		;

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
	SETREG(SATA_CFG_5, BIT12 | BIT11 * BSO_RX_SSC | (BIT9 | BIT10) * BSO_TX_SSC | BIT4 * 0x05);
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

	while ((GETREG(SATA_INT_STAT) & PHY_ONLINE) == 0)
		;

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
