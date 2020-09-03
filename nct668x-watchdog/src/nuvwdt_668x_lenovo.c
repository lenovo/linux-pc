/*
 * Customized nct668x WDT driver.
 *
 * (c) Copyright 2020 Sheng-Yuan Huang <syhuang3@nuvoton.com> (Nuvoton)
 *
 *   1. Modified it for customized nct668x WDT only. (lenovo nano)
 *
 * (c) Copyright 2018 Sheng-Yuan Huang (Nuvoton) 
 *
 *   1. Add support to NCT6796 and NCT6116.
 *   2. Modify code for convenient testing. 
 *   3. Fix some potential problems.
 *   4. Change file name for telling the differece between this one and the
 *      built-in driver.
 *
 * (c) Copyright 2013 Guenter Roeck
 *
 *   converted to watchdog infrastructure
 *
 * (c) Copyright 2007 Vlad Drukker <vlad@storewiz.com>
 *
 *   added support for W83627THF.
 *
 * c) Copyright 2003,2007 Pádraig Brady <P@draigBrady.com>
 *
 *   Based on advantechwdt.c which is based on wdt.c.
 *
 * Original copyright messages:
 *
 * (c) Copyright 2000-2001 Marek Michalkiewicz <marekm@linux.org.pl>
 * (c) Copyright 1996 Alan Cox <alan@lxorguk.ukuu.org.uk>, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * Neither Alan Cox nor CymruNet Ltd. admit liability nor provide warranty for
 * any of this software. This material is provided "AS-IS" and at no charge.
 *
 * (c) Copyright 1995    Alan Cox <alan@lxorguk.ukuu.org.uk>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/watchdog.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/io.h>

#include <linux/string.h>

#define WATCHDOG_NAME "nct6686"
#define ECSPACE_MUTEX "nct6686EC"
#define WATCHDOG_TIMEOUT 60	/* 60 sec default timeout */
#define WATCHDOG_WDT_SEL 1

static int wdt_io;

static int ec_base;		//EC base address

enum chips {
        nct6686dl,
        nct6686dl_nano
};

static int timeout;		/* in seconds */
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout,
                 "Watchdog timeout in seconds. 1 <= timeout <= 255, default="
                 __MODULE_STRING(WATCHDOG_TIMEOUT) ".");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
                 "Watchdog cannot be stopped once started (default="
                 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static int early_disable = 0;
module_param(early_disable, int, 0);
MODULE_PARM_DESC(early_disable, "Disable watchdog at boot time (default=0)");

static int debug = 0;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Disable watchdog at boot time (default=0)");

static int skip_chk_fwver = 0;
module_param(skip_chk_fwver, int, 0);
MODULE_PARM_DESC(skip_chk_fwver, "Skip checking firmware version (default=0)");

/*
 * Kernel methods.
 */

#define WDT_EFER (wdt_io + 0)	/* Extended Function Enable Registers */
#define WDT_EFIR (wdt_io + 0)	/* Extended Function Index Register \
                                 (same as EFER) */
#define WDT_EFDR (WDT_EFIR + 1)	/* Extended Function Data Register */

#define PAGE_REG_OFFSET 0
#define ADDR_REG_OFFSET 1
#define DATA_REG_OFFSET 2

#define NCT6686_LD_ECSPACE 0x0B

#define CHIPID_MASK 0xFFF0
#define NCT6686DL_ID 0xD440

#define NCT6686_CUS_WDT_CFG 0x828
#define NCT6686_CUS_WDT_CNT 0x829
#define NCT6686_CUS_WDT_STS 0x82A
#define NCT6686_CUS_WDT_STS_EVT_POS (0)
#define NCT6686_CUS_WDT_STS_EVT_MSK (0x3 << NCT6686_CUS_WDT_STS_EVT_POS)
#define NCT6686_CUS_FWVER_BASE 0x618	//Page 6 18~1f

#ifndef BIT
#define BIT(nr) (1UL << (nr))
#endif

//int nuvctm_init(void);
//void nuvctm_exit(void);

static void superio_outb(int reg, int val)
{
        outb(reg, WDT_EFER);
        outb(val, WDT_EFDR);
}

static inline int superio_inb(int reg)
{
        outb(reg, WDT_EFER);
        return inb(WDT_EFDR);
}

static int superio_enter(void)
{
        if (!request_muxed_region(wdt_io, 2, WATCHDOG_NAME)) {
                pr_warn("nuv:request IO base fail(wdt_io=%X)\n", wdt_io);
                return -EBUSY;
        }
        outb_p(0x87, WDT_EFER);	/* Enter extended function mode */
        outb_p(0x87, WDT_EFER);	/* Again according to manual */

        return 0;
}

static void superio_select(int ld)
{
        superio_outb(0x07, ld);
}

static void superio_exit(void)
{
        outb_p(0xAA, WDT_EFER);	/* Leave extended function mode */
        release_region(wdt_io, 2);
}

static inline void __nct6686_set_bank(int base_addr, u16 reg)
{
        outb_p(0xFF, base_addr + PAGE_REG_OFFSET);
        outb_p(reg >> 8, base_addr + PAGE_REG_OFFSET);
}

/* Not strictly necessary, but play it safe for now */
static inline void __nct6686_reset_bank(int base_addr, u16 reg)
{
        if (reg & 0xff00) {
                outb_p(0xFF, base_addr + PAGE_REG_OFFSET);
        }
}

static u8 nct6686_read_value(int base_addr, u16 fullreg)
{
        u8 volatile res;

        if (!request_muxed_region(base_addr, 3, ECSPACE_MUTEX)) {
                pr_warn("nuv:request ECSpace fail(base_addr=%X)\n", base_addr);
                return -EBUSY;
        }

        __nct6686_set_bank(base_addr, fullreg);
        outb_p(fullreg & 0xff, base_addr + ADDR_REG_OFFSET);
        res = inb_p(base_addr + DATA_REG_OFFSET);

        __nct6686_reset_bank(base_addr, fullreg);

        release_region(base_addr, 3);

        return res;
}

static int nct6686_write_value(int base_addr, u16 fullreg, u8 value)
{

        if (!request_muxed_region(base_addr, 3, ECSPACE_MUTEX)) {
                pr_warn("nuv:request ECSpace fail(base_addr=%X)\n", base_addr);
                return -EBUSY;
        }

        __nct6686_set_bank(base_addr, fullreg);
        outb_p(fullreg & 0xff, base_addr + ADDR_REG_OFFSET);

        outb_p(value & 0xff, base_addr + DATA_REG_OFFSET);
        __nct6686_reset_bank(base_addr, fullreg);

        release_region(base_addr, 3);
        return 0;
}

static int nct6686_init(struct watchdog_device *wdog, enum chips chip)
{
        volatile unsigned char t, cfg, reg;

        t = nct6686_read_value(ec_base, NCT6686_CUS_WDT_CNT);
        cfg = nct6686_read_value(ec_base, NCT6686_CUS_WDT_CFG);

        if (cfg & BIT(0)) {
                if (early_disable) {
                        pr_warn("Stopping previously enabled watchdog until userland kicks in\n");

                        // Disable WDT:
                        nct6686_write_value(ec_base, NCT6686_CUS_WDT_CFG,
                                            cfg & (~BIT(0)));

                        // Clear CNT:
                        nct6686_write_value(ec_base, NCT6686_CUS_WDT_CNT, 0);
                } else {
                        pr_info("Watchdog already running. Resetting timeout to %d sec\n",
                                wdog->timeout);

                        nct6686_write_value(ec_base, NCT6686_CUS_WDT_CNT,
                                            wdog->timeout);
                }
        }

        /* reset trigger status */
        reg = nct6686_read_value(ec_base, NCT6686_CUS_WDT_STS);
        nct6686_write_value(ec_base, NCT6686_CUS_WDT_STS,
                            reg & ~NCT6686_CUS_WDT_STS_EVT_MSK);

        return 0;
}

static int wdt_enable(bool en)
{
        unsigned char volatile reg;

        reg = nct6686_read_value(ec_base, NCT6686_CUS_WDT_CFG);

        if (en) {
                nct6686_write_value(ec_base, NCT6686_CUS_WDT_CFG, reg | 0x3);
        } else {
                nct6686_write_value(ec_base, NCT6686_CUS_WDT_CFG,
                                    reg & ~BIT(0));
        }

        return 0;
}

static int wdt_set_time(unsigned int timeout)
{
        if (debug != 0) {
                pr_info("nuv:wdt_set_time()\n");
        }

        nct6686_write_value(ec_base, NCT6686_CUS_WDT_CNT, timeout);

        if (timeout != 0) {
                wdt_enable(true);
        } else {
                wdt_enable(false);
        }

        return 0;
}

static int wdt_start(struct watchdog_device *wdog)
{
        unsigned char reg;

        if (debug != 0) {
                pr_info("nuv:wdt_start()\n");
        }

        wdt_set_time(wdog->timeout);

        /* reset trigger status */
        reg = nct6686_read_value(ec_base, NCT6686_CUS_WDT_STS);
        nct6686_write_value(ec_base, NCT6686_CUS_WDT_STS,
                            reg & ~NCT6686_CUS_WDT_STS_EVT_MSK);

        return 0;
}

static int wdt_stop(struct watchdog_device *wdog)
{
        if (debug != 0) {
                pr_info("nuv:wdt_stop()\n");
        }
        return wdt_set_time(0);
}

static int wdt_set_timeout(struct watchdog_device *wdog, unsigned int timeout)
{
        if (debug != 0) {
                pr_info("nuv:wdt_set_timeout()\n");
        }
        wdog->timeout = timeout;

        return 0;
}

static unsigned int wdt_get_time(struct watchdog_device *wdog)
{
        unsigned int timeleft;

        if (debug != 0) {
                pr_info("nuv:wdt_get_time()\n");
        }

        timeleft = nct6686_read_value(ec_base, NCT6686_CUS_WDT_CNT);

        return timeleft;
}

/*
 * Kernel Interfaces
 */

static const struct watchdog_info wdt_info = {
        .options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
        .identity = "NUVOTON Watchdog",
};

static const struct watchdog_ops wdt_ops = {
        .owner = THIS_MODULE,
        .start = wdt_start,
        .stop = wdt_stop,
        .set_timeout = wdt_set_timeout,
        .get_timeleft = wdt_get_time,
};

static struct watchdog_device wdt_dev = {
        .info = &wdt_info,
        .ops = &wdt_ops,
        .timeout = WATCHDOG_TIMEOUT,
        .min_timeout = 1,
        .max_timeout = 255,
};

/*
 * The WDT needs to learn about soft shutdowns in order to turn the timebomb
 * registers off.
 */
static int wdt_find(int addr)
{
        u16 val;
        int ret;
        int i;
        u8 fw_ver[9] = { 0 };	//[8] is for end of string('\0')

        ret = superio_enter();
        if (ret)
                return ret;

        pr_info("Search port %X... ", wdt_io);

        val = superio_inb(0x20);
        val = ((val << 8) | (superio_inb(0x21) & 0xFF)) & CHIPID_MASK;

        switch (val) {
        case NCT6686DL_ID:
                ret = nct6686dl;
                pr_info("Chip found: ChipID=%X (with ID mask)\n", val);
                break;
        default:
                ret = -ENODEV;
                pr_err("Unsupported chip ID: 0x%X\n", val);
                superio_exit();
                return ret;
        }

        superio_select(NCT6686_LD_ECSPACE);
        ec_base = superio_inb(0x60) & 0xFF;
        ec_base = (ec_base << 8) | (superio_inb(0x61) & 0xFF);
        if ((ec_base == 0xFFFF) || (ec_base == 0)) {
                pr_err("Wrong address for EC Space: CR60/61=0x%X\n", ec_base);
                ret = -ENODEV;
        }

        superio_exit();

        //Check FW Ver (default)
        if (skip_chk_fwver == 0) {
                for (i = 0; i < 8; i++) {
                        fw_ver[i] =
                                nct6686_read_value(ec_base,
                                                   NCT6686_CUS_FWVER_BASE + i);
                }
                fw_ver[8] = 0;

                switch (ret) {
                case nct6686dl:
                        //check for lenovo nano
                        if (strncmp(fw_ver, "M2ACT", 5) == 0) {
                                ret = nct6686dl_nano;
                        } else {
                                ret = -ENODEV;
                                pr_err("Unsupported FW Ver: 0x%s\n", fw_ver);
                        }
                        break;
                default:
                        ret = -ENODEV;
                        pr_err("Unsupported FW Ver: 0x%s\n", fw_ver);
                        break;
                }
        }

        return ret;
}

static int __init wdt_init(void)
{
        int ret;
        int chip;
        static const char *const chip_name[] = {
                "NUC6686D_NANO",
        };

        pr_info("WDT driver init...\n");

        wdt_io = 0x4e;
        chip = wdt_find(wdt_io);
        if (chip < 0) {
                wdt_io = 0x2e;
                chip = wdt_find(wdt_io);
                if (chip < 0)
                        return chip;
        }

        pr_info("WDT driver for %s(port:0x%X Super I/O chip initialising, ec_base=%X)\n",
                chip_name[chip], wdt_io, ec_base);

        watchdog_init_timeout(&wdt_dev, timeout, NULL);
        watchdog_set_nowayout(&wdt_dev, nowayout);
        watchdog_stop_on_reboot(&wdt_dev);

        ret = nct6686_init(&wdt_dev, chip);
        if (ret) {
                pr_err("failed to initialize watchdog (err=%d)\n", ret);
                return ret;
        }

        ret = watchdog_register_device(&wdt_dev);
        if (ret)
                return ret;

        pr_info("initialized. timeout=%d sec (nowayout=%d)\n",
                wdt_dev.timeout, nowayout);

        return ret;
}

static void __exit wdt_exit(void)
{
        if (debug != 0) {
                pr_info("nuv:wdt_exit()\n");
        }
        watchdog_unregister_device(&wdt_dev);
}

module_init(wdt_init);
module_exit(wdt_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pádraig  Brady <P@draigBrady.com>");
MODULE_AUTHOR("Sheng-Yuan Huang<syhuang3@nuvoton.com>");
MODULE_DESCRIPTION("nct6686 WDT driver for customized WDT");
