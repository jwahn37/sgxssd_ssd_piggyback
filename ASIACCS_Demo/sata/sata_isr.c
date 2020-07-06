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

//key
//UINT32 H2D_key;
//SGX_PARAM H2D_sgx_param;
//SGX_LBA H2D_sgx_lba;
//EVENT_Q eve_q[Q_SIZE];

#define GEN_FILE 1000000000
EVENT_Q *eve_q;
UINT32 eveq_front;
UINT32 eveq_rear;
UINT32 eveq_size=0;

UINT8 DS_is_cmd(int cmd_code)
{
	//if(cmd_code >= DS_CREATE && cmd_code <= DS_AUTH)
	if(DS_is_write_cmd(cmd_code) || DS_is_read_cmd(cmd_code))
		return TRUE;
	else 
		return FALSE;
}

UINT8 DS_is_write_cmd(int cmd_code)
{
	/*if(cmd_code == DS_CREATE || cmd_code == DS_OPEN \
	|| cmd_code == DS_CLOSE || cmd_code == DS_REMOVE \
	|| cmd_code == DS_WRITE )
	*/
	if(cmd_code > DS_WR_RANGE_MIN && cmd_code < DS_WR_RANGE_MAX)
		return TRUE;
	else
		return FALSE;
}
UINT8 DS_convert_RD_cmd(int cmd_code)
{
	if(DS_is_write_cmd(cmd_code))
	{
		if(cmd_code == DS_CREATE_WR)	return DS_CREATE_RD;
		if(cmd_code == DS_OPEN_WR)	return DS_OPEN_RD;
		if(cmd_code == DS_CLOSE_WR)	return DS_CLOSE_RD;
		if(cmd_code == DS_REMOVE_WR)	return DS_REMOVE_RD;
		if(cmd_code == DS_WRITE_WR)	return DS_WRITE_RD;
		//return cmd_code + (DS_WR_RANGE_MIN - DS_RD_RANGE_MIN);
		//return cmd_code + (DS_RD_RANGE_MIN - DS_WR_RANGE_MIN);
	}
}

UINT8 DS_is_read_cmd(int cmd_code)
{
	//if(cmd_code == DS_READ || cmd_code == DS_AUTH)
	if(cmd_code > DS_RD_RANGE_MIN && cmd_code < DS_RD_RANGE_MAX)
		return TRUE;
	else	
		return FALSE;
}

static UINT32 eveq_init = 1;

static __inline void send_primitive_R_XX(UINT32 response)
{
	UINT32	timeout_value;
	UINT32	g_R_OK_retry_count = 2;

	if ((GETREG(SATA_PHY_STATUS) >> 4) & 0x01)
	{
		timeout_value = ((((UINT32)CLOCK_SPEED / 2 / 1000000) * 300) / PRESCALE_TO_DIV(TIMER_PRESCALE_0));	// 300us
	}
	else
	{
		timeout_value = ((((UINT32)CLOCK_SPEED / 2 / 1000000) * 10) / PRESCALE_TO_DIV(TIMER_PRESCALE_0));	// 10us
	}

	SET_TIMER_LOAD(TIMER_CH4, timeout_value);
	SET_TIMER_CONTROL(TIMER_CH4, TM_ENABLE | TM_SHOT | TM_BIT_32 | TIMER_PRESCALE_0);

SEND_RETRY:

	SETREG(SATA_CTRL_2, response);				// response is R_OK or R_ERR

	while ((GETREG(SATA_INT_STAT) & OPERATION_OK) == 0)	// OPERATION_OK flag will be set upon the completion of R_OK or R_ERR transmission.
	{
		if (GET_TIMER_VALUE(TIMER_CH4) == 0)
		{
			SETREG(SATA_CTRL_1, BIT31 | BIT25 | BIT24 | BIT29);
			SETREG(SATA_CTRL_1, BIT31 | BIT25 | BIT24);

			SETREG(SATA_RESET_FIFO_1, 1); 						// Discard the contents of command layer FIFO
			while (GETREG(SATA_FIFO_1_STATUS) & 0x007F0000); 	// Wait until the command layer FIFO becomes empty

			if(--g_R_OK_retry_count == 0)	return;

			SET_TIMER_LOAD(TIMER_CH4, timeout_value);
			goto SEND_RETRY;
		}
	}

	SET_TIMER_CONTROL(TIMER_CH4, 0);
}

static __inline void handle_srst(void)
{
	send_primitive_R_XX(SEND_R_OK);

	SETREG(SATA_INT_STAT, REG_FIS_RECV | OPERATION_OK);

	if (GETREG(SATA_FIS_H2D_3) & BIT26)
	{
		g_sata_context.srst = TRUE;
 	}
	else if (g_sata_context.srst)
	{
		g_sata_context.srst = FALSE;

		SETREG(SATA_NCQ_CTRL, AUTOINC | FLUSH_NCQ);
		SETREG(SATA_NCQ_CTRL, AUTOINC);

		SETREG(SATA_RESET_FIFO_1, 1);
		while (GETREG(SATA_FIFO_1_STATUS) & 0x007F0000);

		SETREG(SATA_CTRL_1, BIT31 | BIT25 | BIT24 | BIT29);
		SETREG(SATA_CTRL_1, BIT31 | BIT25 | BIT24);

		g_sata_context.slow_cmd.code = ATA_SRST;
		g_sata_context.slow_cmd.status = SLOW_CMD_STATUS_PENDING;
		g_sata_context.slow_cmd.lba = FALSE;	// interrupt bit; see ata_srst().
	}
	else
	{
		SETREG(SATA_INT_STAT, REG_FIS_RECV);
	}
}

///key event queue SW
static __inline UINT32 queue_isFull()
{
	return ((eveq_rear+1)%Q_SIZE == eveq_front);
}

static __inline void queue_push(UINT32 lba, UINT32 sector_count, UINT32 cmd_type, SGX_PARAM sgx_param)
{
	if(eveq_init==1)
	{
		eveq_front=0;
		eveq_rear=0;
		eveq_init=0;
		eve_q = (EVENT_Q *)(EVENTQ_ADDR);
	
	}

	if(!queue_isFull())
	{
		

		if(sgx_param.fid==GEN_FILE)	//general file
		{
		/*
			eve_q[eveq_rear].lba = lba;
			eve_q[eveq_rear].sector_count = sector_count;
			eve_q[eveq_rear].cmd_type = cmd_type;
		//eve_q[eveq_rear].key = key;
		//eve_q[eveq_rear].sgx_lba = sgx_lba;
		//eve_q[eveq_rear].sgx_param = sgx_param;
			eve_q[eveq_rear].sgx_fd = 0;	//GEN FILE이라는뜻
		*/	
		/*
			write_dram_32(&eve_q[eveq_rear].lba, lba);
			write_dram_32(&eve_q[eveq_rear].sector_count, sector_count);
			write_dram_32(&eve_q[eveq_rear].cmd_type, cmd_type);
			write_dram_32(&eve_q[eveq_rear].sgx_fd, 0);
*/
			*((UINT32*)DS_VA_to_PA(&eve_q[eveq_rear].lba))=lba;
			*((UINT32*)DS_VA_to_PA(&eve_q[eveq_rear].sector_count))=sector_count;
			*((UINT32*)DS_VA_to_PA(&eve_q[eveq_rear].cmd_type))=cmd_type;
			*((UINT32*)DS_VA_to_PA(&eve_q[eveq_rear].sgx_fd))=0;

			
			

		}
		else					//sgx file
		{	
			/*
			eve_q[eveq_rear].lba = sgx_param.offset & 0xffffffff;
			eve_q[eveq_rear].sector_count = sector_count;
		//	uart_printf("[queue_pushS] %d", eve_q[eveq_rear].sector_count);
			
			//cmd_type 은 I/O종류(1B) | offset MSB 2B | sgx cmd 1B 
			eve_q[eveq_rear].cmd_type = ((cmd_type & 0x000000ff) << 24 ) | ((sgx_param.offset & 0x0000ffff00000000) >> 24) | (sgx_param.cmd);
			eve_q[eveq_rear].sgx_fd = sgx_param.fid;
			*/
		/*
			write_dram_32(&eve_q[eveq_rear].lba, sgx_param->offset & 0xffffffff);
			write_dram_32(&eve_q[eveq_rear].sector_count, sector_count);
			write_dram_32(&eve_q[eveq_rear].cmd_type, ((cmd_type & 0x000000ff) << 24 ) | ((sgx_param->offset & 0x0000ffff00000000) >> 24) | (sgx_param->cmd));
			write_dram_32(&eve_q[eveq_rear].sgx_fd, sgx_param->fid);
			*/	
		
			*((UINT32*)DS_VA_to_PA(&eve_q[eveq_rear].lba))=sgx_param.offset & 0xffffffff;
			*((UINT32*)DS_VA_to_PA(&eve_q[eveq_rear].sector_count))=sector_count;
			*((UINT32*)DS_VA_to_PA(&eve_q[eveq_rear].cmd_type))=((cmd_type & 0x000000ff) << 24 ) | ((sgx_param.offset & 0x0000ffff00000000) >> 24) | (sgx_param.cmd);
			*((UINT32*)DS_VA_to_PA(&eve_q[eveq_rear].sgx_fd))=sgx_param.fid;
		
		}

		
		
		/*
		if(eveq_size >= Q_SIZE-1)
		{
			uart_printf("event queue over");
		}
		*/
		//uart_printf("push : lba : %x, sec_cnt: %d, type : %d %d",lba,sector_count,cmd_type,eveq_rear);
		//uart_print("push");
		eveq_rear = (eveq_rear+1) % Q_SIZE;
		eveq_size++;
		SETREG(SATA_EQ_STATUS, ((eveq_size) & 0xFF)<<16);
		//SETREG(SATA_EQ_STATUS, ((eveq_rear-eveq_front) & 0xFF)<<16);
		//uart_printf("%d", eveq_size);
		//SETREG(SATA_EQ_STATUS, ((eveq_rear-eveq_front) & 0xFF)<<16);
	}
	else{
	;//	uart_print("qfull");
	//	uart_printf("QUEUEFULL");
	}
}

//DS SATA로부터 커맨드 받아온다.
static __inline void handle_got_cfis(void)
{
	UINT32 lba, sector_count, cmd_code, cmd_type, fis_d1, fis_d3;
	UINT32 reserved[4], i;
	//UINT32 H2D_key;
	//SGX_PARAM *H2D_sgx_param=NULL;
	SGX_PARAM H2D_sgx_param;

	cmd_code = (GETREG(SATA_FIS_H2D_0) & 0x00FF0000) >> 16;
	
	cmd_type = ata_cmd_class_table[cmd_code];
	fis_d1 = GETREG(SATA_FIS_H2D_1);
	fis_d3 = GETREG(SATA_FIS_H2D_3);

//	uart_printf("hgc");
	//uart_printf("[handle_got_cfis] cmd : %x %x", cmd_code, cmd_type);
	//uart_printf("cmd_code in fis is : %d",cmd_code);
//	reserved[0] = (GETREG(SATA_FIS_H2D_4) & 0xFF000000) >> 24;
//	reserved[1] = (GETREG(SATA_FIS_H2D_4) & 0x00FF0000) >> 16;
//	reserved[2] = (GETREG(SATA_FIS_H2D_4) & 0x0000FF00) >> 8;
//	reserved[3] = (GETREG(SATA_FIS_H2D_4) & 0x000000FF);
	//uart_printf("my reserved number is : %d %d %d %d\n", reserved[0], reserved[1], reserved[2], reserved[3]);
 	/*
	 H2D_key = GETREG(SATA_FIS_H2D_4) & 0xFFFFFFFF;	//key
	H2D_sgx_param.LBA[0] = (UINT8) (GETREG(SATA_FIS_H2D_1) & 0x000000FF);
	H2D_sgx_param.LBA[1] = (UINT8)((GETREG(SATA_FIS_H2D_1) & 0x0000FF00) >> 8);
	H2D_sgx_param.LBA[2] = (UINT8)((GETREG(SATA_FIS_H2D_1) & 0x00FF0000) >> 16);
	H2D_sgx_param.LBA[3] = (UINT8)(GETREG(SATA_FIS_H2D_2) & 0x000000FF);
	H2D_sgx_param.LBA[4] = (UINT8)((GETREG(SATA_FIS_H2D_2) & 0x0000FF00) >> 8);
	H2D_sgx_param.LBA[5] = (UINT8)((GETREG(SATA_FIS_H2D_2) & 0x00FF0000) >> 16);
	H2D_sgx_param.flag = (UINT8)(GETREG(SATA_FIS_H2D_4) & 0x000000FF);
*/
	// Diskshield Command
	//cmd_code 밑에서 사용할수있음 유의,
	//if(cmd_code >= DS_RANGE_MIN && cmd_code <= DS_RANGE_MAX)
//	uart_printf("hgc %x", cmd_code);
	H2D_sgx_param.fid = GETREG(SATA_FIS_H2D_4) & 0xFFFFFFFF;
	//if(H2D_sgx_param.fid==1)
	if(DS_is_cmd(cmd_code))
	{
	//	uart_print("DS");
		//uart_printf("SGX");
		H2D_sgx_param.fid = GETREG(SATA_FIS_H2D_4) & 0xFFFFFFFF;
		//H2D_sgx_param.offset = (GETREG(SATA_FIS_H2D_1) & 0x00FFFFFF) || ( (GETREG(SATA_FIS_H2D_2) & 0x00FFFFFF) << 24);
		H2D_sgx_param.offset = (UINT64)(GETREG(SATA_FIS_H2D_1) & 0x00FFFFFF);
	//	UINT64 b= (UINT64) ((GETREG(SATA_FIS_H2D_2) & 0x00FFFFFF) << 24);
		
		H2D_sgx_param.offset *= 512;	//사실 LBA로 들어오는 것이었다.
		//uart_printf("ofst %lld",H2D_sgx_param.offset);
		H2D_sgx_param.cmd =cmd_code; 
//		uart_printf("[handle_got_cfis] DS - fid : %x, offset : %x\n", H2D_sgx_param.fid, H2D_sgx_param.offset);
	//	uart_printf("cmd: %x", H2D_sgx_param.cmd);
	}
	else
	{
		H2D_sgx_param.fid = GEN_FILE;	//gen file이라는 의미
	}

	//uart_printf("handle_got_cfis : cmd_code : %x\n", cmd_code);
	
	if (cmd_type & ATR_LBA_NOR)
	{
		if ((fis_d1 & BIT30) == 0)	// CHS
		{
			//uart_printf("CMD_MODE CHS");
			UINT32 cylinder = (fis_d1 & 0x00FFFF00) >> 8;
			UINT32 head = (fis_d1 & 0x0F000000) >> 24;
			UINT32 sector = fis_d1 & 0x000000FF;

			lba = (cylinder * g_sata_context.chs_cur_heads + head) * g_sata_context.chs_cur_sectors + sector - 1;
		}
		else
		{
			lba = fis_d1 & 0x0FFFFFFF;
		//	uart_printf("CMD_MODE 3.5contiguous flag:%x, lba:%x",H2D_sgx_param.flag, lba);
		}

		sector_count = fis_d3 & 0x000000FF;
			//uart_printf("contiguous : sectour_count :%x\n", sector_count);
		if (sector_count == 0 && (cmd_type & ATR_NO_SECT) == 0)
		{
			sector_count = 0x100;
		}
	}
	//기본적인 4byte LBA
	else if (cmd_type & ATR_LBA_EXT)
	{
		//uart_printf("CMD_MODE general-~32bit");
		lba = (fis_d1 & 0x00FFFFFF) | (GETREG(SATA_FIS_H2D_2) << 24);
		sector_count = fis_d3 & 0x0000FFFF;

		if (sector_count == 0 && (cmd_type & ATR_NO_SECT) == 0)
		{
			sector_count = 0x10000;
		}
	}
	else
	{
	//	uart_printf("CMD_MODE fuckoff");
		lba = 0;
		sector_count = 0;
	}
//여기서부턴 이벤트큐에 세팅하는구간

//Diskshield
//sgx일경우 lba를 맞춰주어서 writebuffer, readbuffer가 잘돌아가도록 하자!
//	queue_push(lba, sector_count, cmd_type, H2D_key);
//다시 확인이 필요한데, 아마 offset이랑 lba가 같은 방식으로 돌아가야할듯.
//offset은 바이트단위. 
//write buffer의 적재적소에 위치시켜야 하므로. 
//그래야 아랫단에서 투명성있는 구현이 됨.
	if(H2D_sgx_param.fid!=GEN_FILE)
//	if(H2D_sgx_param!=NULL)
	//if(H2D_sgx_param.flag!=0)
	{
		//lba = (((UINT32)H2D_sgx_param.LBA[3])<<24) | (((UINT32)H2D_sgx_param.LBA[2])<<16) | (((UINT32)H2D_sgx_param.LBA[1])<<8) | (UINT32)H2D_sgx_param.LBA[0]; 
		lba = H2D_sgx_param.offset/BYTES_PER_SECTOR;
	
	}


//에러 인터럽트?
	if (lba + sector_count > MAX_LBA + 1 && (cmd_type & ATR_NO_SECT) == 0)
	{
		uart_print("INTERRUPT : lba boundary ");
		send_status_to_host(B_IDNF);
	}

	else if (cmd_type & (CCL_FTL_H2D | CCL_FTL_D2H))
	{
		UINT32 action_flags;
//lba, size가 해당 래지스터에 저장됨!
		SETREG(SATA_LBA, lba);
		SETREG(SATA_SECT_CNT, sector_count);

		
		if (cmd_type & CCL_FTL_H2D)	//WRITE
		{
			//command type이 저장됨
		
			SETREG(SATA_INSERT_EQ_W, 1);	// The contents of SATA_LBA and SATA_SECT_CNT are inserted into the event queue as a write command.
			queue_push(lba, sector_count, 1, H2D_sgx_param);	//key
			
//pio만 host에게 보내고, action_flags설정...''
			if (cmd_code == ATA_WRITE_DMA || cmd_code == ATA_WRITE_DMA_EXT \
			//|| ;cmd_code == DS_CREATE || cmd_code == DS_OPEN || cmd_code == DS_CLOSE || cmd_code==DS_REMOVE || cmd_code == DS_CLOSE)
			||DS_is_write_cmd(cmd_code))
			{
				action_flags = DMA_WRITE | COMPLETE;
			}
			else
			{
				//uart_printf("SATA REGISTER FIS RESET");
				UINT32 fis_type = FISTYPE_PIO_SETUP;
				UINT32 flags = B_IRQ;
				UINT32 status = B_DRDY | BIT4 | B_DRQ;
				UINT32 e_status = B_DRDY | BIT4;

				//response 날라갈듯.
				SETREG(SATA_FIS_D2H_0, fis_type | (flags << 8) | (status << 16));
				SETREG(SATA_FIS_D2H_1, GETREG(SATA_FIS_H2D_1));
				SETREG(SATA_FIS_D2H_2, GETREG(SATA_FIS_H2D_2) & 0x00FFFFFF);
				SETREG(SATA_FIS_D2H_3, (e_status << 24) | (GETREG(SATA_FIS_H2D_3) & 0x0000FFFF));
				SETREG(SATA_FIS_D2H_4, BYTES_PER_SECTOR);

				SETREG(SATA_FIS_D2H_LEN, 5);

				action_flags = PIO_WRITE | COMPLETE;
			}
		}
		else
		{
			SETREG(SATA_INSERT_EQ_R, 1);	// The contents of SATA_LBA and SATA_SECT_CNT are inserted into the event queue as a read command.
			queue_push(lba, sector_count, 0, H2D_sgx_param);	//key
		
			//DiskShield command
			if (cmd_code == ATA_READ_DMA || cmd_code == ATA_READ_DMA_EXT \
			//|| cmd_code == DS_READ)
			|| DS_is_read_cmd(cmd_code))
			{
				action_flags = DMA_READ | COMPLETE;
			}
			else
			{
				UINT32 fis_type = FISTYPE_PIO_SETUP;
				UINT32 flags = B_IRQ;
				UINT32 status = B_DRDY | BIT4 | B_DRQ;
				UINT32 e_status = B_DRDY | BIT4;

				SETREG(SATA_FIS_D2H_0, fis_type | (flags << 8) | (status << 16));
				SETREG(SATA_FIS_D2H_1, GETREG(SATA_FIS_H2D_1));
				SETREG(SATA_FIS_D2H_2, GETREG(SATA_FIS_H2D_2) & 0x00FFFFFF);
				SETREG(SATA_FIS_D2H_3, (e_status << 24) | (GETREG(SATA_FIS_H2D_3) & 0x0000FFFF));
				SETREG(SATA_FIS_D2H_4, BYTES_PER_SECTOR);

				SETREG(SATA_FIS_D2H_LEN, 5);

				action_flags = PIO_READ | COMPLETE | CLR_STAT_PIO_SETUP;
			}
		}

		SETREG(SATA_XFER_BYTES, sector_count * BYTES_PER_SECTOR);
		SETREG(SATA_SECT_OFFSET, lba);	// this information is used by SATA hardware to calculate sector offset into page buffer

		if (GETREG(SATA_EQ_STATUS) >> 31)
		{
			g_sata_context.eq_full = TRUE;
			g_sata_action_flags = action_flags;
		}
		else
		{
			SETREG(SATA_CTRL_2, action_flags);
		}
	}
	else
	{
		g_sata_context.slow_cmd.status = SLOW_CMD_STATUS_PENDING;
		g_sata_context.slow_cmd.code = cmd_code;
		g_sata_context.slow_cmd.lba = lba;
		g_sata_context.slow_cmd.sector_count = sector_count;
	}
}

#ifdef __GNUC__
void fiq_handler(void) __attribute__ ((interrupt ("FIQ")));
void fiq_handler(void)
#else
__irq void fiq_handler(void)
#endif
{
	UINT32 unmasked_int_stat = GETREG(SATA_INT_STAT);
	UINT32 masked_int_stat = unmasked_int_stat & GETREG(SATA_INT_ENABLE);
	UINT32 intr_processed = 0;

	if (masked_int_stat & CMD_RECV)
	{
		handle_got_cfis();
		intr_processed = CMD_RECV;
	}
	else if (masked_int_stat & OPERATION_ERR)
	{
		led_blink();
	}
	else if (masked_int_stat & REG_FIS_RECV)
	{
		if ((GETREG(SATA_FIS_H2D_0) & 0x000000FF) == FISTYPE_REGISTER_H2D)
		{
			handle_srst();
			intr_processed = 0;	// SATA_INT_STAT has been already cleared within handle_srst().
		}
		else
		{
			if (GETREG(SATA_ERROR) & BIT25)
			{
				send_primitive_R_XX(SEND_R_ERR);	// unknown type of FIS
				SETREG(SATA_ERROR, 0xFFFFFFFF);
			}
			else
			{
				send_primitive_R_XX(SEND_R_OK);
			}

			intr_processed = REG_FIS_RECV;
		}
	}
	else if (masked_int_stat & PHY_ONLINE)
	{
		intr_processed = 0xFFFFFFFF;

		g_sata_context.slow_cmd.code = ATA_SRST;
		g_sata_context.slow_cmd.status = SLOW_CMD_STATUS_PENDING;
		g_sata_context.slow_cmd.lba = FALSE;

		SETREG(SATA_INT_ENABLE, PHY_ONLINE | CMD_RECV | REG_FIS_RECV | NCQ_CMD_RECV);
	}
	else
	{
		intr_processed = masked_int_stat;
	}

	SETREG(SATA_INT_STAT, intr_processed);
	SETREG(APB_INT_STS, INTR_SATA);
}
