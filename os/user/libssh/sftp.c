/**
 * @file sftp.c
 * @brief SFTP client implementation (SSH File Transfer Protocol v3).
 */

#include "include/sftp.h"
#include "ssh_internal.h"
#include <stdlib.h>
#include <string.h>

/* SFTP packet types */
#define SSH_FXP_INIT 1
#define SSH_FXP_VERSION 2
#define SSH_FXP_OPEN 3
#define SSH_FXP_CLOSE 4
#define SSH_FXP_READ 5
#define SSH_FXP_WRITE 6
#define SSH_FXP_LSTAT 7
#define SSH_FXP_FSTAT 8
#define SSH_FXP_SETSTAT 9
#define SSH_FXP_FSETSTAT 10
#define SSH_FXP_OPENDIR 11
#define SSH_FXP_READDIR 12
#define SSH_FXP_REMOVE 13
#define SSH_FXP_MKDIR 14
#define SSH_FXP_RMDIR 15
#define SSH_FXP_REALPATH 16
#define SSH_FXP_STAT 17
#define SSH_FXP_RENAME 18
#define SSH_FXP_READLINK 19
#define SSH_FXP_SYMLINK 20
#define SSH_FXP_STATUS 101
#define SSH_FXP_HANDLE 102
#define SSH_FXP_DATA 103
#define SSH_FXP_NAME 104
#define SSH_FXP_ATTRS 105
#define SSH_FXP_EXTENDED 200
#define SSH_FXP_EXTENDED_REPLY 201

/* SFTP file flags */
#define SSH_FXF_READ 0x00000001
#define SSH_FXF_WRITE 0x00000002
#define SSH_FXF_APPEND 0x00000004
#define SSH_FXF_CREAT 0x00000008
#define SSH_FXF_TRUNC 0x00000010
#define SSH_FXF_EXCL 0x00000020

/* Max sizes */
#define SFTP_MAX_PACKET_SIZE 34000
#define SFTP_READ_SIZE 32768

struct sftp_session
{
    ssh_session_t *ssh;
    ssh_channel_t *channel;
    uint32_t version;
    uint32_t request_id;
    sftp_error_t error;
    uint8_t packet_buf[SFTP_MAX_PACKET_SIZE];
};

struct sftp_file
{
    sftp_session_t *sftp;
    uint8_t handle[256];
    size_t handle_len;
    uint64_t offset;
    bool eof;
};

struct sftp_dir
{
    sftp_session_t *sftp;
    uint8_t handle[256];
    size_t handle_len;
    bool eof;
    /* Buffer for readdir results */
    sftp_attributes_t *entries;
    int entry_count;
    int entry_pos;
};

/*=============================================================================
 * Packet I/O
 *===========================================================================*/

static int sftp_send_packet(sftp_session_t *sftp, uint8_t type, const uint8_t *data, size_t len)
{
    uint8_t header[5];
    ssh_buf_write_u32(header, len + 1);
    header[4] = type;

    ssize_t rc = ssh_channel_write(sftp->channel, header, 5);
    if (rc < 0)
        return rc;

    if (len > 0)
    {
        rc = ssh_channel_write(sftp->channel, data, len);
        if (rc < 0)
            return rc;
    }

    return SFTP_OK;
}

static int sftp_recv_packet(sftp_session_t *sftp, uint8_t *type, uint8_t *data, size_t *len)
{
    uint8_t header[5];
    size_t total = 0;
    int is_stderr;

    /* Read length and type */
    while (total < 5)
    {
        ssize_t rc = ssh_channel_read(sftp->channel, header + total, 5 - total, &is_stderr);
        if (rc <= 0)
            return SFTP_CONNECTION_LOST;
        total += rc;
    }

    uint32_t packet_len = ssh_buf_read_u32(header);
    *type = header[4];

    if (packet_len < 1 || packet_len > SFTP_MAX_PACKET_SIZE)
    {
        return SFTP_BAD_MESSAGE;
    }

    size_t data_len = packet_len - 1;
    *len = data_len;

    total = 0;
    while (total < data_len)
    {
        ssize_t rc = ssh_channel_read(sftp->channel, data + total, data_len - total, &is_stderr);
        if (rc <= 0)
            return SFTP_CONNECTION_LOST;
        total += rc;
    }

    return SFTP_OK;
}

/*=============================================================================
 * Session Management
 *===========================================================================*/

sftp_session_t *sftp_new(ssh_session_t *ssh)
{
    if (!ssh)
        return NULL;

    sftp_session_t *sftp = calloc(1, sizeof(sftp_session_t));
    if (!sftp)
        return NULL;

    sftp->ssh = ssh;
    sftp->request_id = 1;

    return sftp;
}

int sftp_init(sftp_session_t *sftp)
{
    if (!sftp || !sftp->ssh)
        return SFTP_NO_CONNECTION;

    /* Open channel */
    sftp->channel = ssh_channel_new(sftp->ssh);
    if (!sftp->channel)
        return SFTP_NO_CONNECTION;

    int rc = ssh_channel_open_session(sftp->channel);
    if (rc < 0)
    {
        ssh_channel_free(sftp->channel);
        sftp->channel = NULL;
        return SFTP_NO_CONNECTION;
    }

    /* Request sftp subsystem */
    rc = ssh_channel_request_subsystem(sftp->channel, "sftp");
    if (rc < 0)
    {
        ssh_channel_close(sftp->channel);
        ssh_channel_free(sftp->channel);
        sftp->channel = NULL;
        return SFTP_NO_CONNECTION;
    }

    /* Send SFTP_INIT */
    uint8_t init_packet[4];
    ssh_buf_write_u32(init_packet, 3); /* Version 3 */
    rc = sftp_send_packet(sftp, SSH_FXP_INIT, init_packet, 4);
    if (rc < 0)
        return rc;

    /* Receive VERSION */
    uint8_t type;
    uint8_t response[1024];
    size_t response_len;

    rc = sftp_recv_packet(sftp, &type, response, &response_len);
    if (rc < 0)
        return rc;

    if (type != SSH_FXP_VERSION || response_len < 4)
    {
        return SFTP_BAD_MESSAGE;
    }

    sftp->version = ssh_buf_read_u32(response);
    if (sftp->version < 3)
    {
        return SFTP_OP_UNSUPPORTED;
    }

    return SFTP_OK;
}

void sftp_free(sftp_session_t *sftp)
{
    if (!sftp)
        return;

    if (sftp->channel)
    {
        ssh_channel_close(sftp->channel);
        ssh_channel_free(sftp->channel);
    }

    free(sftp);
}

sftp_error_t sftp_get_error(sftp_session_t *sftp)
{
    return sftp ? sftp->error : SFTP_NO_CONNECTION;
}

/*=============================================================================
 * File Operations
 *===========================================================================*/

sftp_file_t *sftp_open(sftp_session_t *sftp, const char *path, int flags, mode_t mode)
{
    if (!sftp || !path)
        return NULL;

    uint8_t packet[1024];
    size_t pos = 0;

    /* Request ID */
    ssh_buf_write_u32(packet + pos, sftp->request_id++);
    pos += 4;

    /* Path */
    size_t path_len = strlen(path);
    ssh_buf_write_u32(packet + pos, path_len);
    memcpy(packet + pos + 4, path, path_len);
    pos += 4 + path_len;

    /* pflags */
    uint32_t pflags = 0;
    if (flags & SFTP_READ)
        pflags |= SSH_FXF_READ;
    if (flags & SFTP_WRITE)
        pflags |= SSH_FXF_WRITE;
    if (flags & SFTP_APPEND)
        pflags |= SSH_FXF_APPEND;
    if (flags & SFTP_CREAT)
        pflags |= SSH_FXF_CREAT;
    if (flags & SFTP_TRUNC)
        pflags |= SSH_FXF_TRUNC;
    if (flags & SFTP_EXCL)
        pflags |= SSH_FXF_EXCL;
    ssh_buf_write_u32(packet + pos, pflags);
    pos += 4;

    /* attrs */
    if (flags & SFTP_CREAT)
    {
        ssh_buf_write_u32(packet + pos, SFTP_ATTR_PERMISSIONS);
        pos += 4;
        ssh_buf_write_u32(packet + pos, mode);
        pos += 4;
    }
    else
    {
        ssh_buf_write_u32(packet + pos, 0); /* no attrs */
        pos += 4;
    }

    int rc = sftp_send_packet(sftp, SSH_FXP_OPEN, packet, pos);
    if (rc < 0)
        return NULL;

    /* Receive response */
    uint8_t type;
    uint8_t response[1024];
    size_t response_len;

    rc = sftp_recv_packet(sftp, &type, response, &response_len);
    if (rc < 0)
        return NULL;

    /* Skip request ID */
    if (response_len < 4)
        return NULL;

    if (type == SSH_FXP_STATUS)
    {
        if (response_len >= 8)
        {
            sftp->error = ssh_buf_read_u32(response + 4);
        }
        return NULL;
    }

    if (type != SSH_FXP_HANDLE || response_len < 8)
    {
        sftp->error = SFTP_BAD_MESSAGE;
        return NULL;
    }

    /* Parse handle */
    uint32_t handle_len = ssh_buf_read_u32(response + 4);
    if (handle_len > 256 || 8 + handle_len > response_len)
    {
        sftp->error = SFTP_BAD_MESSAGE;
        return NULL;
    }

    sftp_file_t *file = calloc(1, sizeof(sftp_file_t));
    if (!file)
        return NULL;

    file->sftp = sftp;
    memcpy(file->handle, response + 8, handle_len);
    file->handle_len = handle_len;

    return file;
}

int sftp_close(sftp_file_t *file)
{
    if (!file)
        return SFTP_INVALID_HANDLE;

    sftp_session_t *sftp = file->sftp;

    uint8_t packet[512];
    size_t pos = 0;

    ssh_buf_write_u32(packet + pos, sftp->request_id++);
    pos += 4;

    ssh_buf_write_u32(packet + pos, file->handle_len);
    memcpy(packet + pos + 4, file->handle, file->handle_len);
    pos += 4 + file->handle_len;

    int rc = sftp_send_packet(sftp, SSH_FXP_CLOSE, packet, pos);
    if (rc < 0)
    {
        free(file);
        return rc;
    }

    /* Receive response */
    uint8_t type;
    uint8_t response[256];
    size_t response_len;

    rc = sftp_recv_packet(sftp, &type, response, &response_len);

    free(file);

    if (rc < 0)
        return rc;

    if (type == SSH_FXP_STATUS && response_len >= 8)
    {
        uint32_t status = ssh_buf_read_u32(response + 4);
        return (status == 0) ? SFTP_OK : status;
    }

    return SFTP_OK;
}

ssize_t sftp_read(sftp_file_t *file, void *buffer, size_t count)
{
    if (!file || !buffer)
        return -1;
    if (file->eof)
        return 0;

    sftp_session_t *sftp = file->sftp;

    if (count > SFTP_READ_SIZE)
        count = SFTP_READ_SIZE;

    uint8_t packet[512];
    size_t pos = 0;

    ssh_buf_write_u32(packet + pos, sftp->request_id++);
    pos += 4;

    ssh_buf_write_u32(packet + pos, file->handle_len);
    memcpy(packet + pos + 4, file->handle, file->handle_len);
    pos += 4 + file->handle_len;

    /* Offset (uint64) */
    ssh_buf_write_u32(packet + pos, (file->offset >> 32) & 0xFFFFFFFF);
    pos += 4;
    ssh_buf_write_u32(packet + pos, file->offset & 0xFFFFFFFF);
    pos += 4;

    /* Length */
    ssh_buf_write_u32(packet + pos, count);
    pos += 4;

    int rc = sftp_send_packet(sftp, SSH_FXP_READ, packet, pos);
    if (rc < 0)
        return rc;

    /* Receive response */
    uint8_t type;
    uint8_t *response = sftp->packet_buf;
    size_t response_len;

    rc = sftp_recv_packet(sftp, &type, response, &response_len);
    if (rc < 0)
        return rc;

    if (type == SSH_FXP_STATUS)
    {
        if (response_len >= 8)
        {
            uint32_t status = ssh_buf_read_u32(response + 4);
            if (status == SFTP_EOF)
            {
                file->eof = true;
                return 0;
            }
            sftp->error = status;
            return -1;
        }
        return -1;
    }

    if (type != SSH_FXP_DATA || response_len < 8)
    {
        sftp->error = SFTP_BAD_MESSAGE;
        return -1;
    }

    uint32_t data_len = ssh_buf_read_u32(response + 4);
    if (8 + data_len > response_len || data_len > count)
    {
        sftp->error = SFTP_BAD_MESSAGE;
        return -1;
    }

    memcpy(buffer, response + 8, data_len);
    file->offset += data_len;

    return data_len;
}

ssize_t sftp_write(sftp_file_t *file, const void *buffer, size_t count)
{
    if (!file || !buffer)
        return -1;

    sftp_session_t *sftp = file->sftp;

    if (count > SFTP_READ_SIZE)
        count = SFTP_READ_SIZE;

    uint8_t *packet = sftp->packet_buf;
    size_t pos = 0;

    ssh_buf_write_u32(packet + pos, sftp->request_id++);
    pos += 4;

    ssh_buf_write_u32(packet + pos, file->handle_len);
    memcpy(packet + pos + 4, file->handle, file->handle_len);
    pos += 4 + file->handle_len;

    /* Offset (uint64) */
    ssh_buf_write_u32(packet + pos, (file->offset >> 32) & 0xFFFFFFFF);
    pos += 4;
    ssh_buf_write_u32(packet + pos, file->offset & 0xFFFFFFFF);
    pos += 4;

    /* Data */
    ssh_buf_write_u32(packet + pos, count);
    memcpy(packet + pos + 4, buffer, count);
    pos += 4 + count;

    int rc = sftp_send_packet(sftp, SSH_FXP_WRITE, packet, pos);
    if (rc < 0)
        return rc;

    /* Receive response */
    uint8_t type;
    uint8_t response[256];
    size_t response_len;

    rc = sftp_recv_packet(sftp, &type, response, &response_len);
    if (rc < 0)
        return rc;

    if (type == SSH_FXP_STATUS && response_len >= 8)
    {
        uint32_t status = ssh_buf_read_u32(response + 4);
        if (status != 0)
        {
            sftp->error = status;
            return -1;
        }
    }

    file->offset += count;
    return count;
}

int sftp_seek(sftp_file_t *file, uint64_t offset)
{
    if (!file)
        return SFTP_INVALID_HANDLE;
    file->offset = offset;
    file->eof = false;
    return SFTP_OK;
}

uint64_t sftp_tell(sftp_file_t *file)
{
    return file ? file->offset : 0;
}

void sftp_rewind(sftp_file_t *file)
{
    if (file)
    {
        file->offset = 0;
        file->eof = false;
    }
}

/*=============================================================================
 * Stat Operations
 *===========================================================================*/

static sftp_attributes_t *parse_attrs(const uint8_t *data, size_t len, size_t *consumed)
{
    if (len < 4)
        return NULL;

    sftp_attributes_t *attr = calloc(1, sizeof(sftp_attributes_t));
    if (!attr)
        return NULL;

    size_t pos = 0;
    attr->flags = ssh_buf_read_u32(data + pos);
    pos += 4;

    if (attr->flags & SFTP_ATTR_SIZE)
    {
        if (pos + 8 > len)
        {
            free(attr);
            return NULL;
        }
        attr->size =
            ((uint64_t)ssh_buf_read_u32(data + pos) << 32) | ssh_buf_read_u32(data + pos + 4);
        pos += 8;
    }

    if (attr->flags & SFTP_ATTR_UIDGID)
    {
        if (pos + 8 > len)
        {
            free(attr);
            return NULL;
        }
        attr->uid = ssh_buf_read_u32(data + pos);
        attr->gid = ssh_buf_read_u32(data + pos + 4);
        pos += 8;
    }

    if (attr->flags & SFTP_ATTR_PERMISSIONS)
    {
        if (pos + 4 > len)
        {
            free(attr);
            return NULL;
        }
        attr->permissions = ssh_buf_read_u32(data + pos);
        pos += 4;

        /* Determine file type from permissions */
        uint32_t type = attr->permissions & 0170000;
        if (type == 0100000)
            attr->type = SFTP_TYPE_REGULAR;
        else if (type == 0040000)
            attr->type = SFTP_TYPE_DIRECTORY;
        else if (type == 0120000)
            attr->type = SFTP_TYPE_SYMLINK;
        else
            attr->type = SFTP_TYPE_UNKNOWN;
    }

    if (attr->flags & SFTP_ATTR_ACMODTIME)
    {
        if (pos + 8 > len)
        {
            free(attr);
            return NULL;
        }
        attr->atime = ssh_buf_read_u32(data + pos);
        attr->mtime = ssh_buf_read_u32(data + pos + 4);
        pos += 8;
    }

    if (consumed)
        *consumed = pos;
    return attr;
}

sftp_attributes_t *sftp_stat(sftp_session_t *sftp, const char *path)
{
    if (!sftp || !path)
        return NULL;

    uint8_t packet[1024];
    size_t pos = 0;

    ssh_buf_write_u32(packet + pos, sftp->request_id++);
    pos += 4;

    size_t path_len = strlen(path);
    ssh_buf_write_u32(packet + pos, path_len);
    memcpy(packet + pos + 4, path, path_len);
    pos += 4 + path_len;

    int rc = sftp_send_packet(sftp, SSH_FXP_STAT, packet, pos);
    if (rc < 0)
        return NULL;

    uint8_t type;
    uint8_t response[1024];
    size_t response_len;

    rc = sftp_recv_packet(sftp, &type, response, &response_len);
    if (rc < 0)
        return NULL;

    if (type == SSH_FXP_STATUS)
    {
        if (response_len >= 8)
        {
            sftp->error = ssh_buf_read_u32(response + 4);
        }
        return NULL;
    }

    if (type != SSH_FXP_ATTRS || response_len < 8)
    {
        sftp->error = SFTP_BAD_MESSAGE;
        return NULL;
    }

    return parse_attrs(response + 4, response_len - 4, NULL);
}

sftp_attributes_t *sftp_lstat(sftp_session_t *sftp, const char *path)
{
    if (!sftp || !path)
        return NULL;

    uint8_t packet[1024];
    size_t pos = 0;

    ssh_buf_write_u32(packet + pos, sftp->request_id++);
    pos += 4;

    size_t path_len = strlen(path);
    ssh_buf_write_u32(packet + pos, path_len);
    memcpy(packet + pos + 4, path, path_len);
    pos += 4 + path_len;

    int rc = sftp_send_packet(sftp, SSH_FXP_LSTAT, packet, pos);
    if (rc < 0)
        return NULL;

    uint8_t type;
    uint8_t response[1024];
    size_t response_len;

    rc = sftp_recv_packet(sftp, &type, response, &response_len);
    if (rc < 0)
        return NULL;

    if (type == SSH_FXP_STATUS)
    {
        if (response_len >= 8)
        {
            sftp->error = ssh_buf_read_u32(response + 4);
        }
        return NULL;
    }

    if (type != SSH_FXP_ATTRS)
        return NULL;

    return parse_attrs(response + 4, response_len - 4, NULL);
}

sftp_attributes_t *sftp_fstat(sftp_file_t *file)
{
    if (!file)
        return NULL;

    sftp_session_t *sftp = file->sftp;

    uint8_t packet[512];
    size_t pos = 0;

    ssh_buf_write_u32(packet + pos, sftp->request_id++);
    pos += 4;

    ssh_buf_write_u32(packet + pos, file->handle_len);
    memcpy(packet + pos + 4, file->handle, file->handle_len);
    pos += 4 + file->handle_len;

    int rc = sftp_send_packet(sftp, SSH_FXP_FSTAT, packet, pos);
    if (rc < 0)
        return NULL;

    uint8_t type;
    uint8_t response[1024];
    size_t response_len;

    rc = sftp_recv_packet(sftp, &type, response, &response_len);
    if (rc < 0)
        return NULL;

    if (type != SSH_FXP_ATTRS)
        return NULL;

    return parse_attrs(response + 4, response_len - 4, NULL);
}

void sftp_attributes_free(sftp_attributes_t *attr)
{
    if (!attr)
        return;
    free(attr->name);
    free(attr->longname);
    free(attr);
}

/*=============================================================================
 * Directory Operations
 *===========================================================================*/

sftp_dir_t *sftp_opendir(sftp_session_t *sftp, const char *path)
{
    if (!sftp || !path)
        return NULL;

    uint8_t packet[1024];
    size_t pos = 0;

    ssh_buf_write_u32(packet + pos, sftp->request_id++);
    pos += 4;

    size_t path_len = strlen(path);
    ssh_buf_write_u32(packet + pos, path_len);
    memcpy(packet + pos + 4, path, path_len);
    pos += 4 + path_len;

    int rc = sftp_send_packet(sftp, SSH_FXP_OPENDIR, packet, pos);
    if (rc < 0)
        return NULL;

    uint8_t type;
    uint8_t response[1024];
    size_t response_len;

    rc = sftp_recv_packet(sftp, &type, response, &response_len);
    if (rc < 0)
        return NULL;

    if (type == SSH_FXP_STATUS)
    {
        if (response_len >= 8)
        {
            sftp->error = ssh_buf_read_u32(response + 4);
        }
        return NULL;
    }

    if (type != SSH_FXP_HANDLE || response_len < 8)
        return NULL;

    uint32_t handle_len = ssh_buf_read_u32(response + 4);
    if (handle_len > 256)
        return NULL;

    sftp_dir_t *dir = calloc(1, sizeof(sftp_dir_t));
    if (!dir)
        return NULL;

    dir->sftp = sftp;
    memcpy(dir->handle, response + 8, handle_len);
    dir->handle_len = handle_len;

    return dir;
}

/**
 * @brief Parse a single directory entry from the response buffer.
 * @param response Response buffer.
 * @param response_len Total response length.
 * @param offset Current offset in buffer (updated on success).
 * @return Parsed attributes or NULL on error.
 */
static sftp_attributes_t *parse_dir_entry(const uint8_t *response, size_t response_len, size_t *offset)
{
    size_t pos = *offset;

    /* filename */
    if (pos + 4 > response_len)
        return NULL;
    uint32_t name_len = ssh_buf_read_u32(response + pos);
    pos += 4;
    if (pos + name_len > response_len)
        return NULL;

    sftp_attributes_t *attr = calloc(1, sizeof(sftp_attributes_t));
    if (!attr)
        return NULL;

    attr->name = malloc(name_len + 1);
    if (attr->name)
    {
        memcpy(attr->name, response + pos, name_len);
        attr->name[name_len] = '\0';
    }
    pos += name_len;

    /* longname */
    if (pos + 4 > response_len)
    {
        sftp_attributes_free(attr);
        return NULL;
    }
    uint32_t longname_len = ssh_buf_read_u32(response + pos);
    pos += 4;
    if (pos + longname_len > response_len)
    {
        sftp_attributes_free(attr);
        return NULL;
    }

    attr->longname = malloc(longname_len + 1);
    if (attr->longname)
    {
        memcpy(attr->longname, response + pos, longname_len);
        attr->longname[longname_len] = '\0';
    }
    pos += longname_len;

    /* attrs */
    size_t consumed;
    sftp_attributes_t *parsed = parse_attrs(response + pos, response_len - pos, &consumed);
    if (parsed)
    {
        attr->flags = parsed->flags;
        attr->size = parsed->size;
        attr->uid = parsed->uid;
        attr->gid = parsed->gid;
        attr->permissions = parsed->permissions;
        attr->atime = parsed->atime;
        attr->mtime = parsed->mtime;
        attr->type = parsed->type;
        free(parsed);
        pos += consumed;
    }

    *offset = pos;
    return attr;
}

/**
 * @brief Free buffered directory entries.
 */
static void free_dir_entries(sftp_dir_t *dir)
{
    if (dir->entries)
    {
        for (int i = 0; i < dir->entry_count; i++)
        {
            free(dir->entries[i].name);
            free(dir->entries[i].longname);
        }
        free(dir->entries);
        dir->entries = NULL;
    }
    dir->entry_count = 0;
    dir->entry_pos = 0;
}

sftp_attributes_t *sftp_readdir(sftp_dir_t *dir)
{
    if (!dir || dir->eof)
        return NULL;

    /* Return buffered entry if available */
    if (dir->entries && dir->entry_pos < dir->entry_count)
    {
        sftp_attributes_t *src = &dir->entries[dir->entry_pos++];
        sftp_attributes_t *attr = calloc(1, sizeof(sftp_attributes_t));
        if (!attr)
            return NULL;

        /* Copy the entry (caller will free) */
        if (src->name)
            attr->name = strdup(src->name);
        if (src->longname)
            attr->longname = strdup(src->longname);
        attr->flags = src->flags;
        attr->size = src->size;
        attr->uid = src->uid;
        attr->gid = src->gid;
        attr->permissions = src->permissions;
        attr->atime = src->atime;
        attr->mtime = src->mtime;
        attr->type = src->type;
        return attr;
    }

    /* Free old entries before fetching new batch */
    free_dir_entries(dir);

    sftp_session_t *sftp = dir->sftp;

    uint8_t packet[512];
    size_t pos = 0;

    ssh_buf_write_u32(packet + pos, sftp->request_id++);
    pos += 4;

    ssh_buf_write_u32(packet + pos, dir->handle_len);
    memcpy(packet + pos + 4, dir->handle, dir->handle_len);
    pos += 4 + dir->handle_len;

    int rc = sftp_send_packet(sftp, SSH_FXP_READDIR, packet, pos);
    if (rc < 0)
        return NULL;

    uint8_t type;
    uint8_t *response = sftp->packet_buf;
    size_t response_len;

    rc = sftp_recv_packet(sftp, &type, response, &response_len);
    if (rc < 0)
        return NULL;

    if (type == SSH_FXP_STATUS)
    {
        if (response_len >= 8)
        {
            uint32_t status = ssh_buf_read_u32(response + 4);
            if (status == SFTP_EOF)
            {
                dir->eof = true;
                return NULL;
            }
            sftp->error = status;
        }
        return NULL;
    }

    if (type != SSH_FXP_NAME || response_len < 8)
        return NULL;

    uint32_t count = ssh_buf_read_u32(response + 4);
    if (count == 0)
    {
        dir->eof = true;
        return NULL;
    }

    /* Allocate buffer for all entries */
    dir->entries = calloc(count, sizeof(sftp_attributes_t));
    if (!dir->entries)
        return NULL;

    /* Parse all entries from response */
    pos = 8;
    for (uint32_t i = 0; i < count; i++)
    {
        sftp_attributes_t *entry = parse_dir_entry(response, response_len, &pos);
        if (!entry)
        {
            /* Parsing failed - keep what we have */
            break;
        }
        /* Move data into buffer (shallow copy, entry struct is temporary) */
        dir->entries[dir->entry_count].name = entry->name;
        dir->entries[dir->entry_count].longname = entry->longname;
        dir->entries[dir->entry_count].flags = entry->flags;
        dir->entries[dir->entry_count].size = entry->size;
        dir->entries[dir->entry_count].uid = entry->uid;
        dir->entries[dir->entry_count].gid = entry->gid;
        dir->entries[dir->entry_count].permissions = entry->permissions;
        dir->entries[dir->entry_count].atime = entry->atime;
        dir->entries[dir->entry_count].mtime = entry->mtime;
        dir->entries[dir->entry_count].type = entry->type;
        dir->entry_count++;
        /* Free the temporary struct but not its string members (now owned by buffer) */
        free(entry);
    }

    if (dir->entry_count == 0)
    {
        free(dir->entries);
        dir->entries = NULL;
        return NULL;
    }

    /* Return first entry from newly filled buffer */
    dir->entry_pos = 0;
    return sftp_readdir(dir); /* Recurse to use the buffered path */
}

int sftp_dir_eof(sftp_dir_t *dir)
{
    return dir ? dir->eof : 1;
}

int sftp_closedir(sftp_dir_t *dir)
{
    if (!dir)
        return SFTP_OK;

    sftp_session_t *sftp = dir->sftp;

    uint8_t packet[512];
    size_t pos = 0;

    ssh_buf_write_u32(packet + pos, sftp->request_id++);
    pos += 4;

    ssh_buf_write_u32(packet + pos, dir->handle_len);
    memcpy(packet + pos + 4, dir->handle, dir->handle_len);
    pos += 4 + dir->handle_len;

    sftp_send_packet(sftp, SSH_FXP_CLOSE, packet, pos);

    uint8_t type;
    uint8_t response[256];
    size_t response_len;
    sftp_recv_packet(sftp, &type, response, &response_len);

    /* Free buffered directory entries */
    free_dir_entries(dir);

    free(dir);
    return SFTP_OK;
}

int sftp_mkdir(sftp_session_t *sftp, const char *path, mode_t mode)
{
    if (!sftp || !path)
        return SFTP_FAILURE;

    uint8_t packet[1024];
    size_t pos = 0;

    ssh_buf_write_u32(packet + pos, sftp->request_id++);
    pos += 4;

    size_t path_len = strlen(path);
    ssh_buf_write_u32(packet + pos, path_len);
    memcpy(packet + pos + 4, path, path_len);
    pos += 4 + path_len;

    /* attrs */
    ssh_buf_write_u32(packet + pos, SFTP_ATTR_PERMISSIONS);
    pos += 4;
    ssh_buf_write_u32(packet + pos, mode);
    pos += 4;

    int rc = sftp_send_packet(sftp, SSH_FXP_MKDIR, packet, pos);
    if (rc < 0)
        return rc;

    uint8_t type;
    uint8_t response[256];
    size_t response_len;

    rc = sftp_recv_packet(sftp, &type, response, &response_len);
    if (rc < 0)
        return rc;

    if (type == SSH_FXP_STATUS && response_len >= 8)
    {
        return ssh_buf_read_u32(response + 4);
    }

    return SFTP_OK;
}

int sftp_rmdir(sftp_session_t *sftp, const char *path)
{
    if (!sftp || !path)
        return SFTP_FAILURE;

    uint8_t packet[1024];
    size_t pos = 0;

    ssh_buf_write_u32(packet + pos, sftp->request_id++);
    pos += 4;

    size_t path_len = strlen(path);
    ssh_buf_write_u32(packet + pos, path_len);
    memcpy(packet + pos + 4, path, path_len);
    pos += 4 + path_len;

    int rc = sftp_send_packet(sftp, SSH_FXP_RMDIR, packet, pos);
    if (rc < 0)
        return rc;

    uint8_t type;
    uint8_t response[256];
    size_t response_len;

    rc = sftp_recv_packet(sftp, &type, response, &response_len);
    if (rc < 0)
        return rc;

    if (type == SSH_FXP_STATUS && response_len >= 8)
    {
        return ssh_buf_read_u32(response + 4);
    }

    return SFTP_OK;
}

/*=============================================================================
 * File Management
 *===========================================================================*/

int sftp_unlink(sftp_session_t *sftp, const char *path)
{
    if (!sftp || !path)
        return SFTP_FAILURE;

    uint8_t packet[1024];
    size_t pos = 0;

    ssh_buf_write_u32(packet + pos, sftp->request_id++);
    pos += 4;

    size_t path_len = strlen(path);
    ssh_buf_write_u32(packet + pos, path_len);
    memcpy(packet + pos + 4, path, path_len);
    pos += 4 + path_len;

    int rc = sftp_send_packet(sftp, SSH_FXP_REMOVE, packet, pos);
    if (rc < 0)
        return rc;

    uint8_t type;
    uint8_t response[256];
    size_t response_len;

    rc = sftp_recv_packet(sftp, &type, response, &response_len);
    if (rc < 0)
        return rc;

    if (type == SSH_FXP_STATUS && response_len >= 8)
    {
        return ssh_buf_read_u32(response + 4);
    }

    return SFTP_OK;
}

int sftp_rename(sftp_session_t *sftp, const char *oldpath, const char *newpath)
{
    if (!sftp || !oldpath || !newpath)
        return SFTP_FAILURE;

    uint8_t packet[2048];
    size_t pos = 0;

    ssh_buf_write_u32(packet + pos, sftp->request_id++);
    pos += 4;

    size_t old_len = strlen(oldpath);
    ssh_buf_write_u32(packet + pos, old_len);
    memcpy(packet + pos + 4, oldpath, old_len);
    pos += 4 + old_len;

    size_t new_len = strlen(newpath);
    ssh_buf_write_u32(packet + pos, new_len);
    memcpy(packet + pos + 4, newpath, new_len);
    pos += 4 + new_len;

    int rc = sftp_send_packet(sftp, SSH_FXP_RENAME, packet, pos);
    if (rc < 0)
        return rc;

    uint8_t type;
    uint8_t response[256];
    size_t response_len;

    rc = sftp_recv_packet(sftp, &type, response, &response_len);
    if (rc < 0)
        return rc;

    if (type == SSH_FXP_STATUS && response_len >= 8)
    {
        return ssh_buf_read_u32(response + 4);
    }

    return SFTP_OK;
}

char *sftp_realpath(sftp_session_t *sftp, const char *path)
{
    if (!sftp || !path)
        return NULL;

    uint8_t packet[1024];
    size_t pos = 0;

    ssh_buf_write_u32(packet + pos, sftp->request_id++);
    pos += 4;

    size_t path_len = strlen(path);
    ssh_buf_write_u32(packet + pos, path_len);
    memcpy(packet + pos + 4, path, path_len);
    pos += 4 + path_len;

    int rc = sftp_send_packet(sftp, SSH_FXP_REALPATH, packet, pos);
    if (rc < 0)
        return NULL;

    uint8_t type;
    uint8_t response[2048];
    size_t response_len;

    rc = sftp_recv_packet(sftp, &type, response, &response_len);
    if (rc < 0)
        return NULL;

    if (type != SSH_FXP_NAME || response_len < 12)
        return NULL;

    /* Skip count, read first filename */
    pos = 8;
    if (pos + 4 > response_len)
        return NULL;

    uint32_t name_len = ssh_buf_read_u32(response + pos);
    pos += 4;
    if (pos + name_len > response_len)
        return NULL;

    char *result = malloc(name_len + 1);
    if (result)
    {
        memcpy(result, response + pos, name_len);
        result[name_len] = '\0';
    }

    return result;
}

int sftp_chmod(sftp_session_t *sftp, const char *path, mode_t mode)
{
    sftp_attributes_t attr = {0};
    attr.flags = SFTP_ATTR_PERMISSIONS;
    attr.permissions = mode;
    return sftp_setstat(sftp, path, &attr);
}

int sftp_setstat(sftp_session_t *sftp, const char *path, sftp_attributes_t *attr)
{
    if (!sftp || !path || !attr)
        return SFTP_FAILURE;

    uint8_t packet[1024];
    size_t pos = 0;

    ssh_buf_write_u32(packet + pos, sftp->request_id++);
    pos += 4;

    size_t path_len = strlen(path);
    ssh_buf_write_u32(packet + pos, path_len);
    memcpy(packet + pos + 4, path, path_len);
    pos += 4 + path_len;

    /* attrs */
    ssh_buf_write_u32(packet + pos, attr->flags);
    pos += 4;

    if (attr->flags & SFTP_ATTR_SIZE)
    {
        ssh_buf_write_u32(packet + pos, (attr->size >> 32) & 0xFFFFFFFF);
        ssh_buf_write_u32(packet + pos + 4, attr->size & 0xFFFFFFFF);
        pos += 8;
    }
    if (attr->flags & SFTP_ATTR_UIDGID)
    {
        ssh_buf_write_u32(packet + pos, attr->uid);
        ssh_buf_write_u32(packet + pos + 4, attr->gid);
        pos += 8;
    }
    if (attr->flags & SFTP_ATTR_PERMISSIONS)
    {
        ssh_buf_write_u32(packet + pos, attr->permissions);
        pos += 4;
    }
    if (attr->flags & SFTP_ATTR_ACMODTIME)
    {
        ssh_buf_write_u32(packet + pos, attr->atime);
        ssh_buf_write_u32(packet + pos + 4, attr->mtime);
        pos += 8;
    }

    int rc = sftp_send_packet(sftp, SSH_FXP_SETSTAT, packet, pos);
    if (rc < 0)
        return rc;

    uint8_t type;
    uint8_t response[256];
    size_t response_len;

    rc = sftp_recv_packet(sftp, &type, response, &response_len);
    if (rc < 0)
        return rc;

    if (type == SSH_FXP_STATUS && response_len >= 8)
    {
        return ssh_buf_read_u32(response + 4);
    }

    return SFTP_OK;
}

int sftp_chown(sftp_session_t *sftp, const char *path, uid_t uid, gid_t gid)
{
    sftp_attributes_t attr = {0};
    attr.flags = SFTP_ATTR_UIDGID;
    attr.uid = uid;
    attr.gid = gid;
    return sftp_setstat(sftp, path, &attr);
}

int sftp_utimes(sftp_session_t *sftp, const char *path, uint32_t atime, uint32_t mtime)
{
    sftp_attributes_t attr = {0};
    attr.flags = SFTP_ATTR_ACMODTIME;
    attr.atime = atime;
    attr.mtime = mtime;
    return sftp_setstat(sftp, path, &attr);
}

int sftp_symlink(sftp_session_t *sftp, const char *target, const char *dest)
{
    if (!sftp || !target || !dest)
        return SFTP_FAILURE;

    uint8_t packet[2048];
    size_t pos = 0;

    ssh_buf_write_u32(packet + pos, sftp->request_id++);
    pos += 4;

    size_t target_len = strlen(target);
    ssh_buf_write_u32(packet + pos, target_len);
    memcpy(packet + pos + 4, target, target_len);
    pos += 4 + target_len;

    size_t dest_len = strlen(dest);
    ssh_buf_write_u32(packet + pos, dest_len);
    memcpy(packet + pos + 4, dest, dest_len);
    pos += 4 + dest_len;

    int rc = sftp_send_packet(sftp, SSH_FXP_SYMLINK, packet, pos);
    if (rc < 0)
        return rc;

    uint8_t type;
    uint8_t response[256];
    size_t response_len;

    rc = sftp_recv_packet(sftp, &type, response, &response_len);
    if (rc < 0)
        return rc;

    if (type == SSH_FXP_STATUS && response_len >= 8)
    {
        return ssh_buf_read_u32(response + 4);
    }

    return SFTP_OK;
}

char *sftp_readlink(sftp_session_t *sftp, const char *path)
{
    if (!sftp || !path)
        return NULL;

    uint8_t packet[1024];
    size_t pos = 0;

    ssh_buf_write_u32(packet + pos, sftp->request_id++);
    pos += 4;

    size_t path_len = strlen(path);
    ssh_buf_write_u32(packet + pos, path_len);
    memcpy(packet + pos + 4, path, path_len);
    pos += 4 + path_len;

    int rc = sftp_send_packet(sftp, SSH_FXP_READLINK, packet, pos);
    if (rc < 0)
        return NULL;

    uint8_t type;
    uint8_t response[2048];
    size_t response_len;

    rc = sftp_recv_packet(sftp, &type, response, &response_len);
    if (rc < 0)
        return NULL;

    if (type != SSH_FXP_NAME || response_len < 12)
        return NULL;

    pos = 8;
    if (pos + 4 > response_len)
        return NULL;

    uint32_t name_len = ssh_buf_read_u32(response + pos);
    pos += 4;
    if (pos + name_len > response_len)
        return NULL;

    char *result = malloc(name_len + 1);
    if (result)
    {
        memcpy(result, response + pos, name_len);
        result[name_len] = '\0';
    }

    return result;
}

char *sftp_getcwd(sftp_session_t *sftp)
{
    return sftp_realpath(sftp, ".");
}

char *sftp_canonicalize_path(sftp_session_t *sftp, const char *path)
{
    return sftp_realpath(sftp, path);
}
