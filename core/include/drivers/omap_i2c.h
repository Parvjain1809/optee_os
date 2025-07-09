/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Texas Instruments K3 I2C Driver
 *
 * Copyright (C) 2025 Texas Instruments Incorporated - https://www.ti.com/
 */

#ifndef __OMAP_I2C_H
#define __OMAP_I2C_H

#include <drivers/i2c.h>
#include <stdint.h>
#include <tee_api_types.h>
#include <types_ext.h>

#define I2C_TIMEOUT	1000
#define I2C_WAIT	200

#define OMAP_I2C_STAT_REG	0x28
#define OMAP_I2C_WE_REG		0x34
#define OMAP_I2C_SYSS_REG	0x90
#define OMAP_I2C_BUF_REG	0x94
#define OMAP_I2C_CNT_REG	0x98
#define OMAP_I2C_DATA_REG	0x9c
#define OMAP_I2C_SYSC_REG	0x10
#define OMAP_I2C_CON_REG	0xa4
#define OMAP_I2C_OA_REG		0xa8
#define OMAP_I2C_SA_REG		0xac
#define OMAP_I2C_PSC_REG		0xb0
#define OMAP_I2C_SCLL_REG		0xb4
#define OMAP_I2C_SCLH_REG		0xb8
#define OMAP_I2C_SYSTEST_REG	0xbc
#define OMAP_I2C_BUFSTAT_REG	0xc0
#define OMAP_I2C_IP_V2_REVNB_LO 0x00
#define OMAP_I2C_IP_V2_REVNB_HI 0x04
#define OMAP_I2C_IP_V2_IRQSTATUS_RAW 0x24
#define OMAP_I2C_IP_V2_IRQENABLE_SET 0x2c
#define OMAP_I2C_IP_V2_IRQENABLE_CLR 0x30

/* I2C Status Register (I2C_STAT): */

#define I2C_STAT_SBD	BIT(15) /* Single byte data */
#define I2C_STAT_BB		BIT(12) /* Bus busy */
#define I2C_STAT_ROVR	BIT(11) /* Receive overrun */
#define I2C_STAT_XUDF	BIT(10) /* Transmit underflow */
#define I2C_STAT_AAS	BIT(9)  /* Address as slave */
#define I2C_STAT_GC		BIT(5)
#define I2C_STAT_XRDY	BIT(4)  /* Transmit data ready */
#define I2C_STAT_RRDY	BIT(3)  /* Receive data ready */
#define I2C_STAT_ARDY	BIT(2)  /* Register access ready */
#define I2C_STAT_NACK	BIT(1)  /* No acknowledgment interrupt enable */
#define I2C_STAT_AL		BIT(0)  /* Arbitration lost interrupt enable */

/* I2C Interrupt Code Register (I2C_INTCODE): */

#define I2C_INTCODE_MASK	7
#define I2C_INTCODE_NONE	0
#define I2C_INTCODE_AL		1	/* Arbitration lost */
#define I2C_INTCODE_NAK		2	/* No acknowledgment/general call */
#define I2C_INTCODE_ARDY	3	/* Register access ready */
#define I2C_INTCODE_RRDY	4	/* Rcv data ready */
#define I2C_INTCODE_XRDY	5	/* Xmit data ready */

/* I2C Buffer Configuration Register (I2C_BUF): */

#define I2C_BUF_RDMA_EN		BIT(15) /* Receive DMA channel enable */
#define I2C_BUF_XDMA_EN		BIT(7)  /* Transmit DMA channel enable */

/* I2C Configuration Register (I2C_CON): */

#define I2C_CON_EN	BIT(15)  /* I2C module enable */
#define I2C_CON_BE	BIT(14)  /* Big endian mode */
#define I2C_CON_STB	BIT(11)  /* Start byte mode (master mode only) */
#define I2C_CON_MST	BIT(10)  /* Master/slave mode */
#define I2C_CON_TRX	BIT(9)   /* Transmitter/receiver mode */
				   /* (master mode only) */
#define I2C_CON_XA	BIT(8)   /* Expand address */
#define I2C_CON_STP	BIT(1)   /* Stop condition (master mode only) */
#define I2C_CON_STT	BIT(0)   /* Start condition (master mode only) */

/* I2C System Test Register (I2C_SYSTEST): */

#define I2C_SYSTEST_ST_EN	BIT(15) /* System test enable */
#define I2C_SYSTEST_FREE	BIT(14) /* Free running mode, on brkpoint) */
#define I2C_SYSTEST_TMODE_MASK	(3 << 12) /* Test mode select */
#define I2C_SYSTEST_TMODE_SHIFT	(12)	  /* Test mode select */
#define I2C_SYSTEST_SCL_I	BIT(3)  /* SCL line sense input value */
#define I2C_SYSTEST_SCL_O	BIT(2)  /* SCL line drive output value */
#define I2C_SYSTEST_SDA_I	BIT(1)  /* SDA line sense input value */
#define I2C_SYSTEST_SDA_O	BIT(0)  /* SDA line drive output value */

/* I2C System Status Register (I2C_SYSS): */

#define I2C_SYSS_RDONE          BIT(0)  /* Internal reset monitoring */

#define SYSTEM_CLOCK_96		96000000
#ifndef I2C_IP_CLK
#define I2C_IP_CLK		SYSTEM_CLOCK_96
#endif

#ifndef I2C_INTERNAL_SAMPLING_CLK
#define I2C_INTERNAL_SAMPLING_CLK	19200000
#endif

#ifndef I2C_FASTSPEED_SCLL_TRIM
#define I2C_FASTSPEED_SCLL_TRIM		7
#endif
#ifndef I2C_FASTSPEED_SCLH_TRIM
#define I2C_FASTSPEED_SCLH_TRIM		5
#endif

#ifndef I2C_HIGHSPEED_PHASE_ONE_SCLL_TRIM
#define I2C_HIGHSPEED_PHASE_ONE_SCLL_TRIM	I2C_FASTSPEED_SCLL_TRIM
#endif
#ifndef I2C_HIGHSPEED_PHASE_ONE_SCLH_TRIM
#define I2C_HIGHSPEED_PHASE_ONE_SCLH_TRIM	I2C_FASTSPEED_SCLH_TRIM
#endif
#ifndef I2C_HIGHSPEED_PHASE_TWO_SCLL_TRIM
#define I2C_HIGHSPEED_PHASE_TWO_SCLL_TRIM	I2C_FASTSPEED_SCLL_TRIM
#endif
#ifndef I2C_HIGHSPEED_PHASE_TWO_SCLH_TRIM
#define I2C_HIGHSPEED_PHASE_TWO_SCLH_TRIM	I2C_FASTSPEED_SCLH_TRIM
#endif

#define I2C_PSC_MAX		0x0f
#define I2C_PSC_MIN		0x00

#define I2C_SPEED_STANDARD_RATE		100000
#define I2C_SPEED_FAST_RATE			400000
#define I2C_SPEED_FAST_PLUS_RATE	1000000
#define I2C_SPEED_HIGH_RATE			3400000
#define I2C_SPEED_FAST_ULTRA_RATE	5000000

struct omap_i2c_dt_config {
	uint64_t pbase;
	uint64_t reg_size;
	uint32_t bus_rate;
	int slav_addr;
};

struct omap_i2c_dev {
	struct i2c_dev i2c_dev;
	struct i2c_ctrl ctrl;
	vaddr_t base;
	int waitdelay;
	struct pinctrl_state *pinctrl;
};

#endif
