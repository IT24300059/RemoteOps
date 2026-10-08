# RemoteOps Design Diary

## Development Summary

The RemoteOps project was developed as a personalised C client-server application for IE3090 Network Programming.

### Stage 1 — Personalisation and Project Structure
The implementation was personalised using registration number IT24300059. The required values were derived for the TCP port, session ID, authentication token, source-file names, log file and storage path.

### Stage 2 — Agent and Controller
The Agent was developed as the TCP server and the Controller as the administrator client. POSIX threads were used so that the Agent can handle multiple Controller connections concurrently.

### Stage 3 — Authentication and Commands
The authentication mechanism was implemented using the personalised token `OPS-0059`. After authentication, the Controller can request SYSINFO and LISTPROC information and use the restricted EXEC command.

### Stage 4 — File Transfer
PUT and GET were implemented for file upload and download. `testfile.txt` was used during testing, and the successful transfer responses were recorded with the personalised session ID.

### Stage 5 — UDP Monitoring and Logging
A UDP monitoring channel was added for periodic system statistics. MONITOR START begins the stream and MONITOR STOP terminates it. The Agent also records connections, commands and file-transfer/monitoring events in the personalised log file.

### Stage 6 — Testing and Fixes
The Agent and Controller were compiled with GCC and pthread support. Runtime testing was carried out for authentication, system information, process listing, restricted commands, file transfer, UDP monitoring and logging. Compiler/runtime issues encountered during development were corrected before the final tests documented in the Implementation Report.

### Key Design Decisions
- TCP was selected for reliable command and file-transfer communication.
- UDP was used for periodic monitoring statistics.
- POSIX threads were used for concurrent client handling.
- EXEC was restricted to the five commands required by the assignment.
- Personalised values were kept consistent across the source files, log and storage path.
