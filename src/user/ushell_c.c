#include "userlib.h"

#define SHELLC_INPUT_MAX 64
#define SHELLC_CMDLINE_MAX 160

static char shell_input[SHELLC_INPUT_MAX];
static char shell_command_line[SHELLC_CMDLINE_MAX];
static char shell_cwd[SHELLC_CMDLINE_MAX] = "/";
static char shell_path_buffer[SHELLC_CMDLINE_MAX];

static void print_prompt(void) {
    user_write_cstr("csh> ");
}

static void print_usage(const char* usage) {
    user_printf("Usage: %s\n", usage);
}

static void print_command_failed(const char* command) {
    user_printf("%s failed.\n", command);
}

static int normalize_shell_path(const char* input, char* out) {
    if (!user_normalize_path(shell_cwd, input, out, SHELLC_CMDLINE_MAX)) {
        user_puts("Path is invalid or too long.");
        return 0;
    }
    return 1;
}

static int append_token(char* dest, uint32_t* offset, const char* text) {
    uint32_t len = (uint32_t)user_strlen(text);
    if (*offset + len + 1 >= SHELLC_CMDLINE_MAX) {
        return 0;
    }
    for (uint32_t i = 0; i < len; i++) {
        dest[*offset + i] = text[i];
    }
    *offset += len;
    dest[*offset] = '\0';
    return 1;
}

static void print_tools(void) {
    user_write_cstr(
        "Standalone tools:\n"
        "  Files:   uls [path], utouch [file], usave [file] [text], ucat [file], urm [file]\n"
        "           uio [file] [text], uio append [file] [text], uio seek [file] [offset],\n"
        "           uio leak [file] [text]\n"
        "  Status:  upid, uschd, umem, uvers, uboot, umounts, uargs\n"
        "  Proc:    urun\n"
        "Shell shortcuts:\n"
        "  memstat, sched, bootinfo, mounts\n"
        "  ujobs, ulast, uwait\n"
        "Use 'where [name]' to see whether a command is built in, a tool alias,\n"
        "or a shell shortcut.\n");
}

static int run_tool_alias(const char* command, const char* image_name, const char* args) {
    uint64_t image_len = user_strlen(image_name);
    uint64_t args_len = (args != 0 && args[0] != '\0') ? user_strlen(args) : 0;

    if (image_len + (args_len > 0 ? 1 : 0) + args_len + 1 > sizeof(shell_command_line)) {
        user_printf("%s arguments are too long.\n", command);
        return 0;
    }

    shell_command_line[0] = '\0';
    for (uint64_t i = 0; i < image_len; i++) {
        shell_command_line[i] = image_name[i];
    }
    shell_command_line[image_len] = '\0';

    if (args_len > 0) {
        shell_command_line[image_len] = ' ';
        for (uint64_t i = 0; i < args_len; i++) {
            shell_command_line[image_len + 1 + i] = args[i];
        }
        shell_command_line[image_len + 1 + args_len] = '\0';
    }

    if (user_run(shell_command_line) < 0) {
        print_command_failed(command);
        return 0;
    }
    user_reapall_silent();
    return 1;
}

static int run_tool_alias_single_path(const char* command, const char* image_name, const char* path_arg, int default_to_cwd) {
    if ((path_arg == 0 || path_arg[0] == '\0') && !default_to_cwd) {
        return run_tool_alias(command, image_name, 0);
    }

    if (path_arg == 0 || path_arg[0] == '\0') {
        path_arg = shell_cwd;
    }

    if (!normalize_shell_path(path_arg, shell_path_buffer)) {
        return 0;
    }
    return run_tool_alias(command, image_name, shell_path_buffer);
}

static int run_tool_alias_usave(const char* args) {
    char local[SHELLC_CMDLINE_MAX];
    char* file_name;
    char* text;
    uint32_t offset = 0;

    if (args == 0 || args[0] == '\0') {
        return run_tool_alias("usave", "USAVE_C.ELF", 0);
    }

    for (uint32_t i = 0; args[i] != '\0' && i + 1 < sizeof(local); i++) {
        local[i] = args[i];
        local[i + 1] = '\0';
    }
    local[sizeof(local) - 1] = '\0';

    file_name = local;
    text = user_split_token(file_name);
    if (text == 0 || text[0] == '\0') {
        return run_tool_alias("usave", "USAVE_C.ELF", args);
    }
    if (!normalize_shell_path(file_name, shell_path_buffer)) {
        return 0;
    }

    shell_command_line[0] = '\0';
    if (!append_token(shell_command_line, &offset, "USAVE_C.ELF")) {
        user_puts("Command line is too long.");
        return 0;
    }
    shell_command_line[offset++] = ' ';
    shell_command_line[offset] = '\0';
    if (!append_token(shell_command_line, &offset, shell_path_buffer)) {
        user_puts("Command line is too long.");
        return 0;
    }
    shell_command_line[offset++] = ' ';
    shell_command_line[offset] = '\0';
    if (!append_token(shell_command_line, &offset, text)) {
        user_puts("Command line is too long.");
        return 0;
    }
    if (user_run(shell_command_line) < 0) {
        print_command_failed("usave");
        return 0;
    }
    user_reapall_silent();
    return 1;
}

static int run_tool_alias_uio(const char* args) {
    char local[SHELLC_CMDLINE_MAX];
    char* token1;
    char* token2;
    char* rest;
    uint32_t offset = 0;

    if (args == 0 || args[0] == '\0') {
        return run_tool_alias("uio", "UIO_C.ELF", 0);
    }

    for (uint32_t i = 0; args[i] != '\0' && i + 1 < sizeof(local); i++) {
        local[i] = args[i];
        local[i + 1] = '\0';
    }
    local[sizeof(local) - 1] = '\0';

    token1 = local;
    token2 = user_split_token(token1);

    shell_command_line[0] = '\0';
    if (!append_token(shell_command_line, &offset, "UIO_C.ELF")) {
        user_puts("Command line is too long.");
        return 0;
    }

    if (user_str_eq(token1, "append") || user_str_eq(token1, "seek") || user_str_eq(token1, "leak")) {
        if (token2 == 0 || token2[0] == '\0') {
            return run_tool_alias("uio", "UIO_C.ELF", args);
        }
        rest = user_split_token(token2);
        if (!normalize_shell_path(token2, shell_path_buffer)) {
            return 0;
        }

        shell_command_line[offset++] = ' ';
        shell_command_line[offset] = '\0';
        if (!append_token(shell_command_line, &offset, token1)) {
            user_puts("Command line is too long.");
            return 0;
        }
        shell_command_line[offset++] = ' ';
        shell_command_line[offset] = '\0';
        if (!append_token(shell_command_line, &offset, shell_path_buffer)) {
            user_puts("Command line is too long.");
            return 0;
        }
        if (rest != 0 && rest[0] != '\0') {
            shell_command_line[offset++] = ' ';
            shell_command_line[offset] = '\0';
            if (!append_token(shell_command_line, &offset, rest)) {
                user_puts("Command line is too long.");
                return 0;
            }
        }
    } else {
        rest = token2;
        if (!normalize_shell_path(token1, shell_path_buffer)) {
            return 0;
        }
        shell_command_line[offset++] = ' ';
        shell_command_line[offset] = '\0';
        if (!append_token(shell_command_line, &offset, shell_path_buffer)) {
            user_puts("Command line is too long.");
            return 0;
        }
        if (rest != 0 && rest[0] != '\0') {
            shell_command_line[offset++] = ' ';
            shell_command_line[offset] = '\0';
            if (!append_token(shell_command_line, &offset, rest)) {
                user_puts("Command line is too long.");
                return 0;
            }
        }
    }

    if (user_run(shell_command_line) < 0) {
        print_command_failed("uio");
        return 0;
    }
    user_reapall_silent();
    return 1;
}

static void print_builtins(void) {
    user_write_cstr(
        "Built-in commands:\n"
        "  help ? about exit clear cls tools builtins where\n"
        "  version uptime\n"
        "  pwd, cd [path]\n"
        "  ls [path], cat [file], touch [file], save [file] [text], rm [file], mkdir [path], rmdir [path]\n"
        "  pid ppid\n"
        "  jobs ps wait laststatus reapall\n"
        "  run sleep yield resume kill bg fg echo\n");
}

static void print_where(const char* name) {
    if (name == 0 || name[0] == '\0') {
        print_usage("where [command]");
        return;
    }

    user_printf("%s: ", name);

    if (user_str_eq(name, "uls")) {
        user_write_cstr("tool alias - runs ULS_C.ELF\n");
    } else if (user_str_eq(name, "utouch")) {
        user_write_cstr("tool alias - runs UTOUCH_C.ELF\n");
    } else if (user_str_eq(name, "usave")) {
        user_write_cstr("tool alias - runs USAVE_C.ELF\n");
    } else if (user_str_eq(name, "ucat")) {
        user_write_cstr("tool alias - runs UCAT_C.ELF\n");
    } else if (user_str_eq(name, "urm")) {
        user_write_cstr("tool alias - runs URM_C.ELF\n");
    } else if (user_str_eq(name, "uio")) {
        user_write_cstr("tool alias - runs UIO_C.ELF\n");
    } else if (user_str_eq(name, "uboot")) {
        user_write_cstr("tool alias - runs UBOOT_C.ELF\n");
    } else if (user_str_eq(name, "umounts")) {
        user_write_cstr("tool alias - runs UMNTS_C.ELF\n");
    } else if (user_str_eq(name, "upid")) {
        user_write_cstr("tool alias - runs UPID_C.ELF\n");
    } else if (user_str_eq(name, "uschd")) {
        user_write_cstr("tool alias - runs USCHD_C.ELF\n");
    } else if (user_str_eq(name, "umem")) {
        user_write_cstr("tool alias - runs UMEM_C.ELF\n");
    } else if (user_str_eq(name, "uvers")) {
        user_write_cstr("tool alias - runs UVERS_C.ELF\n");
    } else if (user_str_eq(name, "uargs")) {
        user_write_cstr("tool alias - runs UARGS_C.ELF\n");
    } else if (user_str_eq(name, "urun")) {
        user_write_cstr("tool alias - runs URUN_C.ELF\n");
    } else if (user_str_eq(name, "ujobs")) {
        user_write_cstr("shell shortcut - shell shortcut for jobs\n");
    } else if (user_str_eq(name, "ulast")) {
        user_write_cstr("shell shortcut - shell shortcut for laststatus\n");
    } else if (user_str_eq(name, "uwait")) {
        user_write_cstr("shell shortcut - shell shortcut for wait\n");
    } else if (user_str_eq(name, "memstat")) {
        user_write_cstr("shell shortcut - runs UMEM_C.ELF\n");
    } else if (user_str_eq(name, "sched")) {
        user_write_cstr("shell shortcut - runs USCHD_C.ELF\n");
    } else if (user_str_eq(name, "bootinfo")) {
        user_write_cstr("shell shortcut - runs UBOOT_C.ELF\n");
    } else if (user_str_eq(name, "mounts")) {
        user_write_cstr("shell shortcut - runs UMNTS_C.ELF\n");
    } else if (user_str_eq(name, "help") ||
               user_str_eq(name, "?") ||
               user_str_eq(name, "about") ||
               user_str_eq(name, "exit") ||
               user_str_eq(name, "clear") ||
               user_str_eq(name, "cls") ||
               user_str_eq(name, "tools") ||
               user_str_eq(name, "builtins") ||
               user_str_eq(name, "where") ||
               user_str_eq(name, "version") ||
               user_str_eq(name, "uptime") ||
               user_str_eq(name, "pwd") ||
               user_str_eq(name, "cd") ||
               user_str_eq(name, "ls") ||
               user_str_eq(name, "cat") ||
               user_str_eq(name, "touch") ||
               user_str_eq(name, "save") ||
               user_str_eq(name, "rm") ||
               user_str_eq(name, "mkdir") ||
               user_str_eq(name, "rmdir") ||
               user_str_eq(name, "jobs") ||
               user_str_eq(name, "ps") ||
               user_str_eq(name, "pid") ||
               user_str_eq(name, "ppid") ||
               user_str_eq(name, "wait") ||
               user_str_eq(name, "laststatus") ||
               user_str_eq(name, "reapall") ||
               user_str_eq(name, "run") ||
               user_str_eq(name, "sleep") ||
               user_str_eq(name, "yield") ||
               user_str_eq(name, "resume") ||
               user_str_eq(name, "kill") ||
               user_str_eq(name, "bg") ||
               user_str_eq(name, "fg") ||
               user_str_eq(name, "echo")) {
        user_write_cstr("built-in\n");
    } else {
        user_write_cstr("not known to this shell\n");
        return;
    }
}

static void print_help(void) {
    user_write_cstr(
        "Shell:\n"
        "  help, ?, about, exit, clear, cls, tools, builtins, where [command]\n"
        "Built-ins:\n"
        "  version, uptime, jobs, ps, wait, laststatus, reapall\n"
        "  pwd, cd [path]\n"
        "  ls [path], cat [file], touch [file], save [file] [text], rm [file], mkdir [path], rmdir [path]\n"
        "  pid, ppid\n"
        "  run [file], sleep [ticks], yield, resume [pid], kill [pid], bg [pid], fg [pid], echo [text]\n"
        "Shell shortcuts:\n"
        "  sched, memstat, bootinfo, mounts\n"
        "Standalone tools:\n"
        "  uls [path], utouch [file], usave [file] [text], ucat [file], urm [file]\n"
        "  upid, uschd, umem, uvers, uboot, umounts, uargs\n"
        "  uio [file] [text], uio append [file] [text], uio seek [file] [offset], uio leak [file] [text]\n"
        "Processes:\n"
        "  ujobs, ulast, uwait, urun\n"
        "Text:\n"
        "  echo [text]\n");
}

int main(void) {
    user_write_cstr(
        "=== USHELL_C.ELF ===\n"
        "C user shell ready. Type help for commands.\n"
        "Use tools for standalone utilities and where [name] for command origin.\n");

    while (1) {
        char* line;
        char* args;

        print_prompt();
        user_read_line(shell_input, sizeof(shell_input));

        line = user_trim(shell_input);
        if (line[0] == '\0') {
            continue;
        }

        args = user_split_token(line);

        if (user_str_eq(line, "help") || user_str_eq(line, "?")) {
            print_help();
            continue;
        }

        if (user_str_eq(line, "about")) {
            user_write_cstr("USHELL_C.ELF runs entirely in user mode using int 0x80 syscalls.\n");
            continue;
        }

        if (user_str_eq(line, "tools")) {
            print_tools();
            continue;
        }

        if (user_str_eq(line, "builtins")) {
            print_builtins();
            continue;
        }

        if (user_str_eq(line, "exit")) {
            uint32_t active_children = (uint32_t)user_children_active();
            if (active_children > 0) {
                user_printf(
                    "Cannot exit: %u child job(s) are still active. "
                    "Use jobs, fg, bg, kill, or wait first.\n",
                    active_children);
                continue;
            }
            user_write_cstr("Leaving C user shell...\n");
            return 0;
        }

        if (user_str_eq(line, "clear") || user_str_eq(line, "cls")) {
            uint32_t i;
            for (i = 0; i < 24; i++) {
                user_write_cstr("\n");
            }
            continue;
        }

        if (user_str_eq(line, "where")) {
            print_where(args);
            continue;
        }

        if (user_str_eq(line, "pwd")) {
            user_printf("%s\n", shell_cwd);
            continue;
        }

        if (user_str_eq(line, "cd")) {
            const char* target = (args == 0 || args[0] == '\0') ? "/" : args;
            UserVFSInfo info;
            if (!normalize_shell_path(target, shell_path_buffer)) {
                continue;
            }
            if (user_get_file_info(shell_path_buffer, &info) < 0 || info.type != USER_VFS_NODE_DIR) {
                print_command_failed("cd");
                continue;
            }
            for (uint32_t i = 0; shell_path_buffer[i] != '\0' && i + 1 < sizeof(shell_cwd); i++) {
                shell_cwd[i] = shell_path_buffer[i];
                shell_cwd[i + 1] = '\0';
            }
            shell_cwd[sizeof(shell_cwd) - 1] = '\0';
            continue;
        }

        if (user_str_eq(line, "ls")) {
            if (args == 0 || args[0] == '\0') {
                if (user_list_files_at(shell_cwd) < 0) {
                    print_command_failed("ls");
                }
            } else {
                if (!normalize_shell_path(args, shell_path_buffer)) {
                    continue;
                }
                if (user_list_files_at(shell_path_buffer) < 0) {
                    print_command_failed("ls");
                }
            }
            continue;
        }

        if (user_str_eq(line, "uls")) {
            run_tool_alias_single_path("uls", "ULS_C.ELF", args, 1);
            continue;
        }

        if (user_str_eq(line, "upid")) {
            run_tool_alias("upid", "UPID_C.ELF", args);
            continue;
        }

        if (user_str_eq(line, "uschd")) {
            run_tool_alias("uschd", "USCHD_C.ELF", args);
            continue;
        }

        if (user_str_eq(line, "umem")) {
            run_tool_alias("umem", "UMEM_C.ELF", args);
            continue;
        }

        if (user_str_eq(line, "uvers")) {
            run_tool_alias("uvers", "UVERS_C.ELF", args);
            continue;
        }

        if (user_str_eq(line, "uargs")) {
            run_tool_alias("uargs", "UARGS_C.ELF", args);
            continue;
        }

        if (user_str_eq(line, "ujobs")) {
            user_jobs();
            continue;
        }

        if (user_str_eq(line, "ulast")) {
            user_laststatus();
            continue;
        }

        if (user_str_eq(line, "uwait")) {
            user_wait();
            continue;
        }

        if (user_str_eq(line, "urun")) {
            run_tool_alias("urun", "URUN_C.ELF", args);
            continue;
        }

        if (user_str_eq(line, "utouch")) {
            run_tool_alias_single_path("utouch", "UTOUCH_C.ELF", args, 0);
            continue;
        }

        if (user_str_eq(line, "usave")) {
            run_tool_alias_usave(args);
            continue;
        }

        if (user_str_eq(line, "ucat")) {
            run_tool_alias_single_path("ucat", "UCAT_C.ELF", args, 0);
            continue;
        }

        if (user_str_eq(line, "urm")) {
            run_tool_alias_single_path("urm", "URM_C.ELF", args, 0);
            continue;
        }

        if (user_str_eq(line, "uio")) {
            run_tool_alias_uio(args);
            continue;
        }

        if (user_str_eq(line, "uboot")) {
            run_tool_alias("uboot", "UBOOT_C.ELF", args);
            continue;
        }

        if (user_str_eq(line, "umounts")) {
            run_tool_alias("umounts", "UMNTS_C.ELF", args);
            continue;
        }

        if (user_str_eq(line, "version")) {
            user_version();
            continue;
        }

        if (user_str_eq(line, "bootinfo")) {
            run_tool_alias("bootinfo", "UBOOT_C.ELF", args);
            continue;
        }

        if (user_str_eq(line, "memstat")) {
            run_tool_alias("memstat", "UMEM_C.ELF", args);
            continue;
        }

        if (user_str_eq(line, "mounts")) {
            run_tool_alias("mounts", "UMNTS_C.ELF", args);
            continue;
        }

        if (user_str_eq(line, "uptime")) {
            user_uptime();
            continue;
        }

        if (user_str_eq(line, "jobs")) {
            user_jobs();
            continue;
        }

        if (user_str_eq(line, "ps")) {
            user_ps();
            continue;
        }

        if (user_str_eq(line, "pid")) {
            user_printf("pid: %x\n", (uint32_t)user_get_pid());
            continue;
        }

        if (user_str_eq(line, "ppid")) {
            user_printf("ppid: %x\n", (uint32_t)user_get_ppid());
            continue;
        }

        if (user_str_eq(line, "sched")) {
            run_tool_alias("sched", "USCHD_C.ELF", args);
            continue;
        }

        if (user_str_eq(line, "yield")) {
            user_yield();
            continue;
        }

        if (user_str_eq(line, "wait")) {
            user_wait();
            continue;
        }

        if (user_str_eq(line, "laststatus")) {
            user_laststatus();
            continue;
        }

        if (user_str_eq(line, "reapall")) {
            user_reapall();
            continue;
        }

        if (user_str_eq(line, "echo")) {
            if (args == 0 || args[0] == '\0') {
                user_write_cstr("\n");
            } else {
                user_write_cstr(args);
                user_write_cstr("\n");
            }
            continue;
        }

        if (user_str_eq(line, "run")) {
            if (args == 0 || args[0] == '\0') {
                print_usage("run [file]");
                continue;
            }
            if (user_run(args) < 0) {
                print_command_failed("run");
            }
            continue;
        }

        if (user_str_eq(line, "cat")) {
            if (args == 0 || args[0] == '\0') {
                print_usage("cat [file]");
                continue;
            }
            if (!normalize_shell_path(args, shell_path_buffer)) {
                continue;
            }
            if (user_cat(shell_path_buffer) < 0) {
                print_command_failed("cat");
            }
            continue;
        }

        if (user_str_eq(line, "touch")) {
            if (args == 0 || args[0] == '\0') {
                print_usage("touch [file]");
                continue;
            }
            if (!normalize_shell_path(args, shell_path_buffer)) {
                continue;
            }
            if (user_touch(shell_path_buffer) < 0) {
                print_command_failed("touch");
            }
            continue;
        }

        if (user_str_eq(line, "rm")) {
            if (args == 0 || args[0] == '\0') {
                print_usage("rm [file]");
                continue;
            }
            if (!normalize_shell_path(args, shell_path_buffer)) {
                continue;
            }
            if (user_rm(shell_path_buffer) < 0) {
                print_command_failed("rm");
            }
            continue;
        }

        if (user_str_eq(line, "mkdir")) {
            if (args == 0 || args[0] == '\0') {
                print_usage("mkdir [path]");
                continue;
            }
            if (!normalize_shell_path(args, shell_path_buffer)) {
                continue;
            }
            if (user_mkdir(shell_path_buffer) < 0) {
                print_command_failed("mkdir");
            }
            continue;
        }

        if (user_str_eq(line, "rmdir")) {
            if (args == 0 || args[0] == '\0') {
                print_usage("rmdir [path]");
                continue;
            }
            if (!normalize_shell_path(args, shell_path_buffer)) {
                continue;
            }
            if (user_rmdir(shell_path_buffer) < 0) {
                print_command_failed("rmdir");
            }
            continue;
        }

        if (user_str_eq(line, "save")) {
            char* file_name = args;
            char* text;

            if (file_name == 0 || file_name[0] == '\0') {
                print_usage("save [file] [text]");
                continue;
            }

            text = user_split_token(file_name);
            if (text == 0 || *text == '\0') {
                print_usage("save [file] [text]");
                continue;
            }

            if (!normalize_shell_path(file_name, shell_path_buffer)) {
                continue;
            }
            if (user_save(shell_path_buffer, text) < 0) {
                print_command_failed("save");
            }
            continue;
        }

        if (user_str_eq(line, "sleep")) {
            uint32_t ticks;
            if (args == 0 || !user_parse_u32_strict(args, &ticks) || ticks == 0) {
                print_usage("sleep [ticks]");
                continue;
            }
            user_sleep(ticks);
            continue;
        }

        if (user_str_eq(line, "resume")) {
            uint32_t pid;
            if (args == 0 || args[0] == '\0') {
                user_resume(0);
                continue;
            }
            if (!user_parse_u32_strict(args, &pid) || pid == 0) {
                print_usage("resume [pid]");
                continue;
            }
            user_resume((long)pid);
            continue;
        }

        if (user_str_eq(line, "kill")) {
            uint32_t pid;
            if (args == 0 || !user_parse_u32_strict(args, &pid) || pid == 0) {
                print_usage("kill [pid]");
                continue;
            }
            user_kill((long)pid);
            continue;
        }

        if (user_str_eq(line, "bg")) {
            uint32_t pid;
            if (args == 0 || args[0] == '\0') {
                if (user_set_background(0, 1) < 0) {
                    print_command_failed("bg");
                }
                continue;
            }
            if (!user_parse_u32_strict(args, &pid) || pid == 0) {
                print_usage("bg [pid]");
                continue;
            }
            if (user_set_background((long)pid, 1) < 0) {
                print_command_failed("bg");
            }
            continue;
        }

        if (user_str_eq(line, "fg")) {
            uint32_t pid;
            if (args == 0 || args[0] == '\0') {
                if (user_set_background(0, 0) < 0) {
                    print_command_failed("fg");
                    continue;
                }
                if (user_resume(0) < 0) {
                    user_write_cstr("fg resume failed.\n");
                }
                continue;
            }
            if (!user_parse_u32_strict(args, &pid) || pid == 0) {
                print_usage("fg [pid]");
                continue;
            }
            if (user_set_background((long)pid, 0) < 0) {
                print_command_failed("fg");
                continue;
            }
            if (user_resume((long)pid) < 0) {
                user_write_cstr("fg resume failed.\n");
            }
            continue;
        }

        user_printf("Unknown command: %s\nType help to list commands.\n", line);
    }
}
