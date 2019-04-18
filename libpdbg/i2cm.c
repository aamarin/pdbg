/* Copyright 2019 IBM Corp.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * 	http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <endian.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#include "operations.h"
#include "debug.h"
#include "target.h"
#include "libpdbg/bitutils.h"
#include "libpdbg/hwunit.h"

#include <errno.h>
#include <sys/param.h>
#include <dirent.h>

/*
 * I2C common registers
 * - use as is on CFAM
 * - use with I2C_PIB_OFFSET on PIB
 */
#define I2C_FIFO_REG 		0x0
#define I2C_CMD_REG 		0x1
#define I2C_MODE_REG 		0x2
#define I2C_WATERMARK_REG	0x3
#define I2C_INT_MASK_REG	0x4
#define I2C_INT_COND_REG	0x5
#define I2C_STATUS_REG		0x7
#define I2C_IMD_RESET_REG	0x7
#define I2C_IMD_RESET_ERR_REG	0x8
#define I2C_ESTAT_REG		0x8
#define I2C_RESIDUAL_REG	0x9
#define I2C_PORT_BUSY_REG	0xA

/* I2C PIB only */
#define I2C_PIB_OFFSET 		0x4
#define I2C_PIB_ENGINE_0 	0x0000
#define I2C_PIB_ENGINE_1 	0x1000
#define I2C_PIB_ENGINE_2 	0x2000
#define I2C_PIB_ENGINE_3 	0x3000
#define I2C_PIB_FIFO4_REG	0x12

/* I2C command register bits */
#define I2C_CMD_WITH_START		PPC_BIT32(0)
#define I2C_CMD_WITH_ADDR		PPC_BIT32(1)
#define I2C_CMD_READ_CONTINUE	PPC_BIT32(2)
#define I2C_CMD_WITH_STOP		PPC_BIT32(3)
#define I2C_CMD_INT_STEER		PPC_BITMASK32(6, 7)
#define I2C_CMD_DEV_ADDR		PPC_BITMASK32(8, 14)
#define I2C_CMD_READ_NOT_WRITE	PPC_BIT32(15)
#define I2C_CMD_LENGTH			PPC_BITMASK32(16, 31)

/* I2C mode register bits */
#define I2C_MODE_BIT_RATE_DIV	PPC_BITMASK32(0, 15)
#define I2C_MODE_PORT_NUM		PPC_BITMASK32(16, 21)
#define I2C_ENHANCED_MODE		PPC_BIT32(28)
#define I2C_MODE_PACING			PPC_BIT32(30)

/* watermark */
#define I2C_WATERMARK_HIGH		PPC_BITMASK32(16,19)
#define I2C_WATERMARK_LOW		PPC_BITMASK32(24,27)

/* I2C status register */
#define I2C_STAT_INV_CMD		PPC_BIT32(0)
#define I2C_STAT_PARITY			PPC_BIT32(1)
#define I2C_STAT_BE_OVERRUN		PPC_BIT32(2)
#define I2C_STAT_BE_ACCESS		PPC_BIT32(3)
#define I2C_STAT_LOST_ARB		PPC_BIT32(4)
#define I2C_STAT_NACK			PPC_BIT32(5)
#define I2C_STAT_DAT_REQ		PPC_BIT32(6)
#define I2C_STAT_CMD_COMP		PPC_BIT32(7)
#define I2C_STAT_STOP_ERR		PPC_BIT32(8)
#define I2C_STAT_MAX_PORT		PPC_BITMASK32(9, 15)
#define I2C_STAT_ANY_INT		PPC_BIT32(16)
#define I2C_STAT_WAIT_BUSY		PPC_BIT32(17)
#define I2C_STAT_ERR_IN			PPC_BIT32(18)
#define I2C_STAT_PORT_HIST_BUSY	PPC_BIT32(19)
#define I2C_STAT_SCL_IN			PPC_BIT32(20)
#define I2C_STAT_SDA_IN			PPC_BIT32(21)
#define I2C_STAT_PORT_BUSY		PPC_BIT32(22)
#define I2C_STAT_SELF_BUSY		PPC_BIT32(23)
#define I2C_STAT_FIFO_COUNT		PPC_BITMASK32(24, 31)

#define I2C_STAT_ERR		(I2C_STAT_INV_CMD | I2C_STAT_PARITY	\
							 | I2C_STAT_BE_OVERRUN | I2C_STAT_BE_ACCESS \
							 | I2C_STAT_LOST_ARB | I2C_STAT_NACK \
							 | I2C_STAT_STOP_ERR)

/* I2C extended status register */
#define I2C_ESTAT_FIFO_SIZE PPC_BITMASK32(0,7)
#define I2C_ESTAT_MSM_STATE PPC_BITMASK32(11,15)
#define I2C_ESTAT_HIGH_WATER 	PPC_BIT32(22)
#define I2C_ESTAT_LOW_WATER 	PPC_BIT32(23)

/* I2C interrupt mask register */
#define I2C_INT_INV_CMD		PPC_BIT32(16)
#define I2C_INT_PARITY		PPC_BIT32(17)
#define I2C_INT_BE_OVERRUN	PPC_BIT32(18)
#define I2C_INT_BE_ACCESS	PPC_BIT32(19)
#define I2C_INT_LOST_ARB	PPC_BIT32(20)
#define I2C_INT_NACK		PPC_BIT32(21)
#define I2C_INT_DAT_REQ		PPC_BIT32(22)
#define I2C_INT_CMD_COMP	PPC_BIT32(23)
#define I2C_INT_STOP_ERR	PPC_BIT32(24)
#define I2C_INT_BUSY		PPC_BIT32(25)
#define I2C_INT_IDLE		PPC_BIT32(26)

/* I2C residual  register */
#define I2C_RESID_FRONT		PPC_BITMASK32(0,15)
#define I2C_RESID_BACK		PPC_BITMASK32(16,31)

static int _i2cm_reg_write(struct i2cm *i2cm, uint32_t addr, uint32_t data)
{
	if (i2cm->host)
		/* pib addr space is 64 bits and i2cm only uses top 32 bits */
		CHECK_ERR(pib_write(&i2cm->target, addr + I2C_PIB_OFFSET, (uint64_t)data << 32));
	else
		CHECK_ERR(fsi_write(&i2cm->target, addr, data));
	return 0;
}

static int _i2cm_reg_read(struct i2cm *i2cm, uint32_t addr, uint32_t *data)
{
	uint64_t d = (uint64_t)*data;

	if (i2cm->host) {
		CHECK_ERR(pib_read(&i2cm->target, addr + I2C_PIB_OFFSET, &d));
		*data =  d >> 32;
	} else {
		CHECK_ERR(fsi_read(&i2cm->target, addr, data));
	}
	return 0;
}

static void debug_print_reg(struct i2cm *i2cm)
{
	uint32_t fsidata = 0;

	PR_INFO("\t --------\n");
	_i2cm_reg_read(i2cm,  I2C_STATUS_REG, &fsidata);
	PR_INFO("\t status reg \t has value 0x%x \n", fsidata);
	if (fsidata & I2C_STAT_INV_CMD)
		 PR_INFO("\t\tinvalid cmd\n");
	if (fsidata & I2C_STAT_PARITY)
		 PR_INFO("\t\tparity\n");
	if (fsidata & I2C_STAT_BE_OVERRUN)
		 PR_INFO("\t\tback endoverrun\n");
	if (fsidata & I2C_STAT_BE_ACCESS)
		 PR_INFO("\t\tback end access error\n");
	if (fsidata & I2C_STAT_LOST_ARB)
		 PR_INFO("\t\tarbitration lost\n");
	if (fsidata & I2C_STAT_NACK)
		 PR_INFO("\t\tnack\n");
	if (fsidata & I2C_STAT_DAT_REQ)
		 PR_INFO("\t\tdata request\n");
	if (fsidata & I2C_STAT_STOP_ERR)
		 PR_INFO("\t\tstop error\n");
	if (fsidata & I2C_STAT_PORT_BUSY)
		 PR_INFO("\t\ti2c busy\n");
	PR_INFO("\t\tfifo entry count: %li \n",fsidata&I2C_STAT_FIFO_COUNT);

	_i2cm_reg_read(i2cm,  I2C_ESTAT_REG, &fsidata);
	PR_INFO("\t extended status reg has value 0x%x \n", fsidata);
	if (fsidata & I2C_ESTAT_HIGH_WATER)
		PR_INFO("\t\thigh water mark reached\n");
	if (fsidata & I2C_ESTAT_LOW_WATER)
		PR_INFO("\t\tlow water mark reached\n");


	_i2cm_reg_read(i2cm,  I2C_WATERMARK_REG, &fsidata);
	PR_INFO("\t watermark reg  has value 0x%x \n", fsidata);
	PR_INFO("\t\twatermark high: %li \n",fsidata&I2C_WATERMARK_HIGH);
	PR_INFO("\t\twatermark low: %li \n",fsidata&I2C_WATERMARK_LOW);

	_i2cm_reg_read(i2cm,  I2C_RESIDUAL_REG, &fsidata);
	PR_INFO("\t residual reg  has value 0x%x \n", fsidata);
	PR_INFO("\t\tfrontend_len: %li \n",fsidata&I2C_RESID_FRONT);
	PR_INFO("\t\tbackend_len: %li \n",fsidata&I2C_RESID_BACK);

	_i2cm_reg_read(i2cm,  I2C_PORT_BUSY_REG, &fsidata);
	PR_INFO("\t port busy reg  has value 0x%x \n", fsidata);
	PR_INFO("\t -------\n");

}

static void i2c_mode_write(struct i2cm *i2cm, uint16_t port)
{
	uint32_t data = I2C_MODE_PACING;

	// hardcoding bit rate divisor because not too important
	data = SETFIELD(I2C_MODE_BIT_RATE_DIV, data, 28);
	data = SETFIELD(I2C_MODE_PORT_NUM, data, port);
	PR_INFO("setting mode to %x\n", data);
	_i2cm_reg_write(i2cm, I2C_MODE_REG, data);
}

static void i2c_watermark_write(struct i2cm *i2cm)
{
	uint32_t data = 0;

	data = SETFIELD(I2C_WATERMARK_HIGH, data, 4);
	data = SETFIELD(I2C_WATERMARK_LOW, data, 4);
	PR_INFO("setting watermark (0x%x) to: %x\n", I2C_WATERMARK_REG, data);
	_i2cm_reg_write(i2cm, I2C_WATERMARK_REG, data);
}

static int i2c_reset(struct i2cm *i2cm)
{
	uint32_t fsidata = 0;
	debug_print_reg(i2cm);
	PR_INFO("### RESETING i2cm \n");

	fsidata = 0xB;
	_i2cm_reg_write(i2cm, I2C_IMD_RESET_REG, fsidata);
	_i2cm_reg_write(i2cm, I2C_IMD_RESET_ERR_REG, fsidata);

	usleep(10000);
	debug_print_reg(i2cm);
	return 0;
}

/*
 *	If there are errors in the status register, redo the whole
 *	operation after resetting the i2cm.
*/
static int i2c_poll_status(struct i2cm *i2cm, uint32_t *data)
{
	int loop;

	for (loop = 0; loop < 10; loop++)
	{
		usleep(10000);
		_i2cm_reg_read(i2cm, I2C_STATUS_REG, data);

		if (((*data) & I2C_STAT_CMD_COMP))
			break;
	}
	if ((*data) & I2C_STAT_PORT_BUSY)
		PR_INFO("portbusy\n");
	if ((*data) & I2C_STAT_ERR) {
		PR_INFO("ERROR IN STATUS\n");
		debug_print_reg(i2cm);
		return 1;
	}
	return 0;
}

static int i2c_fifo_write(struct i2cm *i2cm, uint32_t *data, uint16_t size)
{
	int bytes_in_fifo = 1;
	int rc = 0, bytes_written = 0;
	uint32_t status;

	while (bytes_written < size) {

		if (bytes_written == size)
			return 0;

		/* Poll status register's FIFO_ENTRY_COUNT to check that FIFO isn't full */
		rc = i2c_poll_status(i2cm, &status);
		bytes_in_fifo = status & I2C_STAT_FIFO_COUNT;
		PR_INFO("%x bytes in fifo \n", bytes_in_fifo);

		if (rc)
			return 0;

		if (bytes_in_fifo == 8)
			continue;

		PR_INFO("\twriting: %x  to FIFO\n", data[bytes_written / 4]);
		if (i2cm->host)
			 rc = _i2cm_reg_write(i2cm, I2C_PIB_FIFO4_REG - I2C_PIB_OFFSET,
					 	data[bytes_written / 4]);
		else
			rc = _i2cm_reg_write(i2cm, I2C_FIFO_REG, data[bytes_written / 4]);
		if (rc)
			return bytes_written;
		bytes_written += 4;
	}
	return bytes_written;
}

static int i2c_fifo_read(struct i2cm *i2cm, uint32_t *data, uint16_t size)
{
	int bytes_to_read = 1;
	int rc = 0, bytes_read = 0;
	uint32_t tmp;
	uint32_t status;

	while (bytes_to_read) {	

		if (bytes_read > size)
			return 0;

		/* Poll status register's FIFO_ENTRY_COUNT to check that there is data to consume */
		rc = i2c_poll_status(i2cm, &status);
		bytes_to_read = status & I2C_STAT_FIFO_COUNT;
		PR_INFO("%x bytes in fifo \n", bytes_to_read);

		if (rc)
			return 0;

		if (!bytes_to_read)
			continue;

		if (i2cm->host)
			rc = _i2cm_reg_read(i2cm, I2C_PIB_FIFO4_REG -I2C_PIB_OFFSET, &tmp);
		else
			rc = _i2cm_reg_read(i2cm, I2C_FIFO_REG, &tmp);

		if (rc)
			return bytes_read;

		memcpy(data + (bytes_read / 4), &tmp, 4);
		PR_INFO(" %x \n", data[bytes_read / 4]);
		bytes_read += 4;
	}
	return bytes_read;
}

static int i2cm_ok_to_use(struct i2cm *i2cm, uint16_t port)
{
	uint32_t data;

	_i2cm_reg_read(i2cm, I2C_STATUS_REG, &data);

	if (!(data & I2C_STAT_CMD_COMP) || (data & I2C_STAT_ERR))
	{
		PR_INFO("Attempting to reset the i2cm %x \n", data);
		i2c_reset(i2cm);
		_i2cm_reg_read(i2cm, I2C_STATUS_REG, &data);
	}
	if (data & I2C_STAT_ERR) {
		PR_ERROR("I2C master error:  %x\n", data);
		return 0;
	}
	i2c_watermark_write(i2cm);
	i2c_mode_write(i2cm, port);
	return 1;
}

static int _i2c_put(struct i2cm *i2cm, uint16_t port, uint8_t addr,
			uint16_t size, uint8_t *d)
{
	uint32_t fsidata;
	uint32_t data = 0;
	int rc = 0;

	if(!i2cm_ok_to_use(i2cm, port)) {
		rc = 1;
		return rc;
	}
	//TODO: if size > 64kB then use I2C_CMD_READ_CONTINUE and do multiple commands
	if (size > 64*1024 -1) {
		PR_ERROR("Can only support up to 64K bytes\n");
		return -1;
	}

	/* Set slave device */
	fsidata = I2C_CMD_WITH_START | I2C_CMD_WITH_ADDR;
	fsidata = SETFIELD(I2C_CMD_DEV_ADDR, fsidata, addr);
	fsidata = SETFIELD(I2C_CMD_LENGTH, fsidata, size);
	_i2cm_reg_write(i2cm, I2C_CMD_REG, fsidata);

	rc = i2c_poll_status(i2cm, &data);
	if (rc) {
		PR_ERROR("FAILED to set i2c device\n");
		return rc;
	}

	/* Write data into the FIFO of the slave device */
	i2c_fifo_write(i2cm, (uint32_t *)d, size);

	rc = i2c_poll_status(i2cm, &data);
	if (rc) {
		PR_ERROR("FAILED to write all data\n");
		return rc;
	}
	return rc;
}

static int _i2c_get(struct i2cm *i2cm, uint16_t port, uint8_t addr,
			uint16_t size, uint8_t *d)
{
	uint32_t fsidata;
	uint32_t data = 0;
	int rc = 0;
	int bytes_read;

	if(!i2cm_ok_to_use(i2cm, port)) {
		rc = 1;
		return rc;
	}

	//TODO: if size > 64kB then use I2C_CMD_READ_CONTINUE and do multiple commands
	if (size > 64*1024 -1) {
		PR_ERROR("Can only support up to 64K bytes\n");
		return -1;
	}

	/* Tell i2c master to read from slave device's fifo */
	fsidata = I2C_CMD_WITH_START | I2C_CMD_WITH_STOP | I2C_CMD_WITH_ADDR | I2C_CMD_READ_NOT_WRITE;
	fsidata = SETFIELD(I2C_CMD_DEV_ADDR, fsidata, addr);
	fsidata = SETFIELD(I2C_CMD_LENGTH, fsidata, size);
	_i2cm_reg_write(i2cm, I2C_CMD_REG, fsidata);

	bytes_read = i2c_fifo_read(i2cm, (uint32_t*)d, size);

	rc = i2c_poll_status(i2cm, &data);
	if (rc) {
		PR_ERROR("Error occured while reading FIFO\n");
		return rc;
	}

	if (bytes_read < size) {
		PR_ERROR("Read %i bytes, expected to read %i bytes\n", bytes_read, size);
		debug_print_reg(i2cm);
		return -1;
	}

	if (data & I2C_STAT_CMD_COMP)
		rc = 0;
	else
		rc = -1;
	return rc;
}

static int i2c_get(struct i2cbus *i2cbus, uint8_t addr, uint16_t size, uint8_t *d)
{
	struct i2cm *i2cm = target_to_i2cm(i2cbus->target.parent);
	return _i2c_get(i2cm, i2cbus->port, addr, size, d);
}

static int i2c_put(struct i2cbus *i2cbus, uint8_t addr, uint16_t size, uint8_t *d)
{
	struct i2cm *i2cm = target_to_i2cm(i2cbus->target.parent);
	return _i2c_put(i2cm, i2cbus->port, addr, size, d);
}

static int i2cm_target_probe(struct pdbg_target *target)
{
	struct i2cbus *i2cbus = target_to_i2cbus(target);
	i2cbus->port = target->index;

	return 0;
}

static struct i2cbus i2c_bus_cfam = {
	.target = {
		.name =	"CFAM I2C bus",
		.compatible = "ibm,power9-i2c-port",
		.class = "i2c_bus",
		.probe = i2cm_target_probe,
	},
	.read = i2c_get,
	.write = i2c_put,
};
DECLARE_HW_UNIT(i2c_bus_cfam);

static struct i2cm i2cm_cfam = {
	.target = {
		.name =	"CFAM I2C Master",
		.compatible = "ibm,fsi-i2c-master",
		.class = "i2cm",
	},
	.host = false
};
DECLARE_HW_UNIT(i2cm_cfam);


/////////////////////////////////////////////////////////////////////////////
#define OCC_BASE  	0x00000000006C08A
#define OCC_CLEAR 	0x00000000006C08B
#define OCC_SET 	0x00000000006C08C


#define OCC_LOCKED_ENGINE_1 PPC_BIT(17)
#define OCC_LOCKED_ENGINE_2 PPC_BIT(19)
#define OCC_LOCKED_ENGINE_3 PPC_BIT(21)
#define I2CM_DT_TO_ID(x) ((x>>12) & 0xf)
static int i2cm_locked_by_occ(int id, uint64_t occ_base)
{
	uint64_t mask;
	switch (id)
	{
	case 1:
		mask = OCC_LOCKED_ENGINE_1;
		break;
	case 2:
		mask = OCC_LOCKED_ENGINE_2;
		break;
	case 3:
		mask = OCC_LOCKED_ENGINE_3;
		break;
	default:
		mask = 0;
		break;
	}
	return !!(mask & occ_base);
}

static int pib_i2c_get(struct i2cbus *i2cbus, uint8_t addr, uint16_t size, uint8_t *d)
{
	uint64_t data = 0;
	uint64_t bit;
	struct pdbg_target *p;
	struct i2cm *i2cm = target_to_i2cm(i2cbus->target.parent);

	pdbg_for_each_class_target("pib", p) {
		if (pdbg_target_probe(p) == PDBG_TARGET_ENABLED)
			break;
	}

	if (!p) {
		fprintf(stderr, "No PIB found\n");
		return 0;
	}
	bit = PPC_BIT(16 + (i2cbus->id - 1) * 2);
	pib_read(p, OCC_BASE, &data);

	if( !i2cm_locked_by_occ(i2cbus->id, data)) {
		/* lock i2cm */
		pib_write(p, OCC_SET, bit);
		pib_read(p, OCC_BASE, &data);

		_i2c_get(i2cm, i2cbus->port, addr, size, d);

		/* unlock i2cm */
		pib_read(p, OCC_BASE, &data);
		pib_read(p, OCC_CLEAR, &data);
		pib_write(p, OCC_CLEAR, bit);
		pib_read(p, OCC_BASE, &data);
	} else {
		PR_INFO("I2C: de%d: occflags = 0x%16" PRIx64 "(locks = %x:%x:%x)\n",
			i2cbus->id, (u64) data, (u16) GETFIELD(PPC_BITMASK(16, 17), data),
			(u16) GETFIELD(PPC_BITMASK(18, 19), data),
			(u16) GETFIELD(PPC_BITMASK(20, 21), data));
		PR_INFO("I2C master %x is locked by OCC :( \n", i2cbus->id);
	}

	return 0;
}

static int pib_i2c_put(struct i2cbus *i2cbus, uint8_t addr, uint16_t size, uint8_t *d)
{
	uint64_t data = 0;
	uint64_t bit;
	struct i2cm *i2cm = target_to_i2cm(i2cbus->target.parent);
	struct pdbg_target *p;

	pdbg_for_each_class_target("pib", p) {
		if (pdbg_target_probe(p) == PDBG_TARGET_ENABLED)
			break;
	}

	if (!p) {
		fprintf(stderr, "No PIB found\n");
		return 0;
	}
	bit = PPC_BIT(16 + (i2cbus->id - 1) * 2);
	pib_read(p, OCC_BASE, &data);

	if( !i2cm_locked_by_occ(i2cbus->id, data)) {
		/* lock i2cm */
		pib_write(p, OCC_SET, bit);
		pib_read(p, OCC_BASE, &data);

		_i2c_put(i2cm, i2cbus->port, addr, size, d);

		/* unlock i2cm */
		pib_read(p, OCC_BASE, &data);
		pib_read(p, OCC_CLEAR, &data);
		pib_write(p, OCC_CLEAR, bit);
		pib_read(p, OCC_BASE, &data);
	} else {
		PR_INFO("I2C: de%d: occflags = 0x%16" PRIx64 "(locks = %x:%x:%x)\n",
			i2cbus->id, (u64) data, (u16) GETFIELD(PPC_BITMASK(16, 17), data),
			(u16) GETFIELD(PPC_BITMASK(18, 19), data),
			(u16) GETFIELD(PPC_BITMASK(20, 21), data));
		PR_INFO("I2C master %x is locked by OCC :( \n", i2cbus->id);
	}
	return 0;
}

int host_i2cm_target_probe(struct pdbg_target *target)
{
	int dt_id = pdbg_target_address(target, NULL);
	int id = I2CM_DT_TO_ID(dt_id);
	struct i2cbus *i2cbus = target_to_i2cbus(target);

	i2cbus->id = id;
	i2cbus->port = target->index;

	return 0;
}

static struct i2cbus i2c_bus_pib = {
	.target = {
		.name =	"PIB I2C Bus",
		.compatible = "ibm,power9-i2c-port",
		.class = "i2c_bus",
		.probe = i2cm_target_probe,
	},
	.read = pib_i2c_get,
	.write = pib_i2c_put,
};
DECLARE_HW_UNIT(i2c_bus_pib);

static struct i2cm i2c_pib = {
	.target = {
		.name =	"PIB I2C Master",
		.compatible = "ibm,power9-i2cm",
		.class = "i2cm",
	},
	.host = true,
};
DECLARE_HW_UNIT(i2c_pib);
/////////////////////////////////////////////////////////////////////////////

#ifdef ENABLE_I2CLIB
#include <i2c/smbus.h>

static int kernel_i2c_get(struct i2cbus *i2cbus, uint8_t addr, uint16_t size, uint8_t *data)
{
	int res = 0;
	//fix this is smbus not i2cget

	if (ioctl(i2cbus->i2c_fd, I2C_SLAVE, addr) < 0)
		return -1;

	res = i2c_smbus_read_byte_data(i2cbus->i2c_fd, 0);
	PR_DEBUG("read %x from device %x\n", res, addr);
	if (res >= 0) {
		*data = (uint64_t)res;
		return 0;
	}
	return -1;
}

static int i2cbus_probe(struct pdbg_target *target)
{
    int i2c_fd = 0;
	int len;
	char i2c_path[NAME_MAX];
	struct i2cbus *i2cbus;

	len = snprintf(i2c_path, NAME_MAX, "/dev/i2c-%i", target->index);
	if (len >= NAME_MAX)
		return -1;
	i2c_fd = open(i2c_path, O_RDWR);
	if (!i2c_fd)
		return -1;

	i2cbus = target_to_i2cbus(target);
	i2cbus->i2c_fd = i2c_fd;
	return 0;
}

static struct i2cbus i2c_bus = {
	.target = {
		.name =	"I2C Bus",
		.compatible = "ibm,power9-i2c-port",
		.class = "i2c_bus",
		.probe = i2cbus_probe,
	},
	.read = kernel_i2c_get,
};
DECLARE_HW_UNIT(i2c_bus);
#endif
