/* NanoKVM compatibility symbols expected by the vendor video driver. */
#include <linux/mutex.h>

static DEFINE_MUTEX(vcodec_mutex);

void vcodec_lock(void)
{
	/* The vendor VPU API has a void lock hook and unconditionally enters the
	 * critical section after this returns.  An interruptible lock cannot be
	 * used here: callers cannot observe -EINTR and will later unlock a mutex
	 * they never acquired.  This is particularly easy to trigger from a Go
	 * process because runtime asynchronous preemption uses SIGURG. */
	mutex_lock(&vcodec_mutex);
}

void vcodec_unlock(void)
{
	mutex_unlock(&vcodec_mutex);
}

int vcodec_trylock(void)
{
	return mutex_trylock(&vcodec_mutex);
}

int vcodec_is_locked(void)
{
	return mutex_is_locked(&vcodec_mutex);
}
