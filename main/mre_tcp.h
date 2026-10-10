#pragma once
#include "vmsys.h"
#include "vmsock.h"

/* One TCP connection. MRE callbacks carry only (handle, event), so every open
   connection is registered in a small table that maps handle -> MreTcp. */
struct MreTcp {
	int handle;
	volatile bool connected;
	volatile bool closed;  /* remote closed, pipe broken or host not found */
	volatile bool failed;  /* connect failed / host not found / broken */
};

#define MRE_TCP_MAX_CONN 2 /* command + PASV data connection */

unsigned int mre_now_ms();

/* Cooperative wait helpers: return after timeout or when thread_next() cycles. */
bool mre_tcp_open(MreTcp *c, const char *host, int port, unsigned int timeout_ms);
/* >0 bytes read, 0 nothing available right now, -1 closed/error and no more data */
int mre_tcp_read(MreTcp *c, void *buf, int len);
bool mre_tcp_write_all(MreTcp *c, const void *buf, int len, unsigned int timeout_ms);
void mre_tcp_close(MreTcp *c);
bool mre_tcp_is_open(const MreTcp *c);
