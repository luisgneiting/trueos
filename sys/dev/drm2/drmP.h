/**
 * \file drmP.h
 * Private header for Direct Rendering Manager
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * Copyright (c) 2009-2010, Code Aurora Forum.
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifndef _DRM_P_H_
#define _DRM_P_H_

#if defined(_KERNEL) || defined(__KERNEL__)

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/sglist.h>
#include <sys/stat.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/fcntl.h>
#include <sys/uio.h>
#include <sys/filio.h>
#include <sys/rwlock.h>
#include <sys/selinfo.h>
#include <sys/sysctl.h>
#include <sys/bus.h>
#include <sys/queue.h>
#include <sys/signalvar.h>
#include <sys/pciio.h>
#include <sys/poll.h>
#include <sys/sbuf.h>
#include <sys/taskqueue.h>
#include <sys/tree.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vm_param.h>
#include <vm/vm_phys.h>
#include <machine/bus.h>
#include <machine/resource.h>
#if defined(__i386__) || defined(__amd64__)
#include <machine/specialreg.h>
#endif
#include <machine/sysarch.h>
#include <sys/endian.h>
#include <sys/mman.h>
#include <sys/rman.h>
#include <sys/memrange.h>
#include <dev/agp/agpvar.h>
#include <sys/agpio.h>
#include <sys/mutex.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <sys/selinfo.h>
#include <sys/bus.h>

#include <linux/io.h>
#include <linux/pci.h>
#include <linux/idr.h>
#include <linux/string.h>
#include <linux/compat.h>
#include <linux/fs.h>
#include <linux/gfp.h>
#include <linux/pm.h>

/*
 * CEM: drm.h brings in drm_os_freebsd.h, which brings in linuxkpi list.h.
 * drm_gem_names and drm_hashtab use FreeBSD queue(9) macros.  Include them
 * first so they get the BSD macros.
 *
 * We probably can (and should) drop drm_hashtab.h entirely and diff-reduce
 * drm_hashtab.c to use the Linux list implementation.
 */

#include <dev/drm2/drm_hashtab.h>

#include <dev/drm2/drm.h>

#include <linux/device.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/i2c.h>

#include <dev/drm2/drm_mm.h>

#include "opt_compat.h"
#include "opt_drm.h"
#include "opt_syscons.h"

#ifdef DRM_DEBUG
#undef DRM_DEBUG
#define DRM_DEBUG_DEFAULT_ON 1
#endif /* DRM_DEBUG */


#define __OS_HAS_AGP (defined(CONFIG_AGP) || (defined(CONFIG_AGP_MODULE) && defined(MODULE)))
#define __OS_HAS_MTRR (defined(CONFIG_MTRR))

#undef DRM_LINUX
#define DRM_LINUX 0

/* place holders */
struct dma_buf { };
struct dma_buf_attachment {
	struct dma_buf *dmabuf;
};

struct drm_file;
struct drm_device;
struct drm_agp_head;
struct drm_local_map;
struct drm_device_dma;
struct drm_dma_handle;
struct drm_gem_object;

struct device_node;
struct videomode;
struct reservation_object;

/*
 * 4 debug categories are defined:
 *
 * CORE: Used in the generic drm code: drm_ioctl.c, drm_mm.c, drm_memory.c, ...
 *	 This is the category used by the DRM_DEBUG() macro.
 *
 * DRIVER: Used in the vendor specific part of the driver: i915, radeon, ...
 *	   This is the category used by the DRM_DEBUG_DRIVER() macro.
 *
 * KMS: used in the modesetting code.
 *	This is the category used by the DRM_DEBUG_KMS() macro.
 *
 * PRIME: used in the prime code.
 *	  This is the category used by the DRM_DEBUG_PRIME() macro.
 *
 * ATOMIC: used in the atomic code.
 *	  This is the category used by the DRM_DEBUG_ATOMIC() macro.
 *
 * VBL: used for verbose debug message in the vblank code
 *	  This is the category used by the DRM_DEBUG_VBL() macro.
 *
 * Enabling verbose debug messages is done through the drm.debug parameter,
 * each category being enabled by a bit.
 *
 * drm.debug=0x1 will enable CORE messages
 * drm.debug=0x2 will enable DRIVER messages
 * drm.debug=0x3 will enable CORE and DRIVER messages
 * ...
 * drm.debug=0x3f will enable all messages
 *
 * An interesting feature is that it's possible to enable verbose logging at
 * run-time by echoing the debug value in its sysfs node:
 *   # echo 0xf > /sys/module/drm/parameters/debug
 */
#define DRM_UT_CORE 		0x01
#define DRM_UT_DRIVER		0x02
#define DRM_UT_KMS		0x04
#define DRM_UT_PRIME		0x08
#define DRM_UT_ATOMIC		0x10
#define DRM_UT_VBL		0x20

extern __printf(2, 3)
void drm_ut_debug_printk(const char *function_name,
			 const char *format, ...);
extern __printf(1, 2)
void drm_err(const char *format, ...);

/***********************************************************************/
/** \name DRM template customization defaults */
/*@{*/

/* driver capabilities and requirements mask */
#define DRIVER_USE_AGP			0x1
#define DRIVER_PCI_DMA			0x8
#define DRIVER_SG			0x10
#define DRIVER_HAVE_DMA			0x20
#define DRIVER_HAVE_IRQ			0x40
#define DRIVER_IRQ_SHARED		0x80
#define DRIVER_GEM			0x1000
#define DRIVER_MODESET			0x2000
#define DRIVER_PRIME			0x4000
#define DRIVER_RENDER			0x8000
#define DRIVER_ATOMIC			0x10000
#define DRIVER_KMS_LEGACY_CONTEXT	0x20000

/***********************************************************************/
/** \name Macros to make printk easier */
/*@{*/

/**
 * Error output.
 *
 * \param fmt printf() like format string.
 * \param arg arguments
 */
#define DRM_ERROR(fmt, ...)				\
	drm_err(fmt, ##__VA_ARGS__)

/**
 * Rate limited error output.  Like DRM_ERROR() but won't flood the log.
 *
 * \param fmt printf() like format string.
 * \param arg arguments
 */
#define DRM_ERROR_RATELIMITED(fmt, ...)				\
({									\
	static DEFINE_RATELIMIT_STATE(_rs,				\
				      DEFAULT_RATELIMIT_INTERVAL,	\
				      DEFAULT_RATELIMIT_BURST);		\
									\
	if (__ratelimit(&_rs))						\
		drm_err(fmt, ##__VA_ARGS__);				\
})

#define DRM_INFO(fmt, ...)				\
	printk(KERN_INFO "[" DRM_NAME "] " fmt, ##__VA_ARGS__)

#define DRM_INFO_ONCE(fmt, ...)				\
	printk_once(KERN_INFO "[" DRM_NAME "] " fmt, ##__VA_ARGS__)

/**
 * Debug output.
 *
 * \param fmt printf() like format string.
 * \param arg arguments
 */
#define DRM_DEBUG(fmt, args...)						\
	do {								\
		if (unlikely(drm_debug & DRM_UT_CORE))			\
			drm_ut_debug_printk(__func__, fmt, ##args);	\
	} while (0)

#define DRM_DEBUG_DRIVER(fmt, args...)					\
	do {								\
		if (unlikely(drm_debug & DRM_UT_DRIVER))		\
			drm_ut_debug_printk(__func__, fmt, ##args);	\
	} while (0)
#define DRM_DEBUG_KMS(fmt, args...)					\
	do {								\
		if (unlikely(drm_debug & DRM_UT_KMS))			\
			drm_ut_debug_printk(__func__, fmt, ##args);	\
	} while (0)
#define DRM_DEBUG_PRIME(fmt, args...)					\
	do {								\
		if (unlikely(drm_debug & DRM_UT_PRIME))			\
			drm_ut_debug_printk(__func__, fmt, ##args);	\
	} while (0)
#define DRM_DEBUG_ATOMIC(fmt, args...)					\
	do {								\
		if (unlikely(drm_debug & DRM_UT_ATOMIC))		\
			drm_ut_debug_printk(__func__, fmt, ##args);	\
	} while (0)
#define DRM_DEBUG_VBL(fmt, args...)					\
	do {								\
		if (unlikely(drm_debug & DRM_UT_VBL))			\
			drm_ut_debug_printk(__func__, fmt, ##args);	\
	} while (0)

/*@}*/

/***********************************************************************/
/** \name Internal types and structures */
/*@{*/

#define DRM_IF_VERSION(maj, min) (maj << 16 | min)

/**
 * Ioctl function type.
 *
 * \param inode device inode.
 * \param file_priv DRM file private pointer.
 * \param cmd command.
 * \param arg argument.
 */
typedef int drm_ioctl_t(struct drm_device *dev, void *data,
			struct drm_file *file_priv);

#define DRM_IOCTL_NR(n)                ((n) & 0xff)
#define DRM_MAJOR       226

#define DRM_AUTH	0x1
#define	DRM_MASTER	0x2
#define DRM_ROOT_ONLY	0x4
#define DRM_CONTROL_ALLOW 0x8
#define DRM_UNLOCKED	0x10
#define DRM_RENDER_ALLOW 0x20

struct drm_ioctl_desc {
	unsigned long cmd;
	int flags;
	drm_ioctl_t *func;
	unsigned int cmd_drv;
};

/**
 * Creates a driver or general drm_ioctl_desc array entry for the given
 * ioctl, for use by drm_ioctl().
 */

#define DRM_IOCTL_DEF(ioctl, _func, _flags) \
	[DRM_IOCTL_NR(ioctl)] = {.cmd = ioctl, .func = _func, .flags = _flags, .cmd_drv = 0}

#define DRM_IOCTL_DEF_DRV(ioctl, _func, _flags)			\
	[DRM_IOCTL_NR(DRM_##ioctl)] = {.cmd = DRM_##ioctl, .func = _func, .flags = _flags, .cmd_drv = DRM_IOCTL_##ioctl}

/* Event queued up for userspace to read */
struct drm_pending_event {
	struct drm_event *event;
	struct list_head link;
	struct list_head pending_link;
	struct drm_file *file_priv;
	pid_t pid; /* pid of requester, no guarantee it's valid by the time
		      we deliver the event, for tracing only */
	void (*destroy)(struct drm_pending_event *event);
};

/* initial implementaton using a linked list - todo hashtab */
struct drm_prime_file_private {
	struct list_head head;
	struct mutex lock;
};

/** File private data */
struct drm_file {
	unsigned authenticated :1;
	/* Whether we're master for a minor. Protected by master_mutex */
	unsigned is_master :1;
	/* true when the client has asked us to expose stereo 3D mode flags */
	unsigned stereo_allowed :1;
	/*
	 * true if client understands CRTC primary planes and cursor planes
	 * in the plane list
	 */
	unsigned universal_planes:1;
	/* true if client understands atomic properties */
	unsigned atomic:1;
	/*
	 * This client is allowed to gain master privileges for @master.
	 * Protected by struct drm_device::master_mutex.
	 */
	unsigned allowed_master:1;
	pid_t pid;
	uid_t uid;
	drm_magic_t magic;
	unsigned long ioctl_count;
	struct list_head lhead;
	struct drm_minor *minor;
	unsigned long lock_count;

	/** Mapping of mm object handles to object pointers. */
	struct idr object_idr;
	/** Lock for synchronization of access to object_idr. */
	spinlock_t table_lock;

	struct file *filp;
	void *driver_priv;

	struct drm_master *master; /* master this node is currently associated with
				      N.B. not always minor->master */
	/**
	 * fbs - List of framebuffers associated with this file.
	 *
	 * Protected by fbs_lock. Note that the fbs list holds a reference on
	 * the fb object to prevent it from untimely disappearing.
	 */
	struct list_head fbs;
	struct mutex fbs_lock;

	struct selinfo event_poll;
	/** User-created blob properties; this retains a reference on the
	 *  property. */
	struct list_head blobs;

	wait_queue_head_t event_wait;
	struct list_head pending_event_list;
	struct list_head event_list;
	int event_space;

	struct mutex event_read_lock;

	struct drm_prime_file_private prime;
};

/**
 * Lock data.
 */
struct drm_lock_data {
	struct drm_hw_lock *hw_lock;	/**< Hardware lock */
	/** Private of lock holder's file (NULL=kernel) */
	struct drm_file *file_priv;
	wait_queue_head_t lock_queue;	/**< Queue of blocked processes */
	unsigned long lock_time;	/**< Time of last lock in jiffies */
	spinlock_t spinlock;
	uint32_t kernel_waiters;
	uint32_t user_waiters;
	int idle_has_lock;
};

/**
 * struct drm_master - drm master structure
 *
 * @refcount: Refcount for this master object.
 * @minor: Link back to minor char device we are master for. Immutable.
 * @unique: Unique identifier: e.g. busid. Protected by drm_global_mutex.
 * @unique_len: Length of unique field. Protected by drm_global_mutex.
 * @magic_map: Map of used authentication tokens. Protected by struct_mutex.
 * @lock: DRI lock information.
 * @driver_priv: Pointer to driver-private information.
 */
struct drm_master {
	struct kref  refcount; /* refcount for this master */
	struct drm_minor *minor; /**< link back to minor we are a master for */
	char *unique;			/**< Unique identifier: e.g., busid */
	int unique_len;			/**< Length of unique field */
	struct drm_lock_data lock;
	void *driver_priv;

	struct list_head head; /**< each minor contains a list of masters */

	int blocked;			/**< Blocked due to VC switch? */

	/** \name Authentication */
	/*@{ */
	struct drm_open_hash magiclist;
	struct list_head magicfree;
	/*@} */

};

/* Size of ringbuffer for vblank timestamps. Just double-buffer
 * in initial implementation.
 */
#define DRM_VBLANKTIME_RBSIZE 2

/* Flags and return codes for get_vblank_timestamp() driver function. */
#define DRM_CALLED_FROM_VBLIRQ 1
#define DRM_VBLANKTIME_SCANOUTPOS_METHOD (1 << 0)
#define DRM_VBLANKTIME_IN_VBLANK         (1 << 1)

/* get_scanout_position() return flags */
#define DRM_SCANOUTPOS_VALID        (1 << 0)
#define DRM_SCANOUTPOS_IN_VBLANK    (1 << 1)
#define DRM_SCANOUTPOS_ACCURATE     (1 << 2)

/**
 * DRM driver structure. This structure represent the common code for
 * a family of cards. There will one drm_device for each card present
 * in this family
 */
struct drm_driver {
	int (*load) (struct drm_device *, unsigned long flags);
	int (*firstopen) (struct drm_device *);
	int (*open) (struct drm_device *, struct drm_file *);
	void (*preclose) (struct drm_device *, struct drm_file *file_priv);
	void (*postclose) (struct drm_device *, struct drm_file *);
	void (*lastclose) (struct drm_device *);
	int (*unload) (struct drm_device *);
	int (*suspend) (struct drm_device *, pm_message_t state);
	int (*resume) (struct drm_device *);
	int (*dma_ioctl) (struct drm_device *dev, void *data, struct drm_file *file_priv);
	int (*dma_quiescent) (struct drm_device *);
	int (*context_dtor) (struct drm_device *dev, int context);
	int (*set_busid)(struct drm_device *dev, struct drm_master *master);

	/**
	 * get_vblank_counter - get raw hardware vblank counter
	 * @dev: DRM device
	 * @pipe: counter to fetcho
	 *
	 * Driver callback for fetching a raw hardware vblank counter for @crtc.
	 * If a device doesn't have a hardware counter, the driver can simply
	 * return the value of drm_vblank_count. The DRM core will account for
	 * missed vblank events while interrupts where disabled based on system
	 * timestamps.
	 *
	 * Wraparound handling and loss of events due to modesetting is dealt
	 * with in the DRM core code.
	 *
	 * RETURNS
	 * Raw vblank counter value.
	 */
	u32 (*get_vblank_counter) (struct drm_device *dev, unsigned int pipe);

	/**
	 * enable_vblank - enable vblank interrupt events
	 * @dev: DRM device
	 * @pipe: which irq to enable
	 *
	 * Enable vblank interrupts for @crtc.  If the device doesn't have
	 * a hardware vblank counter, this routine should be a no-op, since
	 * interrupts will have to stay on to keep the count accurate.
	 *
	 * RETURNS
	 * Zero on success, appropriate errno if the given @crtc's vblank
	 * interrupt cannot be enabled.
	 */
	int (*enable_vblank) (struct drm_device *dev, unsigned int pipe);

	/**
	 * disable_vblank - disable vblank interrupt events
	 * @dev: DRM device
	 * @pipe: which irq to enable
	 *
	 * Disable vblank interrupts for @crtc.  If the device doesn't have
	 * a hardware vblank counter, this routine should be a no-op, since
	 * interrupts will have to stay on to keep the count accurate.
	 */
	void (*disable_vblank) (struct drm_device *dev, unsigned int pipe);

	/**
	 * Called by \c drm_device_is_agp.  Typically used to determine if a
	 * card is really attached to AGP or not.
	 *
	 * \param dev  DRM device handle
	 *
	 * \returns
	 * One of three values is returned depending on whether or not the
	 * card is absolutely \b not AGP (return of 0), absolutely \b is AGP
	 * (return of 1), or may or may not be AGP (return of 2).
	 */
	int (*device_is_agp) (struct drm_device *dev);

	/**
	 * Called by vblank timestamping code.
	 *
	 * Return the current display scanout position from a crtc, and an
	 * optional accurate ktime_get timestamp of when position was measured.
	 *
	 * \param dev  DRM device.
	 * \param pipe Id of the crtc to query.
	 * \param flags Flags from the caller (DRM_CALLED_FROM_VBLIRQ or 0).
	 * \param *vpos Target location for current vertical scanout position.
	 * \param *hpos Target location for current horizontal scanout position.
	 * \param *stime Target location for timestamp taken immediately before
	 *               scanout position query. Can be NULL to skip timestamp.
	 * \param *etime Target location for timestamp taken immediately after
	 *               scanout position query. Can be NULL to skip timestamp.
	 * \param mode Current display timings.
	 *
	 * Returns vpos as a positive number while in active scanout area.
	 * Returns vpos as a negative number inside vblank, counting the number
	 * of scanlines to go until end of vblank, e.g., -1 means "one scanline
	 * until start of active scanout / end of vblank."
	 *
	 * \return Flags, or'ed together as follows:
	 *
	 * DRM_SCANOUTPOS_VALID = Query successful.
	 * DRM_SCANOUTPOS_INVBL = Inside vblank.
	 * DRM_SCANOUTPOS_ACCURATE = Returned position is accurate. A lack of
	 * this flag means that returned position may be offset by a constant
	 * but unknown small number of scanlines wrt. real scanout position.
	 *
	 */
	int (*get_scanout_position) (struct drm_device *dev, unsigned int pipe,
				     unsigned int flags, int *vpos, int *hpos,
				     ktime_t *stime, ktime_t *etime,
				     const struct drm_display_mode *mode);

	/**
	 * Called by \c drm_get_last_vbltimestamp. Should return a precise
	 * timestamp when the most recent VBLANK interval ended or will end.
	 *
	 * Specifically, the timestamp in @vblank_time should correspond as
	 * closely as possible to the time when the first video scanline of
	 * the video frame after the end of VBLANK will start scanning out,
	 * the time immediately after end of the VBLANK interval. If the
	 * @crtc is currently inside VBLANK, this will be a time in the future.
	 * If the @crtc is currently scanning out a frame, this will be the
	 * past start time of the current scanout. This is meant to adhere
	 * to the OpenML OML_sync_control extension specification.
	 *
	 * \param dev dev DRM device handle.
	 * \param pipe crtc for which timestamp should be returned.
	 * \param *max_error Maximum allowable timestamp error in nanoseconds.
	 *                   Implementation should strive to provide timestamp
	 *                   with an error of at most *max_error nanoseconds.
	 *                   Returns true upper bound on error for timestamp.
	 * \param *vblank_time Target location for returned vblank timestamp.
	 * \param flags 0 = Defaults, no special treatment needed.
	 * \param       DRM_CALLED_FROM_VBLIRQ = Function is called from vblank
	 *	        irq handler. Some drivers need to apply some workarounds
	 *              for gpu-specific vblank irq quirks if flag is set.
	 *
	 * \returns
	 * Zero if timestamping isn't supported in current display mode or a
	 * negative number on failure. A positive status code on success,
	 * which describes how the vblank_time timestamp was computed.
	 */
	int (*get_vblank_timestamp) (struct drm_device *dev, unsigned int pipe,
				     int *max_error,
				     struct timeval *vblank_time,
				     unsigned flags);

	/* these have to be filled in */

	irqreturn_t(*irq_handler) (DRM_IRQ_ARGS);
	void (*irq_preinstall) (struct drm_device *dev);
	int (*irq_postinstall) (struct drm_device *dev);
	void (*irq_uninstall) (struct drm_device *dev);

	/* Master routines */
	int (*master_create)(struct drm_device *dev, struct drm_master *master);
	void (*master_destroy)(struct drm_device *dev, struct drm_master *master);
	/**
	 * master_set is called whenever the minor master is set.
	 * master_drop is called whenever the minor master is dropped.
	 */

	int (*master_set)(struct drm_device *dev, struct drm_file *file_priv,
			  bool from_open);
	void (*master_drop)(struct drm_device *dev, struct drm_file *file_priv,
			    bool from_release);

	/**
	 * Driver-specific constructor for drm_gem_objects, to set up
	 * obj->driver_private.
	 *
	 * Returns 0 on success.
	 */
	void (*gem_free_object) (struct drm_gem_object *obj);
	int (*gem_open_object) (struct drm_gem_object *, struct drm_file *);
	void (*gem_close_object) (struct drm_gem_object *, struct drm_file *);

	/**
	 * Hook for allocating the GEM object struct, for use by core
	 * helpers.
	 */
	struct drm_gem_object *(*gem_create_object)(struct drm_device *dev,
						    size_t size);

#ifdef FREEBSD_NOTYET
	/* prime: */
	/* export handle -> fd (see drm_gem_prime_handle_to_fd() helper) */
	int (*prime_handle_to_fd)(struct drm_device *dev, struct drm_file *file_priv,
				uint32_t handle, uint32_t flags, int *prime_fd);
	/* import fd -> handle (see drm_gem_prime_fd_to_handle() helper) */
	int (*prime_fd_to_handle)(struct drm_device *dev, struct drm_file *file_priv,
				int prime_fd, uint32_t *handle);
	/* export GEM -> dmabuf */
	struct dma_buf * (*gem_prime_export)(struct drm_device *dev,
				struct drm_gem_object *obj, int flags);
	/* import dmabuf -> GEM */
	struct drm_gem_object * (*gem_prime_import)(struct drm_device *dev,
				struct dma_buf *dma_buf);
#endif /* defined(FREEBSD_NOTYET) */

	/* low-level interface used by drm_gem_prime_{import,export} */
	int (*gem_prime_pin)(struct drm_gem_object *obj);
	void (*gem_prime_unpin)(struct drm_gem_object *obj);
	struct reservation_object * (*gem_prime_res_obj)(
				struct drm_gem_object *obj);
	struct sg_table *(*gem_prime_get_sg_table)(struct drm_gem_object *obj);
	struct drm_gem_object *(*gem_prime_import_sg_table)(
				struct drm_device *dev,
				struct dma_buf_attachment *attach,
				struct sg_table *sgt);
	void *(*gem_prime_vmap)(struct drm_gem_object *obj);
	void (*gem_prime_vunmap)(struct drm_gem_object *obj, void *vaddr);
	int (*gem_prime_mmap)(struct drm_gem_object *obj,
				struct vm_area_struct *vma);

	/* vga arb irq handler */
	void (*vgaarb_irq)(struct drm_device *dev, bool state);

	
	/* dumb alloc support */
	int (*dumb_create)(struct drm_file *file_priv,
			   struct drm_device *dev,
			   struct drm_mode_create_dumb *args);
	int (*dumb_map_offset)(struct drm_file *file_priv,
			       struct drm_device *dev, uint32_t handle,
			       uint64_t *offset);
	int (*dumb_destroy)(struct drm_file *file_priv,
			    struct drm_device *dev,
			    uint32_t handle);

	/* Driver private ops for this object */
	struct cdev_pager_ops *gem_pager_ops;

	int	(*sysctl_init)(struct drm_device *dev,
		    struct sysctl_ctx_list *ctx, struct sysctl_oid *top);
	void	(*sysctl_cleanup)(struct drm_device *dev);

	int major;
	int minor;
	int patchlevel;
	char *name;
	char *desc;
	char *date;

	u32 driver_features;
	int dev_priv_size;
	struct drm_ioctl_desc *ioctls;
	int num_ioctls;
	const struct file_operations *fops;

	/* List of devices hanging off this driver with stealth attach. */
	struct list_head legacy_dev_list;
};

enum drm_minor_type {
	DRM_MINOR_LEGACY,
	DRM_MINOR_CONTROL,
	DRM_MINOR_RENDER,
	DRM_MINOR_CNT,
};

/**
 * Info file list entry. This structure represents a debugfs or proc file to
 * be created by the drm core
 */
struct drm_info_list {
	const char *name; /** file name */
	int (*show)(struct seq_file*, void*); /** show callback */
	u32 driver_features; /**< Required driver features for this entry */
	void *data;
};

/**
 * debugfs node structure. This structure represents a debugfs file.
 */
struct drm_info_node {
	struct list_head list;
	struct drm_minor *minor;
	const struct drm_info_list *info_ent;
	struct dentry *dent;
};

/**
 * DRM minor structure. This structure represents a drm minor number.
 */
struct drm_minor {
	int index;			/**< Minor device number */
	int type;                       /**< Control or render */
	dev_t device;			/**< Linux's Device number for mknod */
	struct cdev *bsd_device;		/**< Device number for mknod */
	struct device kdev;			/**< Linux device */
	device_t bsd_kdev;			/**< OS device */
	struct drm_device *dev;

	struct drm_master *master; /* currently active master for this node */
	struct list_head master_list;
	struct drm_mode_group mode_group;

	struct sigio *buf_sigio;	/* Processes waiting for SIGIO     */
};

/* mode specified on the command line */
struct drm_cmdline_mode {
	bool specified;
	bool refresh_specified;
	bool bpp_specified;
	int xres, yres;
	int bpp;
	int refresh;
	bool rb;
	bool interlace;
	bool cvt;
	bool margins;
	enum drm_connector_force force;
};


struct drm_pending_vblank_event {
	struct drm_pending_event base;
	unsigned int pipe;
	struct drm_event_vblank event;
};

struct drm_vblank_crtc {
	struct drm_device *dev;		/* pointer to the drm_device */
	wait_queue_head_t queue;	/**< VBLANK wait queue */
	struct timer_list disable_timer;		/* delayed disable timer */

	/* vblank counter, protected by dev->vblank_time_lock for writes */
	u32 count;
	/* vblank timestamps, protected by dev->vblank_time_lock for writes */
	struct timeval time[DRM_VBLANKTIME_RBSIZE];

	atomic_t refcount;		/* number of users of vblank interruptsper crtc */
	u32 last;			/* protected by dev->vbl_lock, used */
					/* for wraparound handling */
	u32 last_wait;			/* Last vblank seqno waited per CRTC */
	unsigned int inmodeset;		/* Display driver is setting mode */
	unsigned int pipe;		/* crtc index */
	int framedur_ns;		/* frame/field duration in ns */
	int linedur_ns;			/* line duration in ns */
	bool enabled;			/* so we don't call enable more than
					   once per disable */
};

/**
 * DRM device structure. This structure represent a complete card that
 * may contain multiple heads.
 */
struct drm_device {
	struct list_head legacy_dev_list;/**< list of devices per driver for stealth attach cleanup */
	int if_version;			/**< Highest interface version set */

	/** \name Lifetime Management */
	/*@{ */
	struct kref ref;		/**< Object ref-count */
	struct device *dev;		/**< Device structure of bus-device */
	struct drm_driver *driver;	/**< DRM driver managing the device */
	void *dev_private;		/**< DRM driver private data */
	struct drm_minor *control;		/**< Control node */
	struct drm_minor *primary;		/**< Primary node */
	struct drm_minor *render;		/**< Render node */
	atomic_t unplugged;			/**< Flag whether dev is dead */
	struct inode *anon_inode;		/**< inode for private address-space */
	char *unique;				/**< unique name of the device */
	/*@} */

	/** \name Locks */
	/*@{ */
	struct mutex struct_mutex;	/**< For others */
	struct mutex master_mutex;      /**< For drm_minor::master and drm_file::is_master */
	/*@} */

	/** \name Usage Counters */
	/*@{ */
	int open_count;			/**< Outstanding files open */
	spinlock_t buf_lock;		/**< For drm_device::buf_use and a few other things. */
	int buf_use;			/**< Buffers in use -- cannot alloc */
	atomic_t buf_alloc;		/**< Buffer allocation in progress */
	/*@} */

	/** \name Performance counters */
	/*@{ */
	unsigned long counters;
	enum drm_stat_type types[15];
	atomic_t counts[15];
	/*@} */

	struct list_head filelist;

	/** \name Memory management */
	/*@{ */
	struct list_head maplist;	/**< Linked list of regions */
	struct drm_open_hash map_hash;	/**< User token hash table for maps */

	/** \name Context handle management */
	/*@{ */
	struct list_head ctxlist;	/**< Linked list of context handles */
	struct mutex ctxlist_mutex;	/**< For ctxlist */

	struct idr ctx_idr;

	/*@} */

	/** \name DMA support */
	/*@{ */
	struct drm_device_dma *dma;		/**< Optional pointer for DMA support */
	/*@} */

	/** \name Context support */
	/*@{ */
	volatile long context_flag;		/**< Context swapping flag */
	int last_context;		/**< Last current context */
	/*@} */

	/** \name VBLANK IRQ support */
	/*@{ */
	bool irq_enabled;
	int irq;

	/*
	 * At load time, disabling the vblank interrupt won't be allowed since
	 * old clients may not call the modeset ioctl and therefore misbehave.
	 * Once the modeset ioctl *has* been called though, we can safely
	 * disable them when unused.
	 */
	int vblank_disable_allowed;

	/*
	 * If true, vblank interrupt will be disabled immediately when the
	 * refcount drops to zero, as opposed to via the vblank disable
	 * timer.
	 * This can be set to true it the hardware has a working vblank
	 * counter and the driver uses drm_vblank_on() and drm_vblank_off()
	 * appropriately.
	 */
	bool vblank_disable_immediate;

	/* array of size num_crtcs */
	struct drm_vblank_crtc *vblank;

	spinlock_t vblank_time_lock;    /**< Protects vblank count and time updates during vblank enable/disable */
	spinlock_t vbl_lock;

	u32 max_vblank_count;           /**< size of vblank counter register */

	/**
	 * List of events
	 */
	struct list_head vblank_event_list;
	spinlock_t event_lock;

	/*@} */

	struct drm_agp_head *agp;	/**< AGP data */

	struct pci_dev *pdev;		/**< PCI device structure */
#ifdef __alpha__
	struct pci_controller *hose;
#endif

	struct platform_device *platformdev; /**< Platform device struture */
	struct virtio_device *virtdev;

	struct drm_sg_mem *sg;	/**< Scatter gather memory */
	unsigned int num_crtcs;                  /**< Number of CRTCs on this device */

	struct {
		int context;
		struct drm_hw_lock *lock;
	} sigdata;

	struct drm_local_map *agp_buffer_map;
	unsigned int agp_buffer_token;

	struct drm_mode_config mode_config;	/**< Current mode config */

	/** \name GEM information */
	/*@{ */
	struct mutex object_name_lock;
	struct idr object_name_idr;
	/*@} */
	int switch_power_state;

	struct drm_sysctl_info *sysctl;
	int		  sysctl_node_idx;

	void		  *drm_ttm_bdev;

	void *sysctl_private;
	char busid_str[128];
	int modesetting;

	const drm_pci_id_list_t *id_entry;	/* PCI ID, name, and chipset private */
};

#define DRM_SWITCH_POWER_ON 0
#define DRM_SWITCH_POWER_OFF 1
#define DRM_SWITCH_POWER_CHANGING 2
#define DRM_SWITCH_POWER_DYNAMIC_OFF 3

static __inline__ int drm_core_check_feature(struct drm_device *dev,
					     int feature)
{
	return ((dev->driver->driver_features & feature) ? 1 : 0);
}

static inline void drm_device_set_unplugged(struct drm_device *dev)
{
	smp_wmb();
	atomic_set(&dev->unplugged, 1);
}

static inline int drm_device_is_unplugged(struct drm_device *dev)
{
	int ret = atomic_read(&dev->unplugged);
	smp_rmb();
	return ret;
}

static inline bool drm_is_render_client(const struct drm_file *file_priv)
{
	return file_priv->minor->type == DRM_MINOR_RENDER;
}

static inline bool drm_is_control_client(const struct drm_file *file_priv)
{
	return file_priv->minor->type == DRM_MINOR_CONTROL;
}

static inline bool drm_is_primary_client(const struct drm_file *file_priv)
{
	return file_priv->minor->type == DRM_MINOR_LEGACY;
}

/******************************************************************/
/** \name Internal function definitions */
/*@{*/

				/* Driver support (drm_drv.h) */
extern int drm_ioctl_permit(u32 flags, struct drm_file *file_priv);
extern long drm_ioctl(struct file *filp,
		      unsigned int cmd, unsigned long arg);
extern long drm_compat_ioctl(struct file *filp,
			     unsigned int cmd, unsigned long arg);
extern bool drm_ioctl_flags(unsigned int nr, unsigned int *flags);

/* File Operations (drm_fops.c) */
int drm_open(struct inode *inode, struct file *filp);
ssize_t drm_read(struct file *filp, char __user *buffer,
		 size_t count, loff_t *offset);
int drm_release(struct inode *inode, struct file *filp);
int drm_new_set_master(struct drm_device *dev, struct drm_file *fpriv);
unsigned int drm_poll(struct file *filp, struct poll_table_struct *wait);
int drm_event_reserve_init_locked(struct drm_device *dev,
				  struct drm_file *file_priv,
				  struct drm_pending_event *p,
				  struct drm_event *e);
int drm_event_reserve_init(struct drm_device *dev,
			   struct drm_file *file_priv,
			   struct drm_pending_event *p,
			   struct drm_event *e);
void drm_event_cancel_free(struct drm_device *dev,
			   struct drm_pending_event *p);
void drm_send_event_locked(struct drm_device *dev, struct drm_pending_event *e);
void drm_send_event(struct drm_device *dev, struct drm_pending_event *e);

/* Misc. IOCTL support (drm_ioctl.c) */
int drm_noop(struct drm_device *dev, void *data,
	     struct drm_file *file_priv);
int drm_invalid_op(struct drm_device *dev, void *data,
		   struct drm_file *file_priv);

/* Cache management (drm_cache.c) */
void drm_clflush_pages(vm_page_t *pages, unsigned long num_pages);
void drm_clflush_sg(struct sg_table *st);
void drm_clflush_virt_range(char *addr, unsigned long length);

/*
 * These are exported to drivers so that they can implement fencing using
 * DMA quiscent + idle. DMA quiescent usually requires the hardware lock.
 */

				/* IRQ support (drm_irq.h) */
extern int drm_irq_install(struct drm_device *dev, int irq);
extern int drm_irq_uninstall(struct drm_device *dev);

extern int drm_vblank_init(struct drm_device *dev, unsigned int num_crtcs);
extern int drm_wait_vblank(struct drm_device *dev, void *data,
			   struct drm_file *filp);
extern u32 drm_vblank_count(struct drm_device *dev, unsigned int pipe);
extern u32 drm_crtc_vblank_count(struct drm_crtc *crtc);
extern u32 drm_vblank_count_and_time(struct drm_device *dev, unsigned int pipe,
				     struct timeval *vblanktime);
extern u32 drm_crtc_vblank_count_and_time(struct drm_crtc *crtc,
					  struct timeval *vblanktime);
extern void drm_send_vblank_event(struct drm_device *dev, unsigned int pipe,
				  struct drm_pending_vblank_event *e);
extern void drm_crtc_send_vblank_event(struct drm_crtc *crtc,
				       struct drm_pending_vblank_event *e);
extern void drm_arm_vblank_event(struct drm_device *dev, unsigned int pipe,
				 struct drm_pending_vblank_event *e);
extern void drm_crtc_arm_vblank_event(struct drm_crtc *crtc,
				      struct drm_pending_vblank_event *e);
extern bool drm_handle_vblank(struct drm_device *dev, unsigned int pipe);
extern bool drm_crtc_handle_vblank(struct drm_crtc *crtc);
extern int drm_vblank_get(struct drm_device *dev, unsigned int pipe);
extern void drm_vblank_put(struct drm_device *dev, unsigned int pipe);
extern int drm_crtc_vblank_get(struct drm_crtc *crtc);
extern void drm_crtc_vblank_put(struct drm_crtc *crtc);
extern void drm_wait_one_vblank(struct drm_device *dev, unsigned int pipe);
extern void drm_crtc_wait_one_vblank(struct drm_crtc *crtc);
extern void drm_vblank_off(struct drm_device *dev, unsigned int pipe);
extern void drm_vblank_on(struct drm_device *dev, unsigned int pipe);
extern void drm_crtc_vblank_off(struct drm_crtc *crtc);
extern void drm_crtc_vblank_reset(struct drm_crtc *crtc);
extern void drm_crtc_vblank_on(struct drm_crtc *crtc);
extern void drm_vblank_cleanup(struct drm_device *dev);
extern u32 drm_vblank_no_hw_counter(struct drm_device *dev, unsigned int pipe);

extern int drm_calc_vbltimestamp_from_scanoutpos(struct drm_device *dev,
						 unsigned int pipe, int *max_error,
						 struct timeval *vblank_time,
						 unsigned flags,
						 const struct drm_display_mode *mode);
extern void drm_calc_timestamping_constants(struct drm_crtc *crtc,
					    const struct drm_display_mode *mode);

/**
 * drm_crtc_vblank_waitqueue - get vblank waitqueue for the CRTC
 * @crtc: which CRTC's vblank waitqueue to retrieve
 *
 * This function returns a pointer to the vblank waitqueue for the CRTC.
 * Drivers can use this to implement vblank waits using wait_event() & co.
 */
static inline wait_queue_head_t *drm_crtc_vblank_waitqueue(struct drm_crtc *crtc)
{
	return &crtc->dev->vblank[drm_crtc_index(crtc)].queue;
}

/* Modesetting support */
extern void drm_vblank_pre_modeset(struct drm_device *dev, unsigned int pipe);
extern void drm_vblank_post_modeset(struct drm_device *dev, unsigned int pipe);

				/* Stub support (drm_stub.h) */
extern struct drm_master *drm_master_get(struct drm_master *master);
extern void drm_master_put(struct drm_master **master);

extern void drm_put_dev(struct drm_device *dev);
extern void drm_unplug_dev(struct drm_device *dev);
extern unsigned int drm_debug;
extern bool drm_atomic;
extern unsigned int drm_notyet;

extern unsigned int drm_vblank_offdelay;
extern unsigned int drm_timestamp_precision;
extern unsigned int drm_timestamp_monotonic;

extern struct class *drm_class;

#ifdef FREEBSD_NOTYET
extern struct dma_buf *drm_gem_prime_export(struct drm_device *dev,
		struct drm_gem_object *obj, int flags);
extern int drm_gem_prime_handle_to_fd(struct drm_device *dev,
		struct drm_file *file_priv, uint32_t handle, uint32_t flags,
		int *prime_fd);
extern struct drm_gem_object *drm_gem_prime_import(struct drm_device *dev,
		struct dma_buf *dma_buf);
extern int drm_gem_prime_fd_to_handle(struct drm_device *dev,
		struct drm_file *file_priv, int prime_fd, uint32_t *handle);
extern void drm_gem_dmabuf_release(struct dma_buf *dma_buf);

extern int drm_prime_sg_to_page_addr_arrays(struct sg_table *sgt, struct page **pages,
					    dma_addr_t *addrs, int max_pages);
extern struct sg_table *drm_prime_pages_to_sg(struct page **pages, unsigned int nr_pages);
extern void drm_prime_gem_destroy(struct drm_gem_object *obj, struct sg_table *sg);
#endif

extern struct drm_dma_handle *drm_pci_alloc(struct drm_device *dev, size_t size,
					    size_t align);
extern void drm_pci_free(struct drm_device *dev, struct drm_dma_handle * dmah);

			       /* sysfs support (drm_sysfs.c) */
extern void drm_sysfs_hotplug_event(struct drm_device *dev);


struct drm_device *drm_dev_alloc(struct drm_driver *driver,
				 struct device *parent);
void drm_dev_ref(struct drm_device *dev);
void drm_dev_unref(struct drm_device *dev);
int drm_dev_register(struct drm_device *dev, unsigned long flags);
void drm_dev_unregister(struct drm_device *dev);
int drm_dev_set_unique(struct drm_device *dev, const char *name);

struct drm_minor *drm_minor_acquire(unsigned int minor_id);
void drm_minor_release(struct drm_minor *minor);

/*@}*/

/* PCI section */
static __inline__ int drm_pci_device_is_agp(struct drm_device *dev)
{
	if (dev->driver->device_is_agp != NULL) {
		int err = (*dev->driver->device_is_agp) (dev);

		if (err != 2) {
			return err;
		}
	}

	return pci_find_capability(dev->pdev, PCI_CAP_ID_AGP);
}
void drm_pci_agp_destroy(struct drm_device *dev);

extern int drm_pci_init(struct drm_driver *driver, struct pci_driver *pdriver);
extern void drm_pci_exit(struct drm_driver *driver, struct pci_driver *pdriver);
#ifdef CONFIG_PCI
extern int drm_get_pci_dev(struct pci_dev *pdev,
			   const struct pci_device_id *ent,
			   struct drm_driver *driver);
extern int drm_pci_set_busid(struct drm_device *dev, struct drm_master *master);
#else
static inline int drm_get_pci_dev(struct pci_dev *pdev,
				  const struct pci_device_id *ent,
				  struct drm_driver *driver)
{
	return -ENOSYS;
}

static inline int drm_pci_set_busid(struct drm_device *dev,
				    struct drm_master *master)
{
	return -ENOSYS;
}
#endif

#define DRM_PCIE_SPEED_25 1
#define DRM_PCIE_SPEED_50 2
#define DRM_PCIE_SPEED_80 4

extern int drm_pcie_get_speed_cap_mask(struct drm_device *dev, u32 *speed_mask);
extern int drm_pcie_get_max_link_width(struct drm_device *dev, u32 *mlw);

/* platform section */
extern int drm_platform_init(struct drm_driver *driver, struct platform_device *platform_device);
extern int drm_platform_set_busid(struct drm_device *d, struct drm_master *m);

/* returns true if currently okay to sleep */
static __inline__ bool drm_can_sleep(void)
{
	if (in_atomic() || in_dbg_master() || irqs_disabled())
		return false;
	return true;
}

/* helper for handling conditionals in various for_each macros */
#define for_each_if(condition) if (!(condition)) {} else

struct ttm_bo_device;
int ttm_bo_mmap_single(struct ttm_bo_device *bdev, vm_ooffset_t *offset,
    vm_size_t size, struct vm_object **obj_res, int nprot);
struct ttm_buffer_object;
void ttm_bo_release_mmap(struct ttm_buffer_object *bo);

#endif				/* __KERNEL__ */
#endif
