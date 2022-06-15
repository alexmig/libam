#include <errno.h>
#include <assert.h>
#include <string.h>
#include <arpa/inet.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#include "libam/libam_fdopers.h"
#include "libam/libam_replace.h"

#ifdef DEBUG
#include "libam/libam_logsink.h"
#define DEBUG_PRINT(fmt, args...)	amlog_sink_log(0, 0, fmt, ##args)
#else
#define DEBUG_PRINT(fmt, args...)
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
			DEBUG_PRINT("Error reading, errno %d\n", errno);
			return AMRC_ERROR;
		}
		if (ret == 0) {
			DEBUG_PRINT("Could not finish reading. Read %u/%u bytes\n", olen - len, olen);
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
		DEBUG_PRINT("Failed to open file '%s'\n", filename);
		return;
	}

	rc = amfd_write(fd, data, length);
	close(fd);
	if (rc) {
		DEBUG_PRINT("Failed to write to file file '%s'\n", filename);
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
		DEBUG_PRINT("Failed to open file '%s'\n", filename);
		return NULL;
	}

	file_size = lseek(fd, 0, SEEK_END);
	if (file_size == -1) {
		DEBUG_PRINT("Failed to obtain size of '%s'\n", filename);
		return NULL;
	}

	rc = lseek(fd, 0, SEEK_SET);
	if (file_size == -1) {
		DEBUG_PRINT("Failed to jump to start of '%s'\n", filename);
		return NULL;
	}

	buf = malloc(file_size);
	if (buf == NULL) {
		DEBUG_PRINT("Failed to obtain memory of %ld bytes\n", file_size);
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


amrc_t skt_listen(const amskt_addr* _addr, amskt_t* fd)
{
	amskt_addr addr;
	int skt = -1;
	int rc;
	int enable = 1;
#ifdef DEBUG
	uint16_t port;
#endif

	if (_addr->s.sa_family != AF_INET && _addr->s.sa_family != AF_INET6) {
		DEBUG_PRINT("Address family %u not supported\n", _addr->s.sa_family);
		goto close;
	}
	memcpy(&addr, _addr, sizeof(addr));

	skt = socket(addr.s.sa_family, SOCK_STREAM, 0);
	if (skt == -1) {
		DEBUG_PRINT("Failed to obtain a socket (error %d)\n", errno);
		goto close;
	}

	rc = setsockopt(skt, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
	if (rc != 0) {
		DEBUG_PRINT("Failed to set reuse address option on server socket (error %d)\n",	errno);
		goto close;
	}

	rc = setsockopt(skt, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable));
	if (rc != 0) {
		DEBUG_PRINT("Failed to set reuse port option on server socket (error %d)\n", errno);
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
		DEBUG_PRINT("Error binding to ANY:%u (error %d)\n", port, errno);
		goto close;
	}

	rc = listen(skt, 5);
	if (rc != 0) {
		DEBUG_PRINT("Can't listen on server socket (error %d)\n", errno);
		goto close;
	}

	*fd = skt;
	return AMRC_SUCCESS;

close:
	if (skt >= 0)
		close(skt);
	return AMRC_ERROR;
}

amrc_t skt_connect(const amskt_addr* addr, amskt_t* fd)
{
	int _fd;

	if (addr->s.sa_family != AF_INET && addr->s.sa_family != AF_INET6) {
		DEBUG_PRINT("Address family %u not supported\n", addr->s.sa_family);
		return AMRC_ERROR;
	}

	_fd = socket(addr->s.sa_family, SOCK_STREAM, 0);
	if (_fd < 0) {
		DEBUG_PRINT("Failed to allocate socket (error %d)\n", errno);
		return AMRC_ERROR;
	}

	if (connect(_fd, &addr->s, sizeof(addr->s)) != 0) {
		DEBUG_PRINT("Connect failed to remote socket\n");
		return AMRC_ERROR;
	}

	*fd = _fd;
	DEBUG_PRINT("connected to remote address\n");
	return AMRC_SUCCESS;
}

amrc_t skt_accept(const amskt_t server, amskt_t* client)
{
	struct sockaddr_in client_name;
	socklen_t client_name_len = sizeof(client_name);
	amskt_t cskt;

	*client = -1;

	cskt = accept(server, (struct sockaddr*) &client_name, &client_name_len);
	if (cskt == -1) {
		DEBUG_PRINT("Failed to accept socket (error %d)\n", errno);
		return AMRC_ERROR;
	}

	*client = cskt;
	return AMRC_SUCCESS;
}

void skt_disconnect(amskt_t* fd)
{
	if (close(*fd) != 0) {
		DEBUG_PRINT("Failed to disconnect socket %d\n", *fd);
	}
	*fd = -1;
}

amrc_t skt_port2addr(const sa_family_t fam, uint16_t port, amskt_addr* addr)
{
	assert(addr != NULL);

	if (fam != AF_INET && fam != AF_INET6) {
		DEBUG_PRINT("Address family %u not supported\n", fam);
		goto error;
	}

	if (fam == AF_INET) {
		if (addr->s4.sin_family != AF_INET && addr->s4.sin_family != AF_UNSPEC) {
			DEBUG_PRINT("Re-assigning port to a non-empty address\n");
			goto error;
		}
		addr->s4.sin_family = AF_INET;
		addr->s4.sin_port = htons(port);
		return AMRC_SUCCESS;
	}

	if (fam == AF_INET6) {
		if (addr->s6.sin6_family != AF_INET6 && addr->s6.sin6_family != AF_UNSPEC) {
			DEBUG_PRINT("Re-assigning port to a non-empty address\n");
			goto error;
		}
		addr->s6.sin6_family = AF_INET6;
		addr->s6.sin6_port = htons(port);
		return AMRC_SUCCESS;
	}

error:
	memset(addr, 0, sizeof(*addr));
	addr->s.sa_family = AF_UNSPEC;
	return AMRC_ERROR;
}

amrc_t skt_str2addr(const char *str, uint16_t port, amskt_addr* addr)
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


	DEBUG_PRINT("Failed to resolve '%s' to an address\n", str);
	memset(addr, 0, sizeof(*addr));
	addr->s.sa_family = AF_UNSPEC;
	return AMRC_ERROR;
}
