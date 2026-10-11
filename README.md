# plain_FTP_client_MRE - console ftp client

A small **plain FTP client for the MRE platform** (`ftp.vxp`), aimed at Nokia 225 / Series 30+ phones and other phones that can run `.vxp` applications. It has a console-style UI that is driven from the phone keypad.

## Commands

| Command | Description |
|---|---|
| `open <host> [port]` | Connect to a server (default port 21). `host` is a host name or IPv4 address. |
| `user <username> <password>` | Log in (`USER`/`PASS`), then switch to binary mode (`TYPE I`). The password is everything after the user name. |
| `pwd` | Show the remote working directory. |
| `cd <path>` | Change the remote directory. |
| `ls [path]` | List a remote directory (`LIST`). |
| `get <remote> [local]` | Download a file. `local` defaults to the remote file name. |
| `put <local> [remote]` | Upload a file. `remote` defaults to the local file name. |
| `mkdir <path>` | Create a remote directory. |
| `rm <path>` | Delete a remote file (`DELE`). |
| `quit` | Close the connection and exit the application. |
| `help` | Show the command list. |

Notes:

- Only **passive mode** (`PASV`) is supported. The data connection is made to the **same host that was given to `open`**, using only the port from the `PASV` reply. (This avoids the private-address problem with servers behind NAT.)
- Remote paths of `cd`, `ls`, `mkdir`, `rm` may contain spaces. For `get`/`put` the two names are separated by a space, so those names cannot contain spaces.
- Local paths without a drive (`x:`) are placed on drive `e:\` (e.g. `get a.txt` writes `e:\a.txt`). Use `/` or `\` as separator, e.g. `put e:\docs\a.txt`.
- Typing: use the on-screen keypad/T9 input of the console (same as gpt.vxp). Enter is the `\r\n` key, backspace deletes a character.
- Server replies (including multiline replies such as `211-` ... `211 `) are printed as they are received. Control characters in server output are replaced with `?`.

## Plain FTP security limitations

FTP is **not encrypted**. User names, passwords, commands, directory listings and file contents all travel in clear text and can be read or changed by anybody on the network path. There is **no FTPS/TLS and no SFTP** support in this version. Use it only on networks and with accounts you trust, and do not reuse important passwords.

## Design (short)

- `main/mre_tcp.*` - thin wrapper over `vm_tcp_connect/read/write/close`. MRE callbacks only receive `(handle, event)`, so a small handle -> connection table (two entries: command and data) routes events to the right connection. All waits use `thread_next()` with timeouts.
- `main/ftp_proto.*` - pure C protocol helpers (reply/multiline parsing, `PASV` parsing, argument checks, tokenizer, output sanitizing). No MRE dependency, so it can be tested on a PC.
- `main/ftp.*` - the client: control connection, `PASV` data connection, `LIST`/`RETR`/`STOR` streaming through a 2 KB static buffer, local files through `vm_file_*`.
- `main/ftpconsole.*` - command line reader and command dispatcher.
- `main/Console*`, `T2Input*`, `CircleBuf*`, `libco*`, `thread*`, `main.cpp` - console UI and cooperative threads taken from the gpt.vxp project layout.

Timeouts: 20 s for connect, 30 s for a server reply and 30 s of idle time during a data transfer. Command arguments containing control characters (CR/LF) are rejected to prevent command injection. No STL, exceptions, Arduino headers or heap allocation are used by the FTP code.

## Known limitations

- Plain FTP only (no FTPS/TLS/SFTP), passive mode only, no active mode, no `EPSV`/IPv6.
- Binary transfers only. No resume, recursive transfers, directory removal (`RMD`), rename, or wildcard transfers.
- Local file names are ASCII only; remote listings are shown as raw `LIST` output with non-ASCII characters replaced by `?`.
- The data connection end is detected from the MRE `PIPE_CLOSED` event or the idle timeout.
- Not tested on real hardware yet.

