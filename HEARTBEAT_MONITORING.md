# Heartbeat Monitoring and Failure Detection

## Overview

Implemented a robust heartbeat monitoring system for detecting Storage Server (SS) failures in the Name Server (NM).

## Implementation Details

### Components

#### 1. Heartbeat Monitor Module (`src/nm/heartbeat_monitor.c/h`)

**Data Structures:**
```c
typedef struct HeartbeatStatus {
    char ss_username[64];      // SS identifier
    time_t last_heartbeat;     // Last heartbeat timestamp
    time_t first_seen;         // Registration time
    SSStatus status;           // ALIVE, FAILED, or UNKNOWN
    int missed_count;          // Consecutive missed heartbeats
    struct HeartbeatStatus *next;
} HeartbeatStatus;
```

**Key Functions:**
- `heartbeat_monitor_init()` - Initialize monitoring system
- `heartbeat_monitor_register_ss()` - Register new SS for monitoring
- `heartbeat_monitor_update()` - Update timestamp when heartbeat received
- `heartbeat_monitor_start()` - Start background monitoring thread
- `heartbeat_monitor_stop()` - Stop monitoring thread
- `heartbeat_monitor_is_alive()` - Check if SS is alive
- `heartbeat_monitor_get_status()` - Get current SS status
- `heartbeat_monitor_set_failure_callback()` - Register callback for failures

#### 2. Monitoring Thread

**Behavior:**
- Runs every 5 seconds (`HEARTBEAT_CHECK_INTERVAL`)
- Checks all registered SS for timeout
- Timeout threshold: 15 seconds (`HEARTBEAT_TIMEOUT_SEC`)
- Marks SS as FAILED after 3 missed heartbeats (`MAX_MISSED_HEARTBEATS`)
- Calls registered failure callback when SS fails
- Automatically marks recovered SS as ALIVE when heartbeat resumes

#### 3. Integration with NM (`src/nm/main.c`)

**Changes:**
1. **On SS_REGISTER**: Register SS with heartbeat monitor
2. **On HEARTBEAT**: Update timestamp via `heartbeat_monitor_update()`
3. **On Startup**: Initialize and start monitoring thread
4. **On Shutdown**: Stop monitoring thread gracefully

### Configuration

| Parameter | Value | Description |
|-----------|-------|-------------|
| `HEARTBEAT_TIMEOUT_SEC` | 15 | Seconds without heartbeat before failure |
| `HEARTBEAT_CHECK_INTERVAL` | 5 | How often to check (seconds) |
| `MAX_MISSED_HEARTBEATS` | 3 | Missed heartbeats before marking failed |

### Logging

**Events Logged:**
- `heartbeat_monitor_init` - System initialization
- `heartbeat_monitor_register` - SS registration/re-registration
- `heartbeat_monitor_update` - SS recovery after failure
- `heartbeat_monitor_check` - Missed heartbeat warnings (WARNING level)
- `heartbeat_monitor_failure` - SS marked as FAILED (ERROR level)
- `heartbeat_monitor_thread` - Thread start/stop

**Example Log Output:**
```json
{"ts":"2025-11-21T10:30:00","level":"INFO","event":"heartbeat_monitor_register","msg":"SS registered for monitoring: ss1"}
{"ts":"2025-11-21T10:30:15","level":"WARNING","event":"heartbeat_monitor_check","msg":"SS ss1 missed heartbeat (count=1, last_seen=16 seconds ago)"}
{"ts":"2025-11-21T10:30:20","level":"ERROR","event":"heartbeat_monitor_failure","msg":"SS ss1 marked as FAILED (timeout=21 seconds, missed=3 heartbeats)"}
```

## Testing

### Manual Test
```bash
./test_heartbeat.sh
```

This script:
1. Starts NM
2. Starts two Storage Servers (ss1, ss2)
3. Waits for heartbeats to stabilize
4. Kills ss1 to simulate failure
5. Waits for NM to detect failure (~15-20 seconds)
6. Verifies ss2 continues functioning

### Expected Behavior
- SS sends heartbeat every 5 seconds
- NM checks for timeouts every 5 seconds
- After 15 seconds without heartbeat, SS is marked FAILED
- When SS reconnects, it's automatically marked ALIVE

## Thread Safety

- All operations use `pthread_mutex_t` for thread-safe access
- Monitoring thread runs independently
- Callbacks executed outside of locks to prevent deadlock
- List traversal handles dynamic modifications

## Future Enhancements (for Replication)

The heartbeat monitor provides a foundation for:
1. **Failure Callbacks**: Trigger failover when SS fails
2. **Recovery Detection**: Detect when failed SS reconnects
3. **Health Checks**: Query SS status before routing requests
4. **Metrics**: Track uptime, downtime, recovery time

## API Usage Example

```c
// Initialize at NM startup
heartbeat_monitor_init();
heartbeat_monitor_start();

// Register callback for failures
void on_ss_failure(const char *ss_username) {
    log_error("failover", "SS %s failed, initiating failover", ss_username);
    // Trigger replication failover here
}
heartbeat_monitor_set_failure_callback(on_ss_failure);

// On SS_REGISTER message
heartbeat_monitor_register_ss(ss_username);

// On HEARTBEAT message
heartbeat_monitor_update(ss_username);

// Check if SS is alive before routing request
if (heartbeat_monitor_is_alive(ss_username)) {
    // Route request to SS
} else {
    // Use replica or return error
}

// Shutdown
heartbeat_monitor_stop();
```

## Notes

- **Non-blocking**: Monitoring thread doesn't block main NM operations
- **Configurable**: Constants can be tuned for different failure detection speeds
- **Extensible**: Easy to add metrics, dashboards, alerts
- **Production-ready**: Comprehensive logging and error handling
