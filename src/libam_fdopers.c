#include <errno.h>
#include <assert.h>
#include <string.h>
#include <arpa/inet.h>
#include <ctype.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>


#include "libam/libam_fdopers.h"
#include "libam/libam_replace.h"

#ifdef DEBUG
#include "libam/libam_log.h"
#define DEBUG_MASK (1UL << 63)
#define DEBUG_PRINT(fmt, args...)	amlog_sink_log(AMLOG_DEBUG, DEBUG_MASK, fmt, ##args)
#define ERROR_PRINT(fmt, args...)	amlog_sink_log(AMLOG_ERROR, DEBUG_MASK, fmt, ##args)

#else
#define DEBUG_PRINT(fmt, args...)
#define ERROR_PRINT(fmt, args...)
#endif

amrc_t amfd_write(const int fd, const void* buf, const uint32_t len) // Returns 0 on success
{
	return (write(fd, buf, len) == (int32_t)len ? AMRC_SUCCESS : AMRC_ERROR);
}

amrc_t amfd_read(const int fd, void* buf, const uint32_t olen) // Returns 0 on success
{
	uint8_t* b = buf;
	uint32_t len = olen;
	int ret;

	while (len > 0) {
		ret = read(fd, b, len);
		if (ret < 0) {
			ERROR_PRINT("Error reading, errno %d\n", errno);
			return AMRC_ERROR;
		}
		if (ret == 0) {
			ERROR_PRINT("Could not finish reading. Read %u/%u bytes\n", olen - len, olen);
			return AMRC_ERROR;
		}
		len -= ret;
		b += ret;
	}

	return AMRC_SUCCESS;
}

void amfile_write(const char* filename, const void* data, const uint32_t length)
{
	int fd;
	int rc;

	fd = open(filename, O_WRONLY | O_TRUNC | O_CREAT, 0777);
	if (fd < 0) {
		ERROR_PRINT("Failed to open file '%s'\n", filename);
		return;
	}

	rc = amfd_write(fd, data, length);
	close(fd);
	if (rc) {
		ERROR_PRINT("Failed to write to file file '%s'\n", filename);
	}
}

void* amfile_read(const char* filename, uint32_t* length)
{
	int fd;
	int rc;
	off_t file_size;
	uint8_t* buf;

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		ERROR_PRINT("Failed to open file '%s'\n", filename);
		return NULL;
	}

	file_size = lseek(fd, 0, SEEK_END);
	if (file_size == -1) {
		ERROR_PRINT("Failed to obtain size of '%s'\n", filename);
		return NULL;
	}

	rc = lseek(fd, 0, SEEK_SET);
	if (file_size == -1) {
		ERROR_PRINT("Failed to jump to start of '%s'\n", filename);
		return NULL;
	}

	buf = malloc(file_size);
	if (buf == NULL) {
		ERROR_PRINT("Failed to obtain memory of %ld bytes\n", file_size);
		return NULL;
	}

	rc = amfd_read(fd, buf, file_size);
	close(fd);
	if (rc) {
		free(buf);
		return NULL;
	}

	if (length != NULL)
		*length = file_size;

	return buf;
}


amrc_t amskt_listen(const amskt_addr* _addr, amskt_t* fd)
{
	amskt_addr addr;
	int skt = -1;
	int rc;
	int enable = 1;
#ifdef DEBUG
	uint16_t port;
#endif

	if (_addr->s.sa_family != AF_INET && _addr->s.sa_family != AF_INET6) {
		ERROR_PRINT("Address family %u not supported\n", _addr->s.sa_family);
		goto close;
	}
	memcpy(&addr, _addr, sizeof(addr));

	skt = socket(addr.s.sa_family, SOCK_STREAM, 0);
	if (skt == -1) {
		ERROR_PRINT("Failed to obtain a socket (error %d)\n", errno);
		goto close;
	}

	rc = setsockopt(skt, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
	if (rc != 0) {
		ERROR_PRINT("Failed to set reuse address option on server socket (error %d)\n",	errno);
		goto close;
	}

	rc = setsockopt(skt, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable));
	if (rc != 0) {
		ERROR_PRINT("Failed to set reuse port option on server socket (error %d)\n", errno);
		goto close;
	}

	if (addr.s.sa_family == AF_INET) {
		addr.s4.sin_addr.s_addr = INADDR_ANY;
#ifdef DEBUG
		port = ntohs(addr.s4.sin_port);
#endif
	}
	else {
		addr.s6.sin6_addr = in6addr_any;
#ifdef DEBUG
		port = ntohs(addr.s6.sin6_port);
#endif
	}

	rc = bind(skt, &addr.s, sizeof(addr.s));
	if (rc != 0) {
		ERROR_PRINT("Error binding to ANY:%u (error %d)\n", port, errno);
		goto close;
	}

	rc = listen(skt, 5);
	if (rc != 0) {
		ERROR_PRINT("Can't listen on server socket (error %d)\n", errno);
		goto close;
	}

	*fd = skt;
	return AMRC_SUCCESS;

close:
	if (skt >= 0)
		close(skt);
	return AMRC_ERROR;
}

amrc_t amskt_connect(const amskt_addr* addr, amskt_t* fd)
{
	int _fd;

	if (addr->s.sa_family != AF_INET && addr->s.sa_family != AF_INET6) {
		ERROR_PRINT("Address family %u not supported\n", addr->s.sa_family);
		return AMRC_ERROR;
	}

	_fd = socket(addr->s.sa_family, SOCK_STREAM, 0);
	if (_fd < 0) {
		ERROR_PRINT("Failed to allocate socket (error %d)\n", errno);
		return AMRC_ERROR;
	}

	if (connect(_fd, &addr->s, sizeof(addr->s)) != 0) {
		ERROR_PRINT("Connect failed to remote socket\n");
		return AMRC_ERROR;
	}

	*fd = _fd;
	DEBUG_PRINT("connected to remote address\n");
	return AMRC_SUCCESS;
}

void amskt_disconnect(amskt_t* fd)
{
	if (close(*fd) != 0) {
		ERROR_PRINT("Failed to disconnect socket %d\n", *fd);
	}
	*fd = -1;
}

amrc_t amskt_port2addr(const sa_family_t fam, uint16_t port, amskt_addr* addr)
{
	assert(addr != NULL);

	if (fam == AF_INET) {
		if (addr->s4.sin_family != AF_INET && addr->s4.sin_family != AF_UNSPEC) {
			ERROR_PRINT("Re-assigning port to a non-empty address\n");
			goto error;
		}
		addr->s4.sin_family = AF_INET;
		addr->s4.sin_port = htons(port);
		return AMRC_SUCCESS;
	}

	if (fam == AF_INET6) {
		if (addr->s6.sin6_family != AF_INET6 && addr->s6.sin6_family != AF_UNSPEC) {
			ERROR_PRINT("Re-assigning port to a non-empty address\n");
			goto error;
		}
		addr->s6.sin6_family = AF_INET6;
		addr->s6.sin6_port = htons(port);
		return AMRC_SUCCESS;
	}

	ERROR_PRINT("Address family %u not supported\n", fam);
error:
	memset(addr, 0, sizeof(*addr));
	addr->s.sa_family = AF_UNSPEC;
	return AMRC_ERROR;
}

amrc_t amskt_str2addr(const char *str, uint16_t port, amskt_addr* addr)
{
	int rc;

	assert(addr != NULL);

	rc = inet_pton(AF_INET, str, &addr->s4.sin_addr);
	if (rc == 1) {
		addr->s4.sin_family = AF_INET;
		addr->s4.sin_port = htons(port);
		return AMRC_SUCCESS;
	}

	rc = inet_pton(AF_INET6, str, &addr->s6.sin6_addr);
	if (rc == 1) {
		addr->s6.sin6_family = AF_INET6;
		addr->s6.sin6_port = htons(port);
		return AMRC_SUCCESS;
	}


	ERROR_PRINT("Failed to resolve '%s' to an address\n", str);
	memset(addr, 0, sizeof(*addr));
	addr->s.sa_family = AF_UNSPEC;
	return AMRC_ERROR;
}



/* Set the port of a socket
 * family - AF_UNSPEC, AF_INET, AF_INET6. If AF_UNSPEC, addr must already be initialized with the correct family.
 * Returns AMRC_SUCCESS / AMRC_ERROR on invalid arguments */
amrc_t amskt_addr_set_port(amskt_addr* addr, const sa_family_t family, const uint16_t port)
{
	if (addr == NULL || (family != AF_UNSPEC && family != AF_INET && family != AF_INET6)) {
		ERROR_PRINT("Invalid argument(s)\n");
		return AMRC_ERROR;
	}

	if (family == AF_UNSPEC && addr->s.sa_family != AF_INET && addr->s.sa_family != AF_INET6) {
		ERROR_PRINT("Socket address not set with supported family and parameter unspecified\n");
		return AMRC_ERROR;
	}

	if (addr->s.sa_family == AF_UNSPEC && family != AF_UNSPEC) {
		memset(addr, 0, sizeof(*addr));
		addr->s.sa_family = family;
	}

	if (family != AF_UNSPEC && family != addr->s.sa_family) {
		ERROR_PRINT("Mismatch between specified family %d and socket family %d\n", family, addr->s.sa_family);
		return AMRC_ERROR;
	}

	if (addr->s.sa_family == AF_INET) {
		addr->s4.sin_port = port;
		return AMRC_SUCCESS;
	}

	if (addr->s.sa_family == AF_INET6) {
		addr->s6.sin6_port = port;
		return AMRC_SUCCESS;
	}

	ERROR_PRINT("Unexpected socket family %d\n", addr->s.sa_family);
	return AMRC_ERROR;
}

/* Ignore case, legnth-terminated compare */
static int strnicmp(const char* s1, const char* s2, size_t num)
{
	size_t i;
	int diff = 0;

	if (s1 == NULL || s2 == NULL)
		return s1 - s2;

	for (i = 0; i < num; i++) {
		diff = tolower(s1[i]) - tolower(s2[i]);
		if (diff != 0 || !s1[i])
			return diff;
	}
	return diff;
}

amrc_t amskt_addr_to_ipstr(const amskt_addr* addr, char* out, uint32_t out_len)
{
	if (addr == NULL || out == NULL || out_len == 0) {
		ERROR_PRINT("Must provide all parameters\n");
		return AMRC_ERROR;
	}

	if (addr->s.sa_family == AF_INET) {
		inet_ntop(AF_INET, &addr->s4.sin_addr, out, out_len);
	}
	else if (addr->s.sa_family == AF_INET6) {
		inet_ntop(AF_INET6, &addr->s6.sin6_addr, out, out_len);
	}
	else {
		ERROR_PRINT("Address family %d not supported\n", addr->s.sa_family);
		return AMRC_ERROR;
	}
	out[out_len - 1] = '\0';
	return AMRC_SUCCESS;
}

amrc_t amskt_addr_to_portstr(const amskt_addr* addr, char* out, uint32_t out_len)
{
	uint16_t port;

	if (addr == NULL || out == NULL || out_len == 0) {
		ERROR_PRINT("Must provide all parameters\n");
		return AMRC_ERROR;
	}

	if (addr->s.sa_family == AF_INET)
		port = addr->s4.sin_port;
	else if (addr->s.sa_family == AF_INET6)
		port = addr->s6.sin6_port;
	else {
		ERROR_PRINT("Address family %d not supported\n", addr->s.sa_family);
		return AMRC_ERROR;
	}
	snprintf(out, out_len, "%u", port);
	out[out_len - 1] = '\0';
	return AMRC_SUCCESS;
}

amrc_t amskt_addr_to_str(const amskt_addr* addr, char* out, uint32_t out_len)
{
	char ipstr[INET6_ADDRSTRLEN];
	uint16_t port;

	if (addr == NULL || out == NULL || out_len == 0) {
		ERROR_PRINT("Must provide all parameters\n");
		return AMRC_ERROR;
	}

	if (addr->s.sa_family == AF_INET) {
		port = addr->s4.sin_port;
		inet_ntop(AF_INET, &addr->s4.sin_addr, ipstr, sizeof(ipstr));
	}
	else if (addr->s.sa_family == AF_INET6) {
		port = addr->s6.sin6_port;
		inet_ntop(AF_INET6, &addr->s6.sin6_addr, ipstr, sizeof(ipstr));
	}
	else {
		ERROR_PRINT("Address family %d not supported\n", addr->s.sa_family);
		return AMRC_ERROR;
	}
	ipstr[sizeof(ipstr) - 1] = '\0';
	snprintf(out, out_len, "%s:%u", ipstr, port);
	out[out_len - 1] = '\0';
	return AMRC_SUCCESS;
}

uint16_t amskt_addr_to_port(const amskt_addr* addr)
{
	if (addr == NULL) {
		ERROR_PRINT("Must provide input parameters\n");
		return 0;
	}
	if (addr->s.sa_family == AF_INET) {
		return addr->s4.sin_port;
	}
	if (addr->s.sa_family == AF_INET6) {
		return addr->s6.sin6_port;
	}
	ERROR_PRINT("Address family %d not supported\n", addr->s.sa_family);
	return 0;
}

/* Returns the most likely address of a destination
 * addr - hostname, IPv4, IPv6 (Must match socket protocol)
 * port - port number. Cannot be 0.
 * Returns AMRC_SUCCESS / AMRC_ERROR */
amrc_t amskt_query_to_addr(const char *addr, uint16_t port, amskt_addr* out_addr)
{
	struct addrinfo hints;
	struct addrinfo* servinfo = NULL;
	struct addrinfo* cur;
	int rc;
	amrc_t ret = AMRC_ERROR;
	amskt_addr* pskt_addr;

	if (addr == NULL || *addr == '\0') {
		ERROR_PRINT("Must provide an address to query\n");
		return AMRC_ERROR;
	}

	if (port == 0) {
		ERROR_PRINT("Must provide a positive port number\n");
		return AMRC_ERROR;
	}

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_flags = AI_NUMERICSERV;

	rc = getaddrinfo(addr, "12345", &hints, &servinfo); /* Dummy port, we ignore that */
	if (rc != 0) {
		ERROR_PRINT("Encountered error during getaddrinfo: %s\n", gai_strerror(rc));
		return AMRC_ERROR;
	}

	for (cur = servinfo; cur != NULL; cur = cur->ai_next) {
		pskt_addr = (amskt_addr*)cur->ai_addr;
		if (pskt_addr->s.sa_family == AF_INET) {
			pskt_addr->s4.sin_port = port;
		}
		else if (pskt_addr->s.sa_family == AF_INET6) {
			pskt_addr->s6.sin6_port = port;
		}
		else {
			ERROR_PRINT("amskt_query_to_addr does not support address family %d\n", cur->ai_family);
			continue;
		}

		memcpy(out_addr, cur->ai_addr, cur->ai_addrlen);
		ret = AMRC_SUCCESS;
		break;
	}

	if (ret != AMRC_SUCCESS) {
		ERROR_PRINT("Ran out of potential interfaces to try %s:%u\n", addr, port);
	}

	if (servinfo)
		freeaddrinfo(servinfo);
	return ret;
}

/* Populates out_addr with details of the local end of an open socket skt
 * Returns AMRC_SUCCESS / AMRC_ERROR */
amrc_t amskt_local_to_addr(amskt_t skt, amskt_addr* out_addr)
{
	int rc;
	socklen_t len;

	if (skt < 0 || out_addr == NULL) {
		ERROR_PRINT("Invalid socket %d or out addr %p\n", skt, out_addr);
		return AMRC_ERROR;
	}

	len = sizeof(out_addr->s);
	rc = getsockname(skt, &out_addr->s, &len);
	if (rc != 0) {
		ERROR_PRINT("Failed to obtain local socket %d information (error %d, %s)\n", skt, errno, strerror(errno));
		return AMRC_ERROR;
	}
	return AMRC_SUCCESS;
}

/* Populates out_addr with details of the remote end of an open, connected socket skt
 * Returns AMRC_SUCCESS / AMRC_ERROR */
amrc_t amskt_remote_to_addr(amskt_t skt, amskt_addr* out_addr)
{
	int rc;
	socklen_t len;

	if (skt < 0 || out_addr == NULL) {
		ERROR_PRINT("Invalid socket %d or out addr %p\n", skt, out_addr);
		return AMRC_ERROR;
	}

	len = sizeof(out_addr->s);
	rc = getpeername(skt, &out_addr->s, &len);
	if (rc != 0) {
		ERROR_PRINT("Failed to obtain remote socket %d information (error %d, %s)\n", skt, errno, strerror(errno));
		return AMRC_ERROR;
	}
	return AMRC_SUCCESS;
}


/* Obtain a bound server socket.
 * family - AF_UNSPEC, AF_INET, AF_INET6
 * type - SOCK_STREAM (for TCP), SOCK_DGRAM (for UDP). TCP should be used to accept new connections, UDP should be used to receive data directly.
 * addr - Either NULL/"ANY" (which would result in binding the primary IP of host) or a specific ip ("127.0.0.1"). IPv4 IPv6 agnostic.
 * port - Port to use when binding 0 mean allocating a temporary one.
 * Returns a socket descriptor upon success / -1 on errors */
amskt_t amskt_get_server_socket(int family, int type, const char *addr, uint16_t port)
{
	struct addrinfo hints;
	struct addrinfo* servinfo = NULL;
	struct addrinfo* cur;
	char ipstr[INET6_ADDRSTRLEN];
	amskt_addr* pskt_addr = NULL;
	amskt_t skt;
	int enable = 1;
	int rc;
	const char* _addr = addr;

	ipstr[0] = '\0';

	if (family != AF_UNSPEC && family != AF_INET && family != AF_INET6) {
		ERROR_PRINT("Invalid family value %d. Must be either one of AF_UNSPEC, AF_INET, AF_INET6\n", family);
		return -1;
	}

	if (type != SOCK_STREAM && type != SOCK_DGRAM) {
		ERROR_PRINT("Invalid type value %d. Must be either one of SOCK_STREAM, SOCK_DGRAM\n", type);
		return -1;
	}

	memset(&hints, 0, sizeof hints);
	hints.ai_family = family;
	hints.ai_socktype = type;
	hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST | AI_NUMERICSERV;

	if (strnicmp("ANY", addr, 4) == 0) {
		_addr = NULL;
	}

	rc = getaddrinfo(_addr, "12345", &hints, &servinfo); /* Dummy port, we ignore that */
	if (rc != 0) {
		ERROR_PRINT("Encountered error during getaddrinfo: %s\n", gai_strerror(rc));
		return -1;
	}

	skt = -1;
	for (cur = servinfo; cur != NULL; cur = cur->ai_next) {
		pskt_addr = (amskt_addr*)cur->ai_addr;
		if (pskt_addr->s.sa_family == AF_INET) {
			pskt_addr->s4.sin_port = port;
			inet_ntop(AF_INET, &pskt_addr->s4.sin_addr, ipstr, sizeof(ipstr));
		}
		else {
			pskt_addr->s6.sin6_port = port;
			inet_ntop(AF_INET6, &pskt_addr->s6.sin6_addr, ipstr, sizeof(ipstr));
		}
		ipstr[sizeof(ipstr) - 1] = '\0';

		DEBUG_PRINT("Trying %s:%u (family %d, type %d, proto %d, addr %s)\n", ipstr, port, cur->ai_family, cur->ai_socktype, cur->ai_protocol, addr);

		skt = socket(cur->ai_family, cur->ai_socktype, cur->ai_protocol);
		if (skt < 0) {
			ERROR_PRINT("Encountered error when obtaining socket [%d, %d, %d] (error %d, %s)\n", cur->ai_family, cur->ai_socktype, cur->ai_protocol, errno, strerror(errno));
			skt = -1;
			continue;
		}

		rc = setsockopt(skt, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
		if (rc != 0) {
			ERROR_PRINT("Failed to set reuse address option on server socket socket %s:%d (error %d, %s)\n", ipstr, port, errno, strerror(errno));
			close(skt);
			skt = -1;
			continue;
		}

		rc = setsockopt(skt, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable));
		if (rc != 0) {
			ERROR_PRINT("Failed to set reuse port option on server socket socket %s:%d (error %d, %s)\n", ipstr, port, errno, strerror(errno));
			close(skt);
			skt = -1;
			continue;
		}

		rc = bind(skt, cur->ai_addr, cur->ai_addrlen);
		if (rc == -1) {
			ERROR_PRINT("Failed to bind socket %s:%d (error %d, %s)\n", ipstr, port, errno, strerror(errno));
			close(skt);
			skt = -1;
			continue;
		}

		if (cur->ai_socktype != SOCK_DGRAM) {
			rc = listen(skt, 15);
			if (rc == -1) {
				ERROR_PRINT("Failed to listen on socket %s:%d (error %d, %s)\n", ipstr, port, errno, strerror(errno));
				close(skt);
				skt = -1;
				continue;
			}
		}

		/* All good! */
		DEBUG_PRINT("Successfully listening on %s:%u, id %d\n", ipstr, port, skt);
		break;
	}
	if (skt == -1) {
		ERROR_PRINT("Ran out of potential interfaces to try %s:%u\n", ipstr, port);
	}

	if (servinfo)
		freeaddrinfo(servinfo);
	return skt;
}

/* Obtains a connected client socket.
 * family - AF_UNSPEC, AF_INET, AF_INET6
 * type - SOCK_STREAM (for TCP), SOCK_DGRAM (for UDP).
 * addr - hostname, IPv4, IPv6
 * port - port number. Cannot be 0.
 * Returns a connected socket upon success / -1 on errors */
amskt_t amskt_get_client_socket(int family, int type, const char *addr, uint16_t port)
{
	struct addrinfo hints;
	struct addrinfo* servinfo = NULL;
	struct addrinfo* cur;
	char ipstr[INET6_ADDRSTRLEN];
	amskt_addr* pskt_addr = NULL;
	amskt_t skt;
	int rc;

	ipstr[0] = '\0';

	if (family != AF_UNSPEC && family != AF_INET && family != AF_INET6) {
		ERROR_PRINT("Invalid family value %d. Must be either one of AF_UNSPEC, AF_INET, AF_INET6\n", family);
		return -1;
	}

	if (type != SOCK_STREAM && type != SOCK_DGRAM) {
		ERROR_PRINT("Invalid type value %d. Must be either one of SOCK_STREAM, SOCK_DGRAM\n", type);
		return -1;
	}

	if (addr == NULL || *addr == '\0') {
		ERROR_PRINT("Must provide an address to connect to\n");
		return -1;
	}

	if (port == 0) {
		ERROR_PRINT("Must provide a positive port number\n");
		return -1;
	}

	memset(&hints, 0, sizeof hints);
	hints.ai_family = family;
	hints.ai_socktype = type;
	hints.ai_flags = AI_NUMERICSERV;

	rc = getaddrinfo(addr, "12345", &hints, &servinfo); /* Dummy port, we ignore that */
	if (rc != 0) {
		ERROR_PRINT("Encountered error during getaddrinfo: %s\n", gai_strerror(rc));
		return -1;
	}

	skt = -1;
	for (cur = servinfo; cur != NULL; cur = cur->ai_next) {
		pskt_addr = (amskt_addr*)cur->ai_addr;
		if (pskt_addr->s.sa_family == AF_INET) {
			pskt_addr->s4.sin_port = port;
			inet_ntop(AF_INET, &pskt_addr->s4.sin_addr, ipstr, sizeof(ipstr));
		}
		else {
			pskt_addr->s6.sin6_port = port;
			inet_ntop(AF_INET6, &pskt_addr->s6.sin6_addr, ipstr, sizeof(ipstr));
		}
		ipstr[sizeof(ipstr) - 1] = '\0';

		DEBUG_PRINT("Trying to connect to %s:%u (family %d, type %d, proto %d, addr %s)\n", ipstr, port, cur->ai_family, cur->ai_socktype, cur->ai_protocol, addr);

		skt = socket(cur->ai_family, cur->ai_socktype, cur->ai_protocol);
		if (skt < 0) {
			ERROR_PRINT("Encountered erro when obtaining socket [%d, %d, %d] (error %d, %s)\n", cur->ai_family, cur->ai_socktype, cur->ai_protocol, errno, strerror(errno));
			skt = -1;
			continue;
		}

		rc = connect(skt, cur->ai_addr, cur->ai_addrlen);
		if (rc == -1) {
			ERROR_PRINT("Failed to connect to socket %s:%d (error %d, %s)\n", ipstr, port, errno, strerror(errno));
			close(skt);
			skt = -1;
			continue;
		}

		/* All good! */
		DEBUG_PRINT("Successfully connected to %s:%u, id %d\n", ipstr, port, skt);
		break;
	}
	if (skt == -1) {
		ERROR_PRINT("Ran out of potential interfaces to try %s:%u\n", ipstr, port);
	}

	if (servinfo)
		freeaddrinfo(servinfo);
	return skt;
}

/* Sets blocking property of socket
 * Returns AMRC_SUCCESS / AMRC_ERROR */
amrc_t amskt_set_blocking(amskt_t skt, ambool_t should_block)
{
	int rc;

	if (skt < 0) {
		ERROR_PRINT("Invalid socket %d\n", skt);
		return AMRC_ERROR;
	}

	rc = fcntl(skt, F_GETFL);
	if (rc < 0) {
		ERROR_PRINT("Failed to obtain current flags (error %d, %s)\n", errno, strerror(errno));
		return AMRC_ERROR;
	}

	rc = (should_block ? (rc & ~O_NONBLOCK) : (rc | O_NONBLOCK)); /* Set blockig flag according to value */
	rc = fcntl(skt, F_SETFL, rc);
	if (rc != 0) {
		ERROR_PRINT("Failed to set blocking flag (error %d, %s)\n", errno, strerror(errno));
		return AMRC_ERROR;
	}

	return AMRC_SUCCESS;
}

/* Accept a connection and store it to update <client>.
 * If <remote_addr> is not NULL, populate with remote client's address
 * Returns AMRC_SUCCESS / AMRC_ERROR on either an error or an empty, non-blocking socket */
amrc_t amskt_accept(const amskt_t server, amskt_t* client, amskt_addr* remote_addr)
{
	socklen_t client_name_len;
	socklen_t* plen;
	amskt_t cskt;

	if (client == NULL) {
		ERROR_PRINT("Must provide client socket\n");
		return AMRC_ERROR;
	}
	*client = -1;

	if (remote_addr == NULL) {
		plen = NULL;
	}
	else {
		client_name_len = sizeof(remote_addr->s);
		plen = &client_name_len;
	}

	_Static_assert(((void*)&((amskt_addr*)0)->s) == ((void*)0), "Drift in structure detected");
	cskt = accept(server, &remote_addr->s, plen);
	if (cskt == -1) {
		if (errno != EWOULDBLOCK && errno != EAGAIN) {
			ERROR_PRINT("Failed to accept socket (error %d, %s)\n", errno, strerror(errno));
		}
		return AMRC_ERROR;
	}

	*client = cskt;
	return AMRC_SUCCESS;
}


/* Send exactly <length> bytes from <buffer>, resume if the error is any one of <ignore>
 * remote - Ignored for TCP communications
 * WARNING: This does not check AMSKT_IGNORE_WOULDBLOCK against the socket flags.
 * Returns exactly <length> / Anything less than <length> when a non-ignored error encountered */
uint32_t amskt_send(const amskt_t skt, const void* buffer, const uint32_t length, const amskt_addr* remote, const amskt_ignore_t ignore)
{
	uint32_t remaining = length;
	const uint8_t* buf = buffer;
	const struct sockaddr* addr = (remote != NULL ? &remote->s : NULL);
	socklen_t addr_len = sizeof(amskt_addr);
	int ret;

	while (remaining > 0) {
		errno = 0;
		ret = sendto(skt, buf, remaining, 0, addr, addr_len);
		if (ret <= 0) {
			if ((errno == EWOULDBLOCK || errno == EAGAIN) && (ignore & AMSKT_IGNORE_WOULDBLOCK))
				continue;
			else if ((errno == EINTR) && (ignore & AMSKT_IGNORE_INTER))
				continue;
			else {
				ERROR_PRINT("Error sending socket %d at %u/%u bytes (error %d, %s)\n", skt, length - remaining, length, errno, strerror(errno));
				return length - remaining;
			}
		}
		remaining -= ret;
		buf += ret;

		if (!(ignore & AMSKT_IGNORE_PACKET))
			return length - remaining;
	}

	return length;
}

/* Receive exactly <length> bytes into <buffer>, resume if the error is any one of <ignore>
 * remote - Ignored for TCP communications
 * The way to distinguish a closed socket from an interrupted (if not ignored) one is via <errno>, which is preserved.
 * WARNING: This does not check AMSKT_IGNORE_WOULDBLOCK against the socket flags.
 * WARNING: When using UDP, any ignored state my combine different source addresses. Caution.
 * Returns exactly <length> / Anything less than <length> when a non-ignored error encountered */
uint32_t amskt_recv(const amskt_t skt, void* buffer, const uint32_t length, amskt_addr* remote, const amskt_ignore_t ignore)
{
	uint32_t remaining = length;
	uint8_t* buf = buffer;
	socklen_t addr_len = sizeof(amskt_addr);
	socklen_t* paddr_len = (remote != NULL ? &addr_len : NULL);
	struct sockaddr* addr = (remote != NULL ? &remote->s : NULL);
	int ret;

	while (remaining > 0) {
		errno = 0;
		addr_len = sizeof(struct sockaddr);
		ret = recvfrom(skt, buf, remaining, 0, addr, paddr_len);
		if (ret == 0) {
			DEBUG_PRINT("Socket %d closed after reading %u/%u bytes\n", skt, length - remaining, length);
			return length - remaining;
		}
		if (ret < 0) {
			if ((errno == EWOULDBLOCK || errno == EAGAIN) && (ignore & AMSKT_IGNORE_WOULDBLOCK))
				continue;
			else if ((errno == EINTR) && (ignore & AMSKT_IGNORE_INTER))
				continue;
			else {
				ERROR_PRINT("Error receiving socket %d at %u/%u bytes (error %d, %s)\n", skt, length - remaining, length, errno, strerror(errno));
				return length - remaining;
			}
		}
		remaining -= ret;
		buf += ret;

		if (!(ignore & AMSKT_IGNORE_PACKET))
			return length - remaining;
	}

	return length;
}
