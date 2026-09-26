#ifndef _CVI_MMF_LOG_H_
#define _CVI_MMF_LOG_H_

#include <linux/types.h>

enum cvi_mmf_log_module {
	CVI_MMF_LOG_BASE,
	CVI_MMF_LOG_VI,
	CVI_MMF_LOG_VPSS,
	CVI_MMF_LOG_VO,
	CVI_MMF_LOG_DWA,
	CVI_MMF_LOG_VENC,
	CVI_MMF_LOG_VPU,
	CVI_MMF_LOG_COUNT,
};

/* Kernel printk severity: 0=off, 1=error, 2=warning, 3=notice,
 * 4=information, 5=debug. Configured in /sys/kernel/config/cvitek_mmf/.
 */
bool cvi_mmf_log_enabled(enum cvi_mmf_log_module module, unsigned int level);

#endif
