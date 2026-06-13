int main(void) {
    refresh_shell_cwd();
    user_write_cstr(
        "=== ushell_c.elf ===\n"
        "C user shell ready. Type help for commands.\n"
        "Use tools for standalone utilities and where [name] for command origin.\n");

    while (1) {
        char normalized_path[SHELLC_CMDLINE_MAX];
        char* line;
        char* args;

        refresh_shell_cwd();
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
            user_write_cstr("ushell_c.elf runs entirely in user mode using int 0x80 syscalls.\n");
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
            if (user_clear_screen() == 0) {
                continue;
            }
            for (i = 0; i < 80; i++) {
                user_write_cstr("\n");
            }
            continue;
        }

        if (user_str_eq(line, "where")) {
            print_where(args);
            continue;
        }

        if (user_str_eq(line, "pwd")) {
            refresh_shell_cwd();
            user_printf("%s\n", shell_cwd);
            continue;
        }

        if (user_str_eq(line, "cd")) {
            const char* target = (args == 0 || args[0] == '\0') ? "/" : args;
            UserVFSInfo info;
            if (!normalize_shell_path(target, normalized_path)) {
                continue;
            }
            if (user_get_file_info(normalized_path, &info) < 0 || info.type != USER_VFS_NODE_DIR) {
                print_command_failed("cd");
                continue;
            }
            if (user_chdir(normalized_path) < 0) {
                print_command_failed("cd");
                continue;
            }
            refresh_shell_cwd();
            continue;
        }

        if (user_str_eq(line, "ls")) {
            if (args == 0 || args[0] == '\0') {
                if (user_list_files_at(shell_cwd) < 0) {
                    print_command_failed("ls");
                }
            } else {
                if (!normalize_shell_path(args, normalized_path)) {
                    continue;
                }
                if (user_list_files_at(normalized_path) < 0) {
                    print_command_failed("ls");
                }
            }
            continue;
        }

        if (user_str_eq(line, "uls")) {
            run_tool_alias_single_path("uls", "uls_c.elf", args, 1);
            continue;
        }

        if (user_str_eq(line, "umkdir")) {
            run_tool_alias_single_path("umkdir", "umkdir_c.elf", args, 0);
            continue;
        }

        if (user_str_eq(line, "urmdir")) {
            run_tool_alias_single_path("urmdir", "urmdir_c.elf", args, 0);
            continue;
        }

        if (user_str_eq(line, "upid")) {
            run_tool_alias("upid", "upid_c.elf", args);
            continue;
        }

        if (user_str_eq(line, "uschd")) {
            run_tool_alias("uschd", "uschd_c.elf", args);
            continue;
        }

        if (user_str_eq(line, "umem")) {
            run_tool_alias("umem", "umem_c.elf", args);
            continue;
        }

        if (user_str_eq(line, "uvers")) {
            run_tool_alias("uvers", "uvers_c.elf", args);
            continue;
        }

        if (user_str_eq(line, "uargs")) {
            run_tool_alias("uargs", "uargs_c.elf", args);
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
            run_tool_alias("urun", "urun_c.elf", args);
            continue;
        }

        if (user_str_eq(line, "utouch")) {
            run_tool_alias_single_path("utouch", "utouch_c.elf", args, 0);
            continue;
        }

        if (user_str_eq(line, "usave")) {
            run_tool_alias_usave(args);
            continue;
        }

        if (user_str_eq(line, "ucat")) {
            run_tool_alias_single_path("ucat", "ucat_c.elf", args, 0);
            continue;
        }

        if (user_str_eq(line, "urm")) {
            run_tool_alias_single_path("urm", "urm_c.elf", args, 0);
            continue;
        }

        if (user_str_eq(line, "uio")) {
            run_tool_alias_uio(args);
            continue;
        }

        if (user_str_eq(line, "uboot")) {
            run_tool_alias("uboot", "uboot_c.elf", args);
            continue;
        }

        if (user_str_eq(line, "umounts")) {
            run_tool_alias("umounts", "umnts_c.elf", args);
            continue;
        }

        if (user_str_eq(line, "version")) {
            user_version();
            continue;
        }

        if (user_str_eq(line, "bootinfo")) {
            run_tool_alias("bootinfo", "uboot_c.elf", args);
            continue;
        }

        if (user_str_eq(line, "memstat")) {
            run_tool_alias("memstat", "umem_c.elf", args);
            continue;
        }

        if (user_str_eq(line, "mounts")) {
            run_tool_alias("mounts", "umnts_c.elf", args);
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
            run_tool_alias("sched", "uschd_c.elf", args);
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
            if (!normalize_shell_path(args, normalized_path)) {
                continue;
            }
            if (user_cat(normalized_path) < 0) {
                print_command_failed("cat");
            }
            continue;
        }

        if (user_str_eq(line, "touch")) {
            if (args == 0 || args[0] == '\0') {
                print_usage("touch [file]");
                continue;
            }
            if (!normalize_shell_path(args, normalized_path)) {
                continue;
            }
            if (user_touch(normalized_path) < 0) {
                print_command_failed("touch");
            }
            continue;
        }

        if (user_str_eq(line, "rm")) {
            if (args == 0 || args[0] == '\0') {
                print_usage("rm [file]");
                continue;
            }
            if (!normalize_shell_path(args, normalized_path)) {
                continue;
            }
            if (user_rm(normalized_path) < 0) {
                print_command_failed("rm");
            }
            continue;
        }

        if (user_str_eq(line, "mkdir")) {
            if (args == 0 || args[0] == '\0') {
                print_usage("mkdir [path]");
                continue;
            }
            if (!normalize_shell_path(args, normalized_path)) {
                continue;
            }
            if (user_mkdir(normalized_path) < 0) {
                print_command_failed("mkdir");
            }
            continue;
        }

        if (user_str_eq(line, "rmdir")) {
            if (args == 0 || args[0] == '\0') {
                print_usage("rmdir [path]");
                continue;
            }
            if (!normalize_shell_path(args, normalized_path)) {
                continue;
            }
            if (user_rmdir(normalized_path) < 0) {
                print_command_failed("rmdir");
            }
            continue;
        }

        if (user_str_eq(line, "rename") || user_str_eq(line, "mv")) {
            char* old_path = args;
            char* new_path;
            char normalized_old[SHELLC_CMDLINE_MAX];
            char normalized_new[SHELLC_CMDLINE_MAX];

            if (old_path == 0 || old_path[0] == '\0') {
                print_usage("rename [old] [new]");
                continue;
            }

            new_path = user_split_token(old_path);
            if (new_path == 0 || new_path[0] == '\0') {
                print_usage("rename [old] [new]");
                continue;
            }

            if (!normalize_shell_path(old_path, normalized_old) ||
                !normalize_shell_path(new_path, normalized_new)) {
                continue;
            }

            if (user_rename(normalized_old, normalized_new) < 0) {
                print_command_failed(user_str_eq(line, "mv") ? "mv" : "rename");
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

            if (!normalize_shell_path(file_name, normalized_path)) {
                continue;
            }
            if (user_save(normalized_path, text) < 0) {
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
