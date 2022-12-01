#include <stdlib.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>

#include "libam/libam_types.h"
#include "libam/libam_log.h"
#include "libam/libam_atomic.h"
#include "libam/libam_fdopers.h"
#include "libam/libam_thread_pool.h"
#include "libam/libam_time.h"
#include "libam/libam_replace.h"

#ifdef NDEBUG
#include <stdio.h>
#undef assert
#define assert(cond) do {if (!(cond)) { fprintf(stderr, "Assertion '" #cond "' failed at %s:%d\n", __FILE__, __LINE__); fflush(stderr); abort(); }} while(0)
#else
#include <assert.h>
#endif


enum constants {
	CONSTANT_MAX_PAYLOAD_LENGTH = 16384,
};

typedef struct globals {
	const char* bind_interfce;
	uint16_t server_port;
} globals_t;

static globals_t globals = { .bind_interfce = "127.0.0.1", .server_port = 31553 };

static volatile uint64_t echo_server_ready = 0;
static volatile uint64_t echo_server_stop = 0;
static volatile uint64_t echo_server_skt_id = 1;
static volatile uint64_t echo_server_skt_errors = 0;
static volatile uint64_t echo_server_skt_requests = 0;
static volatile uint64_t echo_server_skt_done = 0;

static volatile uint64_t echo_client_skt_id = 1;
static volatile uint64_t echo_client_skt_errors = 0;
static volatile uint64_t echo_client_skt_requests = 0;
static volatile uint64_t echo_client_skt_done = 0;
static volatile uint64_t echo_client_ready = 0;
static volatile uint64_t echo_client_go = 0;


static void signal_handler(UNUSED int signo)
{
}

static void set_signal_handler()
{
	int rc;
	struct sigaction sa = {
		    .sa_handler = signal_handler,
		    .sa_flags = SA_SIGINFO,
	};
	sigemptyset(&sa.sa_mask);
	rc = sigaction(SIGUSR1, &sa, NULL);
	assert(rc == 0);
}

static amrc_t echo_server_single_request(uint64_t id, amskt_t skt)
{
	uint8_t buffer[CONSTANT_MAX_PAYLOAD_LENGTH];
	uint32_t net_length;
	uint32_t length;
	uint32_t i;
	amrc_t rc;

	assert(skt >= 0);

	rc = amskt_read(skt, &net_length, sizeof(net_length));
	if (rc != AMRC_SUCCESS) {
		amlog_sink_log(AMLOG_ERROR, 0, "Servlet %lu failed to read initial length from socket\n", id);
		return AMRC_ERROR;
	}

	length = ntohl(net_length);
	if (length > CONSTANT_MAX_PAYLOAD_LENGTH) {
		amlog_sink_log(AMLOG_ERROR, 0, "Servlet %lu received invalid length %u. Must be at most %u\n", id, CONSTANT_MAX_PAYLOAD_LENGTH);
		return AMRC_ERROR;
	}

	rc = amskt_read(skt, buffer, length);
	if (rc != AMRC_SUCCESS) {
		amlog_sink_log(AMLOG_ERROR, 0, "Servlet %lu failed to read %u bytes from socket\n", id, length);
		return AMRC_ERROR;
	}

	for (i = 0; i < length; i++) {
		buffer[i] = ~buffer[i];
	}

	rc = amskt_write(skt, &net_length, sizeof(net_length));
	if (rc != AMRC_SUCCESS) {
		amlog_sink_log(AMLOG_ERROR, 0, "Servlet %lu failed to write initial length to socket\n", id);
		return AMRC_ERROR;
	}

	rc = amskt_write(skt, buffer, length);
	if (rc != AMRC_SUCCESS) {
		amlog_sink_log(AMLOG_ERROR, 0, "Servlet %lu failed to write %u bytes to socket\n", id, length);
		return AMRC_ERROR;
	}

	return AMRC_SUCCESS;
}

static void* echo_server_connection(void* arg)
{
	amskt_t skt = (uint64_t)arg;
	uint32_t net_requests;
	uint32_t requests;
	uint32_t i = 0;
	uint64_t id;
	amrc_t rc;

	assert(skt >= 0);
	id = amsync_inc(&echo_server_skt_id);

	//amlog_sink_log(AMLOG_ERROR, 0, "Thread id %lu started!\n", id);

	rc = amskt_read(skt, &net_requests, sizeof(net_requests));
	if (rc != AMRC_SUCCESS) {
		amlog_sink_log(AMLOG_ERROR, 0, "Servlet %lu failed to read number of requests from socket\n", id);
		amsync_inc(&echo_server_skt_errors);
		goto done;
	}

	requests = ntohl(net_requests);
	for (i = 0; i < requests; i++) {
		rc = echo_server_single_request(id, skt);
		if (rc != AMRC_SUCCESS) {
			amsync_inc(&echo_server_skt_errors);
			goto done;
		}
	}

done:
	amsync_add(&echo_server_skt_requests, i);
	amsync_inc(&echo_server_skt_done);
	if (skt >= 0) close(skt);
	//amlog_sink_log(AMLOG_ERROR, 0, "Thread id %lu done!\n", id);
	return NULL;
}

static void echo_server_init_stats()
{
	echo_server_skt_id = 1;
	echo_server_skt_errors = 0;
	echo_server_skt_requests = 0;
	echo_server_skt_done = 0;
}

static void echo_server_print_stats(const char* type)
{
	amlog_sink_log(AMLOG_INFO, 0, "Echo server (%s) stats: done threads %lu, requests %lu, errors %lu\n", type, echo_server_skt_done, echo_server_skt_requests, echo_server_skt_errors);
}

static void* echo_server_threaded(UNUSED void* arg)
{
	amskt_addr addr;
	amskt_t server_skt = -1;
	amskt_t client_skt;
	pthread_t pt;
	amrc_t rc;

	echo_server_ready = 0;
	echo_server_stop = 0;
	echo_server_init_stats();

	set_signal_handler();

	amlog_sink_log(AMLOG_INFO, 0, "threaded server starting\n");

	rc = amskt_str2addr(globals.bind_interfce, globals.server_port, &addr);
	if (rc != AMRC_SUCCESS) {
		amlog_sink_log(AMLOG_ERROR, 0, "Server failed convert %s:%d to address\n", globals.bind_interfce, globals.server_port);
		return NULL;
	}

	rc = amskt_listen(&addr, &server_skt);
	if (rc != AMRC_SUCCESS) {
		amlog_sink_log(AMLOG_ERROR, 0, "Server failed to start listening on %s:%d\n", globals.bind_interfce, globals.server_port);
		return NULL;
	}

	amlog_sink_log(AMLOG_INFO, 0, "threaded server ready\n");
	echo_server_ready = 1;

	while (!echo_server_stop) {
		rc = amskt_accept(server_skt, &client_skt, NULL);
		if (rc != AMRC_SUCCESS) {
			if (echo_server_stop)
				break;
			amlog_sink_log(AMLOG_ERROR, 0, "Server failed accepting a connection\n");
			close(server_skt);
			return NULL;
		}

		//amlog_sink_log(AMLOG_ERROR, 0, "New connection, starting thread\n");
		rc = pthread_create(&pt, NULL, echo_server_connection, (void*)(uint64_t)client_skt);
		if (rc != 0) {
			amlog_sink_log(AMLOG_ERROR, 0, "Server failed starting thread\n");
			close(client_skt);
			close(server_skt);
			return NULL;
		}

		rc = pthread_detach(pt);
		if (rc != 0) {
			amlog_sink_log(AMLOG_ERROR, 0, "Server failed detaching thread\n");
			close(server_skt);
			return NULL;
		}
	}
	amlog_sink_log(AMLOG_INFO, 0, "threaded stopping\n");

	echo_server_print_stats("threaded");
	close(server_skt);
	return NULL;
}

void echo_server_pooled_stats_print(const lam_thread_pool_stats_t* stats)
{
	char out_buffer[128];

	amlog_sink_log(AMLOG_INFO, 0, "Threads started: %lu\n", stats->threads_created);
	amlog_sink_log(AMLOG_INFO, 0, "Tasks processed: %lu\n", stats->tasks_created);

	amstat_2str(&stats->active_thread_count, out_buffer, sizeof(out_buffer));
	amlog_sink_log(AMLOG_INFO, 0, "Active thread distribution.: %s\n", out_buffer);

	amstat_2str(&stats->idle_thread_count, out_buffer, sizeof(out_buffer));
	amlog_sink_log(AMLOG_INFO, 0, "Idle thread distribution...: %s\n", out_buffer);

	amstat_2str(&stats->task_delay, out_buffer, sizeof(out_buffer));
	amlog_sink_log(AMLOG_INFO, 0, "Task execution delay.......: %s\n", out_buffer);

	amstat_2str(&stats->tasks_processed, out_buffer, sizeof(out_buffer));
	amlog_sink_log(AMLOG_INFO, 0, "Tasks before idle timeout..: %s\n", out_buffer);

	amstat_2str(&stats->busy_task_num, out_buffer, sizeof(out_buffer));
	amlog_sink_log(AMLOG_INFO, 0, "Continuous task streak.....: %s\n", out_buffer);

	amstat_2str(&stats->queue_depth, out_buffer, sizeof(out_buffer));
	amlog_sink_log(AMLOG_INFO, 0, "Queue depth at schedule....: %s\n", out_buffer);

	amlog_sink_log(AMLOG_INFO, 0, "\n");
}

static void* echo_server_pooled(UNUSED void* arg)
{
	lam_thread_pool_config_t tp_config;
	lam_thread_pool_stats_t stats;
	lam_thread_pool_t* tp;
	amskt_addr addr;
	amskt_t server_skt = -1;
	amskt_t client_skt;
	amrc_t rc;

	echo_server_ready = 0;
	echo_server_stop = 0;
	echo_server_init_stats();

	set_signal_handler();

	memset(&tp_config, 0, sizeof(tp_config));
	tp_config.min_threads = 5;
	tp_config.max_threads = 0; /* Unimited */
	tp_config.idle_timeout = 0; /* No shutting down of threads */
	tp_config.default_func = echo_server_connection;

	tp = lam_thread_pool_create(&tp_config);
	if (tp == NULL) {
		amlog_sink_log(AMLOG_ERROR, 0, "Server failed to start thread pool\n");
		return NULL;
	}

	rc = amskt_str2addr(globals.bind_interfce, globals.server_port, &addr);
	if (rc != AMRC_SUCCESS) {
		amlog_sink_log(AMLOG_ERROR, 0, "Server failed convert %s:%d to address\n", globals.bind_interfce, globals.server_port);
		return NULL;
	}

	rc = amskt_listen(&addr, &server_skt);
	if (rc != AMRC_SUCCESS) {
		amlog_sink_log(AMLOG_ERROR, 0, "Server failed to start listening on %s:%d\n", globals.bind_interfce, globals.server_port);
		return NULL;
	}

	echo_server_ready = 1;

	while (!echo_server_stop) {
		rc = amskt_accept(server_skt, &client_skt, NULL);
		if (rc != AMRC_SUCCESS) {
			if (echo_server_stop)
				break;
			amlog_sink_log(AMLOG_ERROR, 0, "Server failed accepting a connection\n");
			close(server_skt);
			return NULL;
		}

		rc = lam_thread_pool_run(tp, NULL, (void*)(uint64_t)client_skt, NULL);
		if (rc != AMRC_SUCCESS) {
			amlog_sink_log(AMLOG_ERROR, 0, "Server failed queuing thread\n");
			close(client_skt);
			close(server_skt);
			return NULL;
		}
	}

	lam_thread_pool_destroy(tp, &stats);
	echo_server_print_stats("pooled");
	echo_server_pooled_stats_print(&stats);
	close(server_skt);
	return NULL;
}













static amrc_t echo_client_single_request(uint64_t id, amskt_t skt, unsigned int* seed)
{
	uint8_t buffer_send[CONSTANT_MAX_PAYLOAD_LENGTH];
	uint8_t buffer_recv[CONSTANT_MAX_PAYLOAD_LENGTH];
	uint32_t net_length;
	uint32_t length;
	uint32_t i;
	amrc_t rc;

	/* Randomize buffer */
	length = rand_r(seed) % CONSTANT_MAX_PAYLOAD_LENGTH;
	for (i = 0; i < length; i++) {
		buffer_send[i] = rand_r(seed) & UINT8_MAX;
	}
	net_length = htonl(length);

	rc = amskt_write(skt, &net_length, sizeof(net_length));
	if (rc != AMRC_SUCCESS) {
		amlog_sink_log(AMLOG_ERROR, 0, "Client %lu failed to write initial length to to server\n", id);
		return AMRC_ERROR;
	}

	rc = amskt_write(skt, buffer_send, length);
	if (rc != AMRC_SUCCESS) {
		amlog_sink_log(AMLOG_ERROR, 0, "Client %lu failed to write %u bytes to to server\n", id);
		return AMRC_ERROR;
	}

	rc = amskt_read(skt, &net_length, sizeof(net_length));
	if (rc != AMRC_SUCCESS) {
		amlog_sink_log(AMLOG_ERROR, 0, "Client %lu failed to read initial length from server\n", id);
		return AMRC_ERROR;
	}

	if (length != ntohl(net_length)) {
		amlog_sink_log(AMLOG_ERROR, 0, "Client %lu read length %u where %u was expected\n", id, ntohl(net_length), length);
		return AMRC_ERROR;
	}

	rc = amskt_read(skt, buffer_recv, length);
	if (rc != AMRC_SUCCESS) {
		amlog_sink_log(AMLOG_ERROR, 0, "Client %lu failed to read %u bytes from server\n", id);
		return AMRC_ERROR;
	}

	for (i = 0; i < length; i++) {
		uint8_t comp = ~buffer_recv[i];
		if (buffer_send[i] != comp) {
			amlog_sink_log(AMLOG_ERROR, 0, "Client %lu failed to validate buffer at %u\n", id, i);
			return AMRC_ERROR;
		}
	}

	return AMRC_SUCCESS;
}

static void echo_client_connection(uint32_t requests)
{
	char serverstr[32] = "";
	uint32_t net_requests;
	amskt_t skt = -1;
	amskt_addr addr;
	uint32_t i = 0;
	uint64_t id;
	amrc_t rc;
	unsigned int seed;

	snprintf(serverstr, sizeof(serverstr), "%s:%d", globals.bind_interfce, globals.server_port);
	serverstr[sizeof(serverstr) - 1] = '\0';

	id = amsync_inc(&echo_client_skt_id);
	seed = id;

	rc = amskt_str2addr(globals.bind_interfce, globals.server_port, &addr);
	if (rc != AMRC_SUCCESS) {
		amlog_sink_log(AMLOG_ERROR, 0, "Client %lu failed to convert %s to an address\n", id, serverstr);
		amsync_inc(&echo_client_skt_errors);
		goto done;
	}

	rc = amskt_connect(&addr, &skt);
	if (rc != AMRC_SUCCESS) {
		amlog_sink_log(AMLOG_ERROR, 0, "Client %lu failed to connect to %s\n", id, serverstr);
		amsync_inc(&echo_client_skt_errors);
		goto done;
	}

	net_requests = htonl(requests);
	rc = amskt_write(skt, &net_requests, sizeof(net_requests));
	if (rc != AMRC_SUCCESS) {
		amlog_sink_log(AMLOG_ERROR, 0, "Client %lu failed to write number of requests to %s\n", id, serverstr);
		amsync_inc(&echo_client_skt_errors);
		goto done;
	}

	for (i = 0; i < requests; i++) {
		rc = echo_client_single_request(id, skt, &seed);
		if (rc != AMRC_SUCCESS) {
			amsync_inc(&echo_client_skt_errors);
			goto done;
		}
	}

done:
	amsync_add(&echo_client_skt_requests, i);
	amsync_inc(&echo_client_skt_done);
	if (skt >= 0) close(skt);
}

typedef struct echo_clinet_thread_config {
	uint32_t thread_id;
	pthread_t pthread;
	uint32_t connection_num;
	uint32_t requests;
} echo_clinet_thread_config_t;

static void* echo_client_thread(void* arg)
{
	const echo_clinet_thread_config_t* conf = arg;
	struct timespec sleep_to = { .tv_sec = 0, .tv_nsec = 1 };
	uint32_t i;

	assert(conf != NULL);

	amsync_inc(&echo_client_ready);
	while (!echo_client_go)
		nanosleep(&sleep_to, NULL);

	for (i = 0; i < conf->connection_num; i++)
		echo_client_connection(conf->requests);

	return NULL;
}

static void echo_client_init_stats()
{
	echo_client_skt_id = 1;
	echo_client_skt_errors = 0;
	echo_client_skt_requests = 0;
	echo_client_skt_done = 0;
}

static void echo_client_print_stats(amtime_t diff)
{
	amlog_sink_log(AMLOG_INFO, 0, "Echo client stats: done connections %lu, requests %lu, errors %lu\n", echo_client_skt_done, echo_client_skt_requests, echo_client_skt_errors);
	amlog_sink_log(AMLOG_INFO, 0, "Echo client done in %.3lf seconds\n", (double)diff / (double)AMTIME_SEC);
}

/* Return time it took to finish running test / 0 on error */
static amtime_t echo_client(uint32_t concurrency, uint32_t connection_num, uint32_t requests)
{
	echo_clinet_thread_config_t* configs;
	struct timespec sleep_to = { .tv_sec = 0, .tv_nsec = 1 };
	uint32_t i = 0;
	amrc_t ret = AMRC_ERROR;
	amtime_t start;
	int rc;

	echo_client_ready = 0;
	echo_client_go = 0;
	echo_client_init_stats();

	configs = malloc(concurrency * sizeof(*configs));
	if (configs == NULL) {
		amlog_sink_log(AMLOG_ERROR, 0, "Client failed to allocate memory\n");
		return 0;
	}
	memset(configs, 0, concurrency * sizeof(*configs));

	for (i = 0; i < concurrency; i++) {
		configs[i].thread_id = i;
		configs[i].connection_num = connection_num;
		configs[i].requests = requests;
		rc = pthread_create(&configs[i].pthread, NULL, echo_client_thread, &configs[i]);
		assert(rc == 0);
	}

	while (echo_client_ready != concurrency)
		nanosleep(&sleep_to, NULL);
	start = amtime_now();
	echo_client_go = 1;

	ret = AMRC_SUCCESS;

	for (i--; i < concurrency; i--) { /* Counting on the 0 -> UINT32_MAX wrap */
		rc = pthread_join(configs[i].pthread, NULL);
		assert(rc == 0);
	}
	echo_client_print_stats(amtime_now() - start);
	free(configs);
	return ret;
}

static amrc_t compare_single(lam_thread_func_t func, uint32_t concurrency, uint32_t connection_num, uint32_t requests)
{
	struct timespec sleep_to = { .tv_sec = 0, .tv_nsec = 1 };
	pthread_t pt;
	amrc_t rc;


	rc = pthread_create(&pt, NULL, func, NULL);
	assert(rc == 0);

	while (!echo_server_ready)
		nanosleep(&sleep_to, NULL);

	rc = echo_client(concurrency, connection_num, requests);
	assert(rc == AMRC_SUCCESS);

	echo_server_stop = 1;
	pthread_kill(pt, SIGUSR1);


	amlog_sink_log(AMLOG_INFO, 0, "Waiting for server to stop\n");
	rc = pthread_join(pt, NULL);
	assert(rc == 0);

	return AMRC_SUCCESS;
}

static amrc_t compare_servers(uint32_t concurrency, uint32_t connection_num, uint32_t requests)
{
	amlog_sink_log(AMLOG_INFO, 0, "Running threaded comparison (%u, %u, %u)\n", concurrency, connection_num, requests);
	compare_single(echo_server_threaded, concurrency, connection_num, requests);
	amlog_sink_log(AMLOG_INFO, 0, "Threaded comparison done (%u, %u, %u)\n", concurrency, connection_num, requests);
	amlog_sink_log(AMLOG_INFO, 0, "Running pooled comparison (%u, %u, %u)\n", concurrency, connection_num, requests);
	compare_single(echo_server_pooled, concurrency, connection_num, requests);
	amlog_sink_log(AMLOG_INFO, 0, "Pooled comparison done (%u, %u, %u)\n", concurrency, connection_num, requests);

	return AMRC_SUCCESS;
}

static amrc_t compare_reqs(uint32_t concurrency)
{
	compare_servers(concurrency, 8191 / concurrency, 1);
	compare_servers(concurrency, 8191 / concurrency, 16);
	compare_servers(concurrency, 8191 / concurrency, 256);
	compare_servers(concurrency, 8191 / concurrency, 8192);

	return AMRC_SUCCESS;
}

int main()
{
	amlog_sink_init(AMLOG_FLAGS_USE_THREAD);
	amlog_sink_register_direct("Default", amlog_sink_dafault_stdout, NULL);

	compare_reqs(1);
	compare_reqs(2);
	compare_reqs(16);
	compare_reqs(32);
	compare_reqs(64);
	compare_reqs(128);

	amlog_sink_term();

	return 0;
}
