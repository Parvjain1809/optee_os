#include <drivers/i2c.h>
#include <kernel/dt.h>
#include <kernel/dt_driver.h>
#include <malloc.h>
#include <trace.h>
#include <string.h>
#include "tmp100.h"
#include <libfdt.h>

#include <drivers/omap_i2c.h>

#define TMP100_REG_TEMP      0
#define TMP100_REG_CONFIG    1
#define TMP100_REG_TLOW      2
#define TMP100_REG_THIGH     3

static TEE_Result tmp100_probe(const void *fdt, int node,
			       const void *compat_data __unused)
{
	struct tmp100_dev *dev = malloc(sizeof(*dev));
	TEE_Result res = TEE_SUCCESS;
	if (!dev)
		return TEE_ERROR_OUT_OF_MEMORY;

	res = i2c_dt_get_dev(fdt, node, &dev->i2c);
	if (res) {
		free(dev);
		return res;
	}

	dev->addr = fdt_read_uint32_default(fdt, node, "reg", 0x48);
	dev->i2c->addr = dev->addr;

	return TEE_SUCCESS;
}

TEE_Result tmp100_read_temp(int *temp_mC)
{
	void *fdt = get_embedded_dt();
	int i2c_node;
	struct i2c_dev *i2c = NULL;
	uint8_t tmp100_addr = 0x48;
	uint8_t reg = TMP100_REG_CONFIG;
	uint8_t buf[2] = { 0 };
	int raw;
	TEE_Result res;

	if (!temp_mC)
		return TEE_ERROR_BAD_PARAMETERS;

	i2c_node = fdt_path_offset(fdt, "/bus@f0000/i2c@20010000/tmp100@48");
	if (i2c_node < 0) {
		IMSG("I2C controller node not found");
		return TEE_ERROR_ITEM_NOT_FOUND;
	}

	res = i2c_dt_get_dev(fdt, i2c_node, &i2c);
	if (res) {
		EMSG("Failed to get I2C device");
		return res;
	}

	i2c->addr = tmp100_addr;

	buf[0] = reg;
	buf[1] = 0x80;

	res = i2c_write(i2c, buf, 2);
	if (res) {
		EMSG("Failed to write TMP100 temperature");
		return res;
	}

	res = i2c_write(i2c, &reg, 1);
	if (res) {
		EMSG("Failed to write TMP100 temperature");
		return res;
	}

	res = i2c_read(i2c, buf, 2);
	if (res) {
		EMSG("Failed to read TMP100 temperature");
		return res;
	}

	raw = ((buf[0] << 8) | buf[1]);
	*temp_mC = raw;
	IMSG("TMP100 temperature: 0x%02x %02x", buf[0], buf[1]);
	return TEE_SUCCESS;
}

static const struct dt_device_match tmp100_match_table[] = {
	{ .compatible = "ti,tmp100" },
	{}
};

DEFINE_DT_DRIVER(tmp100_dt_driver) = {
	.name = "tmp100",
	.type = DT_DRIVER_NOTYPE,
	.match_table = tmp100_match_table,
	.probe = tmp100_probe,
};
