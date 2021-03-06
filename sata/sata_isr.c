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
EVENT_Q eve_q[Q_SIZE];
UINT32 eveq_front;
UINT32 eveq_rear;
static UINT32 eveq_init = 1;

UINT32 isRecoveryCmd(UINT32 cmd_code)
{
	if (cmd_code == CMD_RECOVERY_NOR)
		return 1;
	if (cmd_code == CMD_RECOVERY_EXT)
		return 1;
	if (cmd_code == CMD_RECOVERY_ALL_NOR)
		return 1;
	if (cmd_code == CMD_RECOVERY_ALL_EXT)
		return 1;

	return 0;
}

static __inline void send_primitive_R_XX(UINT32 response)
{
	UINT32 timeout_value;
	UINT32 g_R_OK_retry_count = 2;

	if ((GETREG(SATA_PHY_STATUS) >> 4) & 0x01)
	{
		timeout_value = ((((UINT32)CLOCK_SPEED / 2 / 1000000) * 300) / PRESCALE_TO_DIV(TIMER_PRESCALE_0)); // 300us
	}
	else
	{
		timeout_value = ((((UINT32)CLOCK_SPEED / 2 / 1000000) * 10) / PRESCALE_TO_DIV(TIMER_PRESCALE_0)); // 10us
	}

	SET_TIMER_LOAD(TIMER_CH4, timeout_value);
	SET_TIMER_CONTROL(TIMER_CH4, TM_ENABLE | TM_SHOT | TM_BIT_32 | TIMER_PRESCALE_0);

SEND_RETRY:

	SETREG(SATA_CTRL_2, response); // response is R_OK or R_ERR


	while ((GETREG(SATA_INT_STAT) & OPERATION_OK) == 0) // OPERATION_OK flag will be set upon the completion of R_OK or R_ERR transmission.
	{
		if (GET_TIMER_VALUE(TIMER_CH4) == 0)
		{
			SETREG(SATA_CTRL_1, BIT31 | BIT25 | BIT24 | BIT29);
			SETREG(SATA_CTRL_1, BIT31 | BIT25 | BIT24);

			SETREG(SATA_RESET_FIFO_1, 1); // Discard the contents of command layer FIFO
			while (GETREG(SATA_FIFO_1_STATUS) & 0x007F0000)
				; // Wait until the command layer FIFO becomes empty

			if (--g_R_OK_retry_count == 0)
				return;

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
		while (GETREG(SATA_FIFO_1_STATUS) & 0x007F0000)
			;

		SETREG(SATA_CTRL_1, BIT31 | BIT25 | BIT24 | BIT29);
		SETREG(SATA_CTRL_1, BIT31 | BIT25 | BIT24);

		g_sata_context.slow_cmd.code = ATA_SRST;
		g_sata_context.slow_cmd.status = SLOW_CMD_STATUS_PENDING;
		g_sata_context.slow_cmd.lba = FALSE; // interrupt bit; see ata_srst().
	}
	else
	{
		SETREG(SATA_INT_STAT, REG_FIS_RECV);
	}
}

///key event queue SW
static __inline UINT32 queue_isFull()
{
	return ((eveq_rear + 1) % Q_SIZE == eveq_front);
}

static __inline void queue_push(UINT32 lba, UINT32 sector_count, UINT32 cmd_type, UINT32 recv_meta)
{
	if (eveq_init == 1)
	{
		eveq_front = 0;
		eveq_rear = 0;
		eveq_init = 0;
	}

	if (!queue_isFull())
	{
		eve_q[eveq_rear].lba = lba;
		eve_q[eveq_rear].sector_count = sector_count;
		eve_q[eveq_rear].cmd_type = cmd_type;
		eve_q[eveq_rear].sgx_fd = recv_meta;
		eveq_rear = (eveq_rear + 1) % Q_SIZE;
		SETREG(SATA_EQ_STATUS, ((eveq_rear - eveq_front) & 0xFF) << 16);
		//	uart_printf("queue push : lba : %d, sec_cnt: %d, type : %d, key : %d",lba,sector_count,cmd_type,key);
	}
}

static __inline void handle_got_cfis(void)
{
	UINT32 lba, sector_count, cmd_code, cmd_type, fis_d1, fis_d3;
	UINT32 fd;
	UINT32 recovery_time;
	cmd_code = (GETREG(SATA_FIS_H2D_0) & 0x00FF0000) >> 16;
	cmd_type = ata_cmd_class_table[cmd_code];
	fis_d1 = GETREG(SATA_FIS_H2D_1);
	fis_d3 = GETREG(SATA_FIS_H2D_3);

	if (cmd_type & ATR_LBA_NOR)
	{
		if ((fis_d1 & BIT30) == 0) // CHS
		{
			UINT32 cylinder = (fis_d1 & 0x00FFFF00) >> 8;
			UINT32 head = (fis_d1 & 0x0F000000) >> 24;
			UINT32 sector = fis_d1 & 0x000000FF;

			lba = (cylinder * g_sata_context.chs_cur_heads + head) * g_sata_context.chs_cur_sectors + sector - 1;
		}
		else
		{
			lba = fis_d1 & 0x0FFFFFFF;
		}

		sector_count = fis_d3 & 0x000000FF;

		if (sector_count == 0 && (cmd_type & ATR_NO_SECT) == 0)
		{
			sector_count = 0x100;
		}
	}
	else if (cmd_type & ATR_LBA_EXT)
	{
		lba = (fis_d1 & 0x00FFFFFF) | (GETREG(SATA_FIS_H2D_2) << 24);
		sector_count = fis_d3 & 0x0000FFFF;

		if (sector_count == 0 && (cmd_type & ATR_NO_SECT) == 0)
		{
			sector_count = 0x10000;
		}
	}
	else
	{
		lba = 0;
		sector_count = 0;
	}

	if (lba + sector_count > MAX_LBA + 1 && (cmd_type & ATR_NO_SECT) == 0)
	{
		send_status_to_host(B_IDNF);
	}
	else if (cmd_type & (CCL_FTL_H2D | CCL_FTL_D2H))
	{
		UINT32 action_flags;

		SETREG(SATA_LBA, lba);
		SETREG(SATA_SECT_CNT, sector_count);

		if (cmd_type & CCL_FTL_H2D)
		{
			SETREG(SATA_INSERT_EQ_W, 1); // The contents of SATA_LBA and SATA_SECT_CNT are inserted into the event queue as a write command.

			if (cmd_code == CMD_SGXSSD_WRITE_EXT)
				queue_push(lba, sector_count, CMD_SGXSSD_WRITE_EXT, 0); //sgxssd piggyback
			else if (cmd_code == CMD_SGXSSD_WRITE_NOR)
				queue_push(lba, sector_count, CMD_SGXSSD_WRITE_NOR, 0); //sgxssd piggyback
			else
				queue_push(lba, sector_count, 1, 0); //normal I/O

			//queue_push(lba, sector_count, 1);	//key
			if (cmd_code == ATA_WRITE_DMA || cmd_code == ATA_WRITE_DMA_EXT || cmd_code == CMD_SGXSSD_WRITE_EXT || cmd_code == CMD_SGXSSD_WRITE_NOR)
			{
				action_flags = DMA_WRITE | COMPLETE;
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

				action_flags = PIO_WRITE | COMPLETE;
			}
		}
		else
		{	//D2H READ
			SETREG(SATA_INSERT_EQ_R, 1); // The contents of SATA_LBA and SATA_SECT_CNT are inserted into the event queue as a read command.

			//recovery command
			if(isRecoveryCmd(cmd_code))
			{
				//reserved space에서 2바이트씩 받는걸로. main.c에서 어차피 다시 받음.
				recovery_time = GETREG(SATA_FIS_H2D_4) & 0x0000FFFF;
				fd = (GETREG(SATA_FIS_H2D_4) & 0xFFFF0000) >> 4;
				queue_push(lba, sector_count, cmd_code, GETREG(SATA_FIS_H2D_4));
			}	
			else
			{
				queue_push(lba, sector_count, 0, 0); //normal I/O
			}

			//uart_printf("insert Q cmd_type: %d lba %d size %d", 0, lba, sector_count);
			if (cmd_code == ATA_READ_DMA || cmd_code == ATA_READ_DMA_EXT || isRecoveryCmd(cmd_code))
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
		SETREG(SATA_SECT_OFFSET, lba); // this information is used by SATA hardware to calculate sector offset into page buffer

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
void fiq_handler(void) __attribute__((interrupt("FIQ")));
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
			intr_processed = 0; // SATA_INT_STAT has been already cleared within handle_srst().
		}
		else
		{
			if (GETREG(SATA_ERROR) & BIT25)
			{
				send_primitive_R_XX(SEND_R_ERR); // unknown type of FIS
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
