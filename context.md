Project: LangOS Document Collaboration MVP

**See `requirements.md` for complete requirements specification.**

Phase 1 (Foundations, Protocol, Lifecycle) — C implementation

Decisions
- Language: C (POSIX)
- Transport: TCP sockets (line-delimited text protocol)
- Protocol: TYPE|ID|USERNAME|ROLE|PAYLOAD; Types: SS_REGISTER, CLIENT_REGISTER, HEARTBEAT, ACK, ERROR
- Logging: structured logs to stdout (JSON lines)
- Persistence (SS): creates storage directory; no file IO yet beyond scaffolding

Processes
- Name Server (NM): known IP/port; accepts registrations from Storage Servers (SS) and Clients; maintains registry; responds to heartbeats
- Storage Server (SS): creates local storage directory; registers to NM; sends periodic heartbeats
- Client: registers username to NM

Phase 1 Exit Criteria
- Client and SS register to NM; NM tracks users and servers
- Logs include timestamp, IP, port, username, op, status, error code
- Restarting SS preserves existing files/metadata directory structure

How to build
make

How to run (Phase 1)

Option 1: Automated script (recommended)
./run_all.sh
- Starts NM and SS in background, runs client registration, then waits
- Logs go to nm.log and ss.log
- Press Ctrl+C to stop all servers
- To stop manually: ./stop_all.sh

Option 2: Manual (3 terminals)
Terminal 1 - Start NM:
   ./bin_nm --host 127.0.0.1 --port 5000

Terminal 2 - Start SS:
   ./bin_ss --nm-host 127.0.0.1 --nm-port 5000 --host 127.0.0.1 --client-port 6001 --storage ./storage_ss1 --username ss1

Terminal 3 - Run Client:
   ./bin_client --nm-host 127.0.0.1 --nm-port 5000 --username alice

Expected behavior
- NM logs SS and Client registrations
- SS creates storage directory if missing, and continues heartbeats
- Client registration visible in NM logs

Self-check (validated)
- Built with `make`
- NM started on 127.0.0.1:5000, SS registered (heartbeats every 5s), Client registered (ACK received)

Next (Phase 2)
- See `PHASE2_PLAN.md` for detailed implementation plan
- Core file operations: VIEW, CREATE, READ, DELETE, INFO, LIST
- Access control system (ACL)
- Efficient file indexing and search (O(1) lookup with caching)
- Client-SS direct communication for READ
- Enhanced logging with error codes
- Data persistence on SS


