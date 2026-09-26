/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __CVI_PLATFORM_COMPAT_H__
#define __CVI_PLATFORM_COMPAT_H__

#include <linux/platform_device.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 17, 0)
#define PDE_DATA(_inode) pde_data(_inode)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
#define CVI_CLASS_CONST const
#define CVI_CLASS_CREATE(_name) class_create(_name)
#define CVI_DEFINE_SEMAPHORE(_name) DEFINE_SEMAPHORE(_name, 1)
#else
#define CVI_CLASS_CONST
#define CVI_CLASS_CREATE(_name) class_create(THIS_MODULE, _name)
#define CVI_DEFINE_SEMAPHORE(_name) DEFINE_SEMAPHORE(_name)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 13, 0)
/* secid is only consumed by the pre-4.10 signal API in this driver. */
#define CVI_SECURITY_GETSECID(_secid) (*(_secid) = 0)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 17, 0)
#define CVI_SECURITY_GETSECID(_secid) security_current_getsecid_subj(_secid)
#else
#define CVI_SECURITY_GETSECID(_secid) \
	security_task_getsecid_subj(current, _secid)
#endif

/* platform_driver::remove changed from int to void in Linux 6.11. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
#define CVI_DEFINE_PLATFORM_REMOVE_WRAPPER(_remove) \
	static void _remove##_compat(struct platform_device *pdev) \
	{ \
		(void)_remove(pdev); \
	}
#define CVI_PLATFORM_REMOVE_CALLBACK(_remove) _remove##_compat
#else
#define CVI_DEFINE_PLATFORM_REMOVE_WRAPPER(_remove)
#define CVI_PLATFORM_REMOVE_CALLBACK(_remove) _remove
#endif

#endif
