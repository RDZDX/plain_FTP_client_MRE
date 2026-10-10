#include "mre_tcp.h"
#include "thread.h"

static MreTcp *table[MRE_TCP_MAX_CONN];

unsigned int mre_now_ms() {
	return (unsigned int)vm_get_tick_count();
}

static void tcp_callback(VMINT handle, VMINT event) {
	for (int i = 0; i < MRE_TCP_MAX_CONN; ++i) {
		MreTcp *c = table[i];
		if (!c || c->handle != handle)
			continue;
		switch (event) {
		case VM_TCP_EVT_CONNECTED:
			c->connected = true;
			break;
		case VM_TCP_EVT_HOST_NOT_FOUND:
			c->failed = true;
			c->closed = true;
			break;
		case VM_TCP_EVT_PIPE_BROKEN:
		case VM_TCP_EVT_PIPE_CLOSED:
			c->closed = true;
			if (!c->connected)
				c->failed = true;
			break;
		}
		return;
	}
}

static int slot_of(const MreTcp *c) {
	for (int i = 0; i < MRE_TCP_MAX_CONN; ++i)
		if (table[i] == c)
			return i;
	return -1;
}

bool mre_tcp_open(MreTcp *c, const char *host, int port, unsigned int timeout_ms) {
	int slot = slot_of(c);
	if (slot < 0)
		slot = slot_of(0);
	if (slot < 0)
		return false;

	c->handle = -1;
	c->connected = c->closed = c->failed = false;
	table[slot] = c;

	c->handle = vm_tcp_connect(host, port, VM_APN_USER_DEFINE, tcp_callback);
	if (c->handle < 0) {
		table[slot] = 0;
		c->handle = -1;
		return false;
	}

	unsigned int start = mre_now_ms();
	while (!c->connected && !c->failed) {
		if (mre_now_ms() - start > timeout_ms)
			break;
		thread_next();
	}
	if (!c->connected) {
		mre_tcp_close(c);
		return false;
	}
	return true;
}

int mre_tcp_read(MreTcp *c, void *buf, int len) {
	if (c->handle < 0)
		return -1;
	int n = vm_tcp_read(c->handle, buf, len);
	if (n > 0)
		return n;
	return c->closed ? -1 : 0;
}

bool mre_tcp_write_all(MreTcp *c, const void *buf, int len, unsigned int timeout_ms) {
	const char *p = (const char *)buf;
	unsigned int start = mre_now_ms();

	while (len > 0) {
		if (c->handle < 0 || c->closed)
			return false;
		int n = vm_tcp_write(c->handle, (void *)p, len);
		if (n > 0) {
			p += n;
			len -= n;
			start = mre_now_ms();
			continue;
		}
		if (mre_now_ms() - start > timeout_ms)
			return false;
		thread_next();
	}
	return true;
}

void mre_tcp_close(MreTcp *c) {
	int slot = slot_of(c);
	if (c->handle >= 0)
		vm_tcp_close(c->handle);
	c->handle = -1;
	c->connected = false;
	if (slot >= 0)
		table[slot] = 0;
}

bool mre_tcp_is_open(const MreTcp *c) {
	return c->handle >= 0 && c->connected;
}
