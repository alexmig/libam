#include <pthread.h>
#include <stdlib.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <net/if.h>

#include "test_base.h"

#include "libam/libam_log.h"
#include "libam/libam_types.h"
#include "libam/libam_atomic.h"
#include "libam/libam_fdopers.h"
#include "libam/libam_replace.h"

#include <stdio.h>

typedef struct server_args {
	volatile uint64_t up;
	volatile uint64_t* err;
	volatile ambool_t keep_running;
	char name[64];
	amskt_t fd;
	amskt_addr addr;
} server_args_t;

typedef struct default_interfaces {
	ambool_t	is_working[2];
	amskt_addr	addrs[2];
	uint32_t	count;
} default_interfaces_t;

static default_interfaces_t working_interfaces = { .is_working = { am_false, am_false, }, .count = 0 };


#ifdef NDEBUG
#include <stdio.h>
#undef assert
#define assert(cond) do {if (!(cond)) { fprintf(stderr, "Assertion '" #cond "' failed at %s:%d\n", __FILE__, __LINE__); fflush(stderr); abort(); }} while(0)
#else
#include <assert.h>
#endif

#ifdef err
#undef err
#endif
#ifdef log
#undef log
#endif
#define err(fmt, args...) amlog_sink_log(AMLOG_ERROR, 0, fmt, ##args)
#define log(fmt, args...) amlog_sink_log(AMLOG_DEBUG, 0, fmt, ##args)

static amrc_t get_any_interface(int family, amskt_addr* out)
{
	struct ifaddrs *ifap, *ifa;
	amrc_t ret = AMRC_ERROR;

	if (out == NULL) {
		fprintf(stderr, "ERROR: Must provide output parameters\n");
		return AMRC_ERROR;
	}

	if (family != AF_INET && family != AF_INET6) {
		fprintf(stderr, "ERROR: Unsupported family %d\n", family);
		return AMRC_ERROR;
	}

	getifaddrs (&ifap);
	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != family)
			continue;
		if (!(ifa->ifa_flags & IFF_RUNNING) || !(ifa->ifa_flags & IFF_UP))
			continue;

		if (family == AF_INET)
			memcpy(&out->s4, ifa->ifa_addr, sizeof(out->s4));
		else /* AF_INET6 */
			memcpy(&out->s6, ifa->ifa_addr, sizeof(out->s6));
		ret = AMRC_SUCCESS;
		break;
	}

	freeifaddrs(ifap);
	return ret;
}

static void set_working_interfaces()
{
	amrc_t rc;
	char buff[INET6_ADDRSTRLEN + 1];

	rc = get_any_interface(AF_INET, &working_interfaces.addrs[0]);
	if (rc == AMRC_SUCCESS) {
		working_interfaces.is_working[0] = am_true;
		working_interfaces.count++;
		amskt_addr_to_str(&working_interfaces.addrs[0], buff, sizeof(buff));
		log("Default interface IP for AF_INET set to '%s'\n", buff);
	}

	rc = get_any_interface(AF_INET6, &working_interfaces.addrs[1]);
	if (rc == AMRC_SUCCESS) {
		working_interfaces.is_working[1] = am_true;
		working_interfaces.count++;
		amskt_addr_to_str(&working_interfaces.addrs[1], buff, sizeof(buff));
		log("Default interface IP for AF_INET6 set to '%s'\n", buff);
	}
}

static amrc_t get_default_interface_addr(int family, amskt_addr* out)
{
	if (family == AF_INET) {
		if (working_interfaces.is_working[0]) {
			memcpy(out, &working_interfaces.addrs[0], sizeof(*out));
			return AMRC_SUCCESS;
		}
	}
	if (family == AF_INET6) {
		if (working_interfaces.is_working[1]) {
			memcpy(out, &working_interfaces.addrs[1], sizeof(*out));
			return AMRC_SUCCESS;
		}
	}
	return AMRC_ERROR;
}

static amrc_t get_destination_ip(const amskt_addr* server, amskt_addr* out)
{
	amskt_addr comp;

	if (server == NULL || out == NULL) {
		err("get_destination_ip(): Must be provided with valid addresses\n");
		return AMRC_ERROR;
	}

	memset(&comp, 0, sizeof(comp));

	/* Is unbound? */
	if (server->s.sa_family == AF_INET && memcmp(&server->s4.sin_addr, &comp.s4.sin_addr, sizeof(server->s4.sin_addr)) == 0) {
		get_default_interface_addr(AF_INET, out);
		return AMRC_SUCCESS;
	}
	else if (server->s.sa_family == AF_INET6 && memcmp(&server->s6.sin6_addr, &comp.s6.sin6_addr, sizeof(server->s6.sin6_addr)) == 0) {
		get_default_interface_addr(AF_INET6, out);
		return AMRC_SUCCESS;
	}

	memcpy(out, server, sizeof(amskt_addr));
	return AMRC_SUCCESS;
}

static amrc_t test_amskt_addr_to_str_helper(char* str, uint16_t port)
{
	char* saveptr;
	char* tok;
	ambool_t ipv4 = am_false;
	amskt_addr addr;
	char* end;
	int i;
	char copy[120];
	char compare[128];
	char result[128];
	amrc_t rc;

	memset(&addr, 0, sizeof(addr));
	strncpy(copy, str, sizeof(copy));
	copy[sizeof(copy) - 1] = '\0';

	snprintf(compare, sizeof(compare), "%s:%u", copy, port);
	compare[sizeof(compare) - 1] = '\0';

	saveptr = NULL;
	for (i = 0, tok = strtok_r(copy, ".", &saveptr); tok != NULL; tok = strtok_r(NULL, ".", &saveptr), i++) {
		int num;
		uint8_t unum;

		if (i >= 4) {
			err("test_amskt_addr_to_str_helper(%s): Invalid input string\n", compare);
			return AMRC_ERROR;
		}
		if (i > 0)
			ipv4 = am_true;
		num = -1;
		num = strtol(tok, &end, 10);
		unum = num;
		((uint8_t*)&addr.s4.sin_addr)[i] = unum;
	}

	if (ipv4) {
		addr.s4.sin_port = port;
		addr.s4.sin_family = AF_INET;
	}
	else {
		/* ipv6 */
		int num;
		int bridge = -1;
		uint16_t unum;
		for (i = 0, saveptr = copy, tok = strchr(copy, ':'); tok != NULL || saveptr != NULL; tok = strchr((saveptr = tok + 1), ':')) {
			if (i >= 8) {
				err("test_amskt_addr_to_str_helper(%s): Invalid input string\n", compare);
				return AMRC_ERROR;
			}
			if (tok == saveptr) {
				bridge = i;
				continue;
			}
			if (tok)
				*tok = '\0';
			num = -1;
			num = strtol(saveptr, &end, 16);
			unum = num;
			addr.s6.sin6_addr.__in6_u.__u6_addr16[i] = htons(unum);
			i++;
			if (tok == NULL)
				break;
		}
		if (bridge >= 0) {
			int after_bridge = i - bridge;
			uint16_t* u16 = addr.s6.sin6_addr.__in6_u.__u6_addr16;
			memmove(&u16[8 - after_bridge], &u16[bridge], after_bridge * sizeof(uint16_t));
			memset(&u16[bridge], 0, sizeof(uint16_t) * (7 - after_bridge));
		}
		addr.s6.sin6_port = port;
		addr.s6.sin6_family = AF_INET6;
	}

	rc = amskt_addr_to_str(&addr, result, sizeof(result));
	if (rc != AMRC_SUCCESS) {
		err("test_amskt_addr_to_str_helper(%s): Failed to create string\n", compare);
		return AMRC_ERROR;
	}

	if (strncmp(result, compare, sizeof(result)) != 0) {
		err("test_amskt_addr_to_str_helper(%s): Failed comparison to '%s'\n", compare, result);
		return AMRC_ERROR;
	}

	return AMRC_SUCCESS;
}

static amrc_t test_amskt_addr_set_port_helper(int addr_fam, int arg_fam, amrc_t exp)
{
	amskt_addr addr;
	amskt_addr copy;
	amrc_t rc;
	amrc_t ret = AMRC_SUCCESS;
	uint16_t port = 15;
	memset(&addr, 0, sizeof(addr));
	addr.s.sa_family = addr_fam;
	memcpy(&copy, &addr, sizeof(addr));

	rc = amskt_addr_set_port(&addr, arg_fam, port);
	if (rc != AMRC_SUCCESS && memcmp(&copy, &addr, sizeof(addr)) != 0) {
		err("amskt_addr_set_port(skt=%d, arg=%d): Socket address changed during failed amskt_addr_set_port\n", addr_fam, arg_fam);
		ret = AMRC_ERROR;
	}

	if (rc == AMRC_SUCCESS) {
		if (addr.s.sa_family != AF_INET && addr.s.sa_family != AF_INET6) {
			err("amskt_addr_set_port(skt=%d, arg=%d): Invalid set operation did not result in proper family\n", addr_fam, arg_fam);
			ret = AMRC_ERROR;
		}
		else {
			if (addr.s.sa_family == AF_INET && addr.s4.sin_port != port) {
				err("amskt_addr_set_port(skt=%d, arg=%d): Failed to set IPv4 port\n", addr_fam, arg_fam);
				ret = AMRC_ERROR;
			}
			else if (addr.s.sa_family == AF_INET6 && addr.s6.sin6_port != port) {
				err("amskt_addr_set_port(skt=%d, arg=%d): Failed to set IPv6 port\n", addr_fam, arg_fam);
				ret = AMRC_ERROR;
			}
		}

		if ((arg_fam == AF_INET || arg_fam == AF_INET6) && addr.s.sa_family != arg_fam) {
			err("amskt_addr_set_port(skt=%d, arg=%d): Unexpected family on result\n", addr_fam, arg_fam);
			ret = AMRC_ERROR;
		}
	}

	if (rc != exp) {
		err("amskt_addr_set_port(skt=%d, arg=%d): Expected rc %d, received %d\n", addr_fam, arg_fam, exp, rc);
		ret = AMRC_ERROR;
	}

	return ret;
}


static void* _test_server_tcp_thread(void* arg)
{
	server_args_t* srv = arg;
	amrc_t rc;
	amskt_t cln = -1;
	amskt_addr cln_addr;
	amrc_t ret = AMRC_ERROR;
	uint8_t buffer[64];
	uint32_t received;

	log("_test_server_tcp_thread(%s): Starting\n", srv->name);

	rc = amskt_set_blocking(srv->fd, am_false); /* We do this to be able to poll amskt_accept */
	if (rc != AMRC_SUCCESS) {
		goto close;
	}

	srv->up = 1;
	while (srv->keep_running) {
		/* We are NOT testing concurrency here, just basic sockets */
		rc = amskt_accept(srv->fd, &cln, &cln_addr);
		if (rc != AMRC_SUCCESS) {
			sleep_microseconds(100);
			continue;
		}
		log("_test_server_tcp_thread(%s): Accepted client %d\n", srv->name, cln);

		/* Got a client, do some echo dance */
		while (am_true) {
			received = amskt_recv(cln, buffer, sizeof(buffer), NULL, AMSKT_IGNORE_INTER);
			if (received == 0) {
				break;
			}

			rc = amskt_send(cln, buffer, received, NULL, AMSKT_IGNORE_INTER);
			if (rc != received) {
				err("_test_server_tcp_thread(%s): Failed to echo data, managed only %u/%u bytes\n", srv->name, rc, received);
				goto close;
			}
		}
		log("_test_server_tcp_thread(%s): Closed client %d\n", srv->name, cln);
		close(cln);
		cln = -1;
	}

	ret = AMRC_SUCCESS;
close:
	srv->keep_running = 0;
	if (cln >= 0) {
		log("_test_server_tcp_thread(%s): Closed client %d\n", srv->name, cln);
		close(cln);
	}
	if (ret != AMRC_SUCCESS) amsync_inc(srv->err);
	log("_test_server_tcp_thread(%s): Done\n", srv->name);
	return NULL;
}

static void* _test_server_udp_thread(void* arg)
{
	server_args_t* srv = arg;
	amrc_t rc;
	amskt_addr cln_addr;
	amrc_t ret = AMRC_ERROR;
	uint8_t buffer[64];
	uint32_t transferred;

	set_signal_handler();

	log("_test_server_udp_thread(%s): Starting\n", srv->name);
	srv->up = 1;
	while (srv->keep_running) {
		transferred = amskt_recv(srv->fd, buffer, sizeof(buffer), &cln_addr, 0);
		if (transferred == 0)
			break;

		rc = amskt_send(srv->fd, buffer, transferred, &cln_addr, AMSKT_IGNORE_INTER | AMSKT_IGNORE_PACKET);
		if (rc != transferred) {
			err("_test_server_udp_thread(%s): Failed to echo data, managed only %u/%u bytes\n", srv->name, rc, transferred);
			goto close;
		}
		log("_test_server_udp_thread(%s): Echoed %u bytes\n", srv->name, transferred);
	}

	ret = AMRC_SUCCESS;
close:
	srv->keep_running = 0;
	if (ret != AMRC_SUCCESS) amsync_inc(srv->err);
	log("_test_server_udp_thread(%s): Ended\n", srv->name);
	return NULL;
}

static amrc_t _test_client_tcp_socket(int family, int type, const amskt_addr* srv_addr, uint16_t port)
{
	amskt_t cln = -1;
	amrc_t ret = AMRC_ERROR;
	uint32_t requests_remaining = 3;
	uint32_t transferred;
	char sent[48];
	char rcvd[sizeof(sent)];
	char ip[INET6_ADDRSTRLEN + 1];
	uint32_t to_transfer[] = { 1, 24, 48 };
	amrc_t rc;
	amskt_addr from_addr;
	amskt_addr to_addr;

	rc = get_destination_ip(srv_addr, &to_addr);
	if (rc != AMRC_SUCCESS) {
		err("_test_client_tcp_socket(fam=%d, type=%d, port=%u): Failed to determine destination ip\n", family, type, port);
		goto close;
	}

	rc = amskt_addr_set_port(&to_addr, to_addr.s.sa_family, port);
	if (rc != AMRC_SUCCESS) {
		err("_test_client_tcp_socket(fam=%d, type=%d, port=%u): Failed to set server port\n", family, type, port);
		goto close;
	}

	rc = amskt_addr_to_ipstr(&to_addr, ip, sizeof(ip));
	if (rc != AMRC_SUCCESS) {
		err("_test_client_tcp_socket(fam=%d, type=%d, port=%u): Failed to format destination ip\n", family, type, port);
		goto close;
	}

	log("_test_client_tcp_socket(fam=%d, type=%d, ip=%s, port=%u): Starting\n", family, type, ip, port);

	cln = amskt_get_client_socket(family, type, ip, port);
	if (cln < 0) {
		log("_test_client_tcp_socket(fam=%d, type=%d, ip=%s, port=%u): Failed to get client\n", family, type, ip, port);
		goto close;
	}
	log("_test_client_tcp_socket(fam=%d, type=%d, ip=%s, port=%u): Created socket %d\n", family, type, ip, port, cln);

	while (requests_remaining > 0) {
		requests_remaining--;

		/* Random change in data */
		memset(sent, 0, sizeof(sent));
		((uint32_t*)sent)[0] = requests_remaining;
		((uint32_t*)sent)[3] = requests_remaining;
		transferred = amskt_send(cln, sent, to_transfer[requests_remaining], (type == SOCK_STREAM ? NULL : &to_addr), AMSKT_IGNORE_INTER);
		if (transferred != to_transfer[requests_remaining]) {
			err("_test_client_tcp_socket(fam=%d, type=%d, ip=%s, port=%u): Req %u sent %u/%u bytes\n", family, type, ip, port, requests_remaining, transferred, to_transfer[requests_remaining]);
			goto close;
		}

		memset(rcvd, 0, sizeof(rcvd));
		transferred = amskt_recv(cln, rcvd, to_transfer[requests_remaining], (type == SOCK_STREAM ? NULL : &from_addr), AMSKT_IGNORE_INTER);
		if (transferred != to_transfer[requests_remaining]) {
			err("_test_client_tcp_socket(fam=%d, type=%d, ip=%s, port=%u): Req %u received %u/%u bytes\n", family, type, ip, port, requests_remaining, transferred, to_transfer[requests_remaining]);
			goto close;
		}

		/* Sanity */
		if (memcmp(sent, rcvd, sizeof(sent)) != 0) {
			err("_test_client_tcp_socket(fam=%d, type=%d, ip=%s, port=%u): Req %u failed memory comparison\n", family, type, ip, port, requests_remaining);
			goto close;
		}
	}

	ret = AMRC_SUCCESS;
close:
	if (cln >= 0) {
		log("_test_client_tcp_socket(fam=%d, type=%d, ip=%s, port=%u): Closed socket %d\n", family, type, ip, port, cln);
		close(cln);
	}
	log("_test_client_tcp_socket(fam=%d, type=%d, ip=%s, port=%u): Done\n", family, type, ip, port);
	return ret;
}

static amrc_t _test_server_socket(amskt_t srv, int type)
{
	volatile uint64_t err = 0;
	server_args_t srv_arg = { .up = 0, .err = &err, .keep_running = am_true, .fd = srv, .name = "" };
	char buf[32];
	pthread_t srv_pt = 0;
	uint16_t port;
	ambool_t join = am_false;
	amrc_t rc;
	amrc_t ret =  AMRC_ERROR;

	rc = amskt_local_to_addr(srv, &srv_arg.addr);
	if (rc != AMRC_SUCCESS) {
		err("Failed to obtain local socket information\n");
		goto error;
	}

	rc = amskt_addr_to_str(&srv_arg.addr, buf, sizeof(buf));
	if (rc != AMRC_SUCCESS) {
		err("Failed to translate server local addr to string\n");
		goto error;
	}

	assert(srv_arg.addr.s.sa_family == AF_INET || srv_arg.addr.s.sa_family == AF_INET6);
	snprintf(srv_arg.name, sizeof(srv_arg.name), "fd%d:%s-%d-%s", srv_arg.fd, srv_arg.addr.s.sa_family == AF_INET ? "IPv4" : "IPv6", type, buf);
	srv_arg.name[sizeof(srv_arg.name) - 1] = '\0';

	port = amskt_addr_to_port(&srv_arg.addr);

	assert(type == SOCK_STREAM || type == SOCK_DGRAM);
	if (type == SOCK_STREAM)
		rc = pthread_create(&srv_pt, NULL, _test_server_tcp_thread, &srv_arg);
	if (type == SOCK_DGRAM)
		rc = pthread_create(&srv_pt, NULL, _test_server_udp_thread, &srv_arg);
	if (rc != 0) {
		err("Could not create thread for TCP server '%s'\n", srv_arg.name);
		goto error;
	}
	join = am_true;

	while (srv_arg.up == 0)
		sleep_microseconds(1000);

	/* Now that we're ready, do tests */
	assert(srv_arg.addr.s.sa_family == AF_INET || srv_arg.addr.s.sa_family == AF_INET6);
	if (type == SOCK_STREAM) {
		err += (AMRC_SUCCESS != _test_client_tcp_socket(AF_UNSPEC, SOCK_STREAM, &srv_arg.addr, port));
		err += (AMRC_SUCCESS != _test_client_tcp_socket(srv_arg.addr.s.sa_family, SOCK_STREAM, &srv_arg.addr, port));
	}
	else {
		err += (AMRC_SUCCESS != _test_client_tcp_socket(AF_UNSPEC, SOCK_DGRAM, &srv_arg.addr, port));
		err += (AMRC_SUCCESS != _test_client_tcp_socket(srv_arg.addr.s.sa_family, SOCK_DGRAM, &srv_arg.addr, port));
	}

	if (srv_arg.addr.s.sa_family == AF_INET) {
		err += (AMRC_ERROR != _test_client_tcp_socket(AF_INET6, SOCK_STREAM, &srv_arg.addr, port)); /* Expected failure */
		err += (AMRC_ERROR != _test_client_tcp_socket(AF_INET6, SOCK_DGRAM, &srv_arg.addr, port)); /* Expected failure */
	}
	else {
		err += (AMRC_ERROR != _test_client_tcp_socket(AF_INET, SOCK_STREAM, &srv_arg.addr, port)); /* Expected failure */
		err += (AMRC_ERROR != _test_client_tcp_socket(AF_INET, SOCK_DGRAM, &srv_arg.addr, port)); /* Expected failure */
	}

	ret = AMRC_SUCCESS;
error:
	if (join) {
		srv_arg.keep_running = am_false;
		if (type == SOCK_DGRAM) /* Opted for signaling rather than non-blocking */
			pthread_kill(srv_pt, SIGUSR1);
		rc = pthread_join(srv_pt, NULL);
		if (rc != 0)
			err("Failed to join TCP server '%s'\n", srv_arg.name);
	}

	ret = (err > 0 ? AMRC_ERROR : ret);
	if (ret != AMRC_SUCCESS) {
		err("_test_server_socket(%s) Ended with error(s)\n", srv_arg.name);
	}
	return (err > 0 ? AMRC_ERROR : ret);
}

static amrc_t _test_server_helper(int family, int type, const char* ip, uint16_t port, amrc_t exp)
{
	amrc_t rc;
	amskt_t srv = -1;
	amrc_t ret = AMRC_ERROR;

	srv = amskt_get_server_socket(family, type, ip, port);
	if ((exp == AMRC_SUCCESS && srv < 0) || (exp != AMRC_SUCCESS && srv >= 0)) {
		err("amskt_get_server_socket(fam=%d, type=%d, ip=%s, port=%u): Expectation %d not met, srv %d\n", family, type, ip, port, exp, srv);
		goto close;
	}

	if (srv >= 0) {
		/* We got ourselves a server, test client functionality */
		if (type == SOCK_STREAM || type == SOCK_DGRAM) {
			rc = _test_server_socket(srv, type);
			if (rc != AMRC_SUCCESS) {
				goto close;
			}
		}
		else {
			err("amskt_get_server_socket(fam=%d, type=%d, ip=%s, port=%u): Unexpected success with unknown type\n", family, type, ip, port);
		}
	}

	ret = AMRC_SUCCESS;
close:
	if (srv >= 0)
		close(srv);
	return ret;
}

static amrc_t test_client_server_ipv4()
{
	amrc_t rc;
	uint32_t errors = 0;
	amskt_addr addr;
	char ipstr[sizeof(addr)];

	rc = get_default_interface_addr(AF_INET, &addr);
	if (rc != AMRC_SUCCESS)
		return AMRC_SUCCESS; /* Can't test this locally, should not fail */

	rc = amskt_addr_to_ipstr(&addr, ipstr, sizeof(ipstr));
	if (rc != AMRC_SUCCESS) {
		err("test_client_server_ipv4: Failed to extract default addr\n");
		return AMRC_ERROR;
	}

	errors += (_test_server_helper(AF_INET, SOCK_STREAM, NULL,      0, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_INET, SOCK_STREAM, NULL,  54123, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_INET, SOCK_STREAM, ipstr,     0, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_INET, SOCK_STREAM, ipstr, 54123, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_INET, SOCK_STREAM, "ANY",     0, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_INET, SOCK_STREAM, "ANY", 54123, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_INET, SOCK_DGRAM , NULL,      0, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_INET, SOCK_DGRAM , NULL,  54123, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_INET, SOCK_DGRAM , ipstr,     0, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_INET, SOCK_DGRAM , ipstr, 54123, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_INET, SOCK_DGRAM , "ANY",     0, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_INET, SOCK_DGRAM , "ANY", 54123, AMRC_SUCCESS) != AMRC_SUCCESS);

	return (errors > 0 ? AMRC_ERROR : AMRC_SUCCESS);
}

static amrc_t test_client_server_ipv6()
{
	amrc_t rc;
	uint32_t errors = 0;
	amskt_addr addr;
	char ipstr[sizeof(addr)];

	rc = get_default_interface_addr(AF_INET6, &addr);
	if (rc != AMRC_SUCCESS)
		return AMRC_SUCCESS; /* Can't test this locally, should not fail */

	rc = amskt_addr_to_ipstr(&addr, ipstr, sizeof(ipstr));
	if (rc != AMRC_SUCCESS) {
		err("test_client_server_ipv6: Failed to extract default addr\n");
		return AMRC_ERROR;
	}

	errors += (_test_server_helper(AF_INET6, SOCK_STREAM, NULL,      0, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_INET6, SOCK_STREAM, NULL,  54123, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_INET6, SOCK_STREAM, ipstr,     0, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_INET6, SOCK_STREAM, ipstr, 54123, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_INET6, SOCK_STREAM, "ANY",     0, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_INET6, SOCK_STREAM, "ANY", 54123, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_INET6, SOCK_DGRAM , NULL,      0, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_INET6, SOCK_DGRAM , NULL,  54123, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_INET6, SOCK_DGRAM , ipstr,     0, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_INET6, SOCK_DGRAM , ipstr, 54123, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_INET6, SOCK_DGRAM , "ANY",     0, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_INET6, SOCK_DGRAM , "ANY", 54123, AMRC_SUCCESS) != AMRC_SUCCESS);

	return (errors > 0 ? AMRC_ERROR : AMRC_SUCCESS);
}

static amrc_t test_client_server_unspec()
{
	amrc_t rc;
	uint32_t errors = 0;
	char ipstr[sizeof(amskt_addr)];
	const amskt_addr* addr;

	if (working_interfaces.count == 0)
		return AMRC_SUCCESS; /* Can't test this locally, should not fail */

	addr = (working_interfaces.is_working[0] ? &working_interfaces.addrs[0] : &working_interfaces.addrs[1]);
	rc = amskt_addr_to_ipstr(addr, ipstr, sizeof(ipstr));
	if (rc != AMRC_SUCCESS) {
		err("test_client_server_unspec: Failed to extract default addr\n");
		return AMRC_ERROR;
	}

	errors += (_test_server_helper(AF_UNSPEC, SOCK_STREAM, NULL,      0, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_UNSPEC, SOCK_STREAM, NULL,  54123, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_UNSPEC, SOCK_STREAM, ipstr,     0, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_UNSPEC, SOCK_STREAM, ipstr, 54123, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_UNSPEC, SOCK_STREAM, "ANY",     0, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_UNSPEC, SOCK_STREAM, "ANY", 54123, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_UNSPEC, SOCK_DGRAM , NULL,      0, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_UNSPEC, SOCK_DGRAM , NULL,  54123, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_UNSPEC, SOCK_DGRAM , ipstr,     0, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_UNSPEC, SOCK_DGRAM , ipstr, 54123, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_UNSPEC, SOCK_DGRAM , "ANY",     0, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_UNSPEC, SOCK_DGRAM , "ANY", 54123, AMRC_SUCCESS) != AMRC_SUCCESS);

	return (errors > 0 ? AMRC_ERROR : AMRC_SUCCESS);
}

static amrc_t test_client_server_insanity()
{
	uint32_t errors = 0;
	char* ipv4 = "127.0.0.1";
	char* ipv6 = "::1";

	errors += (_test_server_helper(AF_INET  , SOCK_STREAM, ipv6,      0, AMRC_ERROR  ) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_INET6 , SOCK_STREAM, ipv4,      0, AMRC_ERROR  ) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_INET  , SOCK_STREAM, ipv6,  54123, AMRC_ERROR  ) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_INET6 , SOCK_STREAM, ipv4,  54123, AMRC_ERROR  ) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_INET  , SOCK_DGRAM , ipv6,      0, AMRC_ERROR  ) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_INET6 , SOCK_DGRAM , ipv4,      0, AMRC_ERROR  ) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_INET  , SOCK_DGRAM , ipv6,  54123, AMRC_ERROR  ) != AMRC_SUCCESS);
	errors += (_test_server_helper(AF_INET6 , SOCK_DGRAM , ipv4,  54123, AMRC_ERROR  ) != AMRC_SUCCESS);

	return (errors > 0 ? AMRC_ERROR : AMRC_SUCCESS);
}

static amrc_t test_amskt_addr_set_port()
{
	uint32_t errors = 0;

	errors += (test_amskt_addr_set_port_helper(AF_UNSPEC, AF_UNSPEC, AMRC_ERROR  ) != AMRC_SUCCESS);
	errors += (test_amskt_addr_set_port_helper(AF_UNSPEC, AF_INET,   AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (test_amskt_addr_set_port_helper(AF_UNSPEC, AF_INET6,  AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (test_amskt_addr_set_port_helper(AF_UNSPEC, AF_UNIX,   AMRC_ERROR  ) != AMRC_SUCCESS);

	errors += (test_amskt_addr_set_port_helper(AF_INET,   AF_UNSPEC, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (test_amskt_addr_set_port_helper(AF_INET,   AF_INET,   AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (test_amskt_addr_set_port_helper(AF_INET,   AF_INET6,  AMRC_ERROR  ) != AMRC_SUCCESS);
	errors += (test_amskt_addr_set_port_helper(AF_INET,   AF_UNIX,   AMRC_ERROR  ) != AMRC_SUCCESS);

	errors += (test_amskt_addr_set_port_helper(AF_INET6,  AF_UNSPEC, AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (test_amskt_addr_set_port_helper(AF_INET6,  AF_INET,   AMRC_ERROR  ) != AMRC_SUCCESS);
	errors += (test_amskt_addr_set_port_helper(AF_INET6,  AF_INET6,  AMRC_SUCCESS) != AMRC_SUCCESS);
	errors += (test_amskt_addr_set_port_helper(AF_INET6,  AF_UNIX,   AMRC_ERROR  ) != AMRC_SUCCESS);

	errors += (test_amskt_addr_set_port_helper(AF_UNIX,   AF_UNSPEC, AMRC_ERROR  ) != AMRC_SUCCESS);
	errors += (test_amskt_addr_set_port_helper(AF_UNIX,   AF_INET,   AMRC_ERROR  ) != AMRC_SUCCESS);
	errors += (test_amskt_addr_set_port_helper(AF_UNIX,   AF_INET6,  AMRC_ERROR  ) != AMRC_SUCCESS);
	errors += (test_amskt_addr_set_port_helper(AF_UNIX,   AF_UNIX,   AMRC_ERROR  ) != AMRC_SUCCESS);

	return (errors > 0 ? AMRC_ERROR : AMRC_SUCCESS);
}

static amrc_t test_amskt_addr_to_str()
{
	uint32_t errors = 0;

	errors += (AMRC_SUCCESS != test_amskt_addr_to_str_helper("1.2.3.4", 0));
	errors += (AMRC_SUCCESS != test_amskt_addr_to_str_helper("1.2.3.4", 54321));
	errors += (AMRC_SUCCESS != test_amskt_addr_to_str_helper("aabb:ccdd:eeff:1122:3344:5566:7788:9900", 0));
	errors += (AMRC_SUCCESS != test_amskt_addr_to_str_helper("aabb:ccdd:eeff:1122:3344:5566:7788:9900", 54321));
	errors += (AMRC_SUCCESS != test_amskt_addr_to_str_helper("fe80::3b8:6bf1:c8:f269", 0));
	errors += (AMRC_SUCCESS != test_amskt_addr_to_str_helper("fe80::3b8:6bf1:c8:f269", 54321));
	errors += (AMRC_SUCCESS != test_amskt_addr_to_str_helper("::1", 54321));
	errors += (AMRC_SUCCESS != test_amskt_addr_to_str_helper("::1", 0));
	errors += (AMRC_SUCCESS != test_amskt_addr_to_str_helper("127.0.0.1", 0));
	errors += (AMRC_SUCCESS != test_amskt_addr_to_str_helper("127.0.0.1", 54321));

	return (errors > 0 ? AMRC_ERROR : AMRC_SUCCESS);
}

int main()
{
	amrc_t rc;
	test_t tests[] = {
			TEST(test_amskt_addr_to_str),
			TEST(test_amskt_addr_set_port),
			TEST(test_client_server_ipv4),
			TEST(test_client_server_ipv6),
			TEST(test_client_server_unspec),
			TEST(test_client_server_insanity),
	};
	test_set_t set = {
			.name = "socket_tests",
			.count = ARRAY_SIZE(tests),
			.tests = tests
	};

	amlog_sink_init(AMLOG_FLAGS_ABORT_ON_ERROR);
	set_working_interfaces();
	rc = run_tests(&set);
	amlog_sink_term();

	return (rc == AMRC_SUCCESS ? 0 : -1);
}
