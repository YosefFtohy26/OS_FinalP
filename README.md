# myShell — Custom Command-Line Shell

Faculty of Computers and Data Science, Alexandria University  
Operating Systems — Spring 2026

---

## Compilation

```bash
make          # builds the 'myShell' binary
make clean    # removes the binary
```

Or manually:

```bash
gcc -Wall -Wextra -pedantic -std=c11 -o myShell myShell.c
```

## Running

```bash
./myShell
```

---

## Features

### 1. Basic Shell Loop
- Displays `myShell>` prompt.
- Reads input with `getline()` (handles arbitrarily long lines).
- Exits cleanly on `Ctrl+D` (EOF).

### 2. Built-in Commands

| Command | Description |
|---------|-------------|
| `cd <dir>` | Change directory (`~` and `..` supported). |
| `pwd` | Print working directory. |
| `exit` | Terminate the shell. |
| `history` | Print numbered command history (up to 100 entries). |
| `!n` | Re-execute the n-th history entry. |

### 3. Process Management
- **Foreground**: shell waits for the child with `waitpid()`.
- **Background**: append `&`; shell prints the PID and continues.  
  Example: `sleep 10 &`
- Finished background processes are reaped at each prompt (no zombies).

### 4. I/O Redirection
- `>` — redirect stdout to a file (creates/truncates).
- `<` — redirect stdin from a file.

Examples:
```
myShell> ls > files.txt
myShell> cat < files.txt
myShell> sort < input.txt > sorted.txt
```

### 5. Pipes
- Arbitrary chains of `|`-separated commands.

Examples:
```
myShell> ls -l | grep ".c"
myShell> cat file.txt | sort | uniq
```

### 6. Signal Handling
- `Ctrl+C` (SIGINT): does **not** kill the shell; only terminates the current foreground child process.
- `Ctrl+Z` (SIGTSTP): ignored by the shell.

### 7. Error Handling
- Unknown command: `myShell: <cmd>: command not found`
- File errors: reported via `perror()`
- `fork()`/`pipe()` failures: reported via `perror()`

---

## Implementation Notes

All execution uses the required system calls:
- `fork()` — create child processes
- `execvp()` — from the `exec()` family
- `waitpid()` — wait for foreground children; `WNOHANG` for background reaping
- `pipe()` + `dup2()` — for pipes and I/O redirection

`system()` is **not used** anywhere.

---

## Team Contributions

| Student Name | Student ID | Responsibility |
|---|---|---|
| (Member 1) | (ID) | Parts 1, 2 — Main loop & parser |
| (Member 2) | (ID) | Parts 3, 4 — Built-in commands & history |
| (Member 3) | (ID) | Parts 5, 6 — Process management |
| (Member 4) | (ID) | Parts 7, 8 — I/O redirection & pipes |
| (Member 5) | (ID) | Parts 9, 10 — Signal handling, Makefile, integration |
