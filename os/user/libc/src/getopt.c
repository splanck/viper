/*
 * ViperOS libc - getopt.c
 * Command-line option parsing implementation
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

/*
 * getopt - Parse command-line options
 */
int getopt(int argc, char *const argv[], const char *optstring)
{
    if (optstring == NULL || argc <= 0)
        return -1;

    optarg = NULL;

    /* Check if we need to get next argument */
    if (nextchar == NULL || *nextchar == '\0')
    {
        /* Check if we're done */
        if (optind >= argc)
            return -1;

        /* Get next argument */
        char *arg = argv[optind];

        /* Check for option */
        if (arg[0] != '-' || arg[1] == '\0')
        {
            /* Not an option */
            return -1;
        }

        /* Check for "--" */
        if (arg[1] == '-' && arg[2] == '\0')
        {
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

    if (opt == NULL || c == ':')
    {
        /* Unknown option */
        if (opterr && optstring[0] != ':')
        {
            fprintf(stderr, "%s: invalid option -- '%c'\n", argv[0], c);
        }
        return '?';
    }

    /* Check if option requires argument */
    if (opt[1] == ':')
    {
        if (*nextchar != '\0')
        {
            /* Argument follows option immediately */
            optarg = nextchar;
            nextchar = NULL;
        }
        else if (opt[2] == ':')
        {
            /* Optional argument not present */
            optarg = NULL;
        }
        else if (optind < argc)
        {
            /* Argument is next argv element */
            optarg = argv[optind++];
        }
        else
        {
            /* Missing required argument */
            if (opterr && optstring[0] != ':')
            {
                fprintf(stderr, "%s: option requires an argument -- '%c'\n", argv[0], c);
            }
            return optstring[0] == ':' ? ':' : '?';
        }
    }

    return c;
}

/*
 * getopt_long - Parse long command-line options
 */
int getopt_long(int argc,
                char *const argv[],
                const char *optstring,
                const struct option *longopts,
                int *longindex)
{
    if (optstring == NULL || argc <= 0)
        return -1;

    optarg = NULL;

    /* Check if processing a short option from previous call */
    if (nextchar != NULL && *nextchar != '\0')
    {
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
    if (arg[1] == '-' && arg[2] == '\0')
    {
        optind++;
        return -1;
    }

    /* Check for long option */
    if (arg[1] == '-')
    {
        char *name_end = strchr(arg + 2, '=');
        size_t name_len = name_end ? (size_t)(name_end - (arg + 2)) : strlen(arg + 2);

        /* Search for matching long option */
        for (int i = 0; longopts && longopts[i].name; i++)
        {
            if (strncmp(arg + 2, longopts[i].name, name_len) == 0 &&
                longopts[i].name[name_len] == '\0')
            {
                /* Found match */
                optind++;

                if (longindex)
                    *longindex = i;

                /* Handle argument */
                if (longopts[i].has_arg != no_argument)
                {
                    if (name_end)
                    {
                        /* Argument after '=' */
                        optarg = name_end + 1;
                    }
                    else if (longopts[i].has_arg == required_argument)
                    {
                        if (optind < argc)
                        {
                            optarg = argv[optind++];
                        }
                        else
                        {
                            if (opterr)
                            {
                                fprintf(stderr,
                                        "%s: option '--%s' requires an argument\n",
                                        argv[0],
                                        longopts[i].name);
                            }
                            return optstring[0] == ':' ? ':' : '?';
                        }
                    }
                }
                else if (name_end)
                {
                    /* Argument provided but not expected */
                    if (opterr)
                    {
                        fprintf(stderr,
                                "%s: option '--%s' doesn't allow an argument\n",
                                argv[0],
                                longopts[i].name);
                    }
                    return '?';
                }

                /* Return value or set flag */
                if (longopts[i].flag)
                {
                    *longopts[i].flag = longopts[i].val;
                    return 0;
                }
                return longopts[i].val;
            }
        }

        /* No matching long option */
        if (opterr)
        {
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

/*
 * getopt_long_only - Parse long options with single dash
 */
int getopt_long_only(int argc,
                     char *const argv[],
                     const char *optstring,
                     const struct option *longopts,
                     int *longindex)
{
    if (optstring == NULL || argc <= 0)
        return -1;

    optarg = NULL;

    /* Check if processing a short option from previous call */
    if (nextchar != NULL && *nextchar != '\0')
    {
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
    if (arg[1] == '-' && arg[2] == '\0')
    {
        optind++;
        return -1;
    }

    /* Try long option first (even with single dash) */
    const char *start = (arg[1] == '-') ? arg + 2 : arg + 1;
    char *name_end = strchr(start, '=');
    size_t name_len = name_end ? (size_t)(name_end - start) : strlen(start);

    /* Search for matching long option */
    for (int i = 0; longopts && longopts[i].name; i++)
    {
        if (strncmp(start, longopts[i].name, name_len) == 0 && longopts[i].name[name_len] == '\0')
        {
            /* Found match - delegate to getopt_long */
            if (arg[1] != '-')
            {
                /* Temporarily modify to use -- syntax */
                /* Actually, just handle it here */
            }

            optind++;
            if (longindex)
                *longindex = i;

            if (longopts[i].has_arg != no_argument)
            {
                if (name_end)
                {
                    optarg = name_end + 1;
                }
                else if (longopts[i].has_arg == required_argument && optind < argc)
                {
                    optarg = argv[optind++];
                }
                else if (longopts[i].has_arg == required_argument)
                {
                    if (opterr)
                    {
                        fprintf(stderr,
                                "%s: option '-%s' requires an argument\n",
                                argv[0],
                                longopts[i].name);
                    }
                    return optstring[0] == ':' ? ':' : '?';
                }
            }

            if (longopts[i].flag)
            {
                *longopts[i].flag = longopts[i].val;
                return 0;
            }
            return longopts[i].val;
        }
    }

    /* No long match, try short option */
    if (arg[1] != '-')
    {
        nextchar = arg + 1;
        optind++;
        return getopt(argc, argv, optstring);
    }

    /* Long option not found */
    if (opterr)
    {
        fprintf(stderr, "%s: unrecognized option '%s'\n", argv[0], arg);
    }
    optind++;
    return '?';
}
