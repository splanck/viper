//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/getopt.c
// Purpose: Command-line option parsing for ViperDOS libc.
// Key invariants: POSIX getopt semantics; global state variables.
// Ownership/Lifetime: Library; global state persists across calls.
// Links: user/libc/include/unistd.h
//
//===----------------------------------------------------------------------===//

/**
 * @file getopt.c
 * @brief Command-line option parsing for ViperDOS libc.
 *
 * @details
 * This file implements POSIX/GNU-style command-line option parsing:
 *
 * - getopt: Parse short options (-a, -b value)
 * - getopt_long: Parse long options (--help, --file=value)
 * - getopt_long_only: Long options with single dash (-help)
 *
 * Global variables:
 * - optarg: Points to option argument (if any)
 * - optind: Index of next argv element to process
 * - opterr: Print errors to stderr (default 1)
 * - optopt: Unknown option character
 */

#include "../include/stdio.h"
#include "../include/string.h"
#include "../include/unistd.h"

/* Global getopt state */
char *optarg = NULL;
int optind = 1;
int opterr = 1;
int optopt = '?';

/* Internal state */
static char *nextchar = NULL;

/**
 * @brief Parse short command-line options.
 *
 * @details
 * Parses command-line arguments looking for options specified in optstring.
 * Each option is a single character optionally followed by ':' (requires
 * argument) or '::' (optional argument).
 *
 * Example optstring: "ab:c::" means:
 * - 'a': Simple option, no argument
 * - 'b': Requires an argument (-b value or -bvalue)
 * - 'c': Has an optional argument (-cvalue only)
 *
 * On each call, getopt() returns the next option character. When all options
 * are processed, it returns -1. Non-option arguments can be processed by
 * examining argv[optind] after getopt() returns -1.
 *
 * Special behavior:
 * - "--" stops option processing (everything after is non-option)
 * - Unknown options return '?' and set optopt to the character
 * - Missing required arguments return '?' (or ':' if optstring starts with ':')
 *
 * Global variables modified:
 * - optarg: Points to option's argument (if any)
 * - optind: Index of next argument to process
 * - optopt: The option character that caused an error
 *
 * @param argc Argument count (from main).
 * @param argv Argument vector (from main).
 * @param optstring String describing valid options.
 * @return Option character on success, '?' on error, -1 when done.
 *
 * @see getopt_long, getopt_long_only
 */
int getopt(int argc, char *const argv[], const char *optstring) {
    if (optstring == NULL || argc <= 0)
        return -1;

    optarg = NULL;

    /* Check if we need to get next argument */
    if (nextchar == NULL || *nextchar == '\0') {
        /* Check if we're done */
        if (optind >= argc)
            return -1;

        /* Get next argument */
        char *arg = argv[optind];

        /* Check for option */
        if (arg[0] != '-' || arg[1] == '\0') {
            /* Not an option */
            return -1;
        }

        /* Check for "--" */
        if (arg[1] == '-' && arg[2] == '\0') {
            optind++;
            return -1;
        }

        nextchar = arg + 1;
        optind++;
    }

    /* Get current option character */
    int c = *nextchar++;
    optopt = c;

    /* Look for option in optstring */
    const char *opt = strchr(optstring, c);

    if (opt == NULL || c == ':') {
        /* Unknown option */
        if (opterr && optstring[0] != ':') {
            fprintf(stderr, "%s: invalid option -- '%c'\n", argv[0], c);
        }
        return '?';
    }

    /* Check if option requires argument */
    if (opt[1] == ':') {
        if (*nextchar != '\0') {
            /* Argument follows option immediately */
            optarg = nextchar;
            nextchar = NULL;
        } else if (opt[2] == ':') {
            /* Optional argument not present */
            optarg = NULL;
        } else if (optind < argc) {
            /* Argument is next argv element */
            optarg = argv[optind++];
        } else {
            /* Missing required argument */
            if (opterr && optstring[0] != ':') {
                fprintf(stderr, "%s: option requires an argument -- '%c'\n", argv[0], c);
            }
            return optstring[0] == ':' ? ':' : '?';
        }
    }

    return c;
}

/**
 * @brief Parse long and short command-line options.
 *
 * @details
 * Extended version of getopt() that also handles long options in the form
 * "--option" or "--option=value". Long options are defined by an array of
 * struct option, terminated by a zero-filled entry.
 *
 * The struct option fields are:
 * - name: Long option name (without the leading --)
 * - has_arg: no_argument (0), required_argument (1), or optional_argument (2)
 * - flag: If non-NULL, set *flag to val and return 0; else return val
 * - val: Value to return or store in *flag
 *
 * Example:
 * @code
 * struct option long_options[] = {
 *     {"help",    no_argument,       NULL, 'h'},
 *     {"output",  required_argument, NULL, 'o'},
 *     {"verbose", no_argument,       &verbose_flag, 1},
 *     {0, 0, 0, 0}
 * };
 * @endcode
 *
 * Short options are still processed according to optstring. When a long
 * option is matched, its index in longopts is stored in *longindex (if
 * longindex is not NULL).
 *
 * @param argc Argument count (from main).
 * @param argv Argument vector (from main).
 * @param optstring String describing valid short options.
 * @param longopts Array of struct option describing long options.
 * @param longindex If non-NULL, receives index of matched long option.
 * @return Option character/value on success, '?' on error, -1 when done.
 *
 * @see getopt, getopt_long_only, struct option
 */
int getopt_long(int argc,
                char *const argv[],
                const char *optstring,
                const struct option *longopts,
                int *longindex) {
    if (optstring == NULL || argc <= 0)
        return -1;

    optarg = NULL;

    /* Check if processing a short option from previous call */
    if (nextchar != NULL && *nextchar != '\0') {
        /* Continue processing short option cluster */
        return getopt(argc, argv, optstring);
    }

    nextchar = NULL;

    /* Check if we're done */
    if (optind >= argc)
        return -1;

    char *arg = argv[optind];

    /* Check for non-option */
    if (arg[0] != '-' || arg[1] == '\0')
        return -1;

    /* Check for "--" */
    if (arg[1] == '-' && arg[2] == '\0') {
        optind++;
        return -1;
    }

    /* Check for long option */
    if (arg[1] == '-') {
        char *name_end = strchr(arg + 2, '=');
        size_t name_len = name_end ? (size_t)(name_end - (arg + 2)) : strlen(arg + 2);

        /* Search for matching long option */
        for (int i = 0; longopts && longopts[i].name; i++) {
            if (strncmp(arg + 2, longopts[i].name, name_len) == 0 &&
                longopts[i].name[name_len] == '\0') {
                /* Found match */
                optind++;

                if (longindex)
                    *longindex = i;

                /* Handle argument */
                if (longopts[i].has_arg != no_argument) {
                    if (name_end) {
                        /* Argument after '=' */
                        optarg = name_end + 1;
                    } else if (longopts[i].has_arg == required_argument) {
                        if (optind < argc) {
                            optarg = argv[optind++];
                        } else {
                            if (opterr) {
                                fprintf(stderr,
                                        "%s: option '--%s' requires an argument\n",
                                        argv[0],
                                        longopts[i].name);
                            }
                            return optstring[0] == ':' ? ':' : '?';
                        }
                    }
                } else if (name_end) {
                    /* Argument provided but not expected */
                    if (opterr) {
                        fprintf(stderr,
                                "%s: option '--%s' doesn't allow an argument\n",
                                argv[0],
                                longopts[i].name);
                    }
                    return '?';
                }

                /* Return value or set flag */
                if (longopts[i].flag) {
                    *longopts[i].flag = longopts[i].val;
                    return 0;
                }
                return longopts[i].val;
            }
        }

        /* No matching long option */
        if (opterr) {
            if (name_end)
                fprintf(
                    stderr, "%s: unrecognized option '--%.*s'\n", argv[0], (int)name_len, arg + 2);
            else
                fprintf(stderr, "%s: unrecognized option '%s'\n", argv[0], arg);
        }
        optind++;
        return '?';
    }

    /* Short option */
    nextchar = arg + 1;
    optind++;
    return getopt(argc, argv, optstring);
}

/**
 * @brief Parse long options with single dash.
 *
 * @details
 * Like getopt_long(), but long options can be specified with a single
 * dash (e.g., "-help" instead of "--help"). This provides compatibility
 * with programs that use single-dash long options.
 *
 * The function first tries to match the argument as a long option. If
 * no long option matches and the argument starts with a single dash,
 * it falls back to processing as short options.
 *
 * Example: With "-verbose" and longopts containing "verbose":
 * - getopt_long() would process -v, -e, -r, -b, -o, -s, -e as short opts
 * - getopt_long_only() would match "--verbose" long option
 *
 * Note: Ambiguity between long options and short option clusters is
 * resolved in favor of long options.
 *
 * @param argc Argument count (from main).
 * @param argv Argument vector (from main).
 * @param optstring String describing valid short options.
 * @param longopts Array of struct option describing long options.
 * @param longindex If non-NULL, receives index of matched long option.
 * @return Option character/value on success, '?' on error, -1 when done.
 *
 * @see getopt_long, getopt
 */
int getopt_long_only(int argc,
                     char *const argv[],
                     const char *optstring,
                     const struct option *longopts,
                     int *longindex) {
    if (optstring == NULL || argc <= 0)
        return -1;

    optarg = NULL;

    /* Check if processing a short option from previous call */
    if (nextchar != NULL && *nextchar != '\0') {
        return getopt(argc, argv, optstring);
    }

    nextchar = NULL;

    /* Check if we're done */
    if (optind >= argc)
        return -1;

    char *arg = argv[optind];

    /* Check for non-option */
    if (arg[0] != '-' || arg[1] == '\0')
        return -1;

    /* Check for "--" */
    if (arg[1] == '-' && arg[2] == '\0') {
        optind++;
        return -1;
    }

    /* Try long option first (even with single dash) */
    const char *start = (arg[1] == '-') ? arg + 2 : arg + 1;
    char *name_end = strchr(start, '=');
    size_t name_len = name_end ? (size_t)(name_end - start) : strlen(start);

    /* Search for matching long option */
    for (int i = 0; longopts && longopts[i].name; i++) {
        if (strncmp(start, longopts[i].name, name_len) == 0 && longopts[i].name[name_len] == '\0') {
            /* Found match - delegate to getopt_long */
            if (arg[1] != '-') {
                /* Temporarily modify to use -- syntax */
                /* Actually, just handle it here */
            }

            optind++;
            if (longindex)
                *longindex = i;

            if (longopts[i].has_arg != no_argument) {
                if (name_end) {
                    optarg = name_end + 1;
                } else if (longopts[i].has_arg == required_argument && optind < argc) {
                    optarg = argv[optind++];
                } else if (longopts[i].has_arg == required_argument) {
                    if (opterr) {
                        fprintf(stderr,
                                "%s: option '-%s' requires an argument\n",
                                argv[0],
                                longopts[i].name);
                    }
                    return optstring[0] == ':' ? ':' : '?';
                }
            }

            if (longopts[i].flag) {
                *longopts[i].flag = longopts[i].val;
                return 0;
            }
            return longopts[i].val;
        }
    }

    /* No long match, try short option */
    if (arg[1] != '-') {
        nextchar = arg + 1;
        optind++;
        return getopt(argc, argv, optstring);
    }

    /* Long option not found */
    if (opterr) {
        fprintf(stderr, "%s: unrecognized option '%s'\n", argv[0], arg);
    }
    optind++;
    return '?';
}
