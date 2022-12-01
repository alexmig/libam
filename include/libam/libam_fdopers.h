#ifndef _LIBAM_FDOPERS_H_
#define _LIBAM_FDOPERS_H_

#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "libam/libam_types.h"

typedef int amskt_t;
//_Static_assert(sizeof(amskt_t) > sizeof(int), "In order to function amskt mush exceed the size of a file descriptor");

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
void amskt_disconnect(amskt_t* fd);
#define amskt_write(skt, buf, len) amfd_write((skt), (buf), (len))
#define amskt_read(skt, buf, len) amfd_read((skt), (buf), (len))

amrc_t amskt_port2addr(const sa_family_t fam, uint16_t port, amskt_addr* addr);
amrc_t amskt_str2addr(const char *str, uint16_t port, amskt_addr* addr);



typedef enum amskt_ignore {
	AMSKT_IGNORE_NONE = 0 << 0,
	AMSKT_IGNORE_WOULDBLOCK = 1 << 0,
	AMSKT_IGNORE_INTER = 1 << 1,
	AMSKT_IGNORE_PACKET = 1 << 2, /* Ignore packet boundaries, do not return */
} amskt_ignore_t;

/* Set the port of a socket
 * family - AF_UNSPEC, AF_INET, AF_INET6. If AF_UNSPEC, addr must already be initialized with the correct family.
 * Returns AMRC_SUCCESS / AMRC_ERROR on invalid arguments */
amrc_t amskt_addr_set_port(amskt_addr* addr, const sa_family_t family, const uint16_t port);

amrc_t amskt_addr_to_ipstr(const amskt_addr* addr, char* out, uint32_t out_len);
amrc_t amskt_addr_to_portstr(const amskt_addr* addr, char* out, uint32_t out_len);
amrc_t amskt_addr_to_str(const amskt_addr* addr, char* out, uint32_t out_len);
uint16_t amskt_addr_to_port(const amskt_addr* addr);

/* Returns the most likely address of a destination
 * addr - hostname, IPv4, IPv6 (Must match socket protocol)
 * port - port number. Cannot be 0.
 * Returns AMRC_SUCCESS / AMRC_ERROR */
amrc_t amskt_query_to_addr(const char *addr, uint16_t port, amskt_addr* out_addr);

/* Populates out_addr with details of the local end of an open socket skt
 * Returns AMRC_SUCCESS / AMRC_ERROR */
amrc_t amskt_local_to_addr(amskt_t skt, amskt_addr* out_addr);

/* Populates out_addr with details of the remote end of an open, connected socket skt
 * Returns AMRC_SUCCESS / AMRC_ERROR */
amrc_t amskt_remote_to_addr(amskt_t skt, amskt_addr* out_addr);

/* Obtain a bound server socket.
 * family - AF_UNSPEC, AF_INET, AF_INET6
 * type - SOCK_STREAM (for TCP), SOCK_DGRAM (for UDP). TCP should be used to accept new connections, UDP should be used to receive data directly.
 * addr - Either NULL/"ANY" (which would result in binding the primary IP of host) or a specific ip ("127.0.0.1"). IPv4 IPv6 agnostic.
 * port - Port to use when binding 0 mean allocating a temporary one.
 * Returns a socket descriptor upon success / -1 on errors */
amskt_t amskt_get_server_socket(int family, int type, const char *addr, uint16_t port);

/* Obtains a connected client socket.
 * family - AF_UNSPEC, AF_INET, AF_INET6
 * type - SOCK_STREAM (for TCP), SOCK_DGRAM (for UDP).
 * addr - hostname, IPv4, IPv6
 * port - port number. Cannot be 0.
 * Returns a connected socket upon success / -1 on errors */
amskt_t amskt_get_client_socket(int family, int type, const char *addr, uint16_t port);

/* Sets blocking property of socket
 * Returns AMRC_SUCCESS / AMRC_ERROR */
amrc_t amskt_set_blocking(amskt_t skt, ambool_t should_block);

/* To use sockets, feel free to make use of:
 * send()
 * sendto()
 * sendmsg()
 * recv()
 * recvfrom()
 * recvmsg()
 */

/* Accept a connection and store it to update <client>.
 * If <remote_addr> is not NULL, populate with remote client's address
 * Returns AMRC_SUCCESS / AMRC_ERROR on either an error or an empty, non-blocking socket */
amrc_t amskt_accept(const amskt_t server, amskt_t* client, amskt_addr* remote_addr);

/* Send exactly <length> bytes from <buffer>, resume if the error is any one of <ignore>
 * remote - Ignored for TCP communications
 * WARNING: This does not check AMSKT_IGNORE_WOULDBLOCK against the socket flags.
 * Returns exactly <length> / Anything less than <length> when a non-ignored error encountered */
uint32_t amskt_send(const amskt_t skt, const void* buffer, const uint32_t length, const amskt_addr* remote, const amskt_ignore_t ignore);

/* Receive exactly <length> bytes into <buffer>, resume if the error is any one of <ignore>
 * remote - Ignored for TCP communications
 * The way to distinguish a closed socket from an interrupted (if not ignored) one is via <errno>, which is preserved.
 * WARNING: This does not check AMSKT_IGNORE_WOULDBLOCK against the socket flags.
 * WARNING: When using UDP, any ignored state my combine different source addresses. Caution.
 * Returns exactly <length> / Anything less than <length> when a non-ignored error encountered */
uint32_t amskt_recv(const amskt_t skt, void* buffer, const uint32_t length, amskt_addr* remote, const amskt_ignore_t ignore);

#endif
