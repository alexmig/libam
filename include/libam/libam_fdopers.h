#ifndef _LIBAM_FDOPERS_H_
#define _LIBAM_FDOPERS_H_

#include <sys/socket.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "libam/libam_types.h"

typedef int amskt_t;

typedef union amskt_addr
{
    struct sockaddr s;
    struct sockaddr_in s4;
    struct sockaddr_in6 s6;
    struct sockaddr_storage ss;
} amskt_addr;

// fd io

amrc_t amfd_write(const int fd, const void* buf, const uint32_t len); // Returns 0 on success
amrc_t amfd_read(const int fd, void* buf, const uint32_t len); // Returns 0 on success

// file io (Rread/write entire files)

void amfile_write(const char* filename, const void* data, const uint32_t length);
void* amfile_read(const char* filename, uint32_t* length);

// Sockets (blocking)

amrc_t amskt_connect(const amskt_addr* addr, amskt_t* fd);
amrc_t amskt_listen(const amskt_addr* addr, amskt_t* fd);
amrc_t amskt_accept(const amskt_t server, amskt_t* client);
void amskt_disconnect(amskt_t* fd);
#define amskt_write(skt, buf, len) fd_write((skt), (buf), (len))
#define amskt_read(skt, buf, len) fd_read((skt), (buf), (len))

amrc_t amskt_port2addr(const sa_family_t fam, uint16_t port, amskt_addr* addr);
amrc_t amskt_str2addr(const char *str, uint16_t port, amskt_addr* addr);

#endif
