/* BlesKernOS has no /etc/mesa.conf. Keep Mesa's runtime defaults. */
#include "glheader.h"
#include "context.h"

void _mesa_read_config_file(GLcontext *ctx)
{
    (void)ctx;
}
