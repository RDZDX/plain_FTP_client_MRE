#pragma once

/* Minimal passive-mode plain FTP client on top of MRE TCP.
   All functions print their results to the console and return true on success. */

void fc_init();
bool fc_is_connected();
bool fc_open(const char *host, int port);
bool fc_login(const char *user, const char *pass);
bool fc_simple(const char *verb, const char *arg, int expect_class); /* e.g. PWD, CWD, MKD, DELE */
bool fc_list(const char *path);
bool fc_get(const char *remote, const char *local);
bool fc_put(const char *local, const char *remote);
void fc_quit();
