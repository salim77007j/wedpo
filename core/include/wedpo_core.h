/*
 * Wedpo privacy core — C ABI for the Qt shell.
 * Matches core/src/lib.rs `extern "C"` exports.
 */
#ifndef WEDPO_CORE_H
#define WEDPO_CORE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wedpo_engine wedpo_engine;

const char *wedpo_core_version(void);

wedpo_engine *wedpo_engine_new(void);
void wedpo_engine_free(wedpo_engine *e);

/* Load/replace a filter list by name. Returns 1 on success, -1 on error. */
int wedpo_engine_load_list(wedpo_engine *e, const char *name, const char *text);
/* Remove a list. Returns 1 if removed, 0 if absent, -1 on error. */
int wedpo_engine_remove_list(wedpo_engine *e, const char *name);

/*
 * Network check.
 * rtype: main_frame|sub_frame|stylesheet|script|image|object|xmlhttprequest|
 *        ping|media|font|websocket|fetch|other
 * first_party_host: "" if unknown.
 * Returns 0 = allow, 1 = block, 2 = redirect (redirect URL in redirect_out).
 */
int wedpo_engine_check(wedpo_engine *e, const char *url, const char *rtype,
                       const char *first_party_host,
                       char *redirect_out, int cap);

/* Cosmetic CSS rules for url written to out. Returns css length in bytes. */
int wedpo_engine_cosmetic(wedpo_engine *e, const char *url, char *out, int cap);

/* Strip tracking params. Returns 1 if rewritten (out = new URL), 0 unchanged. */
int wedpo_engine_strip_tracking(wedpo_engine *e, const char *url, char *out, int cap);

/* Download risk 0..100; JSON string reasons in reasons_out. */
int wedpo_engine_risk(wedpo_engine *e, const char *url, const char *mime,
                      char *reasons_out, int cap);

/* Loaded lists JSON: {"lists":[{"name","bytes","rules"}...],"count":N} */
int wedpo_engine_lists_info(wedpo_engine *e, char *out, int cap);

#ifdef __cplusplus
}
#endif

#endif /* WEDPO_CORE_H */
