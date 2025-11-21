# Fault Tolerance Implementation Status

## ✅ PHASE 1: FAILURE DETECTION - **COMPLETE**

### Implemented Components

1. **Heartbeat Monitor Module** (`src/nm/heartbeat_monitor.c/h`)
   - Tracks last heartbeat timestamp for each SS
   - Background thread checks every 5 seconds
   - Marks SS as FAILED after 15 seconds (3 missed heartbeats)
   - Automatic recovery detection when SS reconnects
   - Callback system for failure notifications
   - Thread-safe with mutex protection

2. **Integration with NM**
   - SS registration → register with heartbeat monitor
   - HEARTBEAT message → update timestamp
   - NM startup → initialize and start monitoring thread
   - NM shutdown → stop monitoring gracefully

3. **Logging Support**
   - Added `log_warning()` function to logging module
   - Comprehensive logging at INFO, WARNING, ERROR levels

4. **Test Script**
   - `test_heartbeat.sh` - Tests SS failure detection
   - Simulates failure by killing SS process
   - Verifies detection within 15-20 seconds

### Configuration
- **Timeout**: 15 seconds without heartbeat = failure
- **Check Interval**: Monitor checks every 5 seconds
- **Threshold**: 3 consecutive missed heartbeats

---

## ✅ PHASE 2: REPLICATION INFRASTRUCTURE - **IN PROGRESS**

### Implemented Components

1. **Replication Module** (`src/nm/replication.c/h`)
   - **Pairing Strategy**: ss1 → ss1_backup, ss2 → ss2_backup, etc.
   - **Track pairs**: Primary-Replica mapping with status
   - **Failover support**: Promote replica when primary fails
   - **Recovery support**: Resume original primary after sync
   - **Thread-safe**: Mutex-protected operations

2. **FileEntry Updates** (`src/nm/index.h`)
   - Added replica SS fields:
     - `replica_ss_host`
     - `replica_ss_client_port`
     - `replica_ss_username`

3. **Makefile Updates**
   - Added `src/nm/replication.c` to build

### Key Functions

```c
// Assign replica for primary SS
int replication_assign_replica(const char *primary_ss);

// Get replica/primary SS
const char *replication_get_replica(const char *primary_ss);
const char *replication_get_primary(const char *replica_ss);

// Failover management
int replication_failover(const char *failed_primary);
int replication_recover(const char *recovered_ss);

// Get active primary (handles failover transparently)
const char *replication_get_active_primary(const char *logical_ss);
```

---

## 📋 PHASE 3: REMAINING TASKS

### 1. SS Pairing on Registration
**File**: `src/nm/main.c` - `handle_message()` for `SS_REGISTER`

**What to do**:
```c
// After indexing files in SS_REGISTER handler:
replication_init();  // Once at NM startup
replication_assign_replica(msg->username);  // For each SS

// Store replica info in FileEntry for each file
const char *replica = replication_get_replica(msg->username);
if (replica) {
    // Get replica SS connection info
    char replica_host[64];
    int replica_port;
    registry_get_ss_info(replica, replica_host, sizeof(replica_host), &replica_port);
    
    // Update all files from this SS with replica info
    // (iterate through index and set replica fields)
}
```

### 2. File Replication Protocol
**New Message Types Needed**:

```c
// SS Command: Read file content (for NM to fetch)
"GET_FILE_CONTENT|filename" → returns file data

// SS Command: Write file content (for NM to push)
"PUT_FILE_CONTENT|filename|data" → writes file

// SS Command: Copy metadata
"PUT_METADATA|filename|metadata_data" → writes .meta file

// SS Command: Delete file (for replication)
"DELETE_FILE|filename" → deletes file
```

**Where**: Add to `src/ss/main.c` command handler

### 3. Async Replication Worker
**New File**: `src/nm/replication_worker.c/h`

**Purpose**: Background thread that processes replication jobs

```c
typedef struct ReplicationJob {
    char operation[16];      // "CREATE", "DELETE", "WRITE", "METADATA"
    char filename[256];
    char primary_ss[64];
    char replica_ss[64];
    struct ReplicationJob *next;
} ReplicationJob;

// Queue job for async replication
void replication_queue_job(const char *operation, const char *filename,
                            const char *primary_ss, const char *replica_ss);

// Worker thread processes queue
void *replication_worker_thread(void *arg);
```

### 4. Hook CREATE Command
**File**: `src/nm/commands.c` - `handle_create()`

**Add after successful creation on primary**:
```c
// After CREATE succeeds on primary SS
const char *replica = replication_get_replica(ss_username);
if (replica) {
    // Queue async replication job
    replication_queue_job("CREATE", filename, ss_username, replica);
}
```

### 5. Hook DELETE Command
**File**: `src/nm/commands.c` - `handle_delete()`

**Add after successful deletion**:
```c
const char *replica = replication_get_replica(ss_username);
if (replica) {
    replication_queue_job("DELETE", filename, ss_username, replica);
}
```

### 6. Hook WRITE Command
**Challenge**: WRITE happens directly between client and SS

**Solution**: Add completion notification

**File**: `src/ss/main.c` - After WRITE commit

```c
// After write_session_commit() succeeds:
// Send WRITE_COMPLETE message to NM
Message msg;
snprintf(msg.type, sizeof(msg.type), "WRITE_COMPLETE");
snprintf(msg.payload, sizeof(msg.payload), "filename=%s", filename);
// Send to NM

// NM receives WRITE_COMPLETE → queues replication
```

### 7. Metadata Operations
**Hook these commands**:
- ADDACCESS / REMACCESS → replicate `.meta` file
- CHECKPOINT → replicate checkpoint files
- CREATEFOLDER → replicate folder structure

### 8. Failover Callback
**File**: `src/nm/main.c` - in `main()`

```c
void on_ss_failure(const char *ss_username) {
    log_error("failover", "SS %s failed", ss_username);
    
    // Trigger failover
    replication_failover(ss_username);
    
    // Update index to route requests to replica
    // (All FileEntry records with ss_username should now use replica)
}

heartbeat_monitor_set_failure_callback(on_ss_failure);
```

### 9. Read Request Routing
**File**: `src/nm/commands.c` - READ/STREAM handlers

```c
// Before sending SS info to client:
const char *active_ss = replication_get_active_primary(logical_ss_name);
// Use active_ss instead of logical_ss_name
```

### 10. Recovery Sync
**File**: `src/nm/main.c` - `SS_REGISTER` handler

```c
// Check if SS was previously registered
if (was_previously_active(msg->username)) {
    log_info("recovery", "SS %s reconnected", msg->username);
    
    // Trigger recovery
    replication_recover(msg->username);
    
    // Queue full sync job
    const char *sync_source = determine_sync_source(msg->username);
    replication_queue_job("SYNC_ALL", "", sync_source, msg->username);
}
```

---

## 🎯 IMPLEMENTATION PRIORITY

### Phase 3A: Basic Replication (Highest Priority)
1. ✅ Replication module (DONE)
2. ⏳ SS pairing on registration
3. ⏳ File replication protocol (GET/PUT commands)
4. ⏳ Async replication worker
5. ⏳ Hook CREATE command

### Phase 3B: Complete Replication
6. ⏳ Hook DELETE command
7. ⏳ Hook WRITE command (needs SS notification)
8. ⏳ Metadata replication (ACCESS, CHECKPOINT, FOLDER)

### Phase 3C: Failover & Recovery
9. ⏳ Failover callback integration
10. ⏳ Read request routing to active SS
11. ⏳ Recovery sync on reconnection

---

## 📝 DESIGN DECISIONS CONFIRMED

Based on your requirements:

| Aspect | Decision |
|--------|----------|
| **Pairing** | ss1 → ss1_backup (dedicated backup per SS) |
| **Direction** | Bi-directional (but primary returns to original) |
| **Sync** | Async after every operation (CREATE/DELETE/WRITE/METADATA) |
| **Redundancy** | Complete - all files + metadata |
| **Failure** | Promote replica immediately |
| **Recovery** | Original primary resumes after sync |
| **Metadata** | Yes - replicate .meta, .undo.meta, checkpoints |
| **Sync Path** | NM as middleman (Option C) |
| **Consistency** | Eventual - replica can lag |
| **ACK** | No ACK from replica required |
| **Ports** | Use existing client_port, no new ports needed |

---

## 🧪 TESTING STRATEGY

### Test 1: Basic Replication
```bash
# Start NM, ss1, ss1_backup
# Create file on ss1
# Verify file exists on ss1_backup

CREATE test.txt on ss1
→ ss1 creates file
→ NM triggers async replication
→ ss1_backup receives file
```

### Test 2: Failure Detection
```bash
# Kill ss1
# Wait 15 seconds
# Verify NM detects failure
# Verify replica promoted

kill ss1
→ Heartbeat monitor detects timeout
→ Callback triggers failover
→ Index updated to use ss1_backup
```

### Test 3: Read During Failure
```bash
# ss1 failed, ss1_backup active
# Client reads file
# Verify routed to ss1_backup

READ test.txt
→ NM routes to ss1_backup
→ Client receives data
```

### Test 4: Recovery
```bash
# Restart ss1
# Verify ss1 syncs from ss1_backup
# Verify ss1 resumes as primary

restart ss1
→ ss1 registers
→ NM detects recovery
→ Full sync from ss1_backup
→ ss1 becomes primary again
```

---

## 📊 CURRENT STATUS SUMMARY

✅ **Complete**:
- Heartbeat monitoring
- Failure detection
- Replication data structures
- Pairing logic
- Failover/recovery framework

⏳ **In Progress**:
- SS pairing integration
- File replication protocol

❌ **Not Started**:
- Async replication worker
- Command hooks (CREATE/DELETE/WRITE)
- Metadata replication
- Read routing during failure
- Recovery sync implementation

---

## 🚀 NEXT STEPS

I recommend implementing in this order:

1. **File Replication Protocol** (GET/PUT commands on SS)
   - Enables NM to fetch/push files
   
2. **Async Replication Worker** (background thread + queue)
   - Foundation for all replication operations
   
3. **CREATE Hook** (simplest operation to test)
   - First end-to-end test of replication
   
4. **Failover Integration** (callback + routing)
   - Makes system fault-tolerant
   
5. **DELETE/WRITE/METADATA** (complete replication)
   - Full functionality

6. **Recovery Sync** (most complex)
   - Final piece

Would you like me to:
- A) Implement the file replication protocol (GET/PUT commands)?
- B) Create the async replication worker thread?
- C) Integrate SS pairing on registration?
- D) Something else?

Let me know and I'll continue! 🎯
