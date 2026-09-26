#ifndef _BASE_DEBUG_H_
#define _BASE_DEBUG_H_

#include <linux/debugfs.h>
#include <cvi_mmf_log.h>

extern u32 base_log_lv;

#define CVI_BASE_DBG_ERR        0x1   /* error conditions                     */
#define CVI_BASE_DBG_WARN       0x2   /* warning conditions                   */
#define CVI_BASE_DBG_NOTICE     0x4   /* normal but significant condition     */
#define CVI_BASE_DBG_INFO       0x8  /* informational                        */
#define CVI_BASE_DBG_DEBUG      0x10   /* debug-level messages                 */



#define CVI_TRACE_BASE(level, fmt, ...) \
	do { \
		if (cvi_mmf_log_enabled(CVI_MMF_LOG_BASE, \
			(level == CVI_BASE_DBG_ERR) ? 1 : \
			(level == CVI_BASE_DBG_WARN) ? 2 : \
			(level == CVI_BASE_DBG_NOTICE) ? 3 : \
			(level == CVI_BASE_DBG_INFO) ? 4 : 5)) { \
			if (level == CVI_BASE_DBG_ERR) \
				pr_err("%s:%d(): " fmt, __func__, __LINE__, ##__VA_ARGS__); \
			else if (level == CVI_BASE_DBG_WARN) \
				pr_warn("%s:%d(): " fmt, __func__, __LINE__, ##__VA_ARGS__); \
			else if (level == CVI_BASE_DBG_NOTICE) \
				pr_notice("%s:%d(): " fmt, __func__, __LINE__, ##__VA_ARGS__); \
			else if (level == CVI_BASE_DBG_INFO) \
				pr_info("%s:%d(): " fmt, __func__, __LINE__, ##__VA_ARGS__); \
			else if (level == CVI_BASE_DBG_DEBUG) \
				printk(KERN_DEBUG "%s:%d(): " fmt, __func__, __LINE__, ##__VA_ARGS__); \
		} \
	} while (0)

#endif /* _BASE_DEBUG_H_ */
