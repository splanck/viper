/*
 * ViperOS C Library - fmtmsg implementation
 * Message display functions
 */

#include <fmtmsg.h>
#include <stdio.h>
#include <string.h>

/* Custom severity definitions */
#define MAX_SEVERITIES 16

struct severity_entry {
    int value;
    const char *string;
};

static struct severity_entry custom_severities[MAX_SEVERITIES];
static int num_custom_severities = 0;

/* Get severity string */
static const char *get_severity_string(int severity) {
    /* Check built-in severities */
    switch (severity) {
    case MM_HALT:    return "HALT";
    case MM_ERROR:   return "ERROR";
    case MM_WARNING: return "WARNING";
    case MM_INFO:    return "INFO";
    case MM_NOSEV:   return "";
    default:
        break;
    }

    /* Check custom severities */
    for (int i = 0; i < num_custom_severities; i++) {
        if (custom_severities[i].value == severity) {
            return custom_severities[i].string;
        }
    }

    return "UNKNOWN";
}

int fmtmsg(long classification, const char *label, int severity,
           const char *text, const char *action, const char *tag) {
    int result = MM_OK;
    char message[1024];
    char *p = message;
    char *end = message + sizeof(message) - 1;

    /* Build message: label: severity: text */
    if (label && label[0]) {
        int len = strlen(label);
        if (len > 10) len = 10;  /* Label max 10 chars */
        if (p + len + 2 < end) {
            memcpy(p, label, len);
            p += len;
            *p++ = ':';
            *p++ = ' ';
        }
    }

    const char *sev_str = get_severity_string(severity);
    if (sev_str && sev_str[0]) {
        int len = strlen(sev_str);
        if (p + len + 2 < end) {
            memcpy(p, sev_str, len);
            p += len;
            *p++ = ':';
            *p++ = ' ';
        }
    }

    if (text) {
        int len = strlen(text);
        if (p + len < end) {
            memcpy(p, text, len);
            p += len;
        }
    }

    *p++ = '\n';
    *p = '\0';

    /* Print to stderr if requested */
    if (classification & MM_PRINT) {
        if (fputs(message, stderr) == EOF) {
            result |= MM_NOMSG;
        }
    }

    /* Print action if provided */
    if (action && action[0]) {
        char action_msg[512];
        snprintf(action_msg, sizeof(action_msg), "TO FIX: %s\n", action);

        if (classification & MM_PRINT) {
            if (fputs(action_msg, stderr) == EOF) {
                result |= MM_NOMSG;
            }
        }
    }

    /* Print tag if provided */
    if (tag && tag[0]) {
        char tag_msg[256];
        snprintf(tag_msg, sizeof(tag_msg), "TAG: %s\n", tag);

        if (classification & MM_PRINT) {
            if (fputs(tag_msg, stderr) == EOF) {
                result |= MM_NOMSG;
            }
        }
    }

    /* Console output not supported in this implementation */
    if (classification & MM_CONSOLE) {
        result |= MM_NOCON;
    }

    /* Convert combined flags to return value */
    if (result == MM_OK) {
        return MM_OK;
    } else if ((result & MM_NOMSG) && (result & MM_NOCON)) {
        return MM_NOTOK;
    } else if (result & MM_NOMSG) {
        return MM_NOMSG;
    } else {
        return MM_NOCON;
    }
}

int addseverity(int severity, const char *string) {
    if (string == NULL) {
        /* Remove severity */
        for (int i = 0; i < num_custom_severities; i++) {
            if (custom_severities[i].value == severity) {
                /* Shift remaining entries */
                for (int j = i; j < num_custom_severities - 1; j++) {
                    custom_severities[j] = custom_severities[j + 1];
                }
                num_custom_severities--;
                return MM_OK;
            }
        }
        return MM_NOTOK;
    }

    /* Check for existing severity to update */
    for (int i = 0; i < num_custom_severities; i++) {
        if (custom_severities[i].value == severity) {
            custom_severities[i].string = string;
            return MM_OK;
        }
    }

    /* Add new severity */
    if (num_custom_severities >= MAX_SEVERITIES) {
        return MM_NOTOK;
    }

    custom_severities[num_custom_severities].value = severity;
    custom_severities[num_custom_severities].string = string;
    num_custom_severities++;

    return MM_OK;
}
