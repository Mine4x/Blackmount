#ifndef PATHUTIL_H
#define PATHUTIL_H

#define PATH_SIZE 128

/*
 * Look up a key in a NULL-terminated envp array.
 * Returns a pointer to the value string, or NULL if not found.
 */
const char *getenv_local(const char *key, char **envp);

/*
 * Resolve "." and ".." segments in-place.
 * The buffer must be at least PATH_SIZE bytes.
 */
void normalize_path(char *path);

/*
 * Build an absolute path into `out` (PATH_SIZE bytes).
 *
 *  - Absolute inputs (starting with '/') are used as-is.
 *  - "~" or "~/…" is expanded to `home` (falls back to "/" when NULL).
 *  - Relative inputs are appended to `pwd`.
 *
 * normalize_path() is called on the result before returning.
 */
void build_path(char *out, const char *pwd, const char *input,
                const char *home);

#endif /* PATHUTIL_H */