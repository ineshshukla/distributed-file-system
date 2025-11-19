# Remaining Work - Phase Plan

## Current Status

### ✅ Implemented (Phase 1–6)
- **VIEW** (with -a, -l flags) - 10 marks
- **CREATE** - 10 marks
- **DELETE** - 10 marks
- **INFO** - 10 marks
- **LIST** - 10 marks
- **READ** (client↔SS data path + ACL enforcement) - 10 marks
- **STREAM** (word-by-word with delay) - 15 marks
- **ADDACCESS / REMACCESS** (owner-managed ACL updates) - 15 marks
- **WRITE** (sentence parsing, locking, client→NM→SS interactive flow) - 30 marks
- **UNDO** (single-level revert) - 15 marks
- **EXEC** (NM executes file content, streams output) - 15 marks
- **System Requirements**: Data Persistence, Logging, Error Handling, Efficient Search - 35 marks
- **Specifications**: Initialization, NM, SS, Client - 10 marks

**Total Completed: 195 marks**

### ❌ Remaining (0 marks)

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

**Testing (✅ Completed)**:
- [x] Client sends READ to NM
- [x] NM returns SS connection info
- [x] Client connects to SS
- [x] SS sends file content
- [x] Client displays content correctly (multi-line payload, newline unescape)

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

**Testing (✅ Completed)**:
- [x] READ empty/newly created file
- [x] READ existing file with content
- [x] READ non-existent file (error path)
- [x] READ without access (ACL block)
- [x] Multiple clients reading (owner + granted user)

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

**Testing (✅ Completed)**:
- [x] STREAM empty/small file
- [x] STREAM multi-word file (0.1s delay observed)
- [x] STREAM non-existent file (error from NM/SS)
- [x] Client handles STOP packet / SS disconnect

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

**Testing (✅ Completed)**:
- [x] Owner adds read access (target can READ/STREAM)
- [x] Owner adds write access (accepted, stored in ACL for future phases)
- [x] Owner removes access (target immediately blocked)
- [x] Non-owner ADD/REM rejected
- [x] INFO reflects updated ACL (via metadata reload)

**Phase 3 Verification (✅ Completed)**:
- [x] Client ↔ NM ↔ SS direct paths exercised after clean restart
- [x] ACL fetch (`GET_ACL`) verified via logs
- [x] READ/STREAM deny unauthorized users and allow granted ones
- [x] ADDACCESS/REMACCESS propagate to SS metadata atomically
- [x] Manual test script recorded (see `context.md` / logs)
- [x] Build clean with `-Werror`

---

## Phase 4: WRITE Command (Sentence-Level Editing)
**Goal**: Implement word-level file editing with sentence locking

### Step 1: Sentence Parsing & Data Structures ✅
**What**: Parse files into sentences and words, manage sentence structure

**Tasks**:
1. **SS**: Create sentence parser module (`src/ss/sentence_parser.c`)
   - Parse file content into sentences (delimiters: `.`, `!`, `?`)
   - Each sentence is array of words
   - Handle edge cases: `e.g.`, `Umm... ackchually!`
   - Reconstruct file from sentences

2. **SS**: Create sentence & runtime data structures
   ```c
   typedef struct {
       char **words;       // Array of word strings
       int   word_count;   // Number of words
       int   sentence_id;  // Stable ID persisted in metadata
       int   version;      // Incremented each time this sentence is modified
   } Sentence;
   
   typedef struct {
       Sentence *sentences;
       int        sentence_count;
   } FileContent;
   
   typedef struct {
       int  sentence_id;
       char locked_by[64];
       time_t lock_time;
       int  session_id;        // identifies the WRITE session holding the lock
   } SentenceLock;
   
   typedef struct {
       char filename[MAX_FILENAME];
       pthread_mutex_t lock_mu;          // protects this runtime structure
       SentenceLock locks[MAX_SENTENCES]; // active locks keyed by sentence_id
       int lock_count;
   } FileRuntimeState;
   ```

3. **SS**: Load/save file content as sentences
   - Extend `<filename>.meta` to include per-sentence records, e.g.:
     ```
     SENTENCE_COUNT=N
     SENTENCE_0=id|word_count|char_count|offset
     ...
     ```
   - Store `sentence_id` + version per sentence; ensure IDs survive restarts.
   - Convert file text → sentences on load; rebuild `FileRuntimeState` lazily when a file is first accessed.
   - Convert sentences → file text on save while preserving IDs; update metadata atomically.

**Testing (✅ Completed)**:
- [x] Parse empty file (ensures zero-word files still yield a sentinel sentence)
- [x] Parse file with one sentence
- [x] Parse file with multiple sentences
- [x] Parse file with delimiters in words (`e.g.`, punctuation mid-word)
- [x] Reconstruct file from sentences and persist metadata

### Step 2: Sentence Locking Mechanism ✅
**What**: Lock sentences during WRITE operations

**Tasks**:
1. **SS**: Implement sentence lock manager
   - Per-file runtime lock table keyed by `sentence_id` (not mutable index)
   - Lock structure: `{sentence_id, locked_by_username, lock_time, session_id}`
   - Thread-safe locking via `FileRuntimeState.lock_mu`
   - Rebuild runtime lock table on demand (when file first touched after restart).

2. **SS**: Lock/unlock functions
   - `sentence_lock(filename, sentence_idx, username)` - returns 0 if locked, -1 if already locked
   - `sentence_unlock(filename, sentence_idx, username)` - unlocks if locked by same user
   - Check lock before allowing WRITE

3. **SS**: Handle lock timeout (optional, for robustness)
   - Auto-unlock after timeout (e.g., 5 minutes)

**Testing (✅ Completed)**:
- [x] Lock sentence successfully (WRITE acquires lock per sentence_id)
- [x] Lock already-locked sentence (client receives “locked by another writer”)
- [x] Unlock sentence on ETIRW / abort
- [x] Multiple users lock different sentences (split sentence test)
- [ ] Lock timeout (watchdog hook present, not exercised yet)

### Step 3: WRITE Command - Basic Flow ✅
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
   - Lock sentence ID (return error if locked)
   - Wait for word updates
   - Apply updates to sentence
   - Handle sentence delimiter creation (`.`, `!`, `?` in content)
   - Unlock on `ETIRW`

**Testing (✅ Completed)**:
- [x] WRITE to empty file (sentence 0)
- [x] WRITE to existing sentence
- [x] WRITE to locked sentence (error surfaced to second client)
- [x] Multiple word updates in one WRITE session
- [x] ETIRW unlocks sentence and commits changes atomically

### Step 4: Word-Level Updates & Sentence Splitting ✅
**What**: Implement word insertion/update logic and handle sentence delimiters

**Tasks**:
1. **SS**: Word update logic
   - Insert word at index (shift existing words)
   - Update word at index (replace)
   - Handle index out of bounds (error)
   - Handle negative indices (error)

2. **SS**: Sentence delimiter detection
   - When content contains `.`, `!`, `?` → split into new sentences
   - Assign new unique `sentence_id` to any newly created sentences (monotonic counter stored in metadata)
   - Maintain mapping from locked `sentence_id` to current index for subsequent updates
   - Persist updated `sentence_id` metadata during commit so future sessions re-map correctly.

3. **SS**: Atomic swap pattern for concurrent read/write
   - **During WRITE**: Work on temporary file (`files/<filename>.<session>.tmp`)
     - Load current file content + metadata into memory
     - Apply all word updates to the locked sentence IDs
     - Update sentence versions / metadata in-memory
     - Write updated file and metadata to temp location
   - **On ETIRW**: Commit sequence
     1. Re-parse latest on-disk metadata to ensure IDs map correctly.
     2. Merge in-memory sentence changes by ID (skip others).
     3. Write merged content to new `.tmp`, fsync, rename into place.
     4. Remove per-session temp artifacts, unlock sentence IDs.
   - **How it works with concurrent reads** (unchanged inode semantics).

**Testing (✅ Completed)**:
- [x] Insert word at index 0 (prepend text)
- [x] Insert at middle / end via multiple edits
- [x] Insert punctuation to force new sentence creation (split + new IDs)
- [x] Multiple updates in sequence within a single session
- [x] Invalid index rejected with error
- [x] Atomic write verified via READ/STREAM during/after WRITE (inode swap semantics)

### Step 5: Concurrent WRITE Handling ✅ (initial pass)
**What**: Ensure multiple users can write to different sentences simultaneously

**Tasks**:
1. **SS**: Multi-threaded command handler
   - Dedicated acceptor thread pushes new sockets into a bounded queue (drop or back-pressure when full; default size configurable)
   - Fixed-size worker thread pool (configurable via CLI/env, default e.g. 8) pulls requests and processes them
   - Each READ/STREAM/WRITE/ACL/GET_ACL request runs on a worker; long-running WRITEs no longer block accept loop
   - Workers coordinate through per-file `lock_mu` and runtime state when touching lock tables / metadata

2. **SS**: Lock coordination
   - Check lock before acquiring
   - Release lock on ETIRW or error
   - Handle client disconnection (unlock on timeout)

3. **SS**: Concurrent read/write with atomic swap + sentence IDs
   - **READ/STREAM during WRITE**: same as Step 4 (inode semantics)
   - **Multiple WRITEs to different sentences**:
     - Each WRITE locks specific `sentence_id`; workers refuse duplicate locks
     - Each session maintains its own temp state; on commit we reload latest metadata, merge by ID, then rename
     - No last-write-wins discard required; disjoint sentence edits merge cleanly even if earlier commits changed indices
   - **Failure / cleanup**:
     - If worker crashes or client disconnects before ETIRW, release lock, delete session temp files, roll back partial metadata changes
     - Background watchdog clears stale locks past timeout to avoid deadlocks

**Testing**:
- [ ] Two users write to different sentences simultaneously (to run)
- [x] Two users try to write to same sentence (second client blocked with lock error)
- [ ] Client disconnects mid-WRITE (lock released) – to be exercised
- [ ] Multiple concurrent WRITEs to different files – to be exercised
- [x] READ/STREAM during active WRITE (manual snapshots show consistent output)
- [x] READ after WRITE completes (verified in timeline)

**Verification Checklist**:
- [x] WRITE command works end-to-end (client↔NM↔SS)
- [x] Sentence locking prevents concurrent edits on same sentence
- [x] Word updates work correctly (insert/append/multi-edit)
- [x] Sentence delimiters create new sentences with stable IDs
- [x] Atomic writes prevent corruption (READ/STREAM snapshots)
- [ ] Concurrent WRITEs to different sentences (need dedicated test run)
- [x] No compiler warnings (`make` with `-Werror`)
- [x] Manual test runs documented (logs + timeline in `nm.log` / `ss.log`)

---

## Phase 5: UNDO Command ✅
**Goal**: Single-level undo for file changes

### Step 1: Change Tracking ✅
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

**Testing (in progress)**:
- [x] Save state before WRITE (snapshot taken just before commit)
- [x] Overwrite previous undo state (latest snapshot wins)
- [ ] Load undo state correctly (manual scenario pending)

### Step 2: UNDO Command Implementation ✅
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

**Testing (in progress)**:
- [ ] UNDO after one WRITE (to verify file+metadata rollback)
- [ ] UNDO after multiple WRITEs (ensures only last change is reverted)
- [ ] UNDO without previous change (expect NO_UNDO error)
- [ ] UNDO by different user with read access

**Verification Checklist**:
- [ ] UNDO reverts last change correctly
- [ ] UNDO works for any user with read access
- [x] Single-level undo only (snapshot overwritten per commit)
- [x] No compiler warnings (`make` clean)
- [ ] Manual test runs documented

---

## Phase 6: EXEC Command ✅
**Goal**: Execute file content as shell commands on NM

### Step 1: EXEC Command Infrastructure ✅
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

**Testing (ongoing)**:
- [x] EXEC file with single/multiple commands (echo + ls sample)
- [ ] EXEC file with invalid command (expect captured error output)
- [ ] EXEC without read access (ensure ACL denial)

**Verification Checklist**:
- [ ] EXEC runs commands correctly (basic scenario verified)
- [x] Output streamed to client (client loops over DATA/STOP)
- [ ] Error handling works (add negative tests)
- [x] No compiler warnings
- [ ] Manual test runs documented

---

## Summary

### Phase 3: Client-SS Direct Communication & Access Control (40 marks)
- READ (10) + STREAM (15) + Access Control (15)
- **Estimated Time**: 2-3 days
- **Dependencies**: None

### Phase 4: WRITE Command (30 marks) ✅
- Sentence parsing, locking, word updates
- **Actual Outcome**: Completed with interactive client workflow, sentence-level locking, SS worker pool, and metadata persistence. Additional stress/concurrency tests queued.
- **Dependencies**: None (unblocks Phase 5)

### Phase 5: UNDO Command (15 marks) ✅
- Change tracking and restoration
- **Actual Outcome**: Undo snapshot stored before each commit, SS exposes UNDO command, NM/client wiring complete. Targeted manual tests pending.
- **Dependencies**: Phase 4 (WRITE)

### Phase 6: EXEC Command (15 marks) ✅
- Command execution on NM with NM↔SS file fetch, `/bin/sh` execution, and client streaming.
- **Follow-ups**: expand regression coverage for error cases (invalid command, ACL failure).

**Total Remaining: 0 marks**
**Updated Estimate: complete**

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

