#include "ftpconsole.h"
#include "Console_io.h"
#include "CircleBuf.h"
#include "thread.h"
#include "ftp.h"
#include "ftp_proto.h"
#include "string.h"
#include "vmsys.h"

extern CircleBuf circlebuf;

#define LINE_BUF_SIZE 256

static const char help_text[] =
	"Commands:\n"
	" open <host> [port]\n"
	" user <name> <pass>\n"
	" pwd\n"
	" cd <path>\n"
	" ls [path]\n"
	" get <remote> [local]\n"
	" put <local> [remote]\n"
	" mkdir <path>\n"
	" rm <path>\n"
	" quit\n"
	" help\n"
	"Local files default to e:\\\n";

static void read_line(char *buf, int size) {
	int n = 0;
	for (;;) {
		char c = circlebuf.pop_blok();
		if (c == '\177') {
			if (n > 0) {
				console_str_in("\b \b");
				n--;
			}
		} else if (c == '\r') {
			if (circlebuf.get_data_size() && circlebuf.view() == '\n')
				circlebuf.pop();
			buf[n] = '\0';
			return;
		} else if (c == '\n') {
			buf[n] = '\0';
			return;
		} else if (c >= 0x20 && c < 0x7f && n < size - 1) {
			buf[n++] = c;
			console_char_in(c);
		}
	}
}

static int parse_port(const char *s) {
	int v = 0;
	if (!*s)
		return -1;
	for (; *s; ++s) {
		if (*s < '0' || *s > '9')
			return -1;
		v = v * 10 + (*s - '0');
		if (v > 65535)
			return -1;
	}
	return v > 0 ? v : -1;
}

/* returns false when the app should exit */
static bool execute(char *line) {
	char *argv[2];
	char *sub[2];
	int argc = ftp_split(line, argv, 2); /* argv[1] is the rest of the line */
	int n;

	if (argc == 0)
		return true;

	const char *cmd = argv[0];
	char *rest = argc > 1 ? argv[1] : 0;
	n = rest ? ftp_split(rest, sub, 2) : 0;

	if (!strcmp(cmd, "help")) {
		console_str_in(help_text);
	} else if (!strcmp(cmd, "quit")) {
		return false;
	} else if (!strcmp(cmd, "open")) {
		int port = 21;
		if (n < 1) {
			cprintf("Usage: open <host> [port]\n");
		} else {
			if (n > 1)
				port = parse_port(sub[1]);
			if (port < 0)
				cprintf("Invalid port\n");
			else
				fc_open(sub[0], port);
		}
	} else if (!strcmp(cmd, "user")) {
		if (n < 2)
			cprintf("Usage: user <name> <pass>\n");
		else
			fc_login(sub[0], sub[1]);
	} else if (!strcmp(cmd, "pwd")) {
		fc_simple("PWD", 0, 2);
	} else if (!strcmp(cmd, "cd")) {
		if (!rest) cprintf("Usage: cd <path>\n"); else fc_simple("CWD", rest, 2);
	} else if (!strcmp(cmd, "ls")) {
		fc_list(rest);
	} else if (!strcmp(cmd, "get")) {
		if (n < 1) cprintf("Usage: get <remote> [local]\n"); else fc_get(sub[0], n > 1 ? sub[1] : 0);
	} else if (!strcmp(cmd, "put")) {
		if (n < 1) cprintf("Usage: put <local> [remote]\n"); else fc_put(sub[0], n > 1 ? sub[1] : 0);
	} else if (!strcmp(cmd, "mkdir")) {
		if (!rest) cprintf("Usage: mkdir <path>\n"); else fc_simple("MKD", rest, 2);
	} else if (!strcmp(cmd, "rm")) {
		if (!rest) cprintf("Usage: rm <path>\n"); else fc_simple("DELE", rest, 2);
	} else {
		cprintf("Unknown command. Type help\n");
	}
	return true;
}

void main_ftp() {
	static char line[LINE_BUF_SIZE];

	fc_init();
	cprintf("FTP client for MRE\n");
	cprintf("Plain FTP, passive mode only\n");
	cprintf("Type help for commands\n");

	for (;;) {
		cprintf("ftp> ");
		read_line(line, LINE_BUF_SIZE);
		cprintf("\n");
		if (!execute(line))
			break;
	}

	fc_quit();
	cprintf("Bye\n");
	vm_exit_app();
	thread_end(); //return is forbidden
}
