# Remaining Work - Phase Plan

## Current Status

### ✅ Implemented (Phase 1 & 2)
- **VIEW** (with -a, -l flags) - 10 marks
- **CREATE** - 10 marks
- **DELETE** - 10 marks
- **INFO** - 10 marks
- **LIST** - 10 marks
- **System Requirements**: Data Persistence, Logging, Error Handling, Efficient Search - 35 marks
- **Specifications**: Initialization, NM, SS, Client - 10 marks

**Total Completed: 95 marks**

### ❌ Remaining (100 marks)
1. **READ** - 10 marks
2. **WRITE** - 30 marks (most complex)
3. **UNDO** - 15 marks (depends on WRITE)
4. **STREAM** - 15 marks
5. **ADDACCESS/REMACCESS** - 15 marks
6. **EXEC** - 15 marks

---

## Phase 3: Client-SS Direct Communication & Access Control
**Goal**: Implement READ, STREAM, and Access Control commands

### Step 1: Client-SS Direct Communication Infrastructure
**What**: Set up the protocol for client to connect directly to SS (bypassing NM for data transfer)

**Tasks**:
1. **NM**: Add handler for READ/STREAM requests
   - Lookup file in index to find SS host/port
   - Return SS connection info to client (IP + port)
   - Protocol: `SS_INFO` message with `host=IP,port=PORT`

2. **SS**: Add client connection handler
   - New thread/endpoint for client connections (separate from NM command handler)
   - Listen on `client_port` for client connections
   - Handle `READ` and `STREAM` commands from clients

3. **Client**: Add direct SS connection logic
   - Parse `SS_INFO` response from NM
   - Connect directly to SS
   - Send `READ` or `STREAM` command
   - Receive data until `STOP` packet

**Testing**:
- [ ] Client sends READ to NM
- [ ] NM returns SS connection info
- [ ] Client connects to SS
- [ ] SS sends file content
- [ ] Client displays content correctly

### Step 2: READ Command Implementation
**What**: Full READ command with client-SS direct communication

**Tasks**:
1. **Client**: Parse `READ <filename>` command
2. **NM**: Handle READ request, return SS info
3. **SS**: Handle READ command from client
   - Load file content (read from `files/<filename>`)
   - Check read access (ACL)
   - Send content line-by-line or in chunks
   - Send `STOP` packet when done
   - Update `last_accessed` timestamp
4. **Client**: Display received content

**Note**: When WRITE is implemented (Phase 4), READ will work concurrently:
- READ opens file, reads entire content into memory, then closes file (current implementation)
- Since READ completes quickly (reads all at once), it's unlikely to overlap with WRITE
- If overlap occurs: READ's `fopen()` gets a file descriptor to the inode; even if `rename()` replaces the directory entry, the open file descriptor keeps the old inode alive until `fclose()`
- READ sees consistent snapshot (the version that existed when it opened the file)
- No blocking or corruption

**Testing**:
- [ ] READ empty file
- [ ] READ file with content
- [ ] READ non-existent file (error)
- [ ] READ without access (error)
- [ ] Multiple clients reading same file concurrently

### Step 3: STREAM Command Implementation
**What**: Word-by-word streaming with 0.1s delay

**Tasks**:
1. **Client**: Parse `STREAM <filename>` command
2. **NM**: Handle STREAM request, return SS info (same as READ)
3. **SS**: Handle STREAM command
   - Parse file into words (split by spaces)
   - Send each word with 0.1s delay (`usleep(100000)`)
   - Handle SS failure mid-stream (client detects disconnect)
4. **Client**: Display words as received, handle disconnection

**Note**: When WRITE is implemented (Phase 4), STREAM will work concurrently:
- **STREAM keeps file open**: Opens file with `fopen()` and keeps `FILE*` handle open during entire streaming (word-by-word with delays)
- **Unix inode behavior**: When `rename("file.tmp", "file")` happens:
  - The directory entry "file" now points to the new inode (from `.tmp`)
  - But the open `FILE*` handle still points to the old inode
  - The old inode persists until the last file descriptor is closed
  - STREAM continues reading from the old inode (consistent snapshot)
- **Result**: STREAM sees the version that existed when it opened the file, even if WRITE completes mid-stream
- No blocking or corruption during streaming

**Testing**:
- [ ] STREAM empty file
- [ ] STREAM file with content (verify 0.1s delay)
- [ ] STREAM non-existent file (error)
- [ ] Kill SS mid-stream (client shows error)

### Step 4: Access Control Commands
**What**: ADDACCESS and REMACCESS commands

**Tasks**:
1. **Client**: Parse `ADDACCESS -R/-W <filename> <username>` and `REMACCESS <filename> <username>`
2. **NM**: Handle access control commands
   - Verify requester is file owner
   - Load metadata from SS (or cache)
   - Update ACL (add/remove user with R/W permissions)
   - Save updated metadata to SS
   - Update NM index if needed
3. **SS**: Add metadata update endpoint (or reuse existing)
   - Update ACL in metadata file
   - Save atomically

**Testing**:
- [ ] Owner adds read access
- [ ] Owner adds write access
- [ ] Owner removes access
- [ ] Non-owner tries to modify access (error)
- [ ] Verify access changes reflected in INFO

**Verification Checklist**:
- [ ] All Phase 3 commands work end-to-end
- [ ] Client-SS direct communication works
- [ ] READ displays file content correctly
- [ ] STREAM shows words with delay
- [ ] Access control commands update ACL
- [ ] No compiler warnings
- [ ] Manual test runs documented

---

## Phase 4: WRITE Command (Sentence-Level Editing)
**Goal**: Implement word-level file editing with sentence locking

### Step 1: Sentence Parsing & Data Structures
**What**: Parse files into sentences and words, manage sentence structure

**Tasks**:
1. **SS**: Create sentence parser module (`src/ss/sentence_parser.c`)
   - Parse file content into sentences (delimiters: `.`, `!`, `?`)
   - Each sentence is array of words
   - Handle edge cases: `e.g.`, `Umm... ackchually!`
   - Reconstruct file from sentences

2. **SS**: Create sentence data structure
   ```c
   typedef struct {
       char **words;      // Array of word strings
       int word_count;    // Number of words
       int locked_by;     // User ID or -1 if unlocked
   } Sentence;
   
   typedef struct {
       Sentence *sentences;
       int sentence_count;
   } FileContent;
   ```

3. **SS**: Load/save file content as sentences
   - Convert file text → sentences on load
   - Convert sentences → file text on save

**Testing**:
- [ ] Parse empty file
- [ ] Parse file with one sentence
- [ ] Parse file with multiple sentences
- [ ] Parse file with delimiters in words (`e.g.`, `Umm...`)
- [ ] Reconstruct file from sentences

### Step 2: Sentence Locking Mechanism
**What**: Lock sentences during WRITE operations

**Tasks**:
1. **SS**: Implement sentence lock manager
   - Per-file lock table (sentence index → lock info)
   - Lock structure: `{sentence_idx, locked_by_username, lock_time}`
   - Thread-safe locking (mutex per file or global)

2. **SS**: Lock/unlock functions
   - `sentence_lock(filename, sentence_idx, username)` - returns 0 if locked, -1 if already locked
   - `sentence_unlock(filename, sentence_idx, username)` - unlocks if locked by same user
   - Check lock before allowing WRITE

3. **SS**: Handle lock timeout (optional, for robustness)
   - Auto-unlock after timeout (e.g., 5 minutes)

**Testing**:
- [ ] Lock sentence successfully
- [ ] Lock already-locked sentence (error)
- [ ] Unlock sentence
- [ ] Multiple users lock different sentences
- [ ] Lock timeout (if implemented)

### Step 3: WRITE Command - Basic Flow
**What**: WRITE command with sentence locking

**Tasks**:
1. **Client**: Parse `WRITE <filename> <sentence_number>` command
   - Enter interactive mode for word updates
   - Parse `<word_index> <content>` lines
   - Parse `ETIRW` to end write

2. **NM**: Handle WRITE request
   - Lookup file, check write access (ACL)
   - Return SS connection info

3. **SS**: Handle WRITE command
   - Lock sentence (return error if locked)
   - Wait for word updates
   - Apply updates to sentence
   - Handle sentence delimiter creation (`.`, `!`, `?` in content)
   - Unlock on `ETIRW`

**Testing**:
- [ ] WRITE to empty file (sentence 0)
- [ ] WRITE to existing sentence
- [ ] WRITE to locked sentence (error)
- [ ] Multiple word updates in one WRITE
- [ ] ETIRW unlocks sentence

### Step 4: Word-Level Updates & Sentence Splitting
**What**: Implement word insertion/update logic and handle sentence delimiters

**Tasks**:
1. **SS**: Word update logic
   - Insert word at index (shift existing words)
   - Update word at index (replace)
   - Handle index out of bounds (error)
   - Handle negative indices (error)

2. **SS**: Sentence delimiter detection
   - When content contains `.`, `!`, `?` → split into new sentences
   - Update sentence indices after split
   - Maintain active sentence index for subsequent updates

3. **SS**: Atomic swap pattern for concurrent read/write
   - **During WRITE**: Work on temporary file (`files/<filename>.tmp`)
     - Load current file content into memory (sentences structure)
     - Apply all word updates to in-memory structure
     - Write updated content to `files/<filename>.tmp`
   - **On ETIRW**: Atomically replace original file
     - `rename("files/<filename>.tmp", "files/<filename>")` (atomic operation)
     - Update metadata (word/char counts, last_modified)
     - Unlock sentence
   - **How it works with concurrent reads**:
     - **Unix inode behavior**: `rename()` only changes the directory entry, not the inode
     - Open file descriptors keep the old inode alive until closed
     - READ: Reads entire file quickly, closes immediately (unlikely overlap)
     - STREAM: Keeps file open during streaming; old inode persists until `fclose()`
     - Result: Readers see consistent snapshot (version when they opened)
   - **Benefits**:
     - Concurrent READ/STREAM operations continue reading original file (consistent snapshot)
     - No blocking of readers during write
     - Atomic replacement ensures no corruption
     - Matches requirements hint: "write to a temporary swap file initially, and move the contents to the final file once all updates are complete"

**Testing**:
- [ ] Insert word at index 0
- [ ] Insert word at middle index
- [ ] Insert word at end (append)
- [ ] Insert word with delimiter (sentence split)
- [ ] Multiple updates in sequence
- [ ] Invalid index (error)
- [ ] Atomic write (verify no corruption)

### Step 5: Concurrent WRITE Handling
**What**: Ensure multiple users can write to different sentences simultaneously

**Tasks**:
1. **SS**: Multi-threaded command handler
   - Thread pool or per-connection thread for client commands
   - Each WRITE operation in separate thread
   - File-level mutex for metadata updates

2. **SS**: Lock coordination
   - Check lock before acquiring
   - Release lock on ETIRW or error
   - Handle client disconnection (unlock on timeout)

3. **SS**: Concurrent read/write with atomic swap
   - **READ/STREAM during WRITE**: 
     - Readers continue reading original file (old inode)
     - WRITE works on `.tmp` file
     - When WRITE completes, `rename()` atomically replaces file
     - Readers see consistent snapshot (old version until they reconnect)
     - New readers see new version
   - **Multiple WRITEs to different sentences**:
     - Each WRITE locks its sentence
     - Each WRITE works on its own `.tmp` file
     - Last WRITE to complete wins (atomic rename)
     - Alternative: Merge changes if no conflicts

**Testing**:
- [ ] Two users write to different sentences simultaneously
- [ ] Two users try to write to same sentence (one blocked)
- [ ] Client disconnects mid-WRITE (lock released)
- [ ] Multiple concurrent WRITEs to different files
- [ ] READ/STREAM during active WRITE (sees old version, no corruption)
- [ ] READ after WRITE completes (sees new version)

**Verification Checklist**:
- [ ] WRITE command works end-to-end
- [ ] Sentence locking prevents concurrent edits
- [ ] Word updates work correctly
- [ ] Sentence delimiters create new sentences
- [ ] Atomic writes prevent corruption
- [ ] Concurrent WRITEs to different sentences work
- [ ] No compiler warnings
- [ ] Manual test runs documented (all examples from requirements)

---

## Phase 5: UNDO Command
**Goal**: Single-level undo for file changes

### Step 1: Change Tracking
**What**: Track file state before each WRITE operation

**Tasks**:
1. **SS**: Create undo history structure
   - Store previous file content before each WRITE
   - Single entry per file (only last change)
   - Save to disk: `metadata/<filename>.undo`

2. **SS**: Save state before WRITE
   - Before applying word updates, save current file content
   - Overwrite previous undo state (single-level only)

3. **SS**: Load undo state
   - Read previous file content from undo file
   - Restore file to previous state

**Testing**:
- [ ] Save state before WRITE
- [ ] Overwrite previous undo state
- [ ] Load undo state correctly

### Step 2: UNDO Command Implementation
**What**: Revert file to previous state

**Tasks**:
1. **Client**: Parse `UNDO <filename>` command
2. **NM**: Handle UNDO request
   - Check read access (anyone with read access can undo)
   - Return SS connection info
3. **SS**: Handle UNDO command
   - Check if undo state exists
   - Restore file from undo state
   - Update metadata (last_modified)
   - Clear undo state (single undo only)

**Testing**:
- [ ] UNDO after one WRITE
- [ ] UNDO after multiple WRITEs (reverts last one)
- [ ] UNDO without previous change (error)
- [ ] UNDO by different user (works - file-specific)

**Verification Checklist**:
- [ ] UNDO reverts last change correctly
- [ ] UNDO works for any user with read access
- [ ] Single-level undo only
- [ ] No compiler warnings
- [ ] Manual test runs documented

---

## Phase 6: EXEC Command
**Goal**: Execute file content as shell commands on NM

### Step 1: EXEC Command Infrastructure
**What**: NM executes file commands and streams output to client

**Tasks**:
1. **Client**: Parse `EXEC <filename>` command
2. **NM**: Handle EXEC request
   - Check read access
   - Request file content from SS
   - Execute commands (one line at a time or all at once)
   - Capture stdout/stderr
   - Stream output to client

3. **NM**: Command execution
   - Use `popen()` or `fork() + exec()` to run shell commands
   - Read output line-by-line
   - Send to client as DATA messages
   - Handle errors (command not found, etc.)

**Testing**:
- [ ] EXEC file with single command
- [ ] EXEC file with multiple commands
- [ ] EXEC file with invalid command (error handling)
- [ ] EXEC without read access (error)

**Verification Checklist**:
- [ ] EXEC runs commands correctly
- [ ] Output streamed to client
- [ ] Error handling works
- [ ] No compiler warnings
- [ ] Manual test runs documented

---

## Summary

### Phase 3: Client-SS Direct Communication & Access Control (40 marks)
- READ (10) + STREAM (15) + Access Control (15)
- **Estimated Time**: 2-3 days
- **Dependencies**: None

### Phase 4: WRITE Command (30 marks)
- Sentence parsing, locking, word updates
- **Estimated Time**: 4-5 days (most complex)
- **Dependencies**: None (but needed for UNDO)

### Phase 5: UNDO Command (15 marks)
- Change tracking and restoration
- **Estimated Time**: 1-2 days
- **Dependencies**: Phase 4 (WRITE)

### Phase 6: EXEC Command (15 marks)
- Command execution on NM
- **Estimated Time**: 1 day
- **Dependencies**: None

**Total Remaining: 100 marks**
**Total Estimated Time: 8-11 days**

---

## Testing Strategy

For each phase:
1. **Unit Tests**: Test individual functions (sentence parsing, locking, etc.)
2. **Integration Tests**: Test command flow (client → NM → SS)
3. **Concurrency Tests**: Multiple clients, concurrent operations
4. **Error Tests**: Invalid inputs, permission errors, edge cases
5. **Manual Verification**: Follow examples from requirements.md

## Notes

- **Priority**: Phase 3 → Phase 4 → Phase 5 → Phase 6
- **Critical Path**: Phase 4 (WRITE) is most complex and blocks Phase 5 (UNDO)
- **Parallel Work**: Phase 3 and Phase 6 can be done in parallel (different components)
- **Testing**: Each phase must be fully tested before moving to next

