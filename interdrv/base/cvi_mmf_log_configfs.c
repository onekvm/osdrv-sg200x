#include <linux/configfs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <cvi_mmf_log.h>

static unsigned int levels[CVI_MMF_LOG_COUNT] = { 1, 3, 2, 2, 2, 1, 1 };
static DEFINE_MUTEX(levels_lock);

bool cvi_mmf_log_enabled(enum cvi_mmf_log_module module, unsigned int level)
{
	if (module >= CVI_MMF_LOG_COUNT || level > 5 || !level)
		return false;
	return level <= READ_ONCE(levels[module]);
}
EXPORT_SYMBOL_GPL(cvi_mmf_log_enabled);

#define MMF_LEVEL_ATTR(name, index) \
static ssize_t mmf_##name##_show(struct config_item *item, char *page) \
{ \
	return sysfs_emit(page, "%u\n", READ_ONCE(levels[index])); \
} \
static ssize_t mmf_##name##_store(struct config_item *item, \
		const char *page, size_t count) \
{ \
	unsigned int value; \
	int ret = kstrtouint(page, 10, &value); \
	if (ret) \
		return ret; \
	if (value > 5) \
		return -ERANGE; \
	WRITE_ONCE(levels[index], value); \
	return count; \
} \
CONFIGFS_ATTR(mmf_, name)

MMF_LEVEL_ATTR(base, CVI_MMF_LOG_BASE);
MMF_LEVEL_ATTR(vi, CVI_MMF_LOG_VI);
MMF_LEVEL_ATTR(vpss, CVI_MMF_LOG_VPSS);
MMF_LEVEL_ATTR(vo, CVI_MMF_LOG_VO);
MMF_LEVEL_ATTR(dwa, CVI_MMF_LOG_DWA);
MMF_LEVEL_ATTR(venc, CVI_MMF_LOG_VENC);
MMF_LEVEL_ATTR(vpu, CVI_MMF_LOG_VPU);

static ssize_t mmf_all_show(struct config_item *item, char *page)
{
	return sysfs_emit(page, "write 0..5 to set all kernel MMF log levels\n");
}

static ssize_t mmf_all_store(struct config_item *item, const char *page,
			     size_t count)
{
	unsigned int value;
	int i, ret = kstrtouint(page, 10, &value);

	if (ret)
		return ret;
	if (value > 5)
		return -ERANGE;
	mutex_lock(&levels_lock);
	for (i = 0; i < CVI_MMF_LOG_COUNT; ++i)
		WRITE_ONCE(levels[i], value);
	mutex_unlock(&levels_lock);
	return count;
}
CONFIGFS_ATTR(mmf_, all);

static struct configfs_attribute *mmf_attrs[] = {
	&mmf_attr_all, &mmf_attr_base, &mmf_attr_vi,
	&mmf_attr_vpss, &mmf_attr_vo, &mmf_attr_dwa,
	&mmf_attr_venc, &mmf_attr_vpu, NULL,
};

static const struct config_item_type mmf_type = {
	.ct_attrs = mmf_attrs,
	.ct_owner = THIS_MODULE,
};

static struct configfs_subsystem mmf_subsys = {
	.su_group = {
		.cg_item = {
			.ci_namebuf = "cvitek_mmf",
			.ci_type = &mmf_type,
		},
	},
};

int cvi_mmf_log_configfs_init(void)
{
	config_group_init(&mmf_subsys.su_group);
	mutex_init(&mmf_subsys.su_mutex);
	return configfs_register_subsystem(&mmf_subsys);
}

void cvi_mmf_log_configfs_exit(void)
{
	configfs_unregister_subsystem(&mmf_subsys);
}
