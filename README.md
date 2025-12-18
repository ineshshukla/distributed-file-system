# Distributed File System

This repository contains a **C-based Distributed File System (DFS)** that implements core concepts used in real-world distributed storage systems such as centralized metadata management, decentralized data storage, replication, heartbeat-based failure detection, and client–server coordination.

The goal of this project is to build a distributed system **from scratch**, focusing on low-level systems programming, networking, concurrency, and fault tolerance, without relying on existing DFS frameworks.

---

## Overview

The system follows a classic distributed file system architecture consisting of:

- A **Name Server (NM)** responsible for metadata and coordination
- Multiple **Storage Servers (SS)** responsible for storing file data
- A **Client** that interacts with the system via an interactive command-line interface

The Name Server manages the global namespace and keeps track of live storage servers, while clients communicate directly with storage servers for actual data transfer after resolving metadata through the Name Server.

---

## Key Features

### Centralized Metadata Management
- The Name Server maintains the global file namespace
- Tracks which storage servers hold replicas of each file
- Resolves client requests by returning storage server locations
- Acts as the coordination point for replication and fault handling

### Distributed Storage Servers
- Multiple independent storage servers store files locally
- Each server registers itself with the Name Server at startup
- Handles client read and write requests directly
- Designed to simulate real distributed storage nodes

### Replication and Fault Tolerance
- Supports write replication across multiple storage servers
- Ensures data availability even if a storage server fails
- Metadata is updated dynamically when failures are detected
- Replication behavior is documented and configurable

### Heartbeat-Based Failure Detection
- Storage servers periodically send heartbeats to the Name Server
- The Name Server detects failed nodes using heartbeat timeouts
- Enables safe redirection of clients away from failed servers

### Interactive Client
- Command-line client for interacting with the distributed file system
- Client first contacts the Name Server for metadata
- Data transfer happens directly between the client and storage servers
- Mimics the interaction model of production-grade DFS clients

### Low-Level Systems Implementation
- Implemented entirely in C
- Uses POSIX sockets for networking
- Multi-threaded design using POSIX threads
- Explicit message formats and protocol handling
- No external distributed systems libraries or frameworks

---

## Architecture

```
            +---------------+        +---------------+
            | StorageServer |        | StorageServer |
            |     (SS1)     |        |     (SS2)     |
            +-------+-------+        +-------+-------+
                    \                     /
                     \  Registration /   /
                      \  Heartbeats     /
                       v               v
                    +----------------------+
                    |     Name Server      |
                    | (Metadata, Control,  |
                    |  Coordination)       |
                    +----------+-----------+
                               ^
                               |
                  Client Requests / Metadata Resolution
                               |
                    +----------------------+
                    |        Client        |
                    +----------------------+

        -------- Direct Client ↔ Storage Server Data Transfer --------
        (Performed only after the Name Server provides SS details)

```

---

## Repository Structure

```

.
├── src/                 # Source code for Name Server, Storage Server, and Client
├── include/             # Header files
├── scripts/             # Helper scripts to start/stop components
├── docs/                # Design and implementation documentation
│   ├── requirements.md
│   ├── design_choices.md
│   ├── HEARTBEAT_MONITORING.md
│   ├── WRITE_REPLICATION.md
│   └── REPLICATION_WORKING.md
├── Makefile
└── README.md

````

---

## Setup and Build Instructions

### Prerequisites
- Linux-based environment (tested on Linux / WSL)
- GCC compiler
- Make

### Build
```bash
make clean
make
````

This will generate the following binaries:

* `bin_nm` – Name Server
* `bin_ss` – Storage Server
* `bin_client` – Client

---

## Running the System

Each component should be run in a separate terminal.

### Start the Name Server

```bash
./bin_nm --host 127.0.0.1 --port 5000
```

### Start a Storage Server

```bash
./bin_ss --nm-host 127.0.0.1 --nm-port 5000 --port 6000 --username ss1
```

Multiple storage servers can be started by changing the port and username.

### Start the Client

```bash
./bin_client --nm-host 127.0.0.1 --nm-port 5000 --username client1
```

Once started, the client provides an interactive interface for file system operations.

