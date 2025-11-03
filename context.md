# LangOS Document Collaboration System - Project Context

**See `requirements.md` for complete requirements specification.**

## Project Status

**Phase 1 Complete**: Basic registration and heartbeat system
**Phase 2 Complete**: File operations, indexing, ACL system, and interactive client

## Architecture Overview

### Components

1. **Name Server (NM)** - Central coordinator
   - Listens on configurable host/port (default: 127.0.0.1:5000)
   - Thread-per-connection model for handling multiple clients
   - Maintains registry of SS and Clients
   - Maintains file index with O(1) hash lookup and LRU cache
   - Processes client commands (VIEW, CREATE, DELETE, INFO, LIST)
   - Coordinates with SS for file operations

2. **Storage Server (SS)** - Data storage
   - Registers with NM on startup
   - Scans existing files on startup and reports to NM
   - Listens for commands from NM (CREATE, DELETE)
   - Stores files in `storage_dir/files/`
   - Stores metadata in `storage_dir/metadata/` (owner, timestamps, ACL)
   - Sends periodic heartbeats to NM

3. **Client** - User interface
   - Interactive shell for command input
   - Registers with NM on startup
   - Sends commands to NM and displays responses
   - Supports: VIEW, CREATE, DELETE, INFO, LIST, EXIT

### Protocol

**Message Format**: `TYPE|ID|USERNAME|ROLE|PAYLOAD\n`

**Message Types**:
- `SS_REGISTER` - SS registration (payload: `host=IP,client_port=PORT,storage=DIR,files=file1.txt,file2.txt,...`)
- `CLIENT_REGISTER` - Client registration
- `HEARTBEAT` - Periodic heartbeat from SS
- `ACK` - Acknowledgment
- `ERROR` - Error response (payload: `ERROR_CODE|ERROR_MESSAGE`)
- `DATA` - Data response (for VIEW, LIST, INFO)
- `VIEW` - List files (payload: `flags=FLAGS` or empty)
- `CREATE` - Create file (payload: `filename`)
- `DELETE` - Delete file (payload: `filename`)
- `INFO` - Get file info (payload: `filename`)
- `LIST` - List registered users

**Error Codes**: OK, INVALID, UNAUTHORIZED, NOT_FOUND, CONFLICT, UNAVAILABLE, INTERNAL

## Implementation Details

### File Structure

```
src/
├── common/          # Shared utilities
│   ├── net.c/h      # Socket functions (create_server_socket, connect_to_host, send_all, recv_line)
│   ├── log.c/h      # JSON-line logging
│   ├── protocol.c/h # Message parsing/formatting
│   ├── errors.c/h   # Error code system
│   └── acl.c/h      # Access Control List implementation
├── nm/              # Name Server
│   ├── main.c       # Main server loop, message handling
│   ├── index.c/h    # File index with hash map and LRU cache
│   ├── access_control.c/h  # ACL enforcement
│   ├── commands.c/h # Command handlers (VIEW, CREATE, DELETE, INFO, LIST)
│   └── registry.c/h # Thread-safe registry for SS/Clients
├── ss/              # Storage Server
│   ├── main.c       # Registration, heartbeat, command processing
│   ├── file_scan.c/h    # Directory scanning
│   └── file_storage.c/h # File operations (create, read, delete, metadata)
└── client/          # Client
    ├── main.c       # Interactive shell
    └── commands.c/h # Command parsing and formatting
```

### Key Features Implemented

#### File Index (Step 3)
- **Hash Map**: 1024 buckets with chaining for O(1) average lookup
- **LRU Cache**: 100 most recently accessed files
- **Operations**: add, lookup, remove, get_all, get_by_owner, update_metadata

#### ACL System (Step 4)
- **Structure**: Owner + list of users with read/write permissions
- **Operations**: add_read, add_write, remove, check_read, check_write
- **Persistence**: Stored in metadata files, survives SS restarts
- **Serialization**: Text format in metadata files

#### Command Handlers (Steps 5 & 6)
- **VIEW**: List files (with -a flag for all, -l for details)
- **CREATE**: Create new file (owner = requester)
- **DELETE**: Delete file (owner only)
- **INFO**: Display file metadata
- **LIST**: List registered users

### Data Persistence

**Storage Server Directory Structure**:
```
storage_ss1/
├── files/           # Actual file content
│   ├── file1.txt
│   └── file2.txt
└── metadata/        # File metadata
    ├── file1.txt.meta
    └── file2.txt.meta
```

**Metadata File Format**:
```
owner=username
created=timestamp
last_modified=timestamp
last_accessed=timestamp
size_bytes=size
word_count=count
char_count=count
ACL_START
owner=username
user1=R
user2=RW
ACL_END
```

**Atomic Writes**: Metadata files use temporary files + rename for atomic updates

## Build and Run

### Build
```bash
make clean && make
```

This compiles:
- `bin_nm` - Name Server
- `bin_ss` - Storage Server  
- `bin_client` - Client

### Run Scripts

**Start all components**:
```bash
./run_all.sh
```

**Stop all components**:
```bash
./stop_all.sh
```

### Manual Run

**Terminal 1 - Name Server**:
```bash
./bin_nm --host 127.0.0.1 --port 5000
```

**Terminal 2 - Storage Server**:
```bash
./bin_ss --nm-host 127.0.0.1 --nm-port 5000 \
         --host 127.0.0.1 --client-port 6001 \
         --storage ./storage_ss1 --username ss1
```

**Terminal 3 - Client**:
```bash
./bin_client --nm-host 127.0.0.1 --nm-port 5000 --username alice
```

The client will start an interactive shell:
```
LangOS Client - Type commands (or 'EXIT' to quit)
> 
```

## Sample Test Runs

### Test 1: Create and View Files

```bash
# In client shell:
> CREATE test1.txt
File Created Successfully!

> CREATE test2.txt
File Created Successfully!

> VIEW
--> test1.txt
--> test2.txt

> VIEW -l
---------------------------------------------------------
|  Filename  | Words | Chars | Last Access Time | Owner |
|------------|-------|-------|------------------|-------|
| test1.txt  |     0 |     0 | 2025-01-01 12:00 | alice |
| test2.txt  |     0 |     0 | 2025-01-01 12:00 | alice |
---------------------------------------------------------
```

### Test 2: File Info

```bash
> INFO test1.txt
--> File: test1.txt
--> Owner: alice
--> Created: 2025-01-01 12:00
--> Last Modified: 2025-01-01 12:00
--> Size: 0 bytes
--> Words: 0
--> Characters: 0
--> Last Accessed: 2025-01-01 12:00 by alice
```

### Test 3: Delete File

```bash
> DELETE test1.txt
File deleted successfully!

> VIEW
--> test2.txt
```

### Test 4: Error Handling

```bash
> DELETE nonexistent.txt
ERROR [NOT_FOUND]: File 'nonexistent.txt' not found

> DELETE test2.txt  # If not owner
ERROR [UNAUTHORIZED]: User 'bob' is not the owner of file 'test2.txt'

> CREATE test2.txt  # If already exists
ERROR [CONFLICT]: File 'test2.txt' already exists
```

### Test 5: List Users

```bash
# Start another client (Terminal 4):
./bin_client --nm-host 127.0.0.1 --nm-port 5000 --username bob

# In first client:
> LIST
--> alice
--> bob
```

### Test 6: View with Flags

```bash
> VIEW      # Shows only your files
--> test2.txt

> VIEW -a   # Shows all files (requires ACL implementation)
--> test2.txt

> VIEW -l   # Shows with details
---------------------------------------------------------
|  Filename  | Words | Chars | Last Access Time | Owner |
...
```

## Code Quality

- **Compiler Flags**: `-O2 -Wall -Wextra -Werror -pthread -std=c11`
- **No Warnings**: All code compiles with `-Werror` (treats warnings as errors)
- **Thread Safety**: Registry and index use mutex locks
- **Error Handling**: Comprehensive error codes and messages
- **Comments**: Extensive inline comments explaining logic

## Next Steps / Future Work

### Phase 3 (Future)
- **READ Command**: Client-SS direct communication for file reading
- **Full ACL Enforcement**: Check ACL on all file operations (currently only checks owner)
- **File Content Operations**: Write content to files, update word/char counts
- **Trie for Prefix Search**: Efficient prefix-based file search
- **Multi-SS Support**: Load balancing across multiple storage servers
- **Client-SS Direct Communication**: For READ operations (bypass NM)

### Known Limitations
1. **File Owner on SS Registration**: Files scanned during SS registration show owner=ss1 (placeholder) because metadata isn't loaded during registration. Owner is updated when a user creates/accesses the file. Use `VIEW -a` to see all files including those with placeholder owners.
2. **ACL Checking**: Currently simplified - only checks owner for DELETE/INFO. Full ACL checking would require loading metadata from SS on every operation.
3. **SS Selection**: Uses first available SS (round-robin not fully implemented)
4. **File Content**: Files are created empty - no content writing yet
5. **Metadata Updates**: Word/char counts not updated when files are modified

## Troubleshooting

**Port already in use**:
```bash
# Find and kill process using port 5000
lsof -ti:5000 | xargs kill -9
```

**Build errors**:
- Ensure all source files are present
- Check that includes are correct
- Verify `-Werror` is not failing due to warnings

**Connection refused**:
- Ensure NM is running before starting SS/Client
- Check that host/port arguments are correct
- Verify firewall is not blocking connections

## Handover Notes

For your teammate:
1. All code is extensively commented
2. Error handling uses centralized error code system
3. Protocol is line-delimited text over TCP
4. Logs are JSON-line format to stdout
5. File metadata persists across SS restarts
6. Index is rebuilt on NM restart (files re-registered by SS)
7. Registry is in-memory only (lost on NM restart)

**Key Files to Review**:
- `src/common/protocol.h` - Protocol definition
- `src/common/errors.h` - Error codes
- `src/nm/commands.c` - Command handler implementations
- `src/ss/file_storage.c` - File operations
- `src/client/main.c` - Interactive shell

**Testing**:
- Use the sample test runs above
- Check logs for detailed operation traces
- Verify files are created/deleted in `storage_ss1/files/`
- Verify metadata persists in `storage_ss1/metadata/`
