/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Thread end device on the esp-sdr 802.15.4 software radio. OpenThread runs
 * the network and its shell (`ot ...`); this only brings up USB.
 */

#include <zephyr/app_version.h>
#include <zephyr/kernel.h>

#include "app_cpu.h"
#include "app_usb.h"

int main(void)
{
	printk("thread %s\n", APP_VERSION_STRING);
	/* Before USB starts: its controller interrupt lands on the CPU that allocates it. */
	app_cpu_pin_system_threads(0);
	if (app_usb_init() != 0) {
		printk("thread: usb init failed\n");
	}
	return 0;
}
