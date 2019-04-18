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

#include <errno.h>
#include <sys/param.h>
#include <dirent.h>

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
