# Phase 2 Implementation Plan

## Overview
Phase 2 implements core file operations, access control, efficient search/indexing, and fills Phase 1 gaps. This phase establishes the foundation for all user-facing file operations.

## Goals
1. Fill Phase 1 gaps (SS file scanning, error codes, enhanced logging)
2. Implement file storage and persistence on SS
3. Implement file indexing and efficient search on NM
4. Implement core user commands: VIEW, CREATE, READ, DELETE, INFO, LIST
5. Implement access control (ACL) system
6. Implement client-SS direct communication for READ operations
7. Enhance logging with error codes and operation details

## Phase 2 Scope (User Functionalities: 65 marks)

### Commands to Implement
- [10] VIEW (with flags: -a, -l, -al)
- [10] READ
- [10] CREATE
- [10] DELETE
- [10] INFO
- [10] LIST
- [15] Access Control (ADDACCESS, REMACCESS)

### System Requirements to Implement
- [10] Data Persistence
- [5] Access Control Enforcement
- [5] Enhanced Logging (with error codes)
- [5] Error Handling (comprehensive error codes)
- [15] Efficient Search (O(log N) with caching)

## Implementation Plan

---

## Step 1: Error Code System & Enhanced Logging

### 1.1 Create Error Code Infrastructure
**File**: `src/common/errors.h` and `src/common/errors.c`

**Implementation:**
- Define error code enum/constants:
  - `OK` - Operation successful
  - `INVALID` - Invalid request/parameters
  - `UNAUTHORIZED` - User lacks required permissions
  - `NOT_FOUND` - File/user/resource not found
  - `CONFLICT` - Resource contention (e.g., file locked)
  - `UNAVAILABLE` - Resource temporarily unavailable
  - `INTERNAL` - Internal server error
- Error structure with code and message
- Helper functions: `error_to_string()`, `error_format_message()`

**Protocol Integration:**
- Extend `Message` structure to include error code field
- Update `proto_format_line()` and `proto_parse_line()` to handle error codes
- Error responses: `ERROR|ID|USERNAME|ROLE|ERROR_CODE|ERROR_MESSAGE`

**Exit Criteria:**
- ✅ Error codes defined and accessible throughout system
- ✅ Protocol supports error messages
- ✅ Error codes can be logged and displayed

---

## Step 2: SS File Scanning & Persistence Foundation

### 2.1 SS File Scanning on Startup
**File**: `src/ss/file_scan.c` and `src/ss/file_scan.h`

**Implementation:**
- Scan storage directory on SS startup
- List all files in storage directory
- Create metadata for each file (if metadata file exists, load it)
- Build file list for SS_REGISTER payload
- Send complete file list to NM during registration

**Storage Structure:**
```
storage_ss1/
  ├── files/
  │   ├── file1.txt
  │   └── file2.txt
  └── metadata/
      ├── file1.txt.meta
      └── file2.txt.meta
```

**Metadata Format (JSON or custom format):**
- Owner username
- Created timestamp
- Last modified timestamp
- Last accessed timestamp
- Access control list (ACL)
- File size (bytes)
- Word count
- Character count

**Exit Criteria:**
- ✅ SS scans directory on startup
- ✅ SS sends file list to NM during registration
- ✅ NM receives and stores file list from SS

### 2.2 File Storage Functions
**File**: `src/ss/file_storage.c` and `src/ss/file_storage.h`

**Implementation:**
- `file_create()` - Create empty file
- `file_read()` - Read entire file content
- `file_delete()` - Delete file and metadata
- `file_exists()` - Check if file exists
- `file_write()` - Write content to file (for future WRITE command)
- `metadata_load()` - Load metadata from disk
- `metadata_save()` - Save metadata to disk
- `metadata_update_timestamp()` - Update access/modify timestamps

**Persistence:**
- All file operations write to disk immediately
- Metadata saved atomically (write to temp file, then rename)
- Handle file system errors gracefully

**Exit Criteria:**
- ✅ Files can be created, read, deleted on SS
- ✅ Metadata persists across SS restarts
- ✅ File operations are atomic where needed

---

## Step 3: NM File Index & Efficient Search

### 3.1 File Index Data Structure
**File**: `src/nm/index.c` and `src/nm/index.h`

**Data Structure Choice:**
- **Primary**: Hash map (filename → FileEntry) for O(1) lookup
- **Secondary**: Trie for prefix search (optional, for future)
- **Cache**: LRU cache for recent lookups

**FileEntry Structure:**
```c
typedef struct FileEntry {
    char filename[256];
    char owner[64];
    char ss_host[INET_ADDRSTRLEN];
    int ss_client_port;
    char ss_username[64];  // Which SS hosts this file
    time_t created;
    time_t last_modified;
    time_t last_accessed;
    size_t size_bytes;
    int word_count;
    int char_count;
    // ACL will be stored separately (see Step 6)
} FileEntry;
```

**Implementation:**
- `index_init()` - Initialize index structures
- `index_add_file()` - Add file to index (from SS registration or CREATE)
- `index_remove_file()` - Remove file from index (on DELETE)
- `index_lookup_file()` - O(1) lookup by filename (with cache check)
- `index_list_files()` - Get all files (for VIEW)
- `index_list_files_by_owner()` - Get files owned by user
- `index_list_files_by_access()` - Get files user has access to
- `cache_update()` - Update LRU cache on lookup

**Exit Criteria:**
- ✅ Hash map implemented with O(1) lookup
- ✅ LRU cache implemented for recent searches
- ✅ Files can be indexed by SS, looked up by name
- ✅ Index supports listing operations

### 3.2 SS Registration with File List
**File**: `src/nm/main.c` (update registration handler)

**Implementation:**
- Parse file list from SS_REGISTER payload
- For each file in list, call `index_add_file()`
- Store which SS hosts which files
- Handle SS reconnection: merge new file list, detect removed files

**Exit Criteria:**
- ✅ NM receives file list from SS
- ✅ NM indexes all files from SS
- ✅ Index updates when SS reconnects

---

## Step 4: Access Control List (ACL) System

### 4.1 ACL Data Structure
**File**: `src/common/acl.h` and `src/common/acl.c`

**ACL Entry Structure:**
```c
typedef struct ACLEntry {
    char username[64];
    int read_access;  // 0 or 1
    int write_access;  // 0 or 1
} ACLEntry;

typedef struct ACL {
    char owner[64];
    ACLEntry *entries;  // Dynamic array
    int count;
    int capacity;
} ACL;
```

**Implementation:**
- `acl_init()` - Create ACL with owner
- `acl_add_read()` - Grant read access to user
- `acl_add_write()` - Grant write access to user (includes read)
- `acl_remove()` - Remove all access for user
- `acl_check_read()` - Check if user has read access
- `acl_check_write()` - Check if user has write access
- `acl_serialize()` - Serialize ACL for storage
- `acl_deserialize()` - Deserialize ACL from storage

**Storage:**
- ACL stored in file metadata (on SS)
- ACL also cached in NM index for fast access checks

**Exit Criteria:**
- ✅ ACL data structure implemented
- ✅ ACL can be stored/loaded from metadata
- ✅ ACL permission checks work correctly

### 4.2 ACL Enforcement
**File**: `src/nm/access_control.c` and `src/nm/access_control.h`

**Implementation:**
- `check_file_access()` - Check if user can access file (read/write)
- `check_file_owner()` - Check if user owns file
- Called before all file operations on NM
- Returns appropriate error code if access denied

**Exit Criteria:**
- ✅ Access checks integrated into NM command handlers
- ✅ Unauthorized access returns proper error codes
- ✅ Owner always has read/write access

---

## Step 5: Client Command Processing

### 5.1 Client Interactive Shell
**File**: `src/client/main.c` (major update)

**Implementation:**
- Replace one-time registration with interactive loop
- Read commands from stdin: `VIEW`, `CREATE`, `READ`, `DELETE`, `INFO`, `LIST`, `ADDACCESS`, `REMACCESS`
- Parse command and arguments
- Send command to NM
- Receive and display response
- Handle "STOP" packet for READ operations
- Continue until user exits (Ctrl+D or EXIT command)

**Command Parser:**
- Simple tokenizer: split by spaces
- Handle quoted arguments if needed
- Validate command syntax

**Exit Criteria:**
- ✅ Client accepts commands interactively
- ✅ Client sends commands to NM
- ✅ Client displays responses correctly

### 5.2 Client-SS Direct Communication
**File**: `src/client/direct_ss.c` and `src/client/direct_ss.h`

**Implementation:**
- `connect_to_ss()` - Connect to SS using IP and port from NM
- `receive_until_stop()` - Receive data from SS until "STOP" packet
- Handle connection failures gracefully
- Used for READ operations (and future WRITE/STREAM)

**Exit Criteria:**
- ✅ Client can connect directly to SS
- ✅ Client can receive data until STOP packet
- ✅ Connection errors handled gracefully

---

## Step 6: NM Command Handlers (NM-Side Operations)

### 6.1 VIEW Command Handler
**File**: `src/nm/commands.c` (new file for command handlers)

**Implementation:**
- `handle_view()` - Process VIEW command
- Parse flags: `-a` (all files), `-l` (with details)
- If `-a`: Get all files from index
- If no `-a`: Get files user has access to (check ACL)
- If `-l`: Include metadata (word count, char count, timestamps, owner)
- Format response as table or list
- Send response to client

**Exit Criteria:**
- ✅ VIEW returns files user has access to
- ✅ VIEW -a returns all files
- ✅ VIEW -l includes details
- ✅ VIEW -al combines both flags

### 6.2 CREATE Command Handler
**File**: `src/nm/commands.c`

**Implementation:**
- `handle_create()` - Process CREATE command
- Check if file already exists in index (return CONFLICT error)
- Select appropriate SS (round-robin or based on load)
- Send CREATE command to SS: `CREATE|ID|USERNAME|ROLE|filename`
- Wait for ACK from SS
- If successful: Add file to index with owner = requester
- Create ACL with owner having RW access
- Send success response to client
- If failed: Send error response to client

**SS Selection:**
- Simple round-robin for now
- Can be enhanced later with load balancing

**Exit Criteria:**
- ✅ CREATE creates file on selected SS
- ✅ File added to NM index
- ✅ ACL created with owner permissions
- ✅ Duplicate file creation returns error

### 6.3 DELETE Command Handler
**File**: `src/nm/commands.c`

**Implementation:**
- `handle_delete()` - Process DELETE command
- Check if file exists (return NOT_FOUND if not)
- Check if user is owner (return UNAUTHORIZED if not)
- Find SS hosting the file
- Send DELETE command to SS: `DELETE|ID|USERNAME|ROLE|filename`
- Wait for ACK from SS
- If successful: Remove file from index
- Remove ACL
- Send success response to client

**Exit Criteria:**
- ✅ DELETE removes file from SS
- ✅ File removed from NM index
- ✅ ACL removed
- ✅ Only owner can delete

### 6.4 INFO Command Handler
**File**: `src/nm/commands.c`

**Implementation:**
- `handle_info()` - Process INFO command
- Lookup file in index
- Check if user has read access (return UNAUTHORIZED if not)
- Update last_accessed timestamp
- Gather all metadata:
  - Filename
  - Owner
  - Created timestamp
  - Last modified timestamp
  - Last accessed timestamp
  - Size (bytes)
  - Word count
  - Character count
  - Access list (from ACL)
- Format response (any convenient format)
- Send to client

**Exit Criteria:**
- ✅ INFO returns all required metadata
- ✅ Last accessed timestamp updates
- ✅ Access list formatted correctly

### 6.5 LIST Command Handler
**File**: `src/nm/commands.c`

**Implementation:**
- `handle_list()` - Process LIST command
- Get all registered users from client registry
- Format as simple list
- Send to client

**Exit Criteria:**
- ✅ LIST returns all registered users

---

## Step 7: SS Command Handlers (SS-Side Operations)

### 7.1 SS Command Processing Loop
**File**: `src/ss/main.c` (update to handle commands from NM)

**Implementation:**
- After registration, listen for commands from NM
- In heartbeat thread or separate command thread, read messages from NM
- Handle commands: `CREATE`, `DELETE`, `READ_REQ` (future)
- Process command, perform file operation
- Send ACK or ERROR response back to NM

**Command Types:**
- `CREATE <filename>` - Create empty file
- `DELETE <filename>` - Delete file and metadata
- `READ_REQ <filename>` - Prepare to send file to client (future)

**Exit Criteria:**
- ✅ SS processes CREATE command from NM
- ✅ SS processes DELETE command from NM
- ✅ SS sends ACK/ERROR back to NM

### 7.2 SS CREATE Handler
**File**: `src/ss/file_storage.c`

**Implementation:**
- Create empty file in storage directory
- Create metadata file with owner, timestamps
- Initialize ACL with owner having RW access
- Save metadata to disk
- Return success

**Exit Criteria:**
- ✅ File created on disk
- ✅ Metadata created and saved
- ✅ ACL initialized correctly

### 7.3 SS DELETE Handler
**File**: `src/ss/file_storage.c`

**Implementation:**
- Check if file exists
- Delete file from storage directory
- Delete metadata file
- Return success

**Exit Criteria:**
- ✅ File and metadata deleted
- ✅ Errors handled gracefully

---

## Step 8: READ Operation (Client-SS Direct)

### 8.1 NM READ Handler
**File**: `src/nm/commands.c`

**Implementation:**
- `handle_read()` - Process READ command
- Lookup file in index (return NOT_FOUND if not exists)
- Check if user has read access (return UNAUTHORIZED if not)
- Update last_accessed timestamp in index
- Send updated metadata to SS (if needed)
- Return SS IP and client port to client
- Response format: `READ_RESP|SS_IP|SS_PORT|filename`

**Exit Criteria:**
- ✅ NM validates access and finds file
- ✅ NM returns SS location to client

### 8.2 SS READ Handler (Direct Client Connection)
**File**: `src/ss/file_storage.c` and `src/ss/main.c`

**Implementation:**
- SS listens on client port (separate from NM connection)
- When client connects, receive READ request
- Validate request (optional: check with NM or trust client)
- Read file content
- Send file content line by line (or word by word)
- Send "STOP" packet at end
- Close connection

**SS Client Listener:**
- Create separate server socket on client_port
- Accept connections from clients
- Handle READ requests

**Exit Criteria:**
- ✅ SS accepts client connections on client_port
- ✅ SS reads file and sends to client
- ✅ Client receives file content until STOP packet

### 8.3 Client READ Handler
**File**: `src/client/main.c` and `src/client/direct_ss.c`

**Implementation:**
- Client sends READ command to NM
- Client receives SS location (IP, port)
- Client connects directly to SS
- Client sends READ request: `READ|filename`
- Client receives file content until STOP packet
- Client displays content
- Client closes connection to SS

**Exit Criteria:**
- ✅ Client connects to SS for READ
- ✅ Client receives and displays file content
- ✅ STOP packet handled correctly

---

## Step 9: Access Control Commands

### 9.1 ADDACCESS Command Handler (NM)
**File**: `src/nm/commands.c`

**Implementation:**
- `handle_addaccess()` - Process ADDACCESS command
- Parse flags: `-R` (read) or `-W` (write)
- Lookup file in index
- Check if requester is owner (return UNAUTHORIZED if not)
- Check if target user exists (return NOT_FOUND if not)
- Update ACL: add read or write access
- Send ACL update to SS (for persistence)
- Update ACL in NM index cache
- Send success response to client

**Exit Criteria:**
- ✅ ADDACCESS -R grants read access
- ✅ ADDACCESS -W grants write (and read) access
- ✅ Only owner can grant access
- ✅ ACL persisted on SS

### 9.2 REMACCESS Command Handler (NM)
**File**: `src/nm/commands.c`

**Implementation:**
- `handle_remaccess()` - Process REMACCESS command
- Lookup file in index
- Check if requester is owner
- Remove user from ACL
- Send ACL update to SS
- Update ACL in NM index
- Send success response to client

**Exit Criteria:**
- ✅ REMACCESS removes all access for user
- ✅ Only owner can remove access
- ✅ ACL updated on SS

---

## Step 10: Enhanced Logging & Error Handling

### 10.1 Enhanced Logging
**File**: `src/common/log.c` (update)

**Implementation:**
- Add fields to log entries:
  - IP address (for all operations)
  - Port (for all operations)
  - Error code (for all operations)
  - Operation type
  - File name (for file operations)
  - Result status (success/failure)
- Update log format to include all required fields
- NM prints to terminal (stdout) for important operations

**Exit Criteria:**
- ✅ All logs include timestamp, IP, port, username, operation, status, error code
- ✅ NM prints status messages to terminal

### 10.2 Error Handling Integration
**File**: Throughout (NM, SS, Client)

**Implementation:**
- Replace generic errors with error codes
- Return appropriate error codes for all failure scenarios
- Format error messages consistently
- Log errors with error codes

**Error Scenarios:**
- File not found → `NOT_FOUND`
- Unauthorized access → `UNAUTHORIZED`
- File already exists → `CONFLICT`
- File locked (future) → `CONFLICT`
- Invalid parameters → `INVALID`
- SS unavailable → `UNAVAILABLE`
- Internal errors → `INTERNAL`

**Exit Criteria:**
- ✅ All operations return appropriate error codes
- ✅ Error messages are clear and informative
- ✅ Errors logged with codes

---

## Step 11: Testing & Validation

### 11.1 Unit Tests
- Test file storage operations (create, read, delete)
- Test ACL operations (add, remove, check)
- Test index operations (add, lookup, list)
- Test error code system

### 11.2 Integration Tests
- Test CREATE flow: Client → NM → SS
- Test READ flow: Client → NM → SS (direct)
- Test DELETE flow: Client → NM → SS
- Test VIEW flow: Client → NM
- Test ACCESS control flow: Client → NM
- Test error scenarios (not found, unauthorized, etc.)

### 11.3 End-to-End Tests
- Multiple clients accessing same file
- SS restart (files persist)
- NM restart (index rebuilds from SS registrations)
- Access control enforcement
- File listing with various flags

---

## Phase 2 Exit Criteria

### Functional Requirements ✅
1. ✅ VIEW command works (with -a, -l, -al flags)
2. ✅ CREATE command creates files on SS
3. ✅ READ command retrieves files via direct client-SS connection
4. ✅ DELETE command removes files (owner only)
5. ✅ INFO command displays file metadata
6. ✅ LIST command lists all users
7. ✅ ADDACCESS grants read/write access
8. ✅ REMACCESS removes access
9. ✅ Files persist across SS restarts
10. ✅ Access control enforced on all operations

### System Requirements ✅
1. ✅ Data persistence: Files and metadata survive SS restarts
2. ✅ Access control: Permissions enforced on all operations
3. ✅ Logging: All operations logged with required fields
4. ✅ Error handling: Comprehensive error codes used throughout
5. ✅ Efficient search: O(1) lookup with hash map, LRU cache

### Technical Requirements ✅
1. ✅ NM indexes files with O(1) lookup
2. ✅ Client-SS direct communication for READ
3. ✅ NM handles VIEW/INFO/LIST/ACCESS directly
4. ✅ NM routes CREATE/DELETE to SS
5. ✅ SS stores files and metadata persistently

---

## Implementation Order (Recommended)

1. **Step 1**: Error codes (foundation)
2. **Step 2**: SS file scanning and storage (foundation)
3. **Step 3**: NM index (foundation)
4. **Step 4**: ACL system (foundation)
5. **Step 5**: Client interactive shell (enables testing)
6. **Step 6**: NM command handlers (VIEW, CREATE, DELETE, INFO, LIST)
7. **Step 7**: SS command handlers (CREATE, DELETE)
8. **Step 8**: READ operation (client-SS direct)
9. **Step 9**: Access control commands
10. **Step 10**: Enhanced logging and error handling
11. **Step 11**: Testing and validation

**Dependencies:**
- Steps 1-4 are foundational (can be done in parallel)
- Steps 6-7 depend on steps 1-4
- Step 8 depends on steps 1-7
- Step 9 depends on step 4
- Step 10 can be done incrementally throughout

---

## Files to Create/Modify

### New Files
- `src/common/errors.h` / `src/common/errors.c`
- `src/common/acl.h` / `src/common/acl.c`
- `src/ss/file_scan.h` / `src/ss/file_scan.c`
- `src/ss/file_storage.h` / `src/ss/file_storage.c`
- `src/nm/index.h` / `src/nm/index.c`
- `src/nm/access_control.h` / `src/nm/access_control.c`
- `src/nm/commands.h` / `src/nm/commands.c`
- `src/client/direct_ss.h` / `src/client/direct_ss.c`

### Modified Files
- `src/common/protocol.h` / `src/common/protocol.c` (add error code support)
- `src/common/log.c` (enhance logging)
- `src/nm/main.c` (add command handlers, file list processing)
- `src/ss/main.c` (add command processing, client listener)
- `src/client/main.c` (interactive shell, command parsing)

---

## Notes

1. **Protocol Extensions**: May need to extend protocol for new commands and error codes
2. **Metadata Format**: Choose JSON or custom format for metadata storage
3. **Concurrency**: Ensure thread-safe operations on shared data structures
4. **Error Recovery**: Handle partial failures gracefully (e.g., SS fails during CREATE)
5. **Testing**: Test each step before moving to next
6. **Documentation**: Update context.md as implementation progresses

---

## Phase 2 Deliverables

- ✅ All Phase 1 gaps filled
- ✅ Core file operations working
- ✅ Access control system implemented
- ✅ Efficient search/indexing working
- ✅ Client-SS direct communication working
- ✅ Enhanced logging with error codes
- ✅ Data persistence verified
- ✅ All commands tested and working

