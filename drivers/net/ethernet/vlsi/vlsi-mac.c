// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * vlsi-mac.c
 */
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of_mdio.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/sysfs.h>

#define DEFAULT_MSG_ENABLE	(NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK)

static int debug = -1; /* defaults to 31 */
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0=none,...)");

/* limit to 10Mbit for FPGA tests */
static bool force10mbps;
module_param(force10mbps, bool, 0400);

/*
 * register map, addresses for registers defined as memory offsets.
 */
#define ETH_CF			0x00
#  define ETH_CF_SOFTRESET	0x8000
#  define ETH_CF_FULLDUPLEX	0x4000		/* write 1 */
#  define ETH_CF_TXINT_MODE1	0x1000		/* IRQ on TX end */
#  define ETH_CF_TXINT_MODE2	0x2000		/* IRQ on TX mem avail */
#  define ETH_CF_TXINT_MODE3	0x3000		/* IRQ on TX start */
#  define ETH_CF_TXINT_MODE	0x3000
#  define ETH_CF_RXINT_EN	0x0800
#  define ETH_CF_TESTMEM_TX0	0x0400
#  define ETH_CF_TESTMEM_TX1	0x0500
#  define ETH_CF_TESTMEM_RX0	0x0600
#  define ETH_CF_TESTMEM_RX1	0x0700
#  define ETH_CF_TESTMEM_ANY	0x0700		/* equivalent TO RX1 */
#  define ETH_CF_DSPIRQ		0x0080
#  define ETH_CF_RXCRCEN	0x0020
#  define ETH_CF_TXMEMON	0x0010
#  define ETH_CF_TXSTART	0x0008
#  define ETH_CF_RXMEMON	0x0004
#  define ETH_CF_RXOVR_RESET	0x0001

#define ETH_STATUS		0x04
#  define ETH_STATUS_TX_AVAIL	0x0001
#  define ETH_STATUS_RX_RA0	0x0002
#  define ETH_STATUS_RX_RA1	0x0004
#  define ETH_STATUS_RX_RANY	0x0006
#  define ETH_STATUS_OVR	0x0008
#  define ETH_STATUS_RGMII	0x00f0
#  define ETH_STATUS_RX_PKT0	0x0100
#  define ETH_STATUS_RX_PKT1	0x0200
#  define ETH_STATUS_RX_PSTS	0x0c00
#  define ETH_STATUS_TX_INVLEN	0x1000

#define ETH_DEV_ID		0x08

#define ETH_MDIO_CF		0x40
#  define ETH_MDIO_CF_CMD	0x400
#  define ETH_MDIO_CF_CLKEN	0x200
#  define ETH_MDIO_CF_CLKDIV	GENMASK(7, 0)

#define ETH_MDIO_FRAME		0x44
#  define ETH_MDIO_FRAME_READ	0x800
#  define ETH_MDIO_FRAME_WRITE	0x400
#  define ETH_MDIO_FRAME_PHY	GENMASK(9, 5)
#  define ETH_MDIO_FRAME_REG	GENMASK(4, 0)

#define ETH_MDIO_WRDATA		0x48
#define ETH_MDIO_RDDATA		0x4c

#define ETH_RX_MIN_LENGTH	0x84
#define ETH_RX_MAX_LENGTH	0x88
#define ETH_MULTIPKT_RVMEM_CF	0x8c

#  define ETH_RXF_EN		0x8000
#  define ETH_RXF_PROMISC	0x2000
#  define ETH_RXF_BCAST		0x1000
#  define ETH_RXF_MCAST		0x0800
#  define ETH_RXF_TIMESTAMP	0x0008

#define ETH_RX_FILTER_RV	0x100
#define ETH_HWADDR_HI		0x104
#define ETH_HWADDR_MID		0x108
#define ETH_HWADDR_LO		0x10c

#define ETH_RX_FILTER_VS	0x19c
#define ETH_RX_MODIFY_VS	0x1a0
#define ETH_RX_VLAN_VS		0x1a4
#define ETH_RX_ETHTYPE_EX_VS	0x1a8
#define ETH_RX_UDP_PORT_VS	0x1ac
#define ETH_RX_UDP_PORT_EX_VS	0x1b0
#define ETH_RX_AUDIO_FMT_VS	0x1b4
#define ETH_MAC_ADDRESS_HI_VS	0x1b8
#define ETH_MAC_ADDRESS_MID_VS	0x1bc
#define ETH_MAC_ADDRESS_LO_VS	0x1c0
#define ETH_RX_MCAST3_VS	0x1c4
#define ETH_RX_MCAST2_VS	0x1c8
#define ETH_RX_MCAST1_VS	0x1cc
#define ETH_RX_MCAST0_VS	0x1d0

#define ETH_TXDATA		0x18000
#define ETH_RXDATA		0x19000

struct vsrves01_regmap {
	uint32_t cf;			/* 0x00 */
	uint32_t status;		/* 0x01 */
	uint32_t dev_id;		/* 0x02 */
	uint32_t _undecoded1[13];
	uint32_t mdio_cf;		/* 0x10 */
	uint32_t mdio_frame;		/* 0x11 */
	uint32_t mdio_wrdata;		/* 0x12 */
	uint32_t mdio_rddata;		/* 0x13 */
	uint32_t _undecoded2[13];
	uint32_t eth_rxmin;		/* 0x21 */
	uint32_t eth_rxmax;		/* 0x22 */
	uint32_t eth_mpktcf;		/* 0x23 */
	uint32_t _undecoded3[4];
	uint32_t eth_timerl;		/* 0x28 */
	uint32_t eth_timerh;		/* 0x29 */
	uint32_t eth_pktorun;		/* 0x2a */
	uint32_t _undecoded4[21];
	uint32_t rv_rxfilter;		/* 0x40 */
	uint32_t rv_macaddr_hi;		/* 0x41 */
	uint32_t rv_macaddr_mid;	/* 0x42 */
	uint32_t rv_macaddr_lo;		/* 0x43 */
	uint32_t rv_rxmcast3;		/* 0x44 */
	uint32_t rv_rxmcast2;		/* 0x45 */
	uint32_t rv_rxmcast1;		/* 0x46 */
	uint32_t rv_rxmcast0;		/* 0x47 */
	uint32_t _undecoded5[31];
	uint32_t vs_rxfilter;		/* 0x67 */
	uint32_t vs_rxmodify;		/* 0x68 */
	uint32_t vs_rxvlan;		/* 0x69 */
	uint32_t vs_rxethtype;		/* 0x6a */
	uint32_t vs_rxudpport;		/* 0x6b */
	uint32_t vs_rxudpextra;		/* 0x6c */
	uint32_t vs_rxaudiofmt;		/* 0x6d */
	uint32_t vs_macaddr_hi;		/* 0x6e */
	uint32_t vs_macaddr_mid;	/* 0x6f */
	uint32_t vs_macaddr_lo;		/* 0x70 */
	uint32_t vs_rxmcast3;		/* 0x71 */
	uint32_t vs_rxmcast2;		/* 0x72 */
	uint32_t vs_rxmcast1;		/* 0x73 */
	uint32_t vs_rxmcast0;		/* 0x74 */
};

struct vlsi_mac_private {
	struct device		*dev;
	struct net_device	*netdev;
	struct clk		*clk;
	union {
		void __iomem	*membase;
		struct vsrves01_regmap *regmap;
	};
	spinlock_t		lock;
	u32 			msg_enable;
	u32 			mdio_cf_cmd;

	/* link */
	int			speed;
	int			duplex;
	unsigned		link:1;

	unsigned int		ecnt;
};

/* sysfs interface */
static ssize_t vlsi_mac_sysfs_get_regmem(struct file *file,
					 struct kobject *kobj,
					 const struct bin_attribute *attr,
					 char *buf, loff_t offset, size_t size)
{
	struct device *dev = kobj_to_dev(kobj);
	struct vlsi_mac_private *priv = dev_get_drvdata(dev);

	if (offset > 512)
		return -1;

	if (offset + size > 512)
		size = 512 - offset;

	memcpy_fromio(buf, priv->membase + offset, size);

	return size;
}

static ssize_t vlsi_mac_sysfs_set_regmem(struct file *file,
					 struct kobject *kobj,
					 const struct bin_attribute *attr,
					 char *buf, loff_t offset, size_t size)
{
	struct device *dev = kobj_to_dev(kobj);
	struct vlsi_mac_private *priv = dev_get_drvdata(dev);
	ssize_t ret = 0;

	if (offset > 512)
		return -1;

	offset -= offset % sizeof(u32);

	if (offset + size > 512)
		size = 512 - offset;

	while (size > 0) {
		u32 oldvalue = readl(priv->membase + offset);
		u32 value;

		memcpy(&value, buf, sizeof(value));

		if (offset == 0 && ((oldvalue & ~0x81) == (value & ~0x81))) {
			writel(value, priv->membase + offset);
			ret += 4;
		} else if (offset >= 103 * 4 && offset < 120 * 4) {
			writel(value, priv->membase + offset);
			ret += 4;
		} else {
			return ret;
		}

		buf += 4;
		offset += 4;
		size -= 4;
	}

	return ret;
}

static const struct bin_attribute bin_attr_regmem = {
	.attr = { .name = "regmem", .mode = 0644 },
	.size = 512,
	.read = vlsi_mac_sysfs_get_regmem,
	.write = vlsi_mac_sysfs_set_regmem,
};

/* mdio */
static int vlsi_mac_mdio_wait_for_idle(struct vlsi_mac_private *priv)
{
	u32 val;

	return readl_poll_timeout(&priv->regmap->mdio_cf, val,
	                          !(val & ETH_MDIO_CF_CMD), 1, 15);
}

static int vlsi_mac_mdio_read(struct mii_bus *bus, int addr, int reg)
{
	struct vlsi_mac_private *priv = bus->priv;
	int ret;

	writel(ETH_MDIO_FRAME_READ |
	       FIELD_PREP(ETH_MDIO_FRAME_PHY, addr) |
	       FIELD_PREP(ETH_MDIO_FRAME_REG, reg),
	       &priv->regmap->mdio_frame);
	writel(priv->mdio_cf_cmd, &priv->regmap->mdio_cf);

	ret = vlsi_mac_mdio_wait_for_idle(priv);
	if (!ret)
		ret = readl(&priv->regmap->mdio_rddata);

	dev_dbg(priv->dev, "mdio_read(bus, %d, 0x%02x) = 0x%04x\n",
	        addr, reg, ret);
	return ret;
}

static int vlsi_mac_mdio_write(struct mii_bus *bus, int addr, int reg, u16 data)
{
	struct vlsi_mac_private *priv = bus->priv;
	int ret;

	writel(ETH_MDIO_FRAME_WRITE |
	       FIELD_PREP(ETH_MDIO_FRAME_PHY, addr) |
	       FIELD_PREP(ETH_MDIO_FRAME_REG, reg),
	       &priv->regmap->mdio_frame);
	writel(data, &priv->regmap->mdio_wrdata);
	writel(priv->mdio_cf_cmd, &priv->regmap->mdio_cf);

	ret = vlsi_mac_mdio_wait_for_idle(priv);

	dev_dbg(priv->dev, "mdio_write(bus, %d, 0x%02x, 0x%04hx) = %d\n",
	        addr, reg, data, ret);
	return ret;
}

static int vlsi_mac_mdio_probe(struct vlsi_mac_private *priv)
{
	struct device *dev = priv->dev;
	struct device_node *mdio_node;
	struct mii_bus *bus;
	unsigned long ndiv;
	u32 mdio_cf;
	int ret;

	ndiv = clk_get_rate(priv->clk) / 3500000;
	if (ndiv > 0xffU) {
		dev_warn(dev,
			"sysclk too fast for setting desired mdio clk, forcing max divider\n");
		ndiv = 0xffU;
	}

	mdio_cf = readl(&priv->regmap->mdio_cf);
	if (mdio_cf & ETH_MDIO_CF_CMD)
		dev_warn(priv->dev, "mdio active on init 0x%x\n", mdio_cf);

	mdio_cf = ETH_MDIO_CF_CLKEN | ndiv;
	writel(mdio_cf, &priv->regmap->mdio_cf);
	ret = vlsi_mac_mdio_wait_for_idle(priv);
	if (ret)
		return ret;

	priv->mdio_cf_cmd = ETH_MDIO_CF_CMD | mdio_cf;

	bus = devm_mdiobus_alloc(dev);
	if (!bus)
		return -ENOMEM;

	bus->name = "VLSI MAC MII Bus";
	bus->parent = dev;
	bus->phy_mask = 0x1fU;
	bus->priv = priv;
	bus->read = &vlsi_mac_mdio_read;
	bus->write = &vlsi_mac_mdio_write;

	snprintf(bus->id, MII_BUS_ID_SIZE, "%s", dev_name(dev));

	mdio_node = of_get_child_by_name(dev->of_node, "mdio");
	if (!mdio_node)
		return dev_err_probe(dev, -EINVAL, "mdio node not found\n");

	ret = devm_of_mdiobus_register(dev, bus, mdio_node);
	of_node_put(mdio_node);
	if (ret)
		return dev_err_probe(dev, ret, "cannot register %s as mdio bus\n", bus->name);

	return ret;
}

#if 0
static int vlsi_mac_mdio_init(struct vlsi_mac_private *priv)
{
	struct net_device *netdev = priv->netdev;
	unsigned long ndiv = clk_get_rate(priv->clk) / 3500000;
	uint32_t tmp;
	int try = 0;

	if (ndiv > 0xffU) {
		netdev_warn(netdev,
			"sysclk too fast for setting desired mdio clk, forcing max divider\n");
		ndiv = 0xffU;
	}
	priv->mdio_divider = ndiv;

	writel(ETH_MDIO_CF_CLKEN | priv->mdio_divider, &priv->regmap->mdio_cf);
	if (readl(&priv->regmap->mdio_cf) & ETH_MDIO_CF_CMD) {
		printk("MDIO active on init %x => ", priv->regmap->mdio_cf);
		writel(readl(&priv->regmap->mdio_cf) & ~ETH_MDIO_CF_CMD,
		       &priv->regmap->mdio_cf);
		printk(KERN_CONT "%x\n", priv->regmap->mdio_cf);
	}

	priv->mii.phy_id_mask = 0x1f;
	priv->mii.reg_num_mask = 0x1f;
	priv->mii.dev = netdev;
	priv->mii.mdio_read = vlsi_mac_mdio_read;
	priv->mii.mdio_write = vlsi_mac_mdio_write;

	priv->mii.supports_gmii = 0;

mdio_phy_retry:
	/* now Talk to PHY */
	tmp = _mdio_read(priv, priv->mii.phy_id, MII_PHYSID1) << 16 |
	      _mdio_read(priv, priv->mii.phy_id, MII_PHYSID2);
	if (tmp == 0xffffffff) { /* phy stuck? */
		if (try == 0)
			printk("PHY transciver seems to be stuck, attempting to reset..");
		printk(KERN_CONT "(try %d)", ++try);
		_mdio_write(priv, priv->mii.phy_id, 31, 0x2);
		_mdio_write(priv, priv->mii.phy_id, MII_BMCR, 0x8000);
		mdelay(10);
		if (try < 3)
			goto mdio_phy_retry;
		return -ENODEV;
	} else if ((tmp & 0xfffffff0) ==
		   0x00221640) { /* KSZ9131RNX as it should be */
		uint32_t tmp2;
		if (try != 0)
			printk(KERN_CONT "OK\n");

		_mdio_write(priv, priv->mii.phy_id, 25, 2);		/* MDIO Drive = 2 (push-pull) */
		_mdio_write(priv, priv->mii.phy_id, 26, 0x4000);        /* LED Mode = BIT(14) (KSZ9031 mode) */
		_mdio_write(priv, priv->mii.phy_id, 23, 0x1003);	/* LED Behaviour = BIT(12) | BIT(1) | BIT(0) (pulse enable, link-only/duplex-only) */
		_mdio_write(priv, priv->mii.phy_id, 30, 0x100);         /* External Loopback = BIT(8) (unknown) */
		_mdio_write(priv, priv->mii.phy_id, 22, (14 << 4) | 15);/* LED Mode Select = LED2:off LED1:on */
		/* MMD 2.0 read */
		_mdio_write(priv, priv->mii.phy_id, 13, 2);		/* MMD Access Control = address, 2 */
		_mdio_write(priv, priv->mii.phy_id, 14, 0);		/* MMD Addr/Data = 0 */
		_mdio_write(priv, priv->mii.phy_id, 13, 0x4002);        /* MMD Access Control = data no post inc, 2 */
		tmp2 = _mdio_read(priv, priv->mii.phy_id, 14);		/* from MMD Addr/Data */
		tmp2 ^= 0x10; /* flip 3 color led mode bit */
		_mdio_write(priv, priv->mii.phy_id, 13, 0x4002);	/* MMD Access Control = data no post inc, 2 */
		_mdio_write(priv, priv->mii.phy_id, 14, tmp2);		/* to MMD Addr/Data */

		/* reconfigure leds */
		_mdio_write(priv, priv->mii.phy_id, 26, 0x2000);	/* LED Mode = BIT(13) (Extended mode, unknown) */
		/* led pulse stretch| led nocombine */
		_mdio_write(priv, priv->mii.phy_id, 23,
			    0x0000 | (0x3 << 10) | (0x3 << 5) | 0x3);	/* LED Behaviour =  50 ms pulse-stretch, LED2:pulse-stretch LED1:puls-stretch, link-only/duplex-only */
		_mdio_write(priv, priv->mii.phy_id, 22, 0xee);		/* LED Mode Select = LED2:on LED1:on */
	}

	_mdio_write(priv, priv->mii.phy_id, MII_CTRL1000, 0x0);		/* Auto-neg Master/Slave Control = normal mode, no manual configuration, single-port, phy is not 1000BASE-T full duplex capable */
	tmp = _mdio_read(priv, priv->mii.phy_id, MII_ADVERTISE);
	_mdio_write(priv, priv->mii.phy_id, MII_ADVERTISE,		/* Auto-neg Advertisement = 100BASE-X full duplex, 10BASE-T full duplex, IEE802.3 */
		    0x41 | (force10mbps ? 0 : 0x100));

	if (tmp != _mdio_read(priv, priv->mii.phy_id, MII_ADVERTISE)) { /* if changed */
		_mdio_write(priv, priv->mii.phy_id, MII_BMCR,
			    BMCR_FULLDPLX | BMCR_ANENABLE | BMCR_ANRESTART);
		mdelay(1);
	}

	return 0;
}
#endif

/* more internal helpers */
static void vlsi_mac_updatemtu(struct vlsi_mac_private *priv, int mtu)
{
	u32 mpktcf = ((2047 - (mtu + 20)) >> 1) | 0x8000;

	writel(mpktcf, &priv->regmap->eth_mpktcf);
	writel(mtu + 20, &priv->regmap->eth_rxmax);
}

/* ethtool ops */
static void vlsi_mac_get_drvinfo(struct net_device *netdev,
				 struct ethtool_drvinfo *info)
{
	strscpy(info->driver, "vlsi-mac", sizeof(info->driver));
	strscpy(info->bus_info, dev_name(&netdev->dev), sizeof(info->bus_info));
}

static u32 vlsi_mac_get_msglevel(struct net_device *netdev)
{
	struct vlsi_mac_private *priv = netdev_priv(netdev);

	return priv->msg_enable;
}

static void vlsi_mac_set_msglevel(struct net_device *netdev, u32 value)
{
	struct vlsi_mac_private *priv = netdev_priv(netdev);

	priv->msg_enable = value;
}

static const struct ethtool_ops vlsi_mac_ethtool_ops = {
	.get_drvinfo		= vlsi_mac_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_link_ksettings	= phy_ethtool_get_link_ksettings,
	.set_link_ksettings	= phy_ethtool_set_link_ksettings,
	.nway_reset		= phy_ethtool_nway_reset,
	.get_msglevel		= vlsi_mac_get_msglevel,
	.set_msglevel		= vlsi_mac_set_msglevel,
};

static void vlsi_set_mac_address_low(struct net_device *netdev)
{
	struct vlsi_mac_private *priv = netdev_priv(netdev);
	u32 hi  = netdev->dev_addr[0] << 8 | netdev->dev_addr[1];
	u32 mid = netdev->dev_addr[2] << 8 | netdev->dev_addr[3];
	u32 lo  = netdev->dev_addr[4] << 8 | netdev->dev_addr[5];

	writel(hi,  &priv->regmap->rv_macaddr_hi);
	writel(mid, &priv->regmap->rv_macaddr_mid);
	writel(lo,  &priv->regmap->rv_macaddr_lo);

	writel(hi,  &priv->regmap->vs_macaddr_hi);
	writel(mid, &priv->regmap->vs_macaddr_mid);
	writel(lo,  &priv->regmap->vs_macaddr_lo);
}

static void vlsi_mac_flushrx(struct vlsi_mac_private *priv)
{
	writel(readl(&priv->regmap->cf) | ETH_CF_RXMEMON, &priv->regmap->cf);
	nop();
	writel(readl(&priv->regmap->cf) & ~ETH_CF_RXMEMON, &priv->regmap->cf);
	nop();
	writel(readl(&priv->regmap->cf) | ETH_CF_RXMEMON, &priv->regmap->cf);
	nop();
	writel(readl(&priv->regmap->cf) & ~ETH_CF_RXMEMON, &priv->regmap->cf);
}

static void vlsi_mac_powerup(struct vlsi_mac_private *priv)
{
	/* reset state machine, ensure we are in fdx mode */
	writel(readl(&priv->regmap->cf) | ETH_CF_SOFTRESET | ETH_CF_FULLDUPLEX | ETH_CF_RXCRCEN,
	       &priv->regmap->cf);
	mdelay(1);
	writel(readl(&priv->regmap->cf) &
		       ~(ETH_CF_SOFTRESET | ETH_CF_TXINT_MODE |
			 ETH_CF_RXINT_EN | ETH_CF_TESTMEM_ANY | ETH_CF_RXMEMON |
			 ETH_CF_TXMEMON),
	       &priv->regmap->cf);
	mdelay(1);

	/* discard anything that might linger in memories */
	vlsi_mac_flushrx(priv);

	if (readl(&priv->regmap->status) & ETH_STATUS_OVR)
		writel(readl(&priv->regmap->cf) ^ ETH_CF_RXOVR_RESET,
		       &priv->regmap->cf);

	vlsi_set_mac_address_low(priv->netdev);
	writel(0x16, &priv->regmap->eth_rxmin);
	vlsi_mac_updatemtu(priv, priv->netdev->mtu);
	writel(0, &priv->regmap->rv_rxfilter);
}

static int vlsi_mac_set_mac_address(struct net_device *netdev, void *p)
{
	struct sockaddr *addr = p;

	if (netif_running(netdev))
		return -EBUSY;

	eth_hw_addr_set(netdev, addr->sa_data);
	vlsi_set_mac_address_low(netdev);
	return 0;
}

/* IRQ handler */
static irqreturn_t vlsi_mac_interrupt(int irq, void *dev_id)
{
	struct vlsi_mac_private *priv = dev_id;
	u32 status = readl(&priv->regmap->status);

	if (!(status & ETH_STATUS_RX_RANY)) {
		/* got IRQ but no data indicated, why? */
		netdev_warn(priv->netdev,
			    "IRQ but no rxmem has data st:0x%x cf:0x%x\n",
			    status, readl(&priv->regmap->cf));
		return IRQ_HANDLED;
	}

	return IRQ_WAKE_THREAD;
}

static irqreturn_t vlsi_mac_interrupt_threaded(int irq, void *dev_id)
{
	struct vlsi_mac_private *priv = dev_id;
	struct net_device *netdev = priv->netdev;
	u32 __iomem *rxbase = priv->membase + ETH_RXDATA;
	unsigned long flags;
	u32 pktcount;
	u32 status;
	u32 cf;

	if (!netif_running(netdev)) {
		netdev_warn(netdev,
			    "BUG? interrupt(%d) from stopped card, bailing out\n",
			    irq);
		return IRQ_HANDLED;
	}


	status = readl(&priv->regmap->status);
	if (!(status & ETH_STATUS_RX_RANY)) {
		netdev_warn(netdev,
			    "IRQ scheduled but no rxmem has data st:0x%x cf:0x%x\n",
			    status, readl(&priv->regmap->cf));
		return IRQ_HANDLED;
	}

	/* request rxmem */
	spin_lock_irqsave(&priv->lock, flags);
	cf = readl_relaxed(&priv->regmap->cf);
	writel_relaxed(cf | ETH_CF_RXMEMON, &priv->regmap->cf);
	spin_unlock_irqrestore(&priv->lock, flags);

	nop();				/* delay */
	readl(&rxbase[4]);		/* dummy read, enqueue rd[4] */
	nop();				/* delay */
	pktcount = readl(&rxbase[0]);	/* read @4, enqueue rd[0] */

	/* skip rest of meta header */
	rxbase += 4;

	pktcount &= 0xfU; /* we have at most a handful of packets */

	for (; pktcount; pktcount--) {
		struct sk_buff *skb;
		u32 __iomem *rmax;
		unsigned int plen;
		u32 pktsizeflags;
		u32 *data;

		/* enqueue read at data start, read pktsize */
		pktsizeflags = readl(rxbase++);
		if ((pktsizeflags & 0x0ff0f800U)) {
			netdev_warn(netdev,
				"unexpected packet flags (0x%x), discarding st:0x%x cf:0x%x\n",
				pktsizeflags, priv->regmap->status, priv->regmap->cf);
			break;
		}

		plen = (pktsizeflags & 0x7ffU) - 4;
		if (plen > ETH_FRAME_LEN) {
			netdev_dbg(netdev, "TW capped to ethlen from %d (0x%x) to %d\n",
				plen, pktsizeflags, ETH_FRAME_LEN);
			plen = ETH_FRAME_LEN;
		} else if (plen < ETH_ZLEN) {
			plen = ETH_ZLEN;
		}

		skb = netdev_alloc_skb(netdev, 1600);
		if (unlikely(!skb)) {
			netdev_dbg(netdev,
				"skb_alloc failed, %u bytes packet, pktcount=%u will be discarded.\n",
				plen, pktcount);
			break;
		}

		rmax = rxbase + (plen / 4) + 2;
		data = (u32 *)skb->data;

		skb_reserve(skb, 2);
		skb_put(skb, plen);

		while (rxbase < rmax) {
			*data++ = __raw_readl(rxbase++);
			nop(); nop(); /* delay */
			*data++ = __raw_readl(rxbase++);
		}
		nop(); nop(); /* delay */
		rxbase = rmax;

		readl(rxbase);		/* enqueue read at current addr (next pkt sizeflags) */
		rxbase +=4;		/* set it to next address we want next */

		skb->protocol = eth_type_trans(skb, netdev);

		netif_rx(skb);
		netdev->stats.rx_bytes += plen;
		netdev->stats.rx_packets++;
	}

	spin_lock_irqsave(&priv->lock, flags);
	cf = readl_relaxed(&priv->regmap->cf);

	/* release rxmem */
	cf &= ~ETH_CF_RXMEMON;

	/* count overflows */
	status = readl_relaxed(&priv->regmap->status);
	if (status & ETH_STATUS_OVR) {
		cf ^= ETH_CF_RXOVR_RESET;
		netdev->stats.rx_over_errors++;
	}

	/* sanity check */
	if (cf & ETH_CF_TESTMEM_ANY) {
		netdev_dbg(netdev, "something got messed up in CF register\n");
		cf &= ~ETH_CF_TESTMEM_ANY;
	}
	writel_relaxed(cf, &priv->regmap->cf);
	spin_unlock_irqrestore(&priv->lock, flags);
	return IRQ_HANDLED;
}

static irqreturn_t vlsi_mac_tx_interrupt(int irq, void *dev_id)
{
	struct vlsi_mac_private *priv = dev_id;
	struct net_device *netdev = priv->netdev;

	if (unlikely(!netif_running(netdev))) {
		netdev_warn(netdev,
			"BUG? interrupt(%d) from stopped card, bailing out\n",
			irq);
		return IRQ_NONE;
	}

	if (likely(readl(&priv->regmap->status) & ETH_STATUS_TX_AVAIL))
		netif_wake_queue(netdev);

	return IRQ_HANDLED;
}

static int vlsi_mac_open(struct net_device *netdev)
{
	struct vlsi_mac_private *priv = netdev_priv(netdev);

	if (netif_msg_ifup(priv))
		netdev_dbg(netdev, "enabling %s\n", netdev->name);

	spin_lock_irq(&priv->lock);

	writel(readl(&priv->regmap->cf) & ~(ETH_CF_RXMEMON | ETH_CF_TESTMEM_ANY),
	       &priv->regmap->cf);

	if (readl(&priv->regmap->status) & ETH_STATUS_OVR) {
		writel(readl(&priv->regmap->cf) ^ ETH_CF_RXOVR_RESET,
		       &priv->regmap->cf);
		netdev->stats.rx_over_errors++;
		/* if both buffers full */
		if ((readl(&priv->regmap->status) & ETH_STATUS_RX_RANY) == ETH_STATUS_RX_RANY)
			vlsi_mac_flushrx(priv);
	}

	writel(readl(&priv->regmap->rv_rxfilter) | ETH_RXF_BCAST | ETH_RXF_MCAST | ETH_RXF_TIMESTAMP,
	       &priv->regmap->rv_rxfilter);
	if (netdev->flags & IFF_PROMISC)
		writel(readl(&priv->regmap->rv_rxfilter) | ETH_RXF_PROMISC,
		       &priv->regmap->rv_rxfilter);

	netdev_info(netdev, "bringing hardware up st:0x%x cf:0x%x filter:0x%x\n",
		priv->regmap->status, priv->regmap->cf, priv->regmap->rv_rxfilter);

	vlsi_mac_updatemtu(priv, netdev->mtu);

	phy_start(netdev->phydev);

	netif_start_queue(netdev);

	writel(readl(&priv->regmap->cf) | ETH_CF_RXINT_EN, &priv->regmap->cf);

	writel(readl(&priv->regmap->cf) | ETH_CF_TXINT_MODE1,
	       &priv->regmap->cf);

	writel(readl(&priv->regmap->rv_rxfilter) | ETH_RXF_EN,
	       &priv->regmap->rv_rxfilter);

	spin_unlock_irq(&priv->lock);
	return 0;
}

static int vlsi_mac_stop(struct net_device *netdev)
{
	struct vlsi_mac_private *priv = netdev_priv(netdev);

	spin_lock_irq(&priv->lock);
	netif_stop_queue(netdev);

	phy_stop(netdev->phydev);

	/* filter and IRQ disable */
	writel(0, &priv->regmap->rv_rxfilter);
	writel(readl(&priv->regmap->cf) & ~(ETH_CF_RXINT_EN),
	       &priv->regmap->cf);

	/* clean interfaces */
	writel(readl(&priv->regmap->cf) & ~(ETH_CF_RXMEMON | ETH_CF_TXMEMON | ETH_CF_TESTMEM_ANY),
	       &priv->regmap->cf);

	/* reset overrun if any */
	if (readl(&priv->regmap->status) & ETH_STATUS_OVR)
		writel(readl(&priv->regmap->cf) ^ ETH_CF_RXOVR_RESET,
		       &priv->regmap->cf);

	/* discard any packets that are received and not yet handled */
	if (readl(&priv->regmap->status) & ETH_STATUS_RX_RA1) {
		writel(readl(&priv->regmap->cf) | ETH_CF_RXMEMON,
		       &priv->regmap->cf);
		udelay(1);
		writel(readl(&priv->regmap->cf) & ~ETH_CF_RXMEMON,
		       &priv->regmap->cf);
	}
	if (readl(&priv->regmap->status) & ETH_STATUS_RX_RA0) {
		writel(readl(&priv->regmap->cf) | ETH_CF_RXMEMON,
		       &priv->regmap->cf);
		udelay(1);
		writel(readl(&priv->regmap->cf) & ~ETH_CF_RXMEMON,
		       &priv->regmap->cf);
	}

	spin_unlock_irq(&priv->lock);
	return 0;
}

static netdev_tx_t vlsi_mac_start_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct vlsi_mac_private *priv = netdev_priv(netdev);
	u32 status = readl(&priv->regmap->status);
	unsigned long flags;
	u32 __iomem *txmem;
	u16 *data;
	u32 cf;

	if (status & ETH_STATUS_OVR) {
		netdev_warn(netdev,
			    "rx overrun detected in transmit st:0x%x cf:0x%x\n",
			    status, readl_relaxed(&priv->regmap->cf));

		spin_lock_irqsave(&priv->lock, flags);
		cf = readl_relaxed(&priv->regmap->cf);

		/* force card overrun reset if it is (also discard 1 buffer to make room) */
		cf ^= ETH_CF_RXOVR_RESET;
		writel_relaxed(cf, &priv->regmap->cf);
		cf |= ETH_CF_RXMEMON;
		writel_relaxed(cf, &priv->regmap->cf);
		nop(); nop(); /* we may need couple clockcycle delay */

		cf = readl_relaxed(&priv->regmap->cf);
		writel(cf & ~ETH_CF_RXMEMON, &priv->regmap->cf);
		spin_unlock_irqrestore(&priv->lock, flags);
		netdev->stats.rx_over_errors++;
	}

	if (!(status & ETH_STATUS_TX_AVAIL)) {
		priv->ecnt++;
		netdev_warn(netdev, "busy %u\n", priv->ecnt);
		return NETDEV_TX_BUSY;
	}

	/* we don't support fragmentation of SKBs yet nor 8bit alignment of data */
	if (((uintptr_t)skb->data & 1) || skb_shinfo(skb)->nr_frags != 0) {
		netdev_warn_once(netdev,
			"PACKET DROPPED because memalign %px or fragments %d\n",
			skb->data, skb_shinfo(skb)->nr_frags);
		dev_kfree_skb(skb);
		netdev->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}
	data = (u16 *)skb->data;

	netif_stop_queue(netdev);

	/* request txmem */
	spin_lock_irqsave(&priv->lock, flags);
	cf = readl_relaxed(&priv->regmap->cf);
	writel_relaxed(cf | ETH_CF_TXMEMON, &priv->regmap->cf);
	spin_unlock_irqrestore(&priv->lock, flags);

	txmem = priv->membase + ETH_TXDATA;
	writel(skb->len, txmem++);

	for (unsigned int i = 0; i < skb->len; i += 2)
		__raw_writel(*data++, txmem++);

	skb_tx_timestamp(skb);

	/* release txmem and send it */
	spin_lock_irqsave(&priv->lock, flags);
	cf = readl_relaxed(&priv->regmap->cf);
	cf &= ~ETH_CF_TXMEMON;
	writel_relaxed(cf, &priv->regmap->cf);
	nop(); nop();
	cf |= ETH_CF_TXSTART;
	writel_relaxed(cf, &priv->regmap->cf);
	spin_unlock_irqrestore(&priv->lock, flags);

	netdev->stats.tx_bytes += skb->len;
	netdev->stats.tx_packets++;
	dev_kfree_skb_any(skb);

	return NETDEV_TX_OK;
}

static void vlsi_mac_tx_timeout(struct net_device *netdev, unsigned int txqueue)
{
	struct vlsi_mac_private *priv = netdev_priv(netdev);
	u32 status = readl(&priv->regmap->status);

	if (likely(status & ETH_STATUS_TX_AVAIL))
		netif_wake_queue(netdev);

	netdev_err(netdev,
		"transmit timed out st:0x%x cf:0x%x txqueue:%d\n",
		status, readl_relaxed(&priv->regmap->cf), txqueue);
}

static void vlsi_mac_set_rx_mode(struct net_device *netdev)
{
	struct vlsi_mac_private *priv = netdev_priv(netdev);
	u32 rxfilter = readl(&priv->regmap->rv_rxfilter);

	if (netdev->flags & IFF_PROMISC)
		rxfilter |= ETH_RXF_PROMISC;
	else
		rxfilter &= ~ETH_RXF_PROMISC;

	if (netdev->flags & IFF_UP)
		rxfilter |= ETH_RXF_EN;
	else
		rxfilter &= ~ETH_RXF_EN;

	writel(rxfilter, &priv->regmap->rv_rxfilter);
}

static int vlsi_mac_change_mtu(struct net_device *netdev, int nmtu)
{
	struct vlsi_mac_private *priv = netdev_priv(netdev);

	if (nmtu > 1700)
		return -EINVAL;

	vlsi_mac_updatemtu(priv, nmtu);
	return 0;
}

static const struct net_device_ops vlsi_mac_netdev_ops = {
	.ndo_open		= vlsi_mac_open,
	.ndo_stop		= vlsi_mac_stop,
	.ndo_start_xmit		= vlsi_mac_start_xmit,
	.ndo_set_rx_mode	= vlsi_mac_set_rx_mode,
	.ndo_eth_ioctl		= phy_do_ioctl_running,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= vlsi_mac_set_mac_address,
	.ndo_tx_timeout		= vlsi_mac_tx_timeout,
	.ndo_change_mtu		= vlsi_mac_change_mtu,
};

static void vlsi_mac_adjust_link(struct net_device *netdev)
{
	struct vlsi_mac_private *priv = netdev_priv(netdev);
	struct phy_device *phydev = netdev->phydev;
	bool state_changed = false;

	if (priv->link != phydev->link) {
		priv->link = phydev->link;
		state_changed = true;
	}

	if (priv->speed != phydev->speed) {
		priv->speed = phydev->speed;
		state_changed = true;
	}

	if (priv->duplex != phydev->duplex) {
		bool full = phydev->duplex == DUPLEX_FULL;
		unsigned long flags;
		u32 cf;

		spin_lock_irqsave(&priv->lock, flags);
		cf = readl_relaxed(&priv->regmap->cf);
		if (full)
			cf |= ETH_CF_FULLDUPLEX;
		else
			cf &= ~ETH_CF_FULLDUPLEX;
		writel_relaxed(cf, &priv->regmap->cf);
		spin_unlock_irqrestore(&priv->lock, flags);

		priv->duplex = phydev->duplex;
		state_changed = true;
	}

	if (state_changed)
		phy_print_status(phydev);
}

static int vlsi_mac_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vlsi_mac_private *priv;
	struct net_device *netdev;
	struct phy_device *phydev;
	int tx_irq;
	int ret;

	netdev = devm_alloc_etherdev_mqs(dev, sizeof(*priv), 1, 1);
	if (!netdev)
		return -ENOMEM;

	priv = netdev_priv(netdev);

	/* map memory */
	priv->membase = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->membase))
		return PTR_ERR(priv->membase);

	/* clock */
	priv->clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(priv->clk))
		return dev_err_probe(dev, PTR_ERR(priv->clk), "error getting clock\n");

	/* interrupt */
	netdev->irq = platform_get_irq(pdev, 0);
	if (netdev->irq < 0)
		return netdev->irq;

	/* tx interrupt */
	tx_irq = platform_get_irq(pdev, 1);
	if (tx_irq < 0)
		return tx_irq;

	/* initialize netdev */
	SET_NETDEV_DEV(netdev, dev);
	netdev->netdev_ops = &vlsi_mac_netdev_ops;
	netdev->watchdog_timeo = msecs_to_jiffies(150);
	netdev->ethtool_ops = &vlsi_mac_ethtool_ops;
	netdev->base_addr = (uintptr_t)priv->membase;

	/* read mac-address from dt */
	ret = platform_get_ethdev_address(dev, netdev);
	if (ret) {
		/* if the MAC address is invalid get a random one */
		eth_hw_addr_random(netdev);
		dev_warn(dev, "using random MAC address %pM\n", netdev->dev_addr);
	}

	/* initialize private data */
	priv->dev = dev;
	priv->netdev = netdev;
	priv->speed = SPEED_UNKNOWN;
	priv->duplex = DUPLEX_UNKNOWN;
	priv->msg_enable = netif_msg_init(debug, DEFAULT_MSG_ENABLE);
	spin_lock_init(&priv->lock);

	platform_set_drvdata(pdev, priv);

	/* powerup */
	vlsi_mac_powerup(priv);

	/* request interrupts */
	ret = devm_request_threaded_irq(dev, netdev->irq, vlsi_mac_interrupt,
					vlsi_mac_interrupt_threaded, 0,
					dev_name(dev), priv);
	if (ret)
		return dev_err_probe(dev, ret,
				     "could not request interrupt %d\n",
				     netdev->irq);

	ret = devm_request_irq(dev, tx_irq, vlsi_mac_tx_interrupt, 0,
			       dev_name(dev), priv);
	if (ret)
		return dev_err_probe(dev, ret,
		                     "could not request interrupt %d\n",
		                     tx_irq);

	/* setup mdio bus */
	ret = vlsi_mac_mdio_probe(priv);
	if (ret)
		return ret;

	/* connect phy */
	phydev = of_phy_get_and_connect(netdev, dev->of_node, vlsi_mac_adjust_link);
	if (!phydev)
		return dev_err_probe(dev, -ENODEV, "failed to connect to PHY\n");

	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_1000baseT_Half_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_1000baseT_Full_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_1000baseKX_Full_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_1000baseX_Full_BIT);
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_1000baseT1_Full_BIT);
	if (force10mbps) {
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_100baseT_Half_BIT);
		phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_100baseT_Full_BIT);
	}

	phy_attached_info(phydev);

	/* register device */
	ret = devm_register_netdev(dev, netdev);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register netdev\n");

	ret = device_create_bin_file(dev, &bin_attr_regmem);
	if (ret)
		return dev_err_probe(dev, ret, "failed to create regmap sysfs entry\n");

	netdev_info(netdev, "VLSI MAC:%s, IRQ %d, HwAddr: %pM\n",
		    (readl(&priv->regmap->dev_id) == 0x11) ? "vsrves01" : "dev",
		    netdev->irq, netdev->dev_addr);

	return 0;
}

static void vlsi_mac_remove(struct platform_device *pdev)
{
	device_remove_bin_file(&pdev->dev, &bin_attr_regmem);
}

static const struct of_device_id vlsi_mac_match[] = {
	{ .compatible = "vlsi,vsrves01-mac" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, vlsi_mac_match);

static struct platform_driver vlsi_mac_driver = {
	.driver = {
		.name = "vlsi-mac",
		.of_match_table = vlsi_mac_match,
	},
	.probe = vlsi_mac_probe,
	.remove = vlsi_mac_remove,
};
module_platform_driver(vlsi_mac_driver);

MODULE_AUTHOR("Maciej Pijanka");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("VLSI Ethernet MAC Driver");
