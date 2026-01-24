/*
 * ViperDOS libc - nl_types.h
 * Native language support message catalog functions
 */

#ifndef _NL_TYPES_H
#define _NL_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Message catalog descriptor
 */
typedef void *nl_catd;

/*
 * Message item type
 */
typedef int nl_item;

/*
 * Invalid catalog descriptor
 */
#define NL_CAT_LOCALE ((nl_catd) - 1)

/*
 * Set number used for default set
 */
#define NL_SETD 1

/*
 * catopen flags
 */
#define NL_CAT_LOCALE_FLAG 1 /* Use LANG/LC_MESSAGES for locale */

/*
 * catopen - Open a message catalog
 *
 * Opens a message catalog for reading. The catalog name may be an
 * absolute path or a relative name that is searched for in standard
 * locations.
 *
 * @name: Name of the message catalog
 * @flag: Opening flags (NL_CAT_LOCALE_FLAG or 0)
 *
 * Returns catalog descriptor on success, (nl_catd)-1 on error.
 */
nl_catd catopen(const char *name, int flag);

/*
 * catgets - Read a message from a catalog
 *
 * Retrieves a message from an open message catalog.
 *
 * @catd: Catalog descriptor from catopen()
 * @set_id: Message set number
 * @msg_id: Message number within the set
 * @s: Default string to return if message not found
 *
 * Returns pointer to message string, or 's' if not found.
 * The returned pointer should not be modified or freed.
 */
char *catgets(nl_catd catd, int set_id, int msg_id, const char *s);

/*
 * catclose - Close a message catalog
 *
 * Closes a message catalog that was opened with catopen().
 *
 * @catd: Catalog descriptor to close
 *
 * Returns 0 on success, -1 on error.
 */
int catclose(nl_catd catd);

#ifdef __cplusplus
}
#endif

#endif /* _NL_TYPES_H */
