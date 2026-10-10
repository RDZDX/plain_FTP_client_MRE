#pragma once
/* Pure C helpers for the FTP protocol (no MRE dependencies, host-testable). */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	int code;    /* reply code of the (possibly multiline) reply being read */
	int active;  /* 1 while inside a multiline reply */
} ftp_reply_t;

void ftp_reply_init(ftp_reply_t *r);

/* Feed one reply line (without CRLF).
   Returns 1 when the reply is complete (r->code is the final status),
   0 when more lines are needed. Lines that are not part of a reply are ignored. */
int ftp_reply_feed(ftp_reply_t *r, const char *line);

/* Parse the "(h1,h2,h3,h4,p1,p2)" part of a 227 reply.
   Returns 1 on success and fills ip[4] and *port, 0 on failure. */
int ftp_parse_pasv(const char *reply, unsigned char ip[4], int *port);

/* Returns 1 if the argument is non-empty and contains no CR/LF/NUL-like control characters. */
int ftp_arg_is_safe(const char *arg);

/* Splits line in place into at most max tokens separated by spaces.
   The last token takes the remainder of the line (trailing spaces trimmed).
   Returns the token count. */
int ftp_split(char *line, char **argv, int max);

/* Replaces control (except '\n') and non-ASCII characters with '?'. Returns len. */
int ftp_sanitize(char *buf, int len);

/* Returns pointer to the last path component (after '/' or '\\' or ':'). */
const char *ftp_basename(const char *path);

#ifdef __cplusplus
}
#endif
