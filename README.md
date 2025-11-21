[![Review Assignment Due Date](https://classroom.github.com/assets/deadline-readme-button-22041afd0340ce965d47ae6ef1cefeee28c7c493a6346c4f15d667ab976d596c.svg)](https://classroom.github.com/a/0ek2UV58)

# LangOS Docs++ Prototype

This repository contains our implementation of the OSN course project: a simplified distributed document system with a Name Server (NM), multiple Storage Servers (SS), and CLI clients.

## Prerequisites

- GCC/Clang with POSIX threads support
- GNU Make
- Linux environment (tested on WSL2 + Ubuntu)

## Building

```bash
make clean && make
```

This produces:

- `bin_nm` – Name Server
- `bin_ss` – Storage Server
- `bin_client` – Interactive client

## Running

In separate terminals:

1. **Name Server**
   ```bash
   ./bin_nm --host 127.0.0.1 --port 5000
   ```

2. **Storage Server(s)**
   ```bash
   ./bin_ss \
     --nm-host 127.0.0.1 --nm-port 5000 \
     --host 127.0.0.1 --client-port 6001 \
     --storage ./storage_ss1 --username ss1
   ```
   (You can start additional SS instances by providing distinct `--client-port`, `--storage`, and usernames.)

3. **Client**
   ```bash
   ./bin_client --nm-host 127.0.0.1 --nm-port 5000 --username alice
   ```

   Run `./bin_client --help` to see the available interactive commands (VIEW, READ, WRITE, STREAM, ACL management, checkpoints, folders, etc.).

Sample automation scripts are included:

- `./run_all.sh` – Starts NM + two SS instances + a client (adjust as needed).
- `./stop_all.sh` – Attempts to stop background processes started by the helper script.

## Documentation

- `requirements.md` – Project specification from the course staff
- `context.md` – High-level architecture & implementation plan
- `design_choices.md` – Summary of open-ended design decisions (threading model, sentence locking, ACL caching, etc.)

## Logs & Storage

- NM & SS emit JSON-line logs (`nm.log`, `ss.log`, etc.) in the repo root.
- Each storage server persists files under `storage_ssX/files` and metadata under `storage_ssX/metadata`.
- Client usernames are persisted in `registry_clients.txt` so LIST survives NM restarts.

## Testing Notes

- `make` builds all binaries with `-Wall -Wextra -Werror`.
- WRITE and STREAM commands have been exercised with concurrent clients; refer to `context.md` and log files for manual test traces.

Feel free to reach out if you need additional runbooks or automated tests. Happy hacking!

