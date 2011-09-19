
#undef TRACE_SYSTEM
#define TRACE_SYSTEM omap

#if !defined(_TRACE_OMAP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_OMAP_H

#include <linux/ktime.h>
#include <linux/tracepoint.h>

/* The OMAP events with flags */
DECLARE_EVENT_CLASS(omapflag,

	TP_PROTO(const char *name, unsigned int state, unsigned int flags, unsigned int cpu_id),

	TP_ARGS(name, state, flags, cpu_id),

	TP_STRUCT__entry(
		__string(       name,           name            )
		__field(        u64,            state           )
		__field(        u32,            flags           )
		__field(        u64,            cpu_id          )
	),

	TP_fast_assign(
		__assign_str(name, name);
		__entry->state = state;
		__entry->flags = flags;
		__entry->cpu_id = cpu_id;
	),

	TP_printk("%s state=%lu flags = %lu, cpu_id=%lu", __get_str(name),
		(unsigned long)__entry->state, (unsigned long)__entry->flags, (unsigned long)__entry->cpu_id)
);

/* The regular OMAP events */
DECLARE_EVENT_CLASS(omap,

	TP_PROTO(const char *name, unsigned int state, unsigned int cpu_id),

	TP_ARGS(name, state, cpu_id),

	TP_STRUCT__entry(
		__string(       name,           name            )
		__field(        u64,            state           )
		__field(        u64,            cpu_id          )
	),

	TP_fast_assign(
		__assign_str(name, name);
		__entry->state = state;
		__entry->cpu_id = cpu_id;
	),

	TP_printk("%s state=%lu cpu_id=%lu", __get_str(name),
		(unsigned long)__entry->state, (unsigned long)__entry->cpu_id)
);


DEFINE_EVENT(omap, omap_device_scale,

	TP_PROTO(const char *name, unsigned int state, unsigned int cpu_id),

	TP_ARGS(name, state, cpu_id)
);

DEFINE_EVENT(omap, omap_sr_enable,

	TP_PROTO(const char *name, unsigned int state, unsigned int cpu_id),

	TP_ARGS(name, state, cpu_id)
);

DEFINE_EVENT(omap, omap_sr_disable,

	TP_PROTO(const char *name, unsigned int state, unsigned int cpu_id),

	TP_ARGS(name, state, cpu_id)
);
DEFINE_EVENT(omap, omap_sr_disable_reset_volt,

	TP_PROTO(const char *name, unsigned int state, unsigned int cpu_id),

	TP_ARGS(name, state, cpu_id)
);

DEFINE_EVENT(omapflag, omap_voltdm_scale,

	TP_PROTO(const char *name, unsigned int state, unsigned int flags, unsigned int cpu_id),

	TP_ARGS(name, state, flags, cpu_id)
);

DEFINE_EVENT(omapflag, omap_vp_forceupdate_scale,

	TP_PROTO(const char *name, unsigned int state, unsigned int flags, unsigned int cpu_id),

	TP_ARGS(name, state, flags, cpu_id)
);

#endif /* _TRACE_OMAP_H */

#include <trace/define_trace.h>

