#ifndef SHELL_H
#define SHELL_H

char* get_argument(char* input);
void dump_heap();
void execute_command();

void shell_save_history();

#ifdef __cplusplus
extern "C" {
#endif

void shell_input(char ascii);
void shell_recall_history(int direction);

#ifdef __cplusplus
}
#endif


#endif