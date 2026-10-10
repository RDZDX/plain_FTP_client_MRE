#include "ftp.h"
#include "ftp_proto.h"
#include "mre_tcp.h"
#include "Console_io.h"
#include "thread.h"
#include "vmio.h"
#include "vmstdlib.h"
#include "string.h"

#define CONNECT_TIMEOUT 20000u
#define REPLY_TIMEOUT   30000u
#define DATA_TIMEOUT    30000u
#define LINE_MAX_LEN    511
#define XFER_BUF_SIZE   2048
#define LOCAL_PATH_MAX  255
#define DEFAULT_DRIVE   "e:\\"

static MreTcp ctl = {-1, false, false, false};
static MreTcp dat = {-1, false, false, false};

static char host_name[128];
static bool logged_in = false;

static char rx[256];
static int rx_pos = 0, rx_len = 0;
static char last_line[LINE_MAX_LEN + 1]; /* last line of the last reply */

static char xfer_buf[XFER_BUF_SIZE];

void fc_init() {
	ctl.handle = dat.handle = -1;
	host_name[0] = '\0';
	logged_in = false;
}

bool fc_is_connected() {
	return mre_tcp_is_open(&ctl) && !ctl.closed;
}

static void show(const char *text) {
	char tmp[LINE_MAX_LEN + 2];
	int n = (int)strlen(text);
	if (n > LINE_MAX_LEN) n = LINE_MAX_LEN;
	memcpy(tmp, text, n);
	tmp[n] = '\0';
	ftp_sanitize(tmp, n);
	console_str_with_length_in(tmp, n);
	console_char_in('\n');
}

static void show_err(const char *text) {
	console_str_in("! ");
	show(text);
}

/* Reads one line from the control connection. Returns false on timeout/close. */
static bool ctl_read_line(char *out, unsigned int timeout_ms) {
	int n = 0;
	unsigned int start = mre_now_ms();

	for (;;) {
		if (rx_pos >= rx_len) {
			int r = mre_tcp_read(&ctl, rx, sizeof(rx));
			if (r < 0)
				return false;
			if (r == 0) {
				if (mre_now_ms() - start > timeout_ms)
					return false;
				thread_next();
				continue;
			}
			rx_pos = 0;
			rx_len = r;
		}
		char c = rx[rx_pos++];
		if (c == '\n') {
			if (n > 0 && out[n - 1] == '\r')
				n--;
			out[n] = '\0';
			return true;
		}
		if (n < LINE_MAX_LEN)
			out[n++] = c;
	}
}

/* Reads a complete (possibly multiline) reply, printing it. Returns the code or -1. */
static int read_reply(unsigned int timeout_ms) {
	ftp_reply_t r;
	char line[LINE_MAX_LEN + 1];

	ftp_reply_init(&r);
	for (;;) {
		if (!ctl_read_line(line, timeout_ms)) {
			show_err(fc_is_connected() ? "Reply timeout" : "Connection closed");
			return -1;
		}
		show(line);
		strncpy(last_line, line, LINE_MAX_LEN);
		last_line[LINE_MAX_LEN] = '\0';
		if (ftp_reply_feed(&r, line))
			return r.code;
	}
}

/* Reads replies until a non 1xx reply arrives. */
static int read_final_reply() {
	int code;
	do {
		code = read_reply(REPLY_TIMEOUT);
	} while (code >= 100 && code < 200);
	return code;
}

static bool send_cmd(const char *verb, const char *arg) {
	char line[LINE_MAX_LEN + 3];
	int vl = (int)strlen(verb), al = arg ? (int)strlen(arg) : 0;

	if (!fc_is_connected()) {
		show_err("Not connected. Use: open <host> [port]");
		return false;
	}
	if (arg && !ftp_arg_is_safe(arg)) {
		show_err("Invalid argument");
		return false;
	}
	if (vl + al + 1 > LINE_MAX_LEN) {
		show_err("Argument too long");
		return false;
	}
	memcpy(line, verb, vl);
	if (arg) {
		line[vl++] = ' ';
		memcpy(line + vl, arg, al);
		vl += al;
	}
	line[vl++] = '\r';
	line[vl++] = '\n';
	if (!mre_tcp_write_all(&ctl, line, vl, REPLY_TIMEOUT)) {
		show_err("Send failed");
		return false;
	}
	return true;
}

static void drop_connection() {
	mre_tcp_close(&dat);
	mre_tcp_close(&ctl);
	rx_pos = rx_len = 0;
	logged_in = false;
}

/* sends a command and waits for its final reply; returns the code or -1 */
static int command(const char *verb, const char *arg) {
	if (!send_cmd(verb, arg))
		return -1;
	int code = read_final_reply();
	if (code < 0)
		drop_connection();
	return code;
}

bool fc_open(const char *host, int port) {
	if (!ftp_arg_is_safe(host) || strlen(host) >= sizeof(host_name) || port <= 0 || port > 65535) {
		show_err("Invalid host or port");
		return false;
	}
	drop_connection();
	cprintf("Connecting to %s:%d...\n", host, port);
	if (!mre_tcp_open(&ctl, host, port, CONNECT_TIMEOUT)) {
		show_err("TCP connect failed");
		return false;
	}
	strcpy(host_name, host);
	rx_pos = rx_len = 0;
	int code = read_final_reply();
	if (code != 220) {
		if (code >= 0)
			show_err("Unexpected greeting");
		drop_connection();
		return false;
	}
	return true;
}

bool fc_login(const char *user, const char *pass) {
	int code = command("USER", user);
	if (code == 331 || code == 332) {
		code = command("PASS", pass);
	}
	if (code != 230) {
		if (code >= 0)
			show_err("Login failed");
		return false;
	}
	logged_in = true;
	command("TYPE", "I");
	return true;
}

bool fc_simple(const char *verb, const char *arg, int expect_class) {
	int code = command(verb, arg);
	if (code / 100 != expect_class) {
		if (code >= 0)
			show_err("Command failed");
		return false;
	}
	return true;
}

static bool require_login() {
	if (!fc_is_connected()) {
		show_err("Not connected. Use: open <host> [port]");
		return false;
	}
	if (!logged_in) {
		show_err("Not logged in. Use: user <name> <pass>");
		return false;
	}
	return true;
}

/* PASV + connect the data socket (to the control host, see README). */
static bool open_data() {
	unsigned char ip[4];
	int port;

	mre_tcp_close(&dat);
	int code = command("PASV", 0);
	if (code != 227) {
		if (code >= 0)
			show_err("PASV failed");
		return false;
	}
	/* only the port is used: the data socket goes to the control host (NAT-safe) */
	if (!ftp_parse_pasv(last_line, ip, &port)) {
		show_err("Bad PASV reply");
		return false;
	}
	if (!mre_tcp_open(&dat, host_name, port, CONNECT_TIMEOUT)) {
		show_err("Data connect failed");
		return false;
	}
	return true;
}

/* Sends the transfer command and expects a 125/150 preliminary reply. */
static bool start_transfer(const char *verb, const char *arg) {
	if (!send_cmd(verb, arg)) {
		mre_tcp_close(&dat);
		return false;
	}
	int code = read_reply(REPLY_TIMEOUT);
	if (code == 125 || code == 150)
		return true;
	if (code < 0)
		drop_connection();
	else
		show_err("Transfer refused");
	mre_tcp_close(&dat);
	return false;
}

static bool finish_transfer(bool ok) {
	mre_tcp_close(&dat);
	int code = read_final_reply();
	if (code < 0) {
		drop_connection();
		return false;
	}
	return ok && code / 100 == 2;
}

/* ---- local files ---- */

static bool to_local_path(const char *name, VMWCHAR *out) {
	char path[LOCAL_PATH_MAX + 1];
	int n = 0, i;
	bool has_drive = (name[0] && name[1] == ':');

	if (!has_drive) {
		strcpy(path, DEFAULT_DRIVE);
		n = (int)strlen(path);
	}
	for (i = 0; name[i]; ++i) {
		if (n >= LOCAL_PATH_MAX)
			return false;
		path[n++] = (name[i] == '/') ? '\\' : name[i];
	}
	if (n == 0)
		return false;
	for (i = 0; i < n; ++i)
		out[i] = (VMWCHAR)(unsigned char)path[i];
	out[n] = 0;
	return true;
}

static VMFILE open_local(const char *name, bool write) {
	VMWCHAR wpath[LOCAL_PATH_MAX + 1];
	if (!to_local_path(name, wpath)) {
		show_err("Bad local path");
		return -1;
	}
	VMFILE f = vm_file_open(wpath, write ? MODE_CREATE_ALWAYS_WRITE : MODE_READ, TRUE);
	if (f < 0)
		show_err("Cannot open local file");
	return f;
}

static const char *safe_basename(const char *path) {
	const char *b = ftp_basename(path);
	if (!*b || !strcmp(b, ".") || !strcmp(b, ".."))
		return 0;
	return b;
}

/* ---- transfers ---- */

bool fc_list(const char *path) {
	bool ok = true;
	unsigned int last = 0;

	if (!require_login() || !open_data() || !start_transfer("LIST", path))
		return false;

	last = mre_now_ms();
	for (;;) {
		int n = mre_tcp_read(&dat, xfer_buf, XFER_BUF_SIZE);
		if (n < 0)
			break;
		if (n == 0) {
			if (mre_now_ms() - last > DATA_TIMEOUT) {
				show_err("Data timeout");
				ok = false;
				break;
			}
			thread_next();
			continue;
		}
		last = mre_now_ms();
		int o = 0;
		for (int i = 0; i < n; ++i)
			if (xfer_buf[i] != '\r')
				xfer_buf[o++] = xfer_buf[i];
		ftp_sanitize(xfer_buf, o);
		console_str_with_length_in(xfer_buf, o);
	}
	return finish_transfer(ok);
}

bool fc_get(const char *remote, const char *local) {
	bool ok = true;
	unsigned int total = 0;

	if (!local) {
		local = safe_basename(remote);
		if (!local) {
			show_err("Specify a local file name");
			return false;
		}
	}
	if (!require_login() || !open_data() || !start_transfer("RETR", remote))
		return false;

	VMFILE f = open_local(local, true);
	if (f < 0) {
		mre_tcp_close(&dat);
		read_final_reply();
		return false;
	}

	unsigned int last = mre_now_ms();
	for (;;) {
		int n = mre_tcp_read(&dat, xfer_buf, XFER_BUF_SIZE);
		if (n < 0)
			break;
		if (n == 0) {
			if (mre_now_ms() - last > DATA_TIMEOUT) {
				show_err("Data timeout");
				ok = false;
				break;
			}
			thread_next();
			continue;
		}
		last = mre_now_ms();
		VMUINT written = 0;
		if (vm_file_write(f, xfer_buf, n, &written) < 0 || written != (VMUINT)n) {
			show_err("Local write failed");
			ok = false;
			break;
		}
		if ((total >> 13) != ((total + n) >> 13))
			console_char_in('.');
		total += n;
	}
	vm_file_close(f);
	if (total >= 8192)
		console_char_in('\n');
	ok = finish_transfer(ok);
	cprintf("%d bytes -> %s\n", (int)total, local);
	return ok;
}

bool fc_put(const char *local, const char *remote) {
	bool ok = true;
	unsigned int total = 0;

	if (!remote) {
		remote = safe_basename(local);
		if (!remote) {
			show_err("Specify a remote file name");
			return false;
		}
	}
	if (!require_login())
		return false;
	VMFILE f = open_local(local, false);
	if (f < 0)
		return false;
	if (!open_data() || !start_transfer("STOR", remote)) {
		vm_file_close(f);
		return false;
	}

	for (;;) {
		VMUINT got = 0;
		if (vm_file_read(f, xfer_buf, XFER_BUF_SIZE, &got) < 0) {
			show_err("Local read failed");
			ok = false;
			break;
		}
		if (got == 0)
			break;
		if (!mre_tcp_write_all(&dat, xfer_buf, (int)got, DATA_TIMEOUT)) {
			show_err("Data send failed");
			ok = false;
			break;
		}
		if ((total >> 13) != ((total + got) >> 13))
			console_char_in('.');
		total += got;
	}
	vm_file_close(f);
	if (total >= 8192)
		console_char_in('\n');
	ok = finish_transfer(ok);
	cprintf("%d bytes sent\n", (int)total);
	return ok;
}

void fc_quit() {
	if (fc_is_connected()) {
		send_cmd("QUIT", 0);
		read_reply(5000);
	}
	drop_connection();
}
