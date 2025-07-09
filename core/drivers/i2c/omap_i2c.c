// SPDX-License-Identifier: BSD-2-Clause
/*
 * Texas Instruments K3 I2C Driver
 *
 * Copyright (C) 2025 Texas Instruments Incorporated - https://www.ti.com/
 */

#include <drivers/i2c.h>
#include <drivers/clk.h>
#include <drivers/pinctrl.h>
#include <initcall.h>
#include <io.h>
#include <kernel/delay.h>
#include <kernel/dt.h>
#include <kernel/dt_driver.h>
#include <libfdt.h>
#include <malloc.h>
#include <mm/core_memprot.h>
#include <mm/core_mmu.h>
#include <trace.h>
#include <util.h>

#include "drivers/omap_i2c.h"

static uint16_t i2c_read_reg(vaddr_t base, int reg)
{
	return io_read16(base + reg);
}

static void i2c_write_reg(vaddr_t base, uint16_t val, int reg)
{
	io_write16(base + reg, val);
}

static int i2c_findpsc(uint32_t *pscl, uint32_t *psch, unsigned int speed)
{
	unsigned long internal_clk = 0;
	unsigned long fclk = I2C_IP_CLK / 1000;
	unsigned long prescaler = 0;
	unsigned long scl;

	speed /= 1000;
	if (speed > 100)
		internal_clk = 9600;
	else
		internal_clk = 4000;

	prescaler = fclk / internal_clk;
	prescaler -= 1;

	if (speed > 100) {
		scl = internal_clk / speed;
		*pscl = scl - (scl / 3) - I2C_FASTSPEED_SCLL_TRIM;
		*psch = (scl / 3) - I2C_FASTSPEED_SCLH_TRIM;
	} else {
		*pscl = internal_clk / (speed * 2) - I2C_FASTSPEED_SCLL_TRIM;
		*psch = internal_clk / (speed * 2) - I2C_FASTSPEED_SCLH_TRIM;
	}

	if (*pscl <= 0 || *psch <= 0 || prescaler <= 0)
		return TEE_ERROR_BAD_PARAMETERS;

	return prescaler;
}

static TEE_Result wait_for_bb(vaddr_t base, int waitdelay)
{
	int timeout = I2C_TIMEOUT;
	uint16_t stat;

	i2c_write_reg(base, 0xFFFF, OMAP_I2C_STAT_REG);
	while ((stat = i2c_read_reg(base, OMAP_I2C_IP_V2_IRQSTATUS_RAW) &
		       I2C_STAT_BB) && timeout--) {
		i2c_write_reg(base, stat, OMAP_I2C_STAT_REG);
		udelay(waitdelay);
	}

	if (timeout <= 0)
		return TEE_ERROR_TIMEOUT;

	i2c_write_reg(base, 0xFFFF, OMAP_I2C_STAT_REG);
	return TEE_SUCCESS;
}

static TEE_Result wait_for_event(vaddr_t base, int waitdelay)
{
	int timeout = I2C_TIMEOUT;
	TEE_Result status = TEE_SUCCESS;

	do {
		udelay(waitdelay);
		status = i2c_read_reg(base, OMAP_I2C_IP_V2_IRQSTATUS_RAW);
	} while (!(status & (I2C_STAT_ROVR | I2C_STAT_XUDF | I2C_STAT_XRDY |
			     I2C_STAT_RRDY | I2C_STAT_ARDY | I2C_STAT_NACK |
			     I2C_STAT_AL)) && timeout--);

	if (timeout <= 0) {
		i2c_write_reg(base, 0xFFFF, OMAP_I2C_STAT_REG);
		status = TEE_ERROR_TIMEOUT;
	}
	return status;
}

static void flush_fifo(vaddr_t base)
{
	uint16_t stat;

	while (1) {
		stat = i2c_read_reg(base, OMAP_I2C_STAT_REG);
		if (stat == I2C_STAT_RRDY) {
			i2c_read_reg(base, OMAP_I2C_DATA_REG);
			i2c_write_reg(base, I2C_STAT_RRDY, OMAP_I2C_STAT_REG);
			udelay(1000);
		} else
			break;
	}
}

static TEE_Result i2c_setspeed(vaddr_t base, unsigned int speed, int *waitdelay)
{
	int psc;
	int fsscll = 0;
	int fssclh = 0;
	int hsscll = 0;
	int hssclh = 0;
	uint32_t scll = 0;
	uint32_t sclh = 0;

	if (speed >= I2C_SPEED_HIGH_RATE) {
		psc = I2C_IP_CLK / I2C_INTERNAL_SAMPLING_CLK;
		psc -= 1;
		if (psc < I2C_PSC_MIN) {
			EMSG("Error : I2C unsupported prescaler %d\n", psc);
			return TEE_ERROR_NOT_SUPPORTED;
		}

		fsscll = I2C_INTERNAL_SAMPLING_CLK / (2 * speed);
		fssclh = fsscll;

		fsscll -= I2C_HIGHSPEED_PHASE_ONE_SCLL_TRIM;
		fssclh -= I2C_HIGHSPEED_PHASE_ONE_SCLH_TRIM;

		if ((fsscll < 0) || (fssclh < 0) ||
		    (fsscll > 255) || (fssclh > 255)) {
			EMSG("Error : I2C initializing first phase clock\n");
			return TEE_ERROR_BAD_PARAMETERS;
		}

		hsscll = I2C_INTERNAL_SAMPLING_CLK / (2 * speed);
		hssclh = I2C_INTERNAL_SAMPLING_CLK / (2 * speed);

		hsscll -= I2C_HIGHSPEED_PHASE_TWO_SCLL_TRIM;
		hssclh -= I2C_HIGHSPEED_PHASE_TWO_SCLH_TRIM;
		if ((hsscll < 0) || (hssclh < 0) ||
		    (hsscll > 255) || (hssclh > 255)) {
			EMSG("Error : I2C initializing second phase clock\n");
			return TEE_ERROR_BAD_PARAMETERS;
		}

		scll = (unsigned int)hsscll << 8 | (unsigned int)fsscll;
		sclh = (unsigned int)hssclh << 8 | (unsigned int)fssclh;

	} else {
		psc = i2c_findpsc(&scll, &sclh, speed);
		if (psc < 0) {
			EMSG("Error : I2C initializing clock\n");
			return TEE_ERROR_BAD_PARAMETERS;
		}
	}

	*waitdelay = (10000000 / speed) * 2;

	i2c_write_reg(base, 0, OMAP_I2C_CON_REG);
	i2c_write_reg(base, psc, OMAP_I2C_PSC_REG);
	i2c_write_reg(base, scll, OMAP_I2C_SCLL_REG);
	i2c_write_reg(base, sclh, OMAP_I2C_SCLH_REG);
	i2c_write_reg(base, I2C_CON_EN, OMAP_I2C_CON_REG);
	i2c_write_reg(base, 0xFFFF, OMAP_I2C_STAT_REG);

	return TEE_SUCCESS;
}

static void i2c_deblock(vaddr_t base)
{
	uint16_t systest;
	uint16_t orgsystest;

	orgsystest = i2c_read_reg(base, OMAP_I2C_SYSTEST_REG);
	systest = orgsystest;

	systest |= I2C_SYSTEST_ST_EN;
	i2c_write_reg(base, systest, OMAP_I2C_SYSTEST_REG);
	systest &= ~I2C_SYSTEST_TMODE_MASK;
	systest |= 3 << I2C_SYSTEST_TMODE_SHIFT;
	i2c_write_reg(base, systest, OMAP_I2C_SYSTEST_REG);

	systest |= I2C_SYSTEST_SCL_O | I2C_SYSTEST_SDA_O;
	i2c_write_reg(base, systest, OMAP_I2C_SYSTEST_REG);
	udelay(10);

	for (int i = 0; i < 9; i++) {
		systest &= ~I2C_SYSTEST_SCL_O;
		i2c_write_reg(base, systest, OMAP_I2C_SYSTEST_REG);
		udelay(10);
		systest |= I2C_SYSTEST_SCL_O;
		i2c_write_reg(base, systest, OMAP_I2C_SYSTEST_REG);
		udelay(10);
	}

	systest &= ~I2C_SYSTEST_SDA_O;
	i2c_write_reg(base, systest, OMAP_I2C_SYSTEST_REG);
	udelay(10);
	systest |= I2C_SYSTEST_SCL_O | I2C_SYSTEST_SDA_O;
	i2c_write_reg(base, systest, OMAP_I2C_SYSTEST_REG);
	udelay(10);

	i2c_write_reg(base, orgsystest, OMAP_I2C_SYSTEST_REG);
}

static void omap_i2c_init(vaddr_t base, int speed, int slavaddr, int *waitdelay)
{
	int timeout = I2C_TIMEOUT;
	int deblock = 1;
retry:
	if (i2c_read_reg(base, OMAP_I2C_CON_REG) & I2C_CON_EN) {
		i2c_write_reg(base, 0, OMAP_I2C_CON_REG);
		mdelay(50);
	}

	i2c_write_reg(base, 0x2, OMAP_I2C_SYSC_REG);
	udelay(1000);
	i2c_write_reg(base, I2C_CON_EN, OMAP_I2C_CON_REG);
	while (!(i2c_read_reg(base, OMAP_I2C_SYSS_REG) & I2C_SYSS_RDONE) &&
	       timeout--) {
		if (timeout <= 0)
			return;

		udelay(1000);
	}

	if (i2c_setspeed(base, speed, waitdelay)) {
		EMSG("ERROR: failed to setup I2C bus-speed!\n");
		return;
	}
	i2c_write_reg(base, slavaddr, OMAP_I2C_OA_REG);

	udelay(1000);
	flush_fifo(base);
	i2c_write_reg(base, 0xFFFF, OMAP_I2C_STAT_REG);

	if (wait_for_bb(base, *waitdelay))
		if (deblock == 1) {
			i2c_deblock(base);
			deblock = 0;
			goto retry;
		}
}

static TEE_Result omap_i2c_xfer_msg(vaddr_t base, int waitdelay,
				    unsigned char chip, uint8_t *buffer,
				    int len, uint16_t i2c_con_reg, bool is_read)
{
	int i = 0;
	TEE_Result res = TEE_SUCCESS;
	uint16_t status;
	int timeout = I2C_TIMEOUT;

	if (len < 0) {
		EMSG("data len < 0");
		return TEE_ERROR_BAD_PARAMETERS;
	}
	if (!buffer) {
		EMSG("NULL pointer passed");
		return TEE_ERROR_BAD_PARAMETERS;
	}

	if (wait_for_bb(base, waitdelay)) {
		EMSG("I2C: Bus busy timeout");
		return TEE_ERROR_BUSY;
	}

	i2c_write_reg(base, chip, OMAP_I2C_SA_REG);
	i2c_write_reg(base, len, OMAP_I2C_CNT_REG);
	i2c_write_reg(base, 0xFFFF, OMAP_I2C_STAT_REG);
	i2c_write_reg(base, i2c_con_reg, OMAP_I2C_CON_REG);

	for (i = 0; i < len; i++) {
		status = wait_for_event(base, waitdelay);

		if (!status || (status & I2C_STAT_NACK)) {
			EMSG("I2C: NACK or timeout (status=0x%x)", status);
			res = TEE_ERROR_TIMEOUT;
			goto xfer_exit;
		}
		if (i2c_con_reg & I2C_CON_TRX)
			status &= ~I2C_STAT_RRDY;
		else
			status &= ~I2C_STAT_XRDY;

		if ((!is_read && status == I2C_STAT_XRDY) ||
		    (is_read && status == I2C_STAT_RRDY)) {
			res = TEE_ERROR_BAD_STATE;
			EMSG("pads on bus probably not configured (status=0x%x)",
			     status);
			goto xfer_exit;
		}

		if (!is_read && (status & I2C_STAT_XRDY)) {
			i2c_write_reg(base, buffer[i], OMAP_I2C_DATA_REG);
			i2c_write_reg(base, I2C_STAT_XRDY, OMAP_I2C_STAT_REG);
		} else if (is_read && (status & I2C_STAT_RRDY)) {
			buffer[i] = i2c_read_reg(base, OMAP_I2C_DATA_REG);
			i2c_write_reg(base, I2C_STAT_RRDY, OMAP_I2C_STAT_REG);
		}
	}

	do {
		status = wait_for_event(base, waitdelay);
	} while (!(status & I2C_STAT_ARDY) && timeout--);

	if (timeout <= 0) {
		EMSG("I2C: Timeout waiting for ARDY");
		res = TEE_ERROR_TIMEOUT;
	} else {
		i2c_write_reg(base, I2C_STAT_ARDY, OMAP_I2C_STAT_REG);
		wait_for_bb(base, waitdelay);
	}

xfer_exit:
	flush_fifo(base);
	i2c_write_reg(base, 0xFFFF, OMAP_I2C_STAT_REG);
	return res;
}

static TEE_Result omap_i2c_dev_read(struct i2c_dev *i2c_dev, uint8_t *buf,
				    size_t len)
{
	struct omap_i2c_dev *dev =
		container_of(i2c_dev, struct omap_i2c_dev, i2c_dev);
	int ret = TEE_SUCCESS;
	uint16_t i2c_con_reg = 0;

	i2c_con_reg = I2C_CON_EN | I2C_CON_MST | I2C_CON_STT | I2C_CON_STP;
	i2c_con_reg &= ~I2C_CON_TRX;
	ret = omap_i2c_xfer_msg(dev->base, dev->waitdelay, i2c_dev->addr, buf,
				len, i2c_con_reg, true);
	if (ret)
		return ret;

	return ret;
}

static TEE_Result omap_i2c_dev_write(struct i2c_dev *i2c_dev,
				     const uint8_t *buf, size_t len)
{
	struct omap_i2c_dev *dev =
		container_of(i2c_dev, struct omap_i2c_dev, i2c_dev);
	int ret = TEE_SUCCESS;
	uint16_t i2c_con_reg = 0;

	i2c_con_reg = I2C_CON_EN | I2C_CON_MST | I2C_CON_STT | I2C_CON_STP |
		      I2C_CON_TRX;
	ret = omap_i2c_xfer_msg(dev->base, dev->waitdelay, i2c_dev->addr,
				(uint8_t *)buf, len, i2c_con_reg, false);
	if (ret)
		return ret;

	return ret;
}

static const struct i2c_ctrl_ops omap_i2c_ops = {
	.read = omap_i2c_dev_read,
	.write = omap_i2c_dev_write,
};

static struct omap_i2c_dev *omap_i2c_dev_from_addr(vaddr_t addr)
{
	static struct omap_i2c_dev devs[4];
	static int used[4] = { 0 };
	int i;

	for (i = 0; i < 4; i++) {
		if (used[i] && devs[i].base == addr)
			return &devs[i];
	}
	for (i = 0; i < 4; i++) {
		if (!used[i]) {
			used[i] = 1;
			devs[i].base = addr;
			return &devs[i];
		}
	}
	return NULL;
}

static TEE_Result omap_get_i2c_dev(struct dt_pargs *args, void *data __unused,
				   struct i2c_dev **out_dev)
{
	struct omap_i2c_dev *dev = NULL;
	paddr_t addr = 0;
	vaddr_t vaddr = 0;
	size_t size = 0;

	if (!args || !out_dev)
		return TEE_ERROR_BAD_PARAMETERS;

	addr = fdt_reg_base_address(args->fdt, args->phandle_node);
	if (addr == DT_INFO_INVALID_REG) {
		EMSG("DT_INFO_INVALID_REG");
		return TEE_ERROR_BAD_PARAMETERS;
	}

	if (dt_map_dev(args->fdt, args->phandle_node, &vaddr, &size,
		       DT_MAP_AUTO) < 0) {
		EMSG("Failed to map I2C controller base address");
		return TEE_ERROR_GENERIC;
	}

	dev = omap_i2c_dev_from_addr(vaddr);
	if (!dev) {
		EMSG("dev - omap_i2c_dev_from_addr");
		return TEE_ERROR_OUT_OF_MEMORY;
	}

	dev->ctrl.ops = &omap_i2c_ops;
	dev->i2c_dev.ctrl = &dev->ctrl;
	dev->base = vaddr;

	*out_dev = &dev->i2c_dev;
	return TEE_SUCCESS;
}

static TEE_Result omap_i2c_get_setup_from_fdt(void *fdt, int node,
					      struct omap_i2c_dt_config *cfg)
{
	int len = 0;
	const fdt32_t *cuint = NULL;

	cuint = fdt_getprop(fdt, node, "reg", &len);
	if (!cuint || (size_t)len < (4 * sizeof(uint32_t)))
		return TEE_ERROR_BAD_PARAMETERS;

	cfg->pbase = ((uint64_t)fdt32_to_cpu(cuint[0]) << 32) |
		     fdt32_to_cpu(cuint[1]);
	cfg->reg_size = ((uint64_t)fdt32_to_cpu(cuint[2]) << 32) |
			fdt32_to_cpu(cuint[3]);

	cuint = fdt_getprop(fdt, node, "clock-frequency", NULL);
	cfg->bus_rate = cuint ? fdt32_to_cpu(*cuint) : I2C_SPEED_STANDARD_RATE;

	cuint = fdt_getprop(fdt, node, "slave-address", NULL);
	cfg->slav_addr = cuint ? fdt32_to_cpu(*cuint) : 0;

	return TEE_SUCCESS;
}

static TEE_Result omap_i2c_probe(const void *fdt, int node,
				 const void *compat_data __unused)
{
	TEE_Result res = TEE_SUCCESS;
	struct omap_i2c_dt_config cfg = {};
	vaddr_t vbase;
	int waitdelay = 1000;
	struct i2c_dev *i2cdev;
	struct omap_i2c_dev *dev;
	struct dt_pargs pargs = { 0 };
	size_t size = 0;
	struct pinctrl_state *pinctrl = NULL;

	res = omap_i2c_get_setup_from_fdt((void *)fdt, node, &cfg);
	if (res)
		return res;

	if (dt_map_dev(fdt, node, &vbase, &size, DT_MAP_AUTO) < 0) {
		EMSG("Failed to map I2C controller base address");
		return TEE_ERROR_GENERIC;
	}

	res = pinctrl_get_state_by_name(fdt, node, "default", &pinctrl);
	if (res)
		return res;

	if (pinctrl) {
		res = pinctrl_apply_state(pinctrl);
		if (res) {
			EMSG("Failed to apply default pinctrl state: 0x%x",
			     res);
			pinctrl_free_state(pinctrl);
			return res;
		}
	}

	omap_i2c_init(vbase, cfg.bus_rate, cfg.slav_addr, &waitdelay);

	pargs.fdt = (void *)fdt;
	pargs.phandle_node = node;

	res = omap_get_i2c_dev(&pargs, NULL, &i2cdev);
	if (res)
		return res;

	dev = container_of(i2cdev, struct omap_i2c_dev, i2c_dev);
	dev->waitdelay = waitdelay;
	dev->i2c_dev.addr = cfg.slav_addr;
	dev->base = vbase;
	dev->pinctrl = pinctrl;

	res = i2c_register_provider(fdt, node,
				    (i2c_dt_get_func)omap_get_i2c_dev, NULL);
	if (res) {
		EMSG("i2c_register_provider failed: 0x%x", res);
		return res;
	}
	return res;
}

static const struct dt_device_match omap_i2c_match_table[] = {
	{ .compatible = "ti,omap4-i2c" },
	{}
};

DEFINE_DT_DRIVER(omap_i2c_driver) = {
	.name = "omap_i2c",
	.type = DT_DRIVER_I2C,
	.match_table = omap_i2c_match_table,
	.probe = omap_i2c_probe,
};
