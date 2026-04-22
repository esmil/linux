// SPDX-License-Identifier: GPL-2.0
/*
 * Chromium OS cros_ec driver - eSPI interface
 *
 * Copyright (c) 2012 The Chromium OS Authors.
 * Copyright (c) 2024 SpacemiT, Inc.
 *
 * This driver provides eSPI shared memory interface for ChromeOS EC communication.
 * It converts LPC I/O port accesses to eSPI shared memory accesses while maintaining
 * compatibility with the existing ChromeOS EC protocol stack.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/reboot.h>
#include <linux/spacemit-k3-espi.h>
#include <linux/suspend.h>

#include "cros_ec.h"

#define DRV_NAME "cros_ec_espi"

/* eSPI shared memory access timeout */
#define ESPI_CMD_TIMEOUT_MS 5000
/*
 * After an EC reboot the transport may recover well before the EC firmware
 * finishes repopulating the shared memory window. Allow a longer grace period
 * here so the first post-reboot command does not immediately fall into another
 * full timeout cycle.
 */
#define ESPI_READY_TIMEOUT_MS 20000
#define ESPI_READY_POLL_INTERVAL_US 20000
#define ESPI_RECOVERY_RETRY_INTERVAL_MS 5000
#define ESPI_INVALID_STATUS_VALUE 0xff
#define ESPI_INVALID_STATUS_MAX_POLLS 5

/*
 * eSPI Shared Memory Address Mapping for EC Communication
 *
 * This driver converts LPC I/O port accesses to eSPI shared memory accesses.
 * The original LPC addresses are mapped to offsets within the eSPI shared memory region.
 *
 * Address Mapping Strategy:
 * - LPC addresses are mapped to shared memory offsets
 * - The mapping preserves the relative layout and functionality of the original LPC interface
 * - All accesses go through espi_shared_mem_base + offset
 *
 * Memory Layout in eSPI Shared Memory:
 * 0x000: Host command register (EC_LPC_ADDR_HOST_CMD -> 0x000)
 * 0x001: Host data register (EC_LPC_ADDR_HOST_DATA -> 0x001)
 * 0x010-0x017: Host arguments (8 bytes)
 * 0x018-0x0FF: Host parameters (up to 232 bytes)
 * 0x100-0x1FF: Packet data area (256 bytes for protocol v3)
 * 0x200-0x2FF: Memory map region (256 bytes)
 */

/* eSPI shared memory offsets */
#define ESPI_HOST_CMD_OFFSET 0x000 /* EC_LPC_ADDR_HOST_CMD -> 0x000 */
#define ESPI_HOST_DATA_OFFSET 0x001 /* EC_LPC_ADDR_HOST_DATA -> 0x001 */
#define ESPI_HOST_ARGS_OFFSET 0x010 /* EC_LPC_ADDR_HOST_ARGS -> 0x010 */
#define ESPI_HOST_PARAM_OFFSET 0x018 /* EC_LPC_ADDR_HOST_PARAM -> 0x018 */
#define ESPI_HOST_PACKET_OFFSET 0x100 /* EC_LPC_ADDR_HOST_PACKET -> 0x100 */
#define ESPI_MEMMAP_OFFSET 0x200 /* EC_LPC_ADDR_MEMMAP -> 0x200 */

/**
 * struct cros_ec_espi - eSPI device-specific data
 * @espi_shared_mem_base: Base address of eSPI shared memory region
 * @mmio_memory_base: The first I/O port addressing EC mapped memory
 * @io_mutex: Mutex to protect eSPI shared memory access
 */
struct cros_ec_espi {
	void __iomem *espi_shared_mem_base;
	u16 mmio_memory_base;
	struct mutex io_mutex;
};

/**
 * struct espi_driver_ops - eSPI driver operations
 * @read: Copy length bytes from EC address offset into buffer dest.
 *        Returns a negative error code on error, or the 8-bit checksum
 *        of all bytes read.
 * @write: Copy length bytes from buffer msg into EC address offset.
 *         Returns a negative error code on error, or the 8-bit checksum
 *         of all bytes written.
 */
struct espi_driver_ops {
	int (*read)(unsigned int offset, unsigned int length, u8 *dest);
	int (*write)(unsigned int offset, unsigned int length, const u8 *msg);
};

static struct espi_driver_ops cros_ec_espi_ops = {};

/* Convert LPC address to eSPI shared memory offset */
static inline u32 lpc_to_espi_offset(u32 lpc_addr)
{
	switch (lpc_addr & 0xFF00) {
	case 0x200: /* HOST_CMD/DATA region */
		if (lpc_addr == EC_LPC_ADDR_HOST_CMD)
			return ESPI_HOST_CMD_OFFSET;
		else if (lpc_addr == EC_LPC_ADDR_HOST_DATA)
			return ESPI_HOST_DATA_OFFSET;
		break;
	case 0x800: /* HOST_ARGS/PARAM/PACKET region */
		if (lpc_addr >= EC_LPC_ADDR_HOST_PACKET)
			return ESPI_HOST_PACKET_OFFSET +
			       (lpc_addr - EC_LPC_ADDR_HOST_PACKET);
		else if (lpc_addr >= EC_LPC_ADDR_HOST_ARGS &&
			 lpc_addr < EC_LPC_ADDR_HOST_PARAM)
			return ESPI_HOST_ARGS_OFFSET +
			       (lpc_addr - EC_LPC_ADDR_HOST_ARGS);
		else if (lpc_addr >= EC_LPC_ADDR_HOST_PARAM)
			return ESPI_HOST_PARAM_OFFSET +
			       (lpc_addr - EC_LPC_ADDR_HOST_PARAM);
		break;
	case 0x900: /* MEMMAP region */
		return ESPI_MEMMAP_OFFSET + (lpc_addr - EC_LPC_ADDR_MEMMAP);
	}

	/* Default mapping for unknown addresses */
	pr_warn_once("Unknown LPC address 0x%x, using direct offset\n",
		     lpc_addr);
	return lpc_addr & 0xFF;
}

/* Global pointer to current eSPI device - protected by mutex */
static struct cros_ec_espi *g_ec_espi = NULL;
static DEFINE_MUTEX(g_espi_mutex);

/**
 * cros_ec_espi_lock() - Acquire mutex for eSPI shared memory access
 *
 * @return: Negative error code, or zero for success
 */
static int cros_ec_espi_lock(void)
{
	struct cros_ec_espi *ec_espi;

	mutex_lock(&g_espi_mutex);
	ec_espi = g_ec_espi;
	if (!ec_espi) {
		mutex_unlock(&g_espi_mutex);
		return -ENODEV;
	}

	mutex_lock(&ec_espi->io_mutex);
	mutex_unlock(&g_espi_mutex);
	return 0;
}

/**
 * cros_ec_espi_unlock() - Release mutex for eSPI shared memory access
 */
static void cros_ec_espi_unlock(void)
{
	struct cros_ec_espi *ec_espi;

	mutex_lock(&g_espi_mutex);
	ec_espi = g_ec_espi;
	mutex_unlock(&g_espi_mutex);

	if (ec_espi)
		mutex_unlock(&ec_espi->io_mutex);
}

/*
 * A generic instance of the read function of struct espi_driver_ops, used for
 * the eSPI EC.
 */
static int cros_ec_espi_read_bytes(unsigned int offset, unsigned int length,
				   u8 *dest)
{
	struct cros_ec_espi *ec_espi;
	void __iomem *addr;
	u32 espi_offset;
	u8 sum = 0;
	int i, ret;

	ret = cros_ec_espi_lock();
	if (ret)
		return ret;

	ec_espi = g_ec_espi;
	if (!ec_espi || !ec_espi->espi_shared_mem_base) {
		cros_ec_espi_unlock();
		return -ENODEV;
	}

	for (i = 0; i < length; ++i) {
		espi_offset = lpc_to_espi_offset(offset + i);
		addr = ec_espi->espi_shared_mem_base + espi_offset;

		/* eSPI shared memory: 1-byte access */
		dest[i] = readb(addr);
		sum += dest[i];
	}

	cros_ec_espi_unlock();

	/* Return checksum of all bytes read */
	return sum;
}

/*
 * A generic instance of the write function of struct espi_driver_ops, used for
 * the eSPI EC.
 */
static int cros_ec_espi_write_bytes(unsigned int offset, unsigned int length,
				    const u8 *msg)
{
	struct cros_ec_espi *ec_espi;
	void __iomem *addr;
	u32 espi_offset;
	u8 sum = 0;
	int i, ret;

	ret = cros_ec_espi_lock();
	if (ret)
		return ret;

	ec_espi = g_ec_espi;
	if (!ec_espi || !ec_espi->espi_shared_mem_base) {
		cros_ec_espi_unlock();
		return -ENODEV;
	}

	for (i = 0; i < length; ++i) {
		espi_offset = lpc_to_espi_offset(offset + i);
		addr = ec_espi->espi_shared_mem_base + espi_offset;

		/* eSPI shared memory: 1-byte access */
		writeb(msg[i], addr);
		sum += msg[i];
	}

	cros_ec_espi_unlock();

	/* Return checksum of all bytes written */
	return sum;
}

static int ec_response_timed_out(void)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(ESPI_CMD_TIMEOUT_MS);
	unsigned int invalid_status_polls = 0;
	u8 data;
	int ret;

	usleep_range(200, 300);
	do {
		ret = cros_ec_espi_ops.read(EC_LPC_ADDR_HOST_CMD, 1, &data);
		if (ret < 0)
			return ret;

		if (!(data & EC_LPC_STATUS_BUSY_MASK))
			return 0;

		/*
		 * While the EC is rebooting the shared window reads back as 0xff.
		 * Escalate to controller recovery quickly instead of burning the
		 * whole command timeout on an absent endpoint.
		 */
		if (data == ESPI_INVALID_STATUS_VALUE) {
			if (++invalid_status_polls >= ESPI_INVALID_STATUS_MAX_POLLS)
				return 1;
		} else {
			invalid_status_polls = 0;
		}

		usleep_range(100, 200);
	} while (time_before(jiffies, timeout));

	return 1;
}

static bool cros_ec_espi_valid_id(const u8 *buf)
{
	return buf[0] == 'E' && buf[1] == 'C';
}

static int cros_ec_espi_read_id(u8 *buf)
{
	return cros_ec_espi_ops.read(EC_LPC_ADDR_MEMMAP + EC_MEMMAP_ID, 2, buf);
}

static int cros_ec_espi_recover_bus(struct device *dev)
{
	int ret;

	ret = cros_ec_espi_lock();
	if (ret)
		return ret;

	ret = spacemit_k3_espi_recover(dev->parent);
	cros_ec_espi_unlock();

	return ret;
}

static int cros_ec_espi_wait_ready(struct device *dev, const char *reason)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(ESPI_READY_TIMEOUT_MS);
	unsigned long next_recover =
		jiffies + msecs_to_jiffies(ESPI_RECOVERY_RETRY_INTERVAL_MS);
	u8 buf[2] = {};
	int ret = 0;

	do {
		ret = cros_ec_espi_read_id(buf);
		if (ret >= 0 && cros_ec_espi_valid_id(buf))
			return 0;

		/*
		 * The first recover may happen while the EC is still updating and
		 * leave the controller in a master-only state. Retry link recovery
		 * periodically while waiting so we can renegotiate as soon as the
		 * EC starts responding again.
		 */
		if (time_after_eq(jiffies, next_recover)) {
			ret = cros_ec_espi_recover_bus(dev);
			if (ret)
				dev_dbg(dev, "background eSPI recovery failed: %d\n", ret);
			next_recover = jiffies +
				msecs_to_jiffies(ESPI_RECOVERY_RETRY_INTERVAL_MS);
			continue;
		}

		usleep_range(ESPI_READY_POLL_INTERVAL_US,
			     ESPI_READY_POLL_INTERVAL_US + 10000);
	} while (time_before(jiffies, timeout));

	if (ret < 0)
		dev_warn(dev, "EC not ready after %s: %d\n", reason, ret);
	else
		dev_warn(dev, "EC not ready after %s (id: 0x%02x 0x%02x)\n",
			 reason, buf[0], buf[1]);

	return ret < 0 ? ret : -ETIMEDOUT;
}

static int cros_ec_espi_recover_controller(struct device *dev, const char *reason)
{
	int ret;

	if (!dev->parent)
		return -ENODEV;

	dev_warn(dev, "attempting eSPI controller recovery after %s\n", reason);
	ret = cros_ec_espi_recover_bus(dev);
	if (ret)
		dev_err(dev, "eSPI controller recovery failed: %d\n", ret);

	if (ret)
		return ret;

	return cros_ec_espi_wait_ready(dev, reason);
}

static int cros_ec_pkt_xfer_espi_once(struct cros_ec_device *ec,
				      struct cros_ec_command *msg)
{
	struct ec_host_response response;
	u8 sum;
	int ret = 0;
	u8 *dout;

	/*
	 * Give EC some time between commands to avoid overwhelming it.
	 * This prevents timing issues when multiple commands are sent rapidly.
	 */
	usleep_range(500, 1000);

	ret = cros_ec_prepare_tx(ec, msg);
	if (ret < 0) {
		dev_err(ec->dev, "cros_ec_prepare_tx failed: %d\n", ret);
		goto done;
	}

	/* Write buffer */
	ret = cros_ec_espi_ops.write(EC_LPC_ADDR_HOST_PACKET, ret, ec->dout);
	if (ret < 0) {
		dev_err(ec->dev, "Write to HOST_PACKET failed: %d\n", ret);
		goto done;
	}

	/* Here we go */
	sum = EC_COMMAND_PROTOCOL_3;
	ret = cros_ec_espi_ops.write(EC_LPC_ADDR_HOST_CMD, 1, &sum);
	if (ret < 0) {
		dev_err(ec->dev, "Write to HOST_CMD failed: %d\n", ret);
		goto done;
	}

	ret = ec_response_timed_out();
	if (ret < 0) {
		dev_err(ec->dev, "ec_response_timed_out failed: %d\n", ret);
		goto done;
	}
	if (ret) {
		dev_warn(ec->dev, "EC response timed out\n");
		ret = -ETIMEDOUT;
		goto done;
	}

	/* Check result */
	ret = cros_ec_espi_ops.read(EC_LPC_ADDR_HOST_DATA, 1, &sum);
	if (ret < 0) {
		dev_err(ec->dev, "Read from HOST_DATA failed: %d\n", ret);
		goto done;
	}

	msg->result = sum;
	ret = cros_ec_check_result(ec, msg);
	if (ret) {
		dev_err(ec->dev, "cros_ec_check_result failed: %d\n", ret);
		goto done;
	}

	/* Read back response */
	dout = (u8 *)&response;
	ret = cros_ec_espi_ops.read(EC_LPC_ADDR_HOST_PACKET, sizeof(response),
				    dout);
	if (ret < 0) {
		dev_err(ec->dev, "Read response header failed: %d\n", ret);
		goto done;
	}
	sum = ret;

	msg->result = response.result;

	if (response.data_len > msg->insize) {
		dev_err(ec->dev, "packet too long (%d bytes, expected %d)",
			response.data_len, msg->insize);
		ret = -EMSGSIZE;
		goto done;
	}

	/* Read response and process checksum */
	ret = cros_ec_espi_ops.read(EC_LPC_ADDR_HOST_PACKET + sizeof(response),
				    response.data_len, msg->data);
	if (ret < 0) {
		dev_err(ec->dev, "Read response data failed: %d\n", ret);
		goto done;
	}
	sum += ret;

	if (sum) {
		dev_err(ec->dev, "bad packet checksum %02x\n",
			response.checksum);
		ret = -EBADMSG;
		goto done;
	}

	/* Return actual amount of data received */
	ret = response.data_len;
done:
	return ret;
}

static int cros_ec_espi_xfer_retry(struct cros_ec_device *ec,
				   struct cros_ec_command *msg,
				   int (*xfer_once)(struct cros_ec_device *ec,
						    struct cros_ec_command *msg),
				   const char *name)
{
	int ret;

	ret = xfer_once(ec, msg);
	if (ret != -ETIMEDOUT)
		return ret;

	if (cros_ec_espi_recover_controller(ec->dev, name))
		return ret;

	dev_dbg(ec->dev, "eSPI controller recovered, retrying %s\n", name);
	return xfer_once(ec, msg);
}

static int cros_ec_pkt_xfer_espi(struct cros_ec_device *ec,
				 struct cros_ec_command *msg)
{
	return cros_ec_espi_xfer_retry(ec, msg, cros_ec_pkt_xfer_espi_once,
				       "packet transfer timeout");
}

static int cros_ec_cmd_xfer_espi_once(struct cros_ec_device *ec,
				      struct cros_ec_command *msg)
{
	struct ec_lpc_host_args args;
	u8 sum;
	int ret = 0;

	if (msg->outsize > EC_PROTO2_MAX_PARAM_SIZE ||
	    msg->insize > EC_PROTO2_MAX_PARAM_SIZE) {
		dev_err(ec->dev, "invalid buffer sizes (out %d, in %d)\n",
			msg->outsize, msg->insize);
		return -EINVAL;
	}

	/*
	 * Give EC some time between commands to avoid overwhelming it.
	 * This prevents timing issues when multiple commands are sent rapidly.
	 */
	usleep_range(500, 1000);

	/* Now actually send the command to the EC and get the result */
	args.flags = EC_HOST_ARGS_FLAG_FROM_HOST;
	args.command_version = msg->version;
	args.data_size = msg->outsize;

	/* Initialize checksum */
	sum = msg->command + args.flags + args.command_version + args.data_size;

	/* Copy data and update checksum */
	ret = cros_ec_espi_ops.write(EC_LPC_ADDR_HOST_PARAM, msg->outsize,
				     msg->data);
	if (ret < 0) {
		dev_err(ec->dev, "Write to HOST_PARAM failed: %d\n", ret);
		goto done;
	}
	sum += ret;

	/* Finalize checksum and write args */
	args.checksum = sum;
	ret = cros_ec_espi_ops.write(EC_LPC_ADDR_HOST_ARGS, sizeof(args),
				     (u8 *)&args);
	if (ret < 0) {
		dev_err(ec->dev, "Write to HOST_ARGS failed: %d\n", ret);
		goto done;
	}

	/* Here we go */
	sum = msg->command;
	ret = cros_ec_espi_ops.write(EC_LPC_ADDR_HOST_CMD, 1, &sum);
	if (ret < 0) {
		dev_err(ec->dev, "Write to HOST_CMD failed: %d\n", ret);
		goto done;
	}

	ret = ec_response_timed_out();
	if (ret < 0) {
		dev_err(ec->dev, "ec_response_timed_out failed: %d\n", ret);
		goto done;
	}
	if (ret) {
		dev_warn(ec->dev, "EC response timed out\n");
		ret = -ETIMEDOUT;
		goto done;
	}

	/* Check result */
	ret = cros_ec_espi_ops.read(EC_LPC_ADDR_HOST_DATA, 1, &sum);
	if (ret < 0) {
		dev_err(ec->dev, "Read from HOST_DATA failed: %d\n", ret);
		goto done;
	}

	msg->result = sum;
	ret = cros_ec_check_result(ec, msg);
	if (ret) {
		dev_err(ec->dev, "cros_ec_check_result failed: %d\n", ret);
		goto done;
	}

	/* Read back args */
	ret = cros_ec_espi_ops.read(EC_LPC_ADDR_HOST_ARGS, sizeof(args),
				    (u8 *)&args);
	if (ret < 0) {
		dev_err(ec->dev, "Read HOST_ARGS failed: %d\n", ret);
		goto done;
	}

	if (args.data_size > msg->insize) {
		dev_err(ec->dev, "packet too long (%d bytes, expected %d)",
			args.data_size, msg->insize);
		ret = -ENOSPC;
		goto done;
	}

	/* Start calculating response checksum */
	sum = msg->command + args.flags + args.command_version + args.data_size;

	/* Read response and update checksum */
	ret = cros_ec_espi_ops.read(EC_LPC_ADDR_HOST_PARAM, args.data_size,
				    msg->data);
	if (ret < 0) {
		dev_err(ec->dev, "Read response data failed: %d\n", ret);
		goto done;
	}
	sum += ret;

	/* Verify checksum */
	if (args.checksum != sum) {
		dev_err(ec->dev,
			"bad packet checksum, expected %02x, got %02x\n",
			args.checksum, sum);
		ret = -EBADMSG;
		goto done;
	}

	/* Return actual amount of data received */
	ret = args.data_size;
done:
	return ret;
}

static int cros_ec_cmd_xfer_espi(struct cros_ec_device *ec,
				 struct cros_ec_command *msg)
{
	return cros_ec_espi_xfer_retry(ec, msg, cros_ec_cmd_xfer_espi_once,
				       "command transfer timeout");
}

/* Returns num bytes read, or negative on error. Doesn't need locking. */
static int cros_ec_espi_readmem(struct cros_ec_device *ec, unsigned int offset,
				unsigned int bytes, void *dest)
{
	struct cros_ec_espi *ec_espi = ec->priv;
	int i = offset;
	char *s = dest;
	int cnt = 0;
	int ret;

	if (offset >= EC_MEMMAP_SIZE - bytes)
		return -EINVAL;

	/* fixed length */
	if (bytes) {
		ret = cros_ec_espi_ops.read(ec_espi->mmio_memory_base + offset,
					    bytes, s);
		if (ret < 0)
			return ret;
		return bytes;
	}

	/* string */
	for (; i < EC_MEMMAP_SIZE; i++, s++) {
		ret = cros_ec_espi_ops.read(ec_espi->mmio_memory_base + i, 1,
					    s);
		if (ret < 0)
			return ret;
		cnt++;
		if (!*s)
			break;
	}

	return cnt;
}

static int cros_ec_espi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cros_ec_device *ec_dev;
	struct cros_ec_espi *ec_espi;
	struct resource *res;
	u8 buf[2] = {};
	int irq, ret;

	dev_dbg(dev, "ChromeOS EC eSPI driver probe - START\n");

	ec_espi = devm_kzalloc(dev, sizeof(*ec_espi), GFP_KERNEL);
	if (!ec_espi) {
		dev_err(dev, "Failed to allocate ec_espi structure\n");
		return -ENOMEM;
	}
	dev_dbg(dev, "ec_espi structure allocated successfully\n");

	/* Get eSPI shared memory resource */
	dev_dbg(dev, "Getting eSPI shared memory resource...\n");
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "Failed to get eSPI shared memory resource\n");
		return -ENODEV;
	}
	dev_dbg(dev, "eSPI resource obtained: start=0x%llx, size=0x%llx\n",
		 (unsigned long long)res->start, (unsigned long long)resource_size(res));

	dev_dbg(dev, "Mapping eSPI shared memory...\n");
	ec_espi->espi_shared_mem_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(ec_espi->espi_shared_mem_base)) {
		dev_err(dev, "Failed to map eSPI shared memory\n");
		return PTR_ERR(ec_espi->espi_shared_mem_base);
	}
	dev_dbg(dev, "eSPI shared memory mapped successfully at %p\n", ec_espi->espi_shared_mem_base);

	ec_espi->mmio_memory_base = EC_LPC_ADDR_MEMMAP;
	mutex_init(&ec_espi->io_mutex);
	dev_dbg(dev, "ec_espi structure initialized, mmio_memory_base=0x%x\n", ec_espi->mmio_memory_base);

	dev_dbg(dev, "eSPI shared memory mapped at %p (resource: %pR)\n",
		 ec_espi->espi_shared_mem_base, res);

	/* Set up driver operations */
	dev_dbg(dev, "Setting up driver operations...\n");
	cros_ec_espi_ops.read = cros_ec_espi_read_bytes;
	cros_ec_espi_ops.write = cros_ec_espi_write_bytes;
	dev_dbg(dev, "Driver operations set up successfully\n");

	/* Set global pointer */
	dev_dbg(dev, "Setting global pointer...\n");
	mutex_lock(&g_espi_mutex);
	g_ec_espi = ec_espi;
	mutex_unlock(&g_espi_mutex);
	dev_dbg(dev, "Global pointer set successfully\n");

	/* Try to detect EC by reading ID */
	dev_dbg(dev, "Attempting to read EC ID...\n");
	ret = cros_ec_espi_read_id(buf);
	if (ret < 0 || !cros_ec_espi_valid_id(buf)) {
		if (!cros_ec_espi_recover_controller(dev, "probe-time EC detection"))
			ret = cros_ec_espi_read_id(buf);
	}
	if (ret < 0) {
		dev_err(dev, "Failed to read EC ID: %d\n", ret);
		goto err_cleanup;
	}
	dev_dbg(dev, "EC ID read successfully, ret=%d, buf[0]=0x%02x, buf[1]=0x%02x\n",
		ret, buf[0], buf[1]);

	if (!cros_ec_espi_valid_id(buf)) {
		dev_err(dev, "EC ID not detected (got: 0x%02x 0x%02x)\n",
			buf[0], buf[1]);
		ret = -ENODEV;
		goto err_cleanup;
	}

	dev_dbg(dev, "ChromeOS EC detected via eSPI (ID: %c%c)\n", buf[0],
		 buf[1]);

	/*
	 * Give EC some time to fully initialize before sending commands.
	 * This helps avoid timing issues where EC is not ready to respond.
	 */
	msleep(100);

	dev_dbg(dev, "Allocating ec_dev structure...\n");
	ec_dev = cros_ec_device_alloc(dev);
	if (!ec_dev) {
		dev_err(dev, "Failed to allocate ec_dev structure\n");
		ret = -ENOMEM;
		goto err_cleanup;
	}
	dev_dbg(dev, "ec_dev structure allocated successfully\n");

	dev_dbg(dev, "Setting up ec_dev structure...\n");
	platform_set_drvdata(pdev, ec_dev);
	ec_dev->phys_name = dev_name(dev);
	ec_dev->cmd_xfer = cros_ec_cmd_xfer_espi;
	ec_dev->pkt_xfer = cros_ec_pkt_xfer_espi;
	ec_dev->cmd_readmem = cros_ec_espi_readmem;
	ec_dev->priv = ec_espi;
	dev_dbg(dev, "ec_dev structure configured successfully\n");

	/*
	 * Some boards do not have an IRQ allotted for cros_ec_espi,
	 * which makes ENXIO an expected (and safe) scenario.
	 */
	dev_dbg(dev, "Getting IRQ...\n");
	irq = platform_get_irq_optional(pdev, 0);
	if (irq > 0) {
		ec_dev->irq = irq;
		dev_dbg(dev, "IRQ obtained: %d\n", irq);
	} else if (irq != -ENXIO) {
		dev_err(dev, "couldn't retrieve IRQ number (%d)\n", irq);
		ret = irq;
		goto err_cleanup;
	} else {
		dev_dbg(dev, "No IRQ available (ENXIO), continuing without IRQ\n");
	}

	dev_dbg(dev, "About to call cros_ec_register...\n");
	ret = cros_ec_register(ec_dev);
	if (ret) {
		dev_err(dev, "couldn't register ec_dev (%d)\n", ret);
		goto err_cleanup;
	}
	dev_dbg(dev, "cros_ec_register completed successfully\n");

	dev_dbg(dev, "ChromeOS EC eSPI driver initialized successfully - COMPLETE\n");
	return 0;

err_cleanup:
	dev_err(dev, "Probe failed, cleaning up...\n");
	mutex_lock(&g_espi_mutex);
	g_ec_espi = NULL;
	mutex_unlock(&g_espi_mutex);
	return ret;
}

static void cros_ec_espi_remove(struct platform_device *pdev)
{
	struct cros_ec_device *ec_dev = platform_get_drvdata(pdev);

	cros_ec_unregister(ec_dev);

	/* Clear global pointer */
	mutex_lock(&g_espi_mutex);
	g_ec_espi = NULL;
	mutex_unlock(&g_espi_mutex);
}

static const struct of_device_id cros_ec_espi_of_match[] = {
	{ .compatible = "google,cros-ec-espi" },
	{ .compatible = "google,cros-ec-lpc" }, /* Keep LPC compatibility */
	{}
};
MODULE_DEVICE_TABLE(of, cros_ec_espi_of_match);

#ifdef CONFIG_PM_SLEEP
static int cros_ec_espi_prepare(struct device *dev)
{
	struct cros_ec_device *ec_dev = dev_get_drvdata(dev);
	return cros_ec_suspend_prepare(ec_dev);
}

static void cros_ec_espi_complete(struct device *dev)
{
	struct cros_ec_device *ec_dev = dev_get_drvdata(dev);
	cros_ec_resume_complete(ec_dev);
}

static int cros_ec_espi_suspend_late(struct device *dev)
{
	struct cros_ec_device *ec_dev = dev_get_drvdata(dev);
	return cros_ec_suspend_late(ec_dev);
}

static int cros_ec_espi_resume_early(struct device *dev)
{
	struct cros_ec_device *ec_dev = dev_get_drvdata(dev);
	return cros_ec_resume_early(ec_dev);
}
#endif

static const struct dev_pm_ops cros_ec_espi_pm_ops = {
#ifdef CONFIG_PM_SLEEP
	.prepare = cros_ec_espi_prepare,
	.complete = cros_ec_espi_complete,
#endif
	SET_LATE_SYSTEM_SLEEP_PM_OPS(cros_ec_espi_suspend_late,
				     cros_ec_espi_resume_early)
};

static struct platform_driver cros_ec_espi_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = cros_ec_espi_of_match,
		.pm = &cros_ec_espi_pm_ops,
		/*
		 * Child devices may probe before us, and they racily
		 * check our drvdata pointer. Force synchronous probe until
		 * those races are resolved.
		 */
		.probe_type = PROBE_FORCE_SYNCHRONOUS,
	},
	.probe = cros_ec_espi_probe,
	.remove = cros_ec_espi_remove,
};

module_platform_driver(cros_ec_espi_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ChromeOS EC eSPI driver");
MODULE_ALIAS("platform:" DRV_NAME);
