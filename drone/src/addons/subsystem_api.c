#include "subsystem_api.h"
#include <errno.h>
#include "log.h"

static const char* module_name_str = "subsystem_api";

static int host_enable_rc_override_stub(const uint16_t *channels, size_t channel_count)
{
	(void)channels;
	(void)channel_count;
	INFO("enable_rc_override() is not wired yet");
	return -ENOTSUP;
}


const subsystem_host_api_t g_host_api = {
	.enable_rc_override = host_enable_rc_override_stub,
};