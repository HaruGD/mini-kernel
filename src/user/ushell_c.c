#include "userlib.h"

#define SHELLC_INPUT_MAX 160
#define SHELLC_CMDLINE_MAX 160

static char shell_input[SHELLC_INPUT_MAX];
static char shell_cwd[SHELLC_CMDLINE_MAX] = "/";


#include "ushell/ushell_helpers.inc"
#include "ushell/ushell_main.inc"
