// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (C) 2025 Texas Instruments Incorporated - https://www.ti.com/
 */

#include <drivers/pinctrl.h>
#include <io.h>
#include <kernel/dt.h>
#include <libfdt.h>
#include <mm/core_memprot.h>
#include <stdint.h>
#include <stdlib.h>
#include <trace.h>

struct pinctrl_pin {
	uint32_t offset;
	uint32_t value;
};

struct pinctrl_conf {
	struct pinconf pinconf;
	struct pinctrl_pin *pins;
	unsigned int npins;
	struct pinctrl_priv *priv;
};

struct pinctrl_priv {
	vaddr_t base;
	uint32_t reg_width;
	uint32_t mask;
};

static TEE_Result pinctrl_conf_apply(struct pinconf *conf)
{
	struct pinctrl_conf *c =
		container_of(conf, struct pinctrl_conf, pinconf);
	struct pinctrl_priv *priv = c->priv;

	for (unsigned int i = 0; i < c->npins; i++) {
		vaddr_t reg = priv->base + c->pins[i].offset;
		uint32_t new_val = c->pins[i].value;
		uint32_t mask = priv->mask;
		uint32_t old_val, val;

		switch (priv->reg_width) {
		case 8:
			old_val = io_read8(reg);
			val = (old_val & ~mask) | (new_val & mask);
			io_write8(reg, val);
			break;
		case 16:
			old_val = io_read16(reg);
			val = (old_val & ~mask) | (new_val & mask);
			io_write16(reg, val);
			break;
		default:
			old_val = io_read32(reg);
			val = (old_val & ~mask) | (new_val & mask);
			io_write32(reg, val);
			break;
		}
	}

	return TEE_SUCCESS;
}

static void pinctrl_conf_free(struct pinconf *conf)
{
	struct pinctrl_conf *c =
		container_of(conf, struct pinctrl_conf, pinconf);
	free(c->pins);
	free(c);
}

static const struct pinctrl_ops pinctrl_ops = {
	.conf_apply = pinctrl_conf_apply,
	.conf_free = pinctrl_conf_free,
};

static TEE_Result pinctrl_dt_get(struct dt_pargs *pargs, void *data,
				 struct pinconf **out_pinconf)
{
	const void *fdt = pargs->fdt;
	int node = pargs->phandle_node;
	struct pinctrl_conf *conf = NULL;
	const fdt32_t *prop = NULL;
	int len = 0;
	struct pinctrl_priv *priv = data;

	prop = fdt_getprop(fdt, node, "pinctrl-single,pins", &len);
	if (!prop || len < 8 || (len % 8))
		return TEE_ERROR_BAD_PARAMETERS;

	conf = calloc(1, sizeof(*conf));
	if (!conf)
		return TEE_ERROR_OUT_OF_MEMORY;

	conf->npins = len / 8;
	conf->pins = calloc(conf->npins, sizeof(*conf->pins));
	if (!conf->pins) {
		free(conf);
		return TEE_ERROR_OUT_OF_MEMORY;
	}
	conf->priv = priv;

	for (unsigned int i = 0; i < conf->npins; i++) {
		conf->pins[i].offset = fdt32_to_cpu(prop[i * 2]);
		conf->pins[i].value = fdt32_to_cpu(prop[i * 2 + 1]);
	}

	conf->pinconf.ops = &pinctrl_ops;
	conf->pinconf.priv = NULL;

	*out_pinconf = &conf->pinconf;

	return TEE_SUCCESS;
}

static TEE_Result pinctrl_probe(const void *fdt, int node,
				const void *compat_data __unused)
{
	size_t size = 0;
	vaddr_t base = 0;
	struct pinctrl_priv *priv = NULL;
	const fdt32_t *prop = NULL;
	int len = 0;

	priv = calloc(1, sizeof(*priv));
	if (!priv)
		return TEE_ERROR_OUT_OF_MEMORY;

	if (dt_map_dev(fdt, node, &base, &size, DT_MAP_AUTO) < 0) {
		EMSG("Failed to map pinctrl base address");
		free(priv);
		return TEE_ERROR_GENERIC;
	}
	priv->base = base;

	prop = fdt_getprop(fdt, node, "pinctrl-single,register-width", &len);
	if (!prop || len < 4) {
		free(priv);
		return TEE_ERROR_BAD_PARAMETERS;
	}
	priv->reg_width = fdt32_to_cpu(*prop);

	prop = fdt_getprop(fdt, node, "pinctrl-single,function-mask", &len);
	if (prop && len >= 4)
		priv->mask = fdt32_to_cpu(*prop);
	else
		priv->mask = 0xFFFFFFFF;

	return pinctrl_register_provider(fdt, node, pinctrl_dt_get, priv);
}

static const struct dt_device_match pinctrl_match_table[] = {
	{ .compatible = "pinctrl-single" },
	{}
};

DEFINE_DT_DRIVER(pinctrl_dt_driver) = {
	.name = "pinctrl_single",
	.type = DT_DRIVER_PINCTRL,
	.match_table = pinctrl_match_table,
	.probe = pinctrl_probe,
};
