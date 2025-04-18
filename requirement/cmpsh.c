#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <fcntl.h>
#include <stdbool.h>

#define MAX_INPUT 1024
#define MAX_ARGS 64
#define MAX_PATHS 64
#define MAX_COMMANDS 10  // Support for up to 10 piped commands

// Global variables
char *paths[MAX_PATHS];
int path_count = 1; // Initialized with /bin

// Function prototypes
void init_shell();
void print_prompt();
void read_input(char *input);
void parse_args(char *cmd, char **args, int *arg_count);
void parse_command(char *cmd, char **args, int *arg_count, char **redirect_file);
void parse_input_multi(char *input, char ***all_args, int *arg_counts, char **redirect_files, int *command_count);
void execute_commands(char ***all_args, int *arg_counts, char **redirect_files, int command_count);
int is_builtin_command(char **args, int arg_count);
void handle_builtin(char **args, int arg_count);
void handle_external(char **args, int arg_count, char *redirect_file);
void handle_signal(int sig);
void execute_pipeline(char ***all_args, int *arg_counts, char **redirect_files, int command_count);

int main(int argc, char *argv[]) {
    // Initialize shell
    init_shell();
    
    // Handle non-interactive mode
    if (argc > 1) {
        FILE *script = fopen(argv[1], "r");
        if (!script) {
            fprintf(stderr, "An error has occurred!\n");
            exit(1);
        }
        
        char input[MAX_INPUT];
        while (fgets(input, MAX_INPUT, script)) {
            input[strcspn(input, "\n")] = '\0';
            
            // Allocate memory for all commands
            char **all_args[MAX_COMMANDS];
            int arg_counts[MAX_COMMANDS] = {0};
            char *redirect_files[MAX_COMMANDS] = {NULL};
            int command_count = 0;
            
            for (int i = 0; i < MAX_COMMANDS; i++) {
                all_args[i] = calloc(MAX_ARGS, sizeof(char*));
            }
            
            parse_input_multi(input, all_args, arg_counts, redirect_files, &command_count);
            
            if (command_count > 0 && arg_counts[0] > 0) {
                execute_commands(all_args, arg_counts, redirect_files, command_count);
            }
            
            // Free allocated memory
            for (int i = 0; i < MAX_COMMANDS; i++) {
                for (int j = 0; j < arg_counts[i]; j++) {
                    if (all_args[i][j]) free(all_args[i][j]);
                }
                free(all_args[i]);
                if (redirect_files[i]) free(redirect_files[i]);
            }
        }
        fclose(script);
        exit(0);
    }
    
    // Interactive mode
    char input[MAX_INPUT];
    while (1) {
        print_prompt();
        read_input(input);
        
        // Allocate memory for all commands
        char **all_args[MAX_COMMANDS];
        int arg_counts[MAX_COMMANDS] = {0};
        char *redirect_files[MAX_COMMANDS] = {NULL};
        int command_count = 0;
        
        for (int i = 0; i < MAX_COMMANDS; i++) {
            all_args[i] = calloc(MAX_ARGS, sizeof(char*));
        }
        
        parse_input_multi(input, all_args, arg_counts, redirect_files, &command_count);
        
        if (command_count > 0 && arg_counts[0] > 0) {
            execute_commands(all_args, arg_counts, redirect_files, command_count);
        }
        
        // Free allocated memory
        for (int i = 0; i < MAX_COMMANDS; i++) {
            for (int j = 0; j < arg_counts[i]; j++) {
                if (all_args[i][j]) free(all_args[i][j]);
            }
            free(all_args[i]);
            if (redirect_files[i]) free(redirect_files[i]);
        }
    }
    
    return 0;
}

void init_shell() {
    paths[0] = strdup("/bin");
    path_count = 1;
    signal(SIGINT, handle_signal);
    signal(SIGTSTP, handle_signal);
}

void print_prompt() {
    printf("cmpsh> ");
    fflush(stdout);
}

void read_input(char *input) {
    if (!fgets(input, MAX_INPUT, stdin)) {
        exit(0);
    }
    input[strcspn(input, "\n")] = '\0';
}

void parse_args(char *cmd, char **args, int *arg_count) {
    char *p = cmd;
    char *arg = malloc(1024);
    int arg_len = 0;
    bool in_single = false;
    bool in_double = false;
    *arg_count = 0;
    while (*p) {
        if (*p == '\'' && !in_double) {
            if (!in_single) {
                in_single = true;
            } else {
                in_single = false;
            }
            p++;  // Skip the quote
            continue;
        } else if (*p == '"' && !in_single) {
            if (!in_double) {
                in_double = true;
            } else {
                in_double = false;
            }
            p++;  // Skip the quote
            continue;
        } else if (*p == ' ' && !in_single && !in_double) {
            if (arg_len > 0) {
                arg[arg_len] = '\0';
                args[*arg_count] = strdup(arg);
                (*arg_count)++;
                arg_len = 0;
            }
            while (*p == ' ') p++;
            continue;
        } else {
            arg[arg_len++] = *p++;
        }
    }
    if (arg_len > 0) {
        arg[arg_len] = '\0';
        args[*arg_count] = strdup(arg);
        (*arg_count)++;
    }
    args[*arg_count] = NULL;
    free(arg);
}

void parse_command(char *cmd, char **args, int *arg_count, char **redirect_file) {
    char *redirect_pos = strstr(cmd, ">");
    if (redirect_pos != NULL) {
        if (redirect_pos == cmd || *(redirect_pos + 1) == '\0') {
            fprintf(stderr, "An error has occurred!\n");
            *arg_count = 0;
            return;
        }
        *redirect_pos = '\0';
        char *file_part = redirect_pos + 1;
        while (*file_part == ' ') file_part++;
        char *file_token = strtok(file_part, " ");
        if (file_token && strtok(NULL, " ") != NULL) {
            fprintf(stderr, "An error has occurred!\n");
            *arg_count = 0;
            return;
        }
        *redirect_file = strdup(file_token);
    } else {
        *redirect_file = NULL;
    }
    parse_args(cmd, args, arg_count);
}

void parse_input_multi(char *input, char ***all_args, int *arg_counts, char **redirect_files, int *command_count) {
    *command_count = 0;
    if (input[0] == '\0' || strspn(input, " ") == strlen(input)) {
        return;
    }
    
    // Make a copy of input to work with
    char *input_copy = strdup(input);
    char *cmd = strtok(input_copy, "|");
    
    while (cmd != NULL && *command_count < MAX_COMMANDS) {
        // Trim leading spaces
        while (*cmd == ' ') cmd++;
        
        parse_command(cmd, all_args[*command_count], &arg_counts[*command_count], &redirect_files[*command_count]);
        (*command_count)++;
        
        cmd = strtok(NULL, "|");
    }
    
    free(input_copy);
}

void execute_commands(char ***all_args, int *arg_counts, char **redirect_files, int command_count) {
    if (command_count == 1) {
        // Single command case (no pipes)
        if (is_builtin_command(all_args[0], arg_counts[0])) {
            handle_builtin(all_args[0], arg_counts[0]);
        } else {
            handle_external(all_args[0], arg_counts[0], redirect_files[0]);
        }
    } else if (command_count > 1) {
        // Multiple commands with pipes
        execute_pipeline(all_args, arg_counts, redirect_files, command_count);
    }
}

void execute_pipeline(char ***all_args, int *arg_counts, char **redirect_files, int command_count) {
    int pipes[MAX_COMMANDS-1][2];
    pid_t pids[MAX_COMMANDS];
    int status;
    
    // Create all pipes needed
    for (int i = 0; i < command_count - 1; i++) {
        if (pipe(pipes[i]) == -1) {
            fprintf(stderr, "An error has occurred!\n");
            return;
        }
    }
    
    // Create processes for each command in the pipeline
    for (int i = 0; i < command_count; i++) {
        pids[i] = fork();
        
        if (pids[i] < 0) {
            fprintf(stderr, "An error has occurred!\n");
            // Clean up previously created pipes
            for (int j = 0; j < i; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            return;
        }
        
        if (pids[i] == 0) {  // Child process
            // Configure pipes for this process:
            
            // If not the first command, read from previous pipe
            if (i > 0) {
                if (dup2(pipes[i-1][0], STDIN_FILENO) < 0) {
                    fprintf(stderr, "An error has occurred!\n");
                    exit(1);
                }
            }
            
            // If not the last command, write to next pipe
            if (i < command_count - 1) {
                if (dup2(pipes[i][1], STDOUT_FILENO) < 0) {
                    fprintf(stderr, "An error has occurred!\n");
                    exit(1);
                }
            } else if (redirect_files[i]) {
                // Handle redirection for the last command
                int fd = open(redirect_files[i], O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) {
                    fprintf(stderr, "An error has occurred!\n");
                    exit(1);
                }
                if (dup2(fd, STDOUT_FILENO) < 0) {
                    fprintf(stderr, "An error has occurred!\n");
                    exit(1);
                }
                close(fd);
            }
            
            // Close all pipe file descriptors in the child
            for (int j = 0; j < command_count - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            
            // Check if this is a builtin command (special case in pipeline)
            if (i == 0 && is_builtin_command(all_args[i], arg_counts[i])) {
                // We need to capture the output of builtin commands when in a pipe
                // This is a simplified approach - ideally we'd capture the output
                FILE *temp = tmpfile();
                int original_stdout = dup(STDOUT_FILENO);
                dup2(fileno(temp), STDOUT_FILENO);
                
                handle_builtin(all_args[i], arg_counts[i]);
                
                fflush(stdout);
                rewind(temp);
                dup2(original_stdout, STDOUT_FILENO);
                close(original_stdout);
                
                char buffer[4096];
                while (fgets(buffer, sizeof(buffer), temp) != NULL) {
                    printf("%s", buffer);
                }
                fclose(temp);
                exit(0);
            } else {
                // Regular command: search through paths and execute
                if (all_args[i][0][0] == '/' || (all_args[i][0][0] == '.' && all_args[i][0][1] == '/')) {
                    // Direct path execution
                    execv(all_args[i][0], all_args[i]);
                } else {
                    // Search in paths
                    for (int j = 0; j < path_count; j++) {
                        char full_path[1024];
                        snprintf(full_path, sizeof(full_path), "%s/%s", paths[j], all_args[i][0]);
                        if (access(full_path, X_OK) == 0) {
                            execv(full_path, all_args[i]);
                            break;
                        }
                    }
                }
                // If we get here, execv failed
                fprintf(stderr, "An error has occurred!\n");
                exit(1);
            }
        }
    }
    
    // Parent process: close all pipe fds and wait for all children
    for (int i = 0; i < command_count - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    
    // Wait for all children to complete
    for (int i = 0; i < command_count; i++) {
        waitpid(pids[i], &status, 0);
    }
}

int is_builtin_command(char **args, int arg_count) {
    (void)arg_count;
    if (strcmp(args[0], "exit") == 0) return 1;
    if (strcmp(args[0], "cd") == 0) return 1;
    if (strcmp(args[0], "pwd") == 0) return 1;
    if (strcmp(args[0], "paths") == 0) return 1;
    if (strcmp(args[0], "path") == 0) return 1;  // Added new "path" command
    return 0;
}

void handle_builtin(char **args, int arg_count) {
    if (strcmp(args[0], "exit") == 0) {
        if (arg_count > 1) {
            fprintf(stderr, "An error has occurred!\n");
            return;
        }
        exit(0);
    }
    else if (strcmp(args[0], "cd") == 0) {
        if (arg_count != 2) {
            fprintf(stderr, "An error has occurred!\n");
            return;
        }
        if (chdir(args[1])) {
            fprintf(stderr, "An error has occurred!\n");
        }
    }
    else if (strcmp(args[0], "pwd") == 0) {
        if (arg_count > 1) {
            fprintf(stderr, "An error has occurred!\n");
            return;
        }
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd))) {
            printf("%s\n", cwd);
        } else {
            fprintf(stderr, "An error has occurred!\n");
        }
    }
    else if (strcmp(args[0], "paths") == 0 || strcmp(args[0], "path") == 0) {
        // Free existing paths
        for (int i = 0; i < path_count; i++) {
            free(paths[i]);
        }
        
        if (arg_count == 1) {
            path_count = 0;
        } else {
            path_count = arg_count - 1;
            for (int i = 0; i < path_count; i++) {
                paths[i] = strdup(args[i+1]);
            }
        }
    }
}

void handle_external(char **args, int arg_count, char *redirect_file) {
    (void)arg_count;
    
    // Check if the command has a path prefix (starts with ./ or /)
    if (args[0][0] == '/' || (args[0][0] == '.' && args[0][1] == '/')) {
        pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr, "An error has occurred!\n");
            return;
        }
        if (pid == 0) {  // Child process
            if (redirect_file) {
                int fd = open(redirect_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) {
                    fprintf(stderr, "An error has occurred!\n");
                    exit(1);
                }
                if (dup2(fd, STDOUT_FILENO) < 0) {
                    fprintf(stderr, "An error has occurred!\n");
                    exit(1);
                }
                close(fd);
            }
            
            // Execute with the exact path
            execv(args[0], args);
            fprintf(stderr, "An error has occurred!\n");
            exit(1);
        } else {
            waitpid(pid, NULL, 0);
        }
        return;
    }
    
    // Regular command with path searching
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "An error has occurred!\n");
        return;
    }
    if (pid == 0) {  // Child process
        if (redirect_file) {
            int fd = open(redirect_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                fprintf(stderr, "An error has occurred!\n");
                exit(1);
            }
            if (dup2(fd, STDOUT_FILENO) < 0) {
                fprintf(stderr, "An error has occurred!\n");
                exit(1);
            }
            close(fd);
        }
        
        // Search for the command in all paths
        for (int i = 0; i < path_count; i++) {
            char full_path[1024];
            snprintf(full_path, sizeof(full_path), "%s/%s", paths[i], args[0]);
            if (access(full_path, X_OK) == 0) {
                execv(full_path, args);
                // If execv returns, it failed
                fprintf(stderr, "An error has occurred!\n");
                exit(1);
            }
        }
        
        fprintf(stderr, "An error has occurred!\n");
        exit(1);
    } else {
        waitpid(pid, NULL, 0);
    }
}

void handle_signal(int sig) {
    (void)sig;
}