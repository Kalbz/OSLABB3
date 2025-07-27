# Custom Filesystem for BTHs Operating Systems course

*A miniature filesystem built from scratch in C++.*

---

## 🔧 Supported Commands

| Command          | Description                                                             |
|------------------|-------------------------------------------------------------------------|
| `format`         | Initializes the filesystem, clearing everything                         |
| `create <file>`  | Creates a new file and writes content (until an empty line is entered)  |
| `cat <file>`     | Displays the contents of a file                                         |
| `ls`             | Lists the contents of the current directory                             |
| `cd <dir>`       | Changes the current directory                                           |
| `pwd`            | Prints the absolute path to the current directory                       |
| `mkdir <dir>`    | Creates a new subdirectory                                              |
| `rm <file|dir>`  | Deletes a file or an empty directory                                    |
| `cp <src> <dst>` | Copies a file from source to destination                                |
| `mv <src> <dst>` | Moves (or renames) a file or directory                                  |
| `append <A> <B>` | Appends the contents of file A to file B                                |
| `chmod <rights> <file>` | Changes access rights (e.g. `chmod 6 file.txt` gives rw-)        |

---

## 📁 File Structure

| File             | Description                                      |
|------------------|--------------------------------------------------|
| `disk.cpp/h`     | Simulated disk layer (block-based)               |
| `fs.cpp/h`       | Core filesystem logic and shell command handlers |
| `shell.cpp/h`    | Command parser and interactive shell loop        |
| `main.cpp`       | Entry point launching the shell                  |
| `test_commands.txt` | Sample script with test commands              |
| `Makefile`       | Build configuration for the project              |


