# Step 3 & 4 Implementation Verification Guide

## What Was Implemented

### Step 3: NM File Index & Efficient Search

#### Files Created:
- `src/nm/index.h` / `src/nm/index.c` - File index with hash map and LRU cache
- `src/nm/access_control.h` / `src/nm/access_control.c` - Access control enforcement

#### Features:
1. **Hash Map Index**: O(1) average case file lookup
   - Hash table with 1024 buckets
   - Chaining for collision handling
   - djb2 hash algorithm
2. **LRU Cache**: Caches up to 100 most recently accessed files
   - Fast subsequent lookups for hot files
   - Automatic eviction of least recently used entries
3. **File Entry Structure**: Stores all metadata needed for operations
   - Filename, owner, SS location (host, port)
   - Timestamps (created, modified, accessed)
   - Statistics (size, word count, char count)
4. **Index Operations**:
   - `index_add_file()` - Add file to index
   - `index_lookup_file()` - O(1) lookup by filename
   - `index_remove_file()` - Remove file from index
   - `index_get_all_files()` - Get all files (for VIEW)
   - `index_get_files_by_owner()` - Filter by owner
   - `index_update_metadata()` - Update file stats

### Step 4: Access Control List (ACL) System

#### Files Created:
- `src/common/acl.h` / `src/common/acl.c` - ACL data structure and operations

#### Features:
1. **ACL Structure**: 
   - Owner (always has RW access)
   - List of users with permissions (read or write)
   - Write access implies read access
2. **ACL Operations**:
   - `acl_add_read()` - Grant read access
   - `acl_add_write()` - Grant write access (includes read)
   - `acl_remove()` - Remove all access
   - `acl_check_read()` / `acl_check_write()` - Permission checks
   - `acl_serialize()` / `acl_deserialize()` - Persistence
3. **Integration**:
   - ACL stored in file metadata on SS
   - ACL initialized when file is created
   - ACL persisted across SS restarts

#### Files Modified:
- `src/ss/file_storage.h` / `src/ss/file_storage.c` - Added ACL to FileMetadata
- `src/nm/main.c` - Parses file lists and indexes files on SS registration

## How to Verify

### 1. Build Verification

```bash
cd /home/ineshshukla/dev/OSN/course-project-pain-au-chocolat
make clean
make
```

**Expected**: All binaries compile without warnings or errors

### 2. File Indexing Test

**Test: SS registers with files, NM indexes them**

```bash
# Clean start
./stop_all.sh
rm -rf storage_ss1
mkdir -p storage_ss1/files

# Create test files
echo "content1" > storage_ss1/files/file1.txt
echo "content2" > storage_ss1/files/file2.txt
echo "content3" > storage_ss1/files/file3.txt

# Start NM
./bin_nm --host 127.0.0.1 --port 5000 > nm.log 2>&1 &

# Start SS (should scan and send file list)
sleep 1
./bin_ss --nm-host 127.0.0.1 --nm-port 5000 \
         --host 127.0.0.1 --client-port 6001 \
         --storage ./storage_ss1 --username ss1 > ss.log 2>&1 &

sleep 2

# Check logs
echo "=== NM Index Logs ==="
grep "index" nm.log

echo -e "\n=== SS Registration ==="
grep "register" ss.log | tail -1
```

**Expected Output**:
- NM logs: `nm_index_init`, `nm_file_indexed` for each file
- SS logs: `ss_scan_complete found 3 files`, file list in registration payload
- NM logs: `nm_ss_register files=3 indexed`

**Verify Index Functionality**:
The index should now contain 3 files. You can verify this by checking NM logs:
- Each file should have a `nm_file_indexed` log entry
- Total count should match: `files=3 indexed`

### 3. ACL System Test

**Test: Create file and verify ACL is initialized**

```bash
# Create a test program
cat > test_acl_quick.c << 'EOF'
#include <stdio.h>
#include "src/common/acl.h"

int main() {
    ACL acl = acl_init("alice");
    printf("Owner: %s\n", acl.owner);
    printf("Alice read: %d (expected 1)\n", acl_check_read(&acl, "alice"));
    printf("Alice write: %d (expected 1)\n", acl_check_write(&acl, "alice"));
    
    acl_add_read(&acl, "bob");
    printf("Bob read: %d (expected 1)\n", acl_check_read(&acl, "bob"));
    printf("Bob write: %d (expected 0)\n", acl_check_write(&acl, "bob"));
    
    acl_add_write(&acl, "charlie");
    printf("Charlie read: %d (expected 1)\n", acl_check_read(&acl, "charlie"));
    printf("Charlie write: %d (expected 1)\n", acl_check_write(&acl, "charlie"));
    
    return 0;
}
EOF

gcc -Isrc/common -o test_acl_quick test_acl_quick.c src/common/acl.c
./test_acl_quick
rm -f test_acl_quick test_acl_quick.c
```

**Expected Output**:
```
Owner: alice
Alice read: 1 (expected 1)
Alice write: 1 (expected 1)
Bob read: 1 (expected 1)
Bob write: 0 (expected 0)
Charlie read: 1 (expected 1)
Charlie write: 1 (expected 1)
```

### 4. File Storage with ACL Test

**Test: Create file and verify ACL persists**

```bash
# Create test program
cat > test_file_acl_quick.c << 'EOF'
#include <stdio.h>
#include "src/ss/file_storage.h"

int main() {
    const char *dir = "./storage_ss1";
    const char *file = "test_acl.txt";
    
    // Create file
    file_create(dir, file, "alice");
    
    // Load metadata
    FileMetadata meta;
    metadata_load(dir, file, &meta);
    printf("Owner: %s\n", meta.owner);
    printf("ACL owner: %s\n", meta.acl.owner);
    printf("Alice read: %d\n", acl_check_read(&meta.acl, "alice"));
    
    // Modify ACL
    acl_add_read(&meta.acl, "bob");
    metadata_save(dir, file, &meta);
    
    // Reload and verify
    FileMetadata meta2;
    metadata_load(dir, file, &meta2);
    printf("Bob read (after reload): %d (expected 1)\n", acl_check_read(&meta2.acl, "bob"));
    
    return 0;
}
EOF

gcc -Isrc/ss -Isrc/common -o test_file_acl_quick test_file_acl_quick.c \
    src/ss/file_storage.c src/common/acl.c src/common/errors.c
./test_file_acl_quick
rm -f test_file_acl_quick test_file_acl_quick.c
```

**Expected Output**:
```
Owner: alice
ACL owner: alice
Alice read: 1
Bob read (after reload): 1 (expected 1)
```

**Verify Metadata File**:
```bash
cat storage_ss1/metadata/test_acl.txt.meta
```

**Expected Format**:
```
owner=alice
created=...
last_modified=...
last_accessed=...
size_bytes=0
word_count=0
char_count=0
ACL_START
owner=alice
bob=R
ACL_END
```

### 5. Integration Test (Full Flow)

**Test: SS with files -> NM indexes -> ACL works**

```bash
# Clean start
./stop_all.sh
rm -rf storage_ss1
mkdir -p storage_ss1/files

# Create files manually
echo "File 1" > storage_ss1/files/manual1.txt
echo "File 2" > storage_ss1/files/manual2.txt

# Start system
./bin_nm --host 127.0.0.1 --port 5000 > nm.log 2>&1 &
sleep 1
./bin_ss --nm-host 127.0.0.1 --nm-port 5000 \
         --host 127.0.0.1 --client-port 6001 \
         --storage ./storage_ss1 --username ss1 > ss.log 2>&1 &
sleep 2

# Verify indexing worked
echo "=== Index Verification ==="
grep "nm_file_indexed" nm.log | wc -l  # Should be 2

# Verify file list was sent
echo "=== File List Verification ==="
grep "files=" ss.log | tail -1

# Cleanup
./stop_all.sh
```

**Expected**: 
- 2 files indexed (one log entry per file)
- File list in SS registration payload

## Understanding the Code

### File Index (`src/nm/index.h/c`)

**Purpose**: Fast O(1) file lookup for Name Server

**Key Concepts**:
1. **Hash Map**: 
   - 1024 buckets (power of 2 for efficiency)
   - djb2 hash function: `hash = hash * 33 + c`
   - Chaining handles collisions
2. **LRU Cache**:
   - Doubly-linked list (head = most recent, tail = least recent)
   - Moves accessed files to front
   - Evicts tail when cache is full (100 entries)
3. **FileEntry Structure**:
   - Stores all metadata needed for operations
   - Links to SS location (for client-SS direct communication)
   - Supports filtering by owner

**Usage Example**:
```c
// Initialize index
index_init();

// Add file
FileEntry *entry = index_add_file("test.txt", "alice", "127.0.0.1", 6001, "ss1");

// Lookup (O(1))
FileEntry *found = index_lookup_file("test.txt");
if (found) {
    printf("Owner: %s, SS: %s:%d\n", found->owner, found->ss_host, found->ss_client_port);
}
```

### ACL System (`src/common/acl.h/c`)

**Purpose**: Manage file permissions (read/write access)

**Key Concepts**:
1. **Owner Always Has RW**: Owner permissions are implicit, not stored in entries
2. **Write Implies Read**: Users with write access automatically have read access
3. **Serialization**: ACL stored as text in metadata file
   - Format: `owner=username\nuser1=R\nuser2=RW\n`
4. **Persistence**: ACL saved/loaded with file metadata

**Usage Example**:
```c
// Initialize ACL
ACL acl = acl_init("alice");

// Grant permissions
acl_add_read(&acl, "bob");      // Bob can read
acl_add_write(&acl, "charlie"); // Charlie can read and write

// Check permissions
if (acl_check_read(&acl, "bob")) {
    printf("Bob can read\n");
}

// Serialize for storage
char buf[4096];
acl_serialize(&acl, buf, sizeof(buf));
```

### Access Control Enforcement (`src/nm/access_control.h/c`)

**Purpose**: Check permissions before file operations

**Key Functions**:
- `check_file_access()` - Check if user has read/write access
- `check_file_owner()` - Check if user is owner

**Usage** (will be used in Step 6 for command handlers):
```c
ACL acl;  // Loaded from file metadata
Error err = check_file_access("test.txt", "bob", 0, &acl);  // 0 = need read
if (!error_is_ok(&err)) {
    printf("Access denied: %s\n", err.message);
}
```

### Integration Points

1. **SS Registration**:
   - SS scans directory, builds file list
   - Sends file list to NM in registration payload
   - NM parses payload, extracts files, indexes each file
   - Files indexed with SS location (host, port)

2. **File Creation** (future - Step 6):
   - When file is created, ACL initialized with owner
   - File added to NM index
   - ACL stored in metadata

3. **File Operations** (future - Step 6):
   - Before any operation, check ACL
   - Load ACL from metadata (or cache)
   - Use `check_file_access()` to verify permissions

## Quick Verification Checklist

Run these commands to verify everything works:

```bash
# 1. Build
make clean && make

# 2. Test file indexing
./stop_all.sh
rm -rf storage_ss1 && mkdir -p storage_ss1/files
echo "test" > storage_ss1/files/file1.txt
./bin_nm --host 127.0.0.1 --port 5000 > nm.log 2>&1 &
./bin_ss --nm-host 127.0.0.1 --nm-port 5000 --storage ./storage_ss1 --username ss1 > ss.log 2>&1 &
sleep 2
grep "nm_file_indexed" nm.log | wc -l  # Should be 1

# 3. Test ACL persistence
# (Use test programs above or check metadata files manually)
cat storage_ss1/metadata/*.meta | grep ACL_START  # Should see ACL sections

# 4. Cleanup
./stop_all.sh
```

## Summary

✅ **Step 3 Complete**: File index with O(1) lookup and LRU cache implemented
✅ **Step 4 Complete**: ACL system with persistence implemented

**Next Steps** (Step 5): Client interactive shell for command processing

