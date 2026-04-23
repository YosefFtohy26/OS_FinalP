/*
 * myShell.c - A functional command-line shell
 * Alexandria University - Operating Systems Spring 2026
 * Implements: prompt, built-ins (cd, pwd, exit, history),
 *             foreground/background execution, I/O redirection,
 *             pipes, signal handling, and error handling.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE   700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>

/* ─── Constants ────────────────────────────────────────────────────────────── */
#define SHELL_MAX_INPUT 1024
#define MAX_ARGS       128
#define MAX_PIPES       16
#define MAX_HISTORY    100
#define MAX_BG_PROCS    64

/* ─── History ───────────────────────────────────────────────────────────────── */
static char *history[MAX_HISTORY];
static int   history_count = 0;

void history_add(const char *line) {
    if (history_count < MAX_HISTORY) {
        history[history_count++] = strdup(line);
    } else {
        /* Shift out oldest entry */
        free(history[0]);
        memmove(history, history + 1, (MAX_HISTORY - 1) * sizeof(char *));
        history[MAX_HISTORY - 1] = strdup(line);
    }
}

void history_print(void) {
    for (int i = 0; i < history_count; i++)
        printf("%4d  %s\n", i + 1, history[i]);
}

void history_free(void) {
    for (int i = 0; i < history_count; i++) free(history[i]);
}

/* ─── Background process tracking ──────────────────────────────────────────── */
static pid_t bg_pids[MAX_BG_PROCS];
static int   bg_count = 0;

void bg_add(pid_t pid) {
    if (bg_count < MAX_BG_PROCS)
        bg_pids[bg_count++] = pid;
}

/* Reap any finished background processes (call every prompt iteration) */
void bg_reap(void) {
    for (int i = 0; i < bg_count; ) {
        int status;
        pid_t result = waitpid(bg_pids[i], &status, WNOHANG);
        if (result > 0) {
            printf("[%d] Done\n", bg_pids[i]);
            bg_pids[i] = bg_pids[--bg_count]; /* remove by swapping with last */
        } else {
            i++;
        }
    }
}

/* ─── Signal handling ───────────────────────────────────────────────────────── */
static pid_t fg_pid = -1; /* PID of the current foreground child, or -1 */

void sigint_handler(int sig) {
    (void)sig;
    if (fg_pid > 0) {
        kill(fg_pid, SIGINT);
    } else {
        /* Print a newline and re-display prompt */
        write(STDOUT_FILENO, "\n", 1);
    }
}

/* ─── Command structure ─────────────────────────────────────────────────────── */
typedef struct {
    char *argv[MAX_ARGS]; /* NULL-terminated argument list */
    int   argc;
    char *input_file;     /* NULL if no redirection */
    char *output_file;    /* NULL if no redirection */
    int   background;     /* 1 if & was detected */
} Command;

/* ─── Parser ────────────────────────────────────────────────────────────────── */

/*
 * Split the raw input line by '|' into segments.
 * Returns number of segments.
 */
int split_pipes(char *line, char *segments[], int max_segs) {
    int count = 0;
    char *tok = strtok(line, "|");
    while (tok && count < max_segs) {
        segments[count++] = tok;
        tok = strtok(NULL, "|");
    }
    return count;
}

/*
 * Parse a single command segment (no pipes) into a Command struct.
 * Handles <, >, &.
 */
void parse_command(char *segment, Command *cmd) {
    memset(cmd, 0, sizeof(Command));

    char *tokens[MAX_ARGS];
    int   ntok = 0;

    char *tok = strtok(segment, " \t\r\n");
    while (tok && ntok < MAX_ARGS - 1) {
        tokens[ntok++] = tok;
        tok = strtok(NULL, " \t\r\n");
    }
    tokens[ntok] = NULL;

    for (int i = 0; i < ntok; i++) {
        if (strcmp(tokens[i], "<") == 0) {
            if (i + 1 < ntok) cmd->input_file = tokens[++i];
        } else if (strcmp(tokens[i], ">") == 0) {
            if (i + 1 < ntok) cmd->output_file = tokens[++i];
        } else if (strcmp(tokens[i], "&") == 0) {
            cmd->background = 1;
        } else {
            cmd->argv[cmd->argc++] = tokens[i];
        }
    }
    cmd->argv[cmd->argc] = NULL;
}

/* ─── Built-in commands ─────────────────────────────────────────────────────── */

/* Returns 1 if the command was a built-in (and was handled), 0 otherwise */
int handle_builtin(Command *cmd) {
    if (cmd->argc == 0) return 1; /* empty command */

    /* exit */
    if (strcmp(cmd->argv[0], "exit") == 0) {
        history_free();
        exit(EXIT_SUCCESS);
    }

    /* pwd */
    if (strcmp(cmd->argv[0], "pwd") == 0) {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)))
            printf("%s\n", cwd);
        else
            perror("pwd");
        return 1;
    }

    /* cd */
    if (strcmp(cmd->argv[0], "cd") == 0) {
        const char *dir;
        if (cmd->argc < 2 || strcmp(cmd->argv[1], "~") == 0) {
            dir = getenv("HOME");
            if (!dir) { fprintf(stderr, "cd: HOME not set\n"); return 1; }
        } else if (strcmp(cmd->argv[1], "..") == 0) {
            dir = "..";
        } else {
            dir = cmd->argv[1];
        }
        if (chdir(dir) != 0)
            perror("cd");
        return 1;
    }

    /* history */
    if (strcmp(cmd->argv[0], "history") == 0) {
        history_print();
        return 1;
    }

    /* !n — run the n-th history entry */
    if (cmd->argv[0][0] == '!' && cmd->argc == 1) {
        int n = atoi(cmd->argv[0] + 1);
        if (n < 1 || n > history_count) {
            fprintf(stderr, "history: no such entry: %d\n", n);
        } else {
            printf("%s\n", history[n - 1]);
            /* Execute it by reprinting into a fresh buffer and parsing */
            char buf[SHELL_MAX_INPUT];
            strncpy(buf, history[n - 1], sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            /* We'll re-use execute_line defined below — forward declaration via pointer trick.
               Instead, just print a notice; full recursive execution shown in main loop. */
        }
        return 1;
    }

    return 0; /* not a built-in */
}

/* ─── Execution helpers ─────────────────────────────────────────────────────── */

/*
 * Set up I/O redirection for a child process.
 * Call after fork(), before exec().
 */
void setup_redirection(Command *cmd) {
    if (cmd->input_file) {
        int fd = open(cmd->input_file, O_RDONLY);
        if (fd < 0) { perror(cmd->input_file); exit(EXIT_FAILURE); }
        if (dup2(fd, STDIN_FILENO) < 0) { perror("dup2 stdin"); exit(EXIT_FAILURE); }
        close(fd);
    }
    if (cmd->output_file) {
        int fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { perror(cmd->output_file); exit(EXIT_FAILURE); }
        if (dup2(fd, STDOUT_FILENO) < 0) { perror("dup2 stdout"); exit(EXIT_FAILURE); }
        close(fd);
    }
}

/*
 * Execute a single command (no pipe) — fork + exec.
 */
void execute_single(Command *cmd) {
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return; }

    if (pid == 0) {
        /* Child: restore SIGINT to default */
        signal(SIGINT, SIG_DFL);
        setup_redirection(cmd);
        execvp(cmd->argv[0], cmd->argv);
        /* execvp only returns on error */
        fprintf(stderr, "myShell: %s: command not found\n", cmd->argv[0]);
        exit(EXIT_FAILURE);
    }

    /* Parent */
    if (cmd->background) {
        printf("[%d]\n", pid);
        bg_add(pid);
    } else {
        fg_pid = pid;
        int status;
        waitpid(pid, &status, 0);
        fg_pid = -1;
    }
}

/*
 * Execute a pipeline of 'nseg' commands.
 * segments[] are already populated Command structs.
 */
void execute_pipeline(Command *cmds, int nseg) {
    if (nseg == 1) {
        execute_single(&cmds[0]);
        return;
    }

    int pipes[MAX_PIPES][2];
    pid_t pids[MAX_PIPES + 1];

    /* Create all pipes */
    for (int i = 0; i < nseg - 1; i++) {
        if (pipe(pipes[i]) < 0) { perror("pipe"); return; }
    }

    for (int i = 0; i < nseg; i++) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return; }

        if (pid == 0) {
            signal(SIGINT, SIG_DFL);

            /* Hook up stdin from previous pipe */
            if (i > 0) {
                if (dup2(pipes[i-1][0], STDIN_FILENO) < 0) { perror("dup2"); exit(EXIT_FAILURE); }
            }
            /* Hook up stdout to next pipe */
            if (i < nseg - 1) {
                if (dup2(pipes[i][1], STDOUT_FILENO) < 0) { perror("dup2"); exit(EXIT_FAILURE); }
            }

            /* Close all pipe fds in child */
            for (int j = 0; j < nseg - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            /* Apply any explicit file redirections (first/last segment) */
            setup_redirection(&cmds[i]);

            execvp(cmds[i].argv[0], cmds[i].argv);
            fprintf(stderr, "myShell: %s: command not found\n", cmds[i].argv[0]);
            exit(EXIT_FAILURE);
        }

        pids[i] = pid;
    }

    /* Parent: close all pipe fds */
    for (int i = 0; i < nseg - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    /* Background flag is taken from the last command */
    if (cmds[nseg - 1].background) {
        for (int i = 0; i < nseg; i++) bg_add(pids[i]);
        printf("[%d]\n", pids[nseg - 1]);
    } else {
        /* Wait for all children */
        fg_pid = pids[nseg - 1];
        for (int i = 0; i < nseg; i++) {
            int status;
            waitpid(pids[i], &status, 0);
        }
        fg_pid = -1;
    }
}

/* ─── Main execution entry point ────────────────────────────────────────────── */

void execute_line(char *line) {
    /* Split into pipe segments */
    char *seg_strs[MAX_PIPES + 1];
    /* Work on a copy so strtok doesn't clobber the original */
    char line_copy[SHELL_MAX_INPUT];
    strncpy(line_copy, line, sizeof(line_copy) - 1);
    line_copy[sizeof(line_copy) - 1] = '\0';

    int nseg = split_pipes(line_copy, seg_strs, MAX_PIPES + 1);
    if (nseg == 0) return;

    Command cmds[MAX_PIPES + 1];
    for (int i = 0; i < nseg; i++)
        parse_command(seg_strs[i], &cmds[i]);

    /* If it's a single command, check built-ins first */
    if (nseg == 1 && handle_builtin(&cmds[0]))
        return;

    /* Check argv[0] is not empty */
    if (cmds[0].argc == 0) return;

    execute_pipeline(cmds, nseg);
}

/* ─── Main loop ─────────────────────────────────────────────────────────────── */

int main(void) {
    /* Set up SIGINT: shell ignores it; we forward to fg child manually */
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, NULL);

    /* Shell ignores SIGTSTP (Ctrl+Z) — optional extra */
    signal(SIGTSTP, SIG_IGN);

    char  *line = NULL;
    size_t line_cap = 0;
    ssize_t nread;

    while (1) {
        /* Reap finished background processes */
        bg_reap();

        /* Display prompt */
        printf("myShell> ");
        fflush(stdout);

        /* Read a line */
        nread = getline(&line, &line_cap, stdin);
        if (nread == -1) {
            /* EOF (Ctrl+D) */
            if (feof(stdin)) {
                printf("\n");
                break;
            }
            /* Interrupted by signal — just loop */
            clearerr(stdin);
            printf("\n");
            continue;
        }

        /* Strip trailing newline */
        if (nread > 0 && line[nread - 1] == '\n')
            line[nread - 1] = '\0';

        /* Skip blank lines */
        if (line[0] == '\0') continue;

        /* Handle !n history execution before adding to history */
        if (line[0] == '!' && line[1] != '\0') {
            int n = atoi(line + 1);
            if (n < 1 || n > history_count) {
                fprintf(stderr, "myShell: %s: event not found\n", line);
                continue;
            }
            /* Print and execute the recalled command */
            char recalled[SHELL_MAX_INPUT];
            strncpy(recalled, history[n - 1], sizeof(recalled) - 1);
            recalled[sizeof(recalled) - 1] = '\0';
            printf("%s\n", recalled);
            history_add(recalled);
            execute_line(recalled);
            continue;
        }

        /* Add to history */
        history_add(line);

        /* Execute */
        execute_line(line);
    }

    free(line);
    history_free();
    return 0;
}
