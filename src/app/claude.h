#ifndef CLAUDE_H
#define CLAUDE_H

/*
 * Talking to Claude through Cloudflare AI Gateway: keeps one verified TLS
 * connection open across questions, reconnecting when it has dropped.
 * Everything here blocks; progress is reported through the status callback.
 */
#include <stddef.h>

typedef void (*claude_status_fn)(const char *status);

/* Once at startup: serial port, and the first key pair (~8 s). */
void claude_init(claude_status_fn status);

/*
 * Send the whole conversation (from the transcript) and get Claude's reply,
 * in the Atari character set. Returns NULL on success with *reply set
 * (malloc'd, caller frees), or an error message for the user.
 */
const char *claude_ask(char **reply, size_t *reply_len);

/* Hang up, e.g. on quit. */
void claude_disconnect(void);

#endif
