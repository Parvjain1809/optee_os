#ifndef TMP100_H
#define TMP100_H

#include <tee_api_types.h>
#include <drivers/i2c.h>
#include <stdint.h>

struct tmp100_dev {
	struct i2c_dev *i2c;
	uint8_t addr;
};

extern TEE_Result tmp100_read_temp(int *temp_mC);

#endif