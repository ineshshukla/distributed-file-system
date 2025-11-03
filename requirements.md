# LangOS Document Collaboration System - Requirements

## System Architecture

### Core Components

#### User Clients
- Represent users interacting with the system
- Provide interface for file operations (create, view, read, write, delete, etc.)
- Multiple clients may run concurrently
- All clients must be able to interact with the system simultaneously

#### Name Server (NM)
- Acts as central coordinator
- Handles all communication between clients and storage servers
- Maintains mapping between file names and their storage locations
- Ensures efficient and correct access to files across the system
- Single instance running at any point
- If NM fails, entire system is down (restart required)

#### Storage Servers (SS)
- Responsible for storing and retrieving file data
- Ensure durability, persistence, and efficient access to files
- Support concurrent access by multiple clients (reads and writes)
- Multiple instances can connect to NM
- Can disconnect and reconnect at any time (graceful handling required)

### Connection Model
- Single NM instance, multiple SS instances, multiple Client instances
- Clients and SS can disconnect/reconnect at any time
- System must handle these events gracefully
- NM failure = system down (out of scope to handle)

## File System

### File Definition
- Files are fundamental units of data
- Each file uniquely identified by a name
- Files restricted to text data only
- No limit on file size or total number of files
- System must efficiently handle both small and large documents (variable growth)

### File Structure
- Every file consists of multiple sentences
- Each sentence made up of words
- **Sentence**: Sequence of words ending with period (.), exclamation mark (!), or question mark (?)
- **Word**: Sequence of ASCII characters without spaces
- Words within a sentence separated by spaces
- System handles segmentation; user accesses file as a whole

### Important Sentence Delimiter Rules
- Every period, exclamation mark, or question mark is a sentence delimiter
- Even if in middle of a word like "e.g." or "Umm… ackchually!"
- Delimiters create separate sentences accordingly

### Concurrent Access
- Files support concurrent access for both reading and writing
- When user edits a sentence, that sentence is locked for editing by others until operation completes
- Multiple users can view or edit file simultaneously
- Prevents simultaneous edits to the same sentence

## User Functionalities (150 marks)

### [10] View Files
User can view all files they have access to. View all files on system using "-a" flag. "-l" flag lists files with details (word count, character count, last access, owner, etc.). Flags can be combined like "-al".

**Commands:**
- `VIEW` - Lists all files user has access to
- `VIEW -a` - Lists all files on the system
- `VIEW -l` - Lists all user-access files with details
- `VIEW -al` - Lists all system files with details

### [10] Read a File
Users can retrieve the contents of files stored within the system.

**Command:**
- `READ <filename>` - Prints the content of the complete file

### [10] Create a File
Users can create new files.

**Command:**
- `CREATE <filename>` - Creates an empty file with name <filename>
- If file already exists, respond with appropriate error

### [30] Write to a File
Users can update file content at word level. Operation allows modification and appending.

**Command Flow:**
```
WRITE <filename> <sentence_number>  # Locks sentence for other users
<word_index> <content>              # Updates sentence at word_index
...
<word_index> <content>              # Multiple updates allowed
ETIRW                               # Relieves sentence lock
```

**Important Points:**
- After each WRITE completion, sentence index updates
- Concurrent WRITEs must be handled correctly
- Content may contain sentence delimiters (., !, ?) - system recognizes and creates separate sentences
- Multiple writes within single WRITE call = single operation (for UNDO purposes)
- Updates applied in order received; later updates operate on already modified sentence

**Error Handling:**
- Attempting to write without access
- Attempting to write a locked sentence
- Invalid indices (negative or > number of words + 1)

**Hint:** Use temporary swap file initially, move contents to final file once all updates complete. May use locks, semaphores, algorithmic approaches.

### [15] Undo Change
Users can revert the last changes made to a file.

**Command:**
- `UNDO <filename>` - Reverts the last change made to the file

**Notes:**
- Undo is file-specific, not user-specific
- If user A makes change, user B can undo it
- Undo history maintained by storage server
- Only one undo operation supported (single-level)
- Undo operates at Storage Server level
- Reverts most recent change regardless of who made it

### [10] Get Additional Information
Users can access supplementary information about files (file size, access rights, timestamps, metadata).

**Command:**
- `INFO <filename>` - Display details in any convenient format
- Must include: file size, access rights, timestamps, owner, etc.

### [10] Delete a File
Owners can remove files from the system.

**Command:**
- `DELETE <filename>` - Deletes the file <filename>
- All data like user access should be updated accordingly

### [15] Stream Content
Client establishes direct connection with Storage Server and fetches & displays content word-by-word with 0.1 second delay between each word.

**Command:**
- `STREAM <filename>` - Streams content word by word with 0.1 second delay

**Note:**
- If storage server goes down mid-streaming, display appropriate error message

### [10] List Users
Users can view list of all users registered in the system.

**Command:**
- `LIST` - Lists all users in the system

### [15] Access Control
File creator (owner) can provide access to other users. Owner can provide read or write access. Owner can remove access. Owner always has both read and write access.

**Commands:**
- `ADDACCESS -R <filename> <username>` - Adds read access to the user
- `ADDACCESS -W <filename> <username>` - Adds write (and read) access to the user
- `REMACCESS <filename> <username>` - Removes all access

### [15] Executable File
Users (with read access) can "execute" the file. Execute means executing file content as shell commands.

**Command:**
- `EXEC <filename>` - Executes the file content as shell commands

**Note:**
- Execution must happen on the name server
- Outputs as-is should be piped to client interface

## System Requirements (40 marks)

### [10] Data Persistence
- All files and associated metadata (access control lists) must be stored persistently
- Data remains intact and accessible after Storage Servers restart or fail

### [5] Access Control
- System must enforce access control policies
- Only authorized users can read or write to files based on permissions set by file owner

### [5] Logging
- NM and SS record every request, acknowledgment, and response
- NM should display (print in terminal) relevant messages indicating status and outcome of each operation
- Each entry should include: timestamps, IP, port, usernames, and other important operation details

### [5] Error Handling
- Clear and informative error messages for all expected/unexpected failures
- Failures include interactions between clients, NM, and SS
- Define comprehensive set of error codes:
  - Unauthorized access
  - File not found
  - Resource contention (e.g., file locked for writing)
  - System failures
- Error codes should be universal throughout the system

### [15] Efficient Search
- Name Server should implement efficient search algorithms
- Quickly locate files based on names or metadata
- Minimize latency in file access operations
- Caching should be implemented for recent searches
- Approach faster than O(N) time complexity expected
- Efficient data structures like Tries, Hashmaps, etc. can be used

## Specifications (10 marks)

### 1. Initialisation

#### Name Server (NM)
- Initialize Naming Server as central coordination point
- IP address and port of NM known publicly (provided to Clients and Storage servers)

#### Storage Server (SS)
- Upon initialization, SS sends vital details to Naming Server:
  - IP address
  - Port for NM connection
  - Port for client connection
  - List of files on it

#### Client
- Clients on initialization ask user for username (for file accesses)
- Pass username along with IP, NM port, and SS port to Name Server

### 2. Name Server

#### Storing Storage Server Data
- NM serves as central repository for information provided by SS upon connection
- Information maintained by NM to direct data requests to appropriate storage server
- Lookups need to be efficient

#### Client Task Feedback
- NM provides timely and relevant feedback to requesting clients
- Important for client response latency in real systems

### 3. Storage Servers

#### Adding New Storage Servers
- New Storage Servers can dynamically add entries to NM at any point during execution
- Initialization process follows same protocol as Specification 1

#### Commands Issued by NM
- NM can issue specific commands to Storage Servers (creating, editing, deleting files)
- Storage Servers execute commands as directed by NM

#### Client Interactions
- Some operations require client to establish direct connection with storage server
- Storage server facilitates these interactions as needed

### 4. Client

#### Username
- Client boots up, asks user for username
- Username used for all file access control operations
- System ensures users can only perform actions on files they have permissions for
- Username relayed to NM, stored along with client information until client disconnects

#### Communication Flow

**Any file access request:**
1. Client sends request to NM
2. NM locates corresponding Storage Server hosting that file using locally stored information

**Operation Categories:**

**Reading, Writing, Streaming:**
- NM identifies correct Storage Server
- Returns IP address and client port for that SS to client
- Client directly communicates with designated SS
- Client continuously receives information packets from SS until "STOP" packet sent or task completion condition met
- "STOP" packet signals operation conclusion

**Listing files, Basic Info, Access Control:**
- NM handles these requests directly
- Processes client's request, retrieves necessary information from local storage
- Sends information back to client
- No Storage Server involved

**Creating and Deleting Files:**
- NM determines appropriate SS
- Forwards request to appropriate SS for execution
- SS processes request and performs action (create/delete file)
- SS sends acknowledgment (ACK) to NM to confirm task completion
- NM conveys information back to client

**Execute:**
- NM requests information from SS
- Main processing and communication handled by NM directly
- NM executes commands contained within file
- NM captures output and relays back to client

## Bonus Functionalities (50 marks - Optional)

### [10] Hierarchical Folder Structure
Allow users to create folders and subfolders to organize files. Users can navigate through hierarchy when performing file operations.

**Commands:**
- `CREATEFOLDER <foldername>` - Creates a new folder
- `MOVE <filename> <foldername>` - Moves the file to specified folder
- `VIEWFOLDER <foldername>` - Lists all files in specified folder

### [15] Checkpoints
Implement checkpointing mechanism allowing users to save file state at specific points in time. Users can revert to these checkpoints.

**Commands:**
- `CHECKPOINT <filename> <checkpoint_tag>` - Creates checkpoint with given tag
- `VIEWCHECKPOINT <filename> <checkpoint_tag>` - Views content of specified checkpoint
- `REVERT <filename> <checkpoint_tag>` - Reverts file to specified checkpoint
- `LISTCHECKPOINTS <filename>` - Lists all checkpoints for file

### [5] Requesting Access
Users can request access to files they do not own. Owner can approve or deny requests. No push-notification mechanism needed - simple storing of requests and owner-side feature to view and approve/reject requests.

### [15] Fault Tolerance
Ensure robustness and reliability through fault tolerance and data replication strategies.

**Replication:**
- Duplicate every file and folder in an SS in another SS
- In event of SS failure, NM retrieves requested data from one of replicated stores
- Every write command duplicated asynchronously across all replicated stores
- NM does not wait for acknowledgment but ensures data redundantly stored

**Failure Detection:**
- NM equipped to detect SS failures
- System responds promptly to disruptions in SS availability

**SS Recovery:**
- When SS comes back online (reconnects to NM), duplicated stores matched back to original SS
- SS synchronized with current state of system
- SS resumes role in data storage and retrieval

### [5] The Unique Factor
What sets your implementation apart from others? Showcase innovation and creativity.

## Examples

Note: Command format is flexible - implement in any format as long as functionality remains the same.

See original document for detailed examples (Example 1-11) showing expected behavior for:
- VIEW (with various flags)
- READ
- CREATE
- WRITE (with various scenarios)
- UNDO
- INFO
- DELETE
- STREAM
- LIST
- Access Control (ADDACCESS, REMACCESS)
- EXEC

## Grading

- User Functionalities: 150 marks
- System Requirements: 40 marks
- Specifications: 10 marks
- Bonus Functionalities: 50 marks (Optional)

**Total: 250 marks**

## Implementation Notes

### Technical Requirements
- Use TCP sockets
- May use any POSIX C library
- Use Wireshark for debugging TCP communications
- Use netcat for client/server stubs for testing
- Decompose problem and write modular code
- Cite resources if taking ideas or code
- Make necessary assumptions

### Command Format
- No specification on exact format of commands
- Commands mentioned in examples are indicative
- Implement in any format as long as functionality remains the same

