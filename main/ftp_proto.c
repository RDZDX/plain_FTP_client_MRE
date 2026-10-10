#include "ftp_proto.h"

static int is_digit(char c) { return c >= '0' && c <= '9'; }

static int parse_code(const char *line, int *code, char *sep) {
	if (!line || !is_digit(line[0]) || !is_digit(line[1]) || !is_digit(line[2]))
		return 0;
	*code = (line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0');
	*sep = line[3];
	if (*sep == '\0') { *sep = ' '; return 1; }
	return (*sep == ' ' || *sep == '-');
}

void ftp_reply_init(ftp_reply_t *r) {
	r->code = 0;
	r->active = 0;
}

int ftp_reply_feed(ftp_reply_t *r, const char *line) {
	int code;
	char sep;
	int ok = parse_code(line, &code, &sep);

	if (!r->active) {
		if (!ok)
			return 0;
		r->code = code;
		if (sep == '-') {
			r->active = 1;
			return 0;
		}
		return 1;
	}
	/* inside a multiline reply: only "<code><space>" ends it */
	if (ok && code == r->code && sep == ' ') {
		r->active = 0;
		return 1;
	}
	return 0;
}

int ftp_parse_pasv(const char *reply, unsigned char ip[4], int *port) {
	int v[6], n = 0;
	const char *p = reply;

	if (!reply)
		return 0;
	while (*p && *p != '(')
		p++;
	if (*p == '(') {
		p++;
	} else {
		/* no parentheses: skip the 3 digit code and look for the first digit */
		p = reply;
		while (is_digit(*p)) p++;
		while (*p && !is_digit(*p)) p++;
	}
	while (n < 6) {
		int val = 0, digits = 0;
		while (is_digit(*p)) {
			val = val * 10 + (*p - '0');
			if (val > 255)
				return 0;
			digits++;
			p++;
		}
		if (!digits)
			return 0;
		v[n++] = val;
		if (n < 6) {
			if (*p != ',')
				return 0;
			p++;
		}
	}
	for (n = 0; n < 4; n++)
		ip[n] = (unsigned char)v[n];
	*port = v[4] * 256 + v[5];
	return *port > 0;
}

int ftp_arg_is_safe(const char *arg) {
	if (!arg || !*arg)
		return 0;
	for (; *arg; arg++)
		if ((unsigned char)*arg < 0x20 || *arg == 0x7f)
			return 0;
	return 1;
}

int ftp_split(char *line, char **argv, int max) {
	int n = 0;
	char *p = line;

	while (n < max) {
		char *end;
		while (*p == ' ' || *p == '\t') p++;
		if (!*p)
			break;
		argv[n++] = p;
		if (n == max) {
			end = p;
			while (*end) end++;
			while (end > p && (end[-1] == ' ' || end[-1] == '\t')) end--;
			*end = '\0';
			break;
		}
		while (*p && *p != ' ' && *p != '\t') p++;
		if (*p)
			*p++ = '\0';
	}
	return n;
}

int ftp_sanitize(char *buf, int len) {
	int i;
	for (i = 0; i < len; i++) {
		unsigned char c = (unsigned char)buf[i];
		if (c == '\n' || (c >= 0x20 && c < 0x7f))
			continue;
		buf[i] = '?';
	}
	return len;
}

const char *ftp_basename(const char *path) {
	const char *base = path;
	for (; *path; path++)
		if (*path == '/' || *path == '\\' || *path == ':')
			base = path + 1;
	return base;
}
