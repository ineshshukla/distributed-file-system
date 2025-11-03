# Step 1 & 2 Implementation Verification Guide

## What Was Implemented

### Step 1: Error Code System & Enhanced Logging

#### Files Created:
- `src/common/errors.h` - Error code definitions and structures
- `src/common/errors.c` - Error code implementation

#### Files Modified:
- `src/common/protocol.h` - Added error message helper functions
- `src/common/protocol.c` - Implemented error message formatting and parsing

#### Features:
1. **Error Code Constants**: 7 error codes defined (OK, INVALID, UNAUTHORIZED, NOT_FOUND, CONFLICT, UNAVAILABLE, INTERNAL)
2. **Error Structure**: Contains error code and human-readable message
3. **Helper Functions**:
   - `error_code_to_string()` - Convert enum to string
   - `error_create()` - Create error with formatted message (printf-style)
   - `error_simple()` - Create error with simple string
   - `error_ok()` - Create success (no error)
   - `error_is_ok()` - Check if error indicates success
4. **Protocol Support**: Error messages can be sent/received via protocol

### Step 2: SS File Scanning & Persistence Foundation

#### Files Created:
- `src/ss/file_scan.h` - File scanning interface
- `src/ss/file_scan.c` - Directory scanning implementation
- `src/ss/file_storage.h` - File storage interface
- `src/ss/file_storage.c` - File operations and metadata management

#### Files Modified:
- `src/ss/main.c` - Integrated file scanning and storage
- `src/nm/main.c` - Parses and logs file lists from SS
- `Makefile` - Added new source files

#### Features:
1. **File Scanning**: SS scans `storage_dir/files/` on startup
2. **File List**: SS sends comma-separated file list to NM during registration
3. **File Storage Functions**:
   - `file_create()` - Create empty file with metadata
   - `file_read()` - Read entire file content
   - `file_delete()` - Delete file and metadata
   - `file_exists()` - Check if file exists
4. **Metadata Management**:
   - `metadata_load()` - Load metadata from disk
   - `metadata_save()` - Save metadata atomically
   - `metadata_update_last_accessed()` - Update access timestamp
   - `metadata_update_last_modified()` - Update modify timestamp
5. **Storage Structure**:
   ```
   storage_ss1/
     ├── files/          # Actual file content
     └── metadata/       # File metadata (*.meta files)
   ```

## How to Verify

### 1. Build Verification

```bash
cd /home/ineshshukla/dev/OSN/course-project-pain-au-chocolat
make clean
make
```

**Expected**: All binaries compile without warnings or errors
- `bin_nm` - Name Server
- `bin_ss` - Storage Server  
- `bin_client` - Client

### 2. Error Code System Test

Create a simple test program:

```bash
cat > test_errors.c << 'EOF'
#include <stdio.h>
#include "src/common/errors.h"

int main() {
    printf("Error codes:\n");
    printf("  ERR_OK: %s\n", error_code_to_string(ERR_OK));
    printf("  ERR_NOT_FOUND: %s\n", error_code_to_string(ERR_NOT_FOUND));
    
    Error err = error_create(ERR_NOT_FOUND, "File '%s' not found", "test.txt");
    printf("Error message: %s\n", err.message);
    
    return 0;
}
EOF

gcc -Isrc/common -o test_errors test_errors.c src/common/errors.c
./test_errors
```

**Expected Output**:
```
Error codes:
  ERR_OK: OK
  ERR_NOT_FOUND: NOT_FOUND
Error message: File 'test.txt' not found
```

### 3. File Scanning Test

**Test 1: Empty Directory**

```bash
# Clean storage directory
rm -rf storage_ss1
mkdir -p storage_ss1/files

# Start NM
./bin_nm --host 127.0.0.1 --port 5000 > nm.log 2>&1 &

# Start SS (should scan empty directory)
./bin_ss --nm-host 127.0.0.1 --nm-port 5000 \
         --host 127.0.0.1 --client-port 6001 \
         --storage ./storage_ss1 --username ss1 > ss.log 2>&1 &

sleep 2

# Check logs
grep "ss_scan" ss.log
grep "files=" ss.log
```

**Expected in ss.log**:
```
{"ts":"...","level":"INFO","event":"ss_scan_start","msg":"scanning storage directory: ./storage_ss1"}
{"ts":"...","level":"INFO","event":"ss_scan_complete","msg":"found 0 files"}
{"ts":"...","level":"INFO","event":"ss_registered","msg":"payload=...,files="}
```

**Test 2: Directory with Files**

```bash
# Create test files
mkdir -p storage_ss1/files
echo "Hello" > storage_ss1/files/file1.txt
echo "World" > storage_ss1/files/file2.txt

# Stop previous instances
./stop_all.sh

# Start fresh
./bin_nm --host 127.0.0.1 --port 5000 > nm.log 2>&1 &
sleep 1
./bin_ss --nm-host 127.0.0.1 --nm-port 5000 \
         --host 127.0.0.1 --client-port 6001 \
         --storage ./storage_ss1 --username ss1 > ss.log 2>&1 &

sleep 2

# Check SS logs
grep "ss_scan" ss.log
grep "files=" ss.log

# Check NM logs
grep "nm_ss_file_list" nm.log
```

**Expected in ss.log**:
```
{"ts":"...","level":"INFO","event":"ss_scan_complete","msg":"found 2 files"}
{"ts":"...","level":"INFO","event":"ss_registered","msg":"payload=...,files=file1.txt,file2.txt"}
```

**Expected in nm.log**:
```
{"ts":"...","level":"INFO","event":"nm_ss_register","msg":"ip=127.0.0.1 user=ss1 files=2"}
{"ts":"...","level":"INFO","event":"nm_ss_file_list","msg":"user=ss1 list=file1.txt,file2.txt"}
```

### 4. File Storage Test

**Test: Create and Read File**

```bash
cat > test_storage.c << 'EOF'
#include <stdio.h>
#include "src/ss/file_storage.h"

int main() {
    const char *dir = "./storage_ss1";
    const char *file = "test.txt";
    
    // Create file
    if (file_create(dir, file, "alice") == 0) {
        printf("✓ File created\n");
    }
    
    // Check exists
    if (file_exists(dir, file)) {
        printf("✓ File exists\n");
    }
    
    // Load metadata
    FileMetadata meta;
    if (metadata_load(dir, file, &meta) == 0) {
        printf("✓ Metadata loaded: owner=%s\n", meta.owner);
    }
    
    return 0;
}
EOF

gcc -Isrc/ss -Isrc/common -o test_storage test_storage.c \
    src/ss/file_storage.c src/common/errors.c

./test_storage
```

**Expected Output**:
```
✓ File created
✓ File exists
✓ Metadata loaded: owner=alice
```

**Verify Files Created**:

```bash
ls -la storage_ss1/files/
ls -la storage_ss1/metadata/
cat storage_ss1/metadata/test.txt.meta
```

**Expected**:
- `storage_ss1/files/test.txt` exists (empty file)
- `storage_ss1/metadata/test.txt.meta` exists with metadata:
  ```
  owner=alice
  created=...
  last_modified=...
  last_accessed=...
  size_bytes=0
  word_count=0
  char_count=0
  ```

### 5. Integration Test (Full Flow)

```bash
# Clean start
./stop_all.sh
rm -rf storage_ss1
mkdir -p storage_ss1/files

# Create some files manually
echo "File 1 content" > storage_ss1/files/manual1.txt
echo "File 2 content" > storage_ss1/files/manual2.txt

# Start system
./bin_nm --host 127.0.0.1 --port 5000 > nm.log 2>&1 &
sleep 1
./bin_ss --nm-host 127.0.0.1 --nm-port 5000 \
         --host 127.0.0.1 --client-port 6001 \
         --storage ./storage_ss1 --username ss1 > ss.log 2>&1 &
sleep 2

# Verify file scanning worked
echo "=== SS Logs ==="
cat ss.log | grep -E "(scan|register)"

echo -e "\n=== NM Logs ==="
cat nm.log | grep -E "(register|file_list)"

# Cleanup
./stop_all.sh
```

**Expected**: SS should scan and report 2 files, NM should receive and log the file list.

## Understanding the Code

### Error Code System (`src/common/errors.h/c`)

**Purpose**: Provide universal error codes across the entire system (NM, SS, Client).

**Key Concepts**:
1. **ErrorCode enum**: 7 predefined error types matching requirements
2. **Error structure**: Contains both code (enum) and message (string)
3. **Formatted messages**: Use `error_create()` with printf-style formatting
4. **Protocol integration**: Error codes can be sent via network protocol

**Usage Example**:
```c
// Create an error
Error err = error_create(ERR_NOT_FOUND, "File '%s' not found", filename);

// Check if error
if (!error_is_ok(&err)) {
    printf("Error: %s - %s\n", error_code_to_string(err.code), err.message);
}
```

### File Scanning (`src/ss/file_scan.h/c`)

**Purpose**: Discover existing files in storage directory when SS starts.

**Key Concepts**:
1. **ScanResult**: Structure holding array of discovered files
2. **Directory traversal**: Uses `opendir()`/`readdir()` to scan files
3. **File information**: Captures filename, size, metadata existence
4. **File list string**: Builds comma-separated list for protocol

**Usage Example**:
```c
// Scan directory
ScanResult result = scan_directory("./storage_ss1", "files");

// Build file list string
char file_list[4096];
build_file_list_string(&result, file_list, sizeof(file_list));
// file_list now contains: "file1.txt,file2.txt,..."
```

### File Storage (`src/ss/file_storage.h/c`)

**Purpose**: Handle all file operations with persistence.

**Key Concepts**:
1. **Atomic operations**: Metadata writes use temp file + rename (prevents corruption)
2. **Metadata format**: Simple key=value text format (easy to parse)
3. **Directory structure**: Separate `files/` and `metadata/` directories
4. **Timestamp tracking**: Created, modified, accessed times stored
5. **Statistics**: Word count and character count (for INFO command)

**Storage Layout**:
```
storage_ss1/
  ├── files/
  │   └── test.txt          # Actual file content
  └── metadata/
      └── test.txt.meta      # Metadata file
```

**Usage Example**:
```c
// Create file
file_create("./storage_ss1", "test.txt", "alice");

// Read file
char content[1024];
size_t size;
file_read("./storage_ss1", "test.txt", content, sizeof(content), &size);

// Load metadata
FileMetadata meta;
metadata_load("./storage_ss1", "test.txt", &meta);
printf("Owner: %s\n", meta.owner);
```

### Integration Points

1. **SS Startup**: 
   - SS calls `scan_directory()` to find existing files
   - Builds file list string using `build_file_list_string()`
   - Includes file list in SS_REGISTER payload to NM

2. **NM Registration**:
   - NM receives SS_REGISTER with file list in payload
   - Parses file list (extracts "files=..." part)
   - Logs file count and list for verification
   - (Full indexing will be in Step 3)

3. **File Operations**:
   - All file operations use `file_storage.c` functions
   - Metadata automatically saved/loaded
   - Files persist across SS restarts

## Summary

✅ **Step 1 Complete**: Error code system implemented and tested
✅ **Step 2 Complete**: File scanning and storage foundation implemented and tested

**Next Steps** (Step 3): File indexing on NM for efficient search

