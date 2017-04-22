//
// Created by Kareem Halabi - 260616162 on 1/26/2017.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wait.h>
#include <sys/fcntl.h>

int getcmd(char *prompt, char *args[], char ** line_buffer, int *background, int *redir, int *pipe_loc);
void handle_signal(int sig);
int execute_internal(char *args[]);
void print_jobs();

pid_t current_pid = 0;

// Current capacity is 20 jobs
pid_t process_pids[20];
char * process_cmds[20];
int process_num = 0;

// For storing line from std in
char ** line_buffer;

int main() {

    line_buffer = calloc(20, sizeof(char*));
    char * args[20] = {};
    int bg, redir, pipe_loc, std_out = 0, fd = 0, pipe_write = 0;
    int p[2];

    // Handle Ctrl+C and Ctrl+Z (for the latter, a new line is printed instead of completely ignoring it)
    if(signal(SIGINT, handle_signal) == SIG_ERR || signal(SIGTSTP, handle_signal) == SIG_ERR ) {
        printf("Could not bind signal handler");
        free(*line_buffer);
        free(line_buffer);
        exit(EXIT_FAILURE);
    }

    while(1) {
        bg = 0, redir = 0, pipe_loc = 0;
        int count = getcmd("\nsh >> ", args, line_buffer, &bg, &redir, &pipe_loc);

        // Check for output redirection (assuming file name has no spaces)
        if(redir) {
            std_out = dup(STDOUT_FILENO);
            close(STDOUT_FILENO);
            fd = open(args[count-1],  O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
            args[count-1] = NULL;
        }

        // Check for built in commands
        if(execute_internal(args) == 0) {

            // Command was not built in, delegate to child process
            current_pid = fork();

            // Error
            if (current_pid == (pid_t) -1) {
                printf("An error has occurred in forking\n");
                free(*line_buffer);
                free(line_buffer);
                exit(EXIT_FAILURE);
            }

            // Child runs
            else if (current_pid == (pid_t) 0) {

                if(bg) {
                    signal(SIGINT, SIG_IGN); // Ignore Ctrl + C if process is to run in background
                }

                // Check for pipe
                if (pipe_loc != 0) {
                    if(pipe(p) == -1) {
                        printf("Error occurred during pipe, abort\n");
                        free(*line_buffer);
                        free(line_buffer);
                        exit(EXIT_FAILURE);
                    }

                    // Redirect std_out to pipe
                    std_out = dup(STDOUT_FILENO);
                    close(STDOUT_FILENO);
                    pipe_write = dup(p[1]);

                     // Create another child to run second command
                    if(fork() == 0) {
                        // Redirect STDIN to read portion of pipe
                        close(STDIN_FILENO);
                        dup(p[0]);

                        // Reset STD_OUT to terminal
                        close(pipe_write);
                        dup(std_out);

                        // Close remaining file descriptors
                        close(p[0]);
                        close(p[1]);
                        close(std_out);

                        // Execute second part of command
                        execvp( *(args + pipe_loc), args + pipe_loc);

                        printf("An error has occurred in executing \"%s\"\n", *(args + pipe_loc));
                        free(*line_buffer);
                        free(line_buffer);
                        exit(EXIT_FAILURE);
                    }
                }

                execvp(args[0], args);

                printf("An error has occurred in executing \"%s\"\n",args[0]);
                free(*line_buffer);
                free(line_buffer);
                exit(EXIT_FAILURE);
            } else {
                // Foreground, wait for child to finish
                if (!bg) {
                    int status;
                    waitpid(current_pid, &status, 0);
                    usleep(10000); // to allow enough time for pipe command to be on own line

                    if (WEXITSTATUS(status) != 0) // print exit code of child if error occured
                        printf("Process finished with exit code %d\n", WEXITSTATUS(status));
                }

                // Add background processes to jobs list
                else {
                    process_pids[process_num] = current_pid;
                    process_cmds[process_num] = args[0];
                    process_num++;
                }
            }

            // Indicate no current process running in foreground
            current_pid = 0;
        }

        // Reset default std out
        if(redir) {
            close(fd);
            dup(std_out);
            close(std_out);
        }
        memset(args, 0, sizeof(args) * sizeof(char*)); // wipe the argument list from previous command
        free(*line_buffer); // free line buffer
    }
}

void handle_signal(int sig) {
    // Kill a foreground process if Ctrl+C is sent
    if(current_pid !=0 && sig == SIGINT) {
        kill(current_pid, SIGKILL);
    }
    // Print new line otherwise
    else {
        printf("\nsh >> ");
        fflush(stdout);
    }
}

// Modified function from assignment description
int getcmd(char *prompt, char *args[], char ** line_buffer, int *background, int *redir, int *pipe_loc) {
    int i = 0;
    char *token, *loc;
    char *line = NULL;
    size_t linecap = 0;

    printf("%s", prompt);

    if(getline(&line, &linecap, stdin) <= 0) {
        free(*line_buffer);
        free(line_buffer);
        exit(-1);
    }

    // Set so that buffer can be freed later
    *line_buffer = line;

    // Check for background
    if( (loc = index(line, '&')) != NULL ) {
        *background = 1;
        *loc = ' ';
    } else
        *background = 0;

    // Check for output redirection
    if ((loc = index(line, '>')) != NULL) {
        *redir = 1;
        *loc = ' ';
    } else
        *redir = 0;

    // Cycle through every argument
    while( (token = strsep(&line, " \t\n")) != NULL ) {

        for (int j = 0; j < strlen(token); j++) {
            // Ignore control characters
            if (token[j] <= 32)
                token[j] = '\0';
        }

        // Copy non-empty tokens to args array
        if(strlen(token) > 0) {

            // Store location of pipe if exists (separating both args with a NULL)
            if(strcmp(token, "|") == 0) {
                args[i++] = NULL;
                *pipe_loc = i;
            } else {
                args[i++] = token;
            }
        }
    }

    return i;
}

// Checks if a command is a shell command and executes it
// Returns 1 if command is in fact a shell command, 0 otherwise
int execute_internal(char *args[]) {

    int is_internal = 0;

    char * cmd = args[0];
    if(strcmp(cmd, "pwd") == 0) {
        printf("%s%s", getcwd(NULL, 0), "\n");
        is_internal = 1;
    } else if (strcmp(cmd, "cd") == 0) {
        chdir(args[1]);
        printf("New wd: %s\n", getcwd(NULL, 0));
        is_internal = 1;
    } else if (strcmp(cmd, "exit") == 0) {
        free(*line_buffer);
        free(line_buffer);
        exit(EXIT_SUCCESS);
    } else if (strcmp(cmd, "jobs") == 0) {
        print_jobs();
        is_internal = 1;
    } else if (strcmp(cmd, "fg") == 0) {
        // Check if job number is specified
        if (args[1] != NULL) {

            // Check if job number is valid
            if (process_pids[atoi(args[1]) != 0])
                waitpid(process_pids[atoi(args[1])], NULL, 0);
            else
                printf("Invalid job");
        }
        else
            printf("No job selected");
        is_internal = 1;
    }

    return is_internal;
}

// Prints all jobs that are still running
void print_jobs() {
    for(int i = 0; i < process_num; i++) {
        if(waitpid(process_pids[i], NULL, WNOHANG) == 0) { //Process has not exited
            printf("[%d]\t%s\t%d\n", i, process_cmds[i], process_pids[i]);
        }
    }
}
