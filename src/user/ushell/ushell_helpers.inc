static void refresh_shell_cwd(void) {
    if (user_getcwd(shell_cwd, sizeof(shell_cwd)) < 0) {
        shell_cwd[0] = '/';
        shell_cwd[1] = '\0';
    }
}

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
        "  Files:   uls [path], umkdir [path], urmdir [path], utouch [file], usave [file] [text],\n"
        "           ucat [file], urm [file]\n"
        "           uio [file] [text], uio append [file] [text], uio seek [file] [offset],\n"
        "           uio fill [file] [size],\n"
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
    char command_line[SHELLC_CMDLINE_MAX];
    uint64_t image_len = user_strlen(image_name);
    uint64_t args_len = (args != 0 && args[0] != '\0') ? user_strlen(args) : 0;

    if (image_len + (args_len > 0 ? 1 : 0) + args_len + 1 > sizeof(command_line)) {
        user_printf("%s arguments are too long.\n", command);
        return 0;
    }

    command_line[0] = '\0';
    for (uint64_t i = 0; i < image_len; i++) {
        command_line[i] = image_name[i];
    }
    command_line[image_len] = '\0';

    if (args_len > 0) {
        command_line[image_len] = ' ';
        for (uint64_t i = 0; i < args_len; i++) {
            command_line[image_len + 1 + i] = args[i];
        }
        command_line[image_len + 1 + args_len] = '\0';
    }

    if (user_run(command_line) < 0) {
        print_command_failed(command);
        return 0;
    }
    user_reapall_silent();
    return 1;
}

static int run_tool_alias_single_path(const char* command, const char* image_name, const char* path_arg, int default_to_cwd) {
    char normalized_path[SHELLC_CMDLINE_MAX];
    if ((path_arg == 0 || path_arg[0] == '\0') && !default_to_cwd) {
        return run_tool_alias(command, image_name, 0);
    }

    if (path_arg == 0 || path_arg[0] == '\0') {
        path_arg = shell_cwd;
    }

    if (!normalize_shell_path(path_arg, normalized_path)) {
        return 0;
    }
    return run_tool_alias(command, image_name, normalized_path);
}

static int run_tool_alias_usave(const char* args) {
    char local[SHELLC_CMDLINE_MAX];
    char normalized_path[SHELLC_CMDLINE_MAX];
    char* file_name;
    char* text;

    if (args == 0 || args[0] == '\0') {
        print_usage("usave [file] [text]");
        return 0;
    }

    for (uint32_t i = 0; args[i] != '\0' && i + 1 < sizeof(local); i++) {
        local[i] = args[i];
        local[i + 1] = '\0';
    }
    local[sizeof(local) - 1] = '\0';

    file_name = local;
    text = user_split_token(file_name);
    if (text == 0 || text[0] == '\0') {
        print_usage("usave [file] [text]");
        return 0;
    }
    if (!normalize_shell_path(file_name, normalized_path)) {
        return 0;
    }

    if (user_save_silent(normalized_path, text) < 0) {
        print_command_failed("usave");
        return 0;
    }
    user_printf("Saved to %s\n", normalized_path);
    return 1;
}

static int run_tool_alias_uio(const char* args) {
    char local[SHELLC_CMDLINE_MAX];
    char command_line[SHELLC_CMDLINE_MAX];
    char normalized_path[SHELLC_CMDLINE_MAX];
    char* token1;
    char* token2;
    char* rest;
    uint32_t offset = 0;

    if (args == 0 || args[0] == '\0') {
        return run_tool_alias("uio", "uio_c.elf", 0);
    }

    for (uint32_t i = 0; args[i] != '\0' && i + 1 < sizeof(local); i++) {
        local[i] = args[i];
        local[i + 1] = '\0';
    }
    local[sizeof(local) - 1] = '\0';

    token1 = local;
    token2 = user_split_token(token1);

    command_line[0] = '\0';
    if (!append_token(command_line, &offset, "uio_c.elf")) {
        user_puts("Command line is too long.");
        return 0;
    }

    if (user_str_eq(token1, "append") || user_str_eq(token1, "seek") ||
        user_str_eq(token1, "leak") || user_str_eq(token1, "fill")) {
        if (token2 == 0 || token2[0] == '\0') {
            return run_tool_alias("uio", "uio_c.elf", args);
        }
        rest = user_split_token(token2);
        if (!normalize_shell_path(token2, normalized_path)) {
            return 0;
        }

        if (offset + 1 >= sizeof(command_line)) {
            user_puts("Command line is too long.");
            return 0;
        }
        command_line[offset++] = ' ';
        command_line[offset] = '\0';
        if (!append_token(command_line, &offset, token1)) {
            user_puts("Command line is too long.");
            return 0;
        }
        if (offset + 1 >= sizeof(command_line)) {
            user_puts("Command line is too long.");
            return 0;
        }
        command_line[offset++] = ' ';
        command_line[offset] = '\0';
        if (!append_token(command_line, &offset, normalized_path)) {
            user_puts("Command line is too long.");
            return 0;
        }
        if (rest != 0 && rest[0] != '\0') {
            if (offset + 1 >= sizeof(command_line)) {
                user_puts("Command line is too long.");
                return 0;
            }
            command_line[offset++] = ' ';
            command_line[offset] = '\0';
            if (!append_token(command_line, &offset, rest)) {
                user_puts("Command line is too long.");
                return 0;
            }
        }
    } else {
        rest = token2;
        if (!normalize_shell_path(token1, normalized_path)) {
            return 0;
        }
        if (offset + 1 >= sizeof(command_line)) {
            user_puts("Command line is too long.");
            return 0;
        }
        command_line[offset++] = ' ';
        command_line[offset] = '\0';
        if (!append_token(command_line, &offset, normalized_path)) {
            user_puts("Command line is too long.");
            return 0;
        }
        if (rest != 0 && rest[0] != '\0') {
            if (offset + 1 >= sizeof(command_line)) {
                user_puts("Command line is too long.");
                return 0;
            }
            command_line[offset++] = ' ';
            command_line[offset] = '\0';
            if (!append_token(command_line, &offset, rest)) {
                user_puts("Command line is too long.");
                return 0;
            }
        }
    }

    if (user_run(command_line) < 0) {
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
        "  rename [old] [new], mv [old] [new]\n"
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
        user_write_cstr("tool alias - runs uls_c.elf\n");
    } else if (user_str_eq(name, "umkdir")) {
        user_write_cstr("tool alias - runs umkdir_c.elf\n");
    } else if (user_str_eq(name, "urmdir")) {
        user_write_cstr("tool alias - runs urmdir_c.elf\n");
    } else if (user_str_eq(name, "utouch")) {
        user_write_cstr("tool alias - runs utouch_c.elf\n");
    } else if (user_str_eq(name, "usave")) {
        user_write_cstr("shell shortcut - saves with normalized cwd-aware path handling\n");
    } else if (user_str_eq(name, "ucat")) {
        user_write_cstr("tool alias - runs ucat_c.elf\n");
    } else if (user_str_eq(name, "urm")) {
        user_write_cstr("tool alias - runs urm_c.elf\n");
    } else if (user_str_eq(name, "uio")) {
        user_write_cstr("tool alias - runs uio_c.elf\n");
    } else if (user_str_eq(name, "uboot")) {
        user_write_cstr("tool alias - runs uboot_c.elf\n");
    } else if (user_str_eq(name, "umounts")) {
        user_write_cstr("tool alias - runs umnts_c.elf\n");
    } else if (user_str_eq(name, "upid")) {
        user_write_cstr("tool alias - runs upid_c.elf\n");
    } else if (user_str_eq(name, "uschd")) {
        user_write_cstr("tool alias - runs uschd_c.elf\n");
    } else if (user_str_eq(name, "umem")) {
        user_write_cstr("tool alias - runs umem_c.elf\n");
    } else if (user_str_eq(name, "uvers")) {
        user_write_cstr("tool alias - runs uvers_c.elf\n");
    } else if (user_str_eq(name, "uargs")) {
        user_write_cstr("tool alias - runs uargs_c.elf\n");
    } else if (user_str_eq(name, "urun")) {
        user_write_cstr("tool alias - runs urun_c.elf\n");
    } else if (user_str_eq(name, "ujobs")) {
        user_write_cstr("shell shortcut - shell shortcut for jobs\n");
    } else if (user_str_eq(name, "ulast")) {
        user_write_cstr("shell shortcut - shell shortcut for laststatus\n");
    } else if (user_str_eq(name, "uwait")) {
        user_write_cstr("shell shortcut - shell shortcut for wait\n");
    } else if (user_str_eq(name, "memstat")) {
        user_write_cstr("shell shortcut - runs umem_c.elf\n");
    } else if (user_str_eq(name, "sched")) {
        user_write_cstr("shell shortcut - runs uschd_c.elf\n");
    } else if (user_str_eq(name, "bootinfo")) {
        user_write_cstr("shell shortcut - runs uboot_c.elf\n");
    } else if (user_str_eq(name, "mounts")) {
        user_write_cstr("shell shortcut - runs umnts_c.elf\n");
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
               user_str_eq(name, "rename") ||
               user_str_eq(name, "mv") ||
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
        "  rename [old] [new], mv [old] [new]\n"
        "  pid, ppid\n"
        "  run [file], sleep [ticks], yield, resume [pid], kill [pid], bg [pid], fg [pid], echo [text]\n"
        "Shell shortcuts:\n"
        "  sched, memstat, bootinfo, mounts\n"
        "Standalone tools:\n"
        "  uls [path], umkdir [path], urmdir [path], utouch [file], usave [file] [text],\n"
        "  ucat [file], urm [file]\n"
        "  upid, uschd, umem, uvers, uboot, umounts, uargs\n"
        "  uio [file] [text], uio append [file] [text], uio seek [file] [offset],\n"
        "  uio fill [file] [size], uio leak [file] [text]\n"
        "Processes:\n"
        "  ujobs, ulast, uwait, urun\n"
        "Text:\n"
        "  echo [text]\n");
}
