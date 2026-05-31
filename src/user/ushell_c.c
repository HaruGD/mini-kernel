#include "userlib.h"

#define SHELLC_INPUT_MAX 64

static void print_prompt(void) {
    user_write_cstr("csh> ");
}

static void print_usage(const char* usage) {
    user_printf("Usage: %s\n", usage);
}

static void print_command_failed(const char* command) {
    user_printf("%s failed.\n", command);
}

static void print_tools(void) {
    user_write_cstr(
        "Standalone tools:\n"
        "  Files:   uls, utouch, usave, ucat, urm, uio\n"
        "  Status:  upid, uschd, umem, uvers, uargs\n"
        "  Proc:    urun\n"
        "Shell shortcuts:\n"
        "  ujobs, ulast, uwait\n"
        "Use 'where [name]' to see whether a command is built in, a tool alias,\n"
        "or a shell shortcut.\n");
}

static int run_tool_alias(const char* command, const char* image_name) {
    if (user_run(image_name) < 0) {
        print_command_failed(command);
        return 0;
    }
    user_reapall_silent();
    return 1;
}

static void print_builtins(void) {
    user_write_cstr(
        "Built-in commands:\n"
        "  help ? about exit clear cls tools builtins where\n"
        "  version bootinfo memstat uptime mounts\n"
        "  ls [path] cat touch save rm\n"
        "  jobs ps pid ppid sched wait laststatus reapall\n"
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
               user_str_eq(name, "bootinfo") ||
               user_str_eq(name, "memstat") ||
               user_str_eq(name, "mounts") ||
               user_str_eq(name, "uptime") ||
               user_str_eq(name, "ls") ||
               user_str_eq(name, "cat") ||
               user_str_eq(name, "touch") ||
               user_str_eq(name, "save") ||
               user_str_eq(name, "rm") ||
               user_str_eq(name, "jobs") ||
               user_str_eq(name, "ps") ||
               user_str_eq(name, "pid") ||
               user_str_eq(name, "ppid") ||
               user_str_eq(name, "sched") ||
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
        "System:\n"
        "  version, bootinfo, memstat, uptime, mounts\n"
        "Files:\n"
        "  ls [path], cat [file], touch [file], save [file] [text], rm [file]\n"
        "  uls, utouch, usave, ucat, urm, uio\n"
        "Status Tools:\n"
        "  upid, uschd, umem, uvers, uargs\n"
        "Processes:\n"
        "  jobs, ps, pid, ppid, sched, wait, laststatus, reapall\n"
        "  ujobs, ulast, uwait, urun\n"
        "Control:\n"
        "  run [file], sleep [ticks], yield, resume [pid], kill [pid], bg [pid], fg [pid]\n"
        "Text:\n"
        "  echo [text]\n");
}

int main(void) {
    char input[SHELLC_INPUT_MAX];

    user_write_cstr(
        "=== USHELL_C.ELF ===\n"
        "C user shell ready. Type help for commands.\n"
        "Use tools for standalone utilities and where [name] for command origin.\n");

    while (1) {
        char* line;
        char* args;

        print_prompt();
        user_read_line(input, sizeof(input));

        line = user_trim(input);
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

        if (user_str_eq(line, "ls")) {
            if (args == 0 || args[0] == '\0') {
                user_list_files();
            } else {
                if (user_list_files_at(args) < 0) {
                    print_command_failed("ls");
                }
            }
            continue;
        }

        if (user_str_eq(line, "uls")) {
            run_tool_alias("uls", "ULS_C.ELF");
            continue;
        }

        if (user_str_eq(line, "upid")) {
            run_tool_alias("upid", "UPID_C.ELF");
            continue;
        }

        if (user_str_eq(line, "uschd")) {
            run_tool_alias("uschd", "USCHD_C.ELF");
            continue;
        }

        if (user_str_eq(line, "umem")) {
            run_tool_alias("umem", "UMEM_C.ELF");
            continue;
        }

        if (user_str_eq(line, "uvers")) {
            run_tool_alias("uvers", "UVERS_C.ELF");
            continue;
        }

        if (user_str_eq(line, "uargs")) {
            run_tool_alias("uargs", "UARGS_C.ELF");
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
            run_tool_alias("urun", "URUN_C.ELF");
            continue;
        }

        if (user_str_eq(line, "utouch")) {
            run_tool_alias("utouch", "UTOUCH_C.ELF");
            continue;
        }

        if (user_str_eq(line, "usave")) {
            run_tool_alias("usave", "USAVE_C.ELF");
            continue;
        }

        if (user_str_eq(line, "ucat")) {
            run_tool_alias("ucat", "UCAT_C.ELF");
            continue;
        }

        if (user_str_eq(line, "urm")) {
            run_tool_alias("urm", "URM_C.ELF");
            continue;
        }

        if (user_str_eq(line, "uio")) {
            run_tool_alias("uio", "UIO_C.ELF");
            continue;
        }

        if (user_str_eq(line, "version")) {
            user_version();
            continue;
        }

        if (user_str_eq(line, "bootinfo")) {
            user_bootinfo();
            continue;
        }

        if (user_str_eq(line, "memstat")) {
            user_memstat();
            continue;
        }

        if (user_str_eq(line, "mounts")) {
            user_mounts();
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
            user_sched();
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
            if (user_cat(args) < 0) {
                print_command_failed("cat");
            }
            continue;
        }

        if (user_str_eq(line, "touch")) {
            if (args == 0 || args[0] == '\0') {
                print_usage("touch [file]");
                continue;
            }
            if (user_touch(args) < 0) {
                print_command_failed("touch");
            }
            continue;
        }

        if (user_str_eq(line, "rm")) {
            if (args == 0 || args[0] == '\0') {
                print_usage("rm [file]");
                continue;
            }
            if (user_rm(args) < 0) {
                print_command_failed("rm");
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

            if (user_save(file_name, text) < 0) {
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
