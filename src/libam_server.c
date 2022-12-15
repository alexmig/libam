#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <errno.h>

#include "libam/libam_server.h"

#ifdef DEBUG
#include "libam/libam_log.h"
#define debug_log(fmt, args...)	amlog_sink_message(__FILE_NAME__, __FUNCTION__, __LINE__, AMLOG_DEBUG, 0, fmt, ##args)
#else
#define debug_log(fmt, args...)
#endif

#include "libam/libam_atomic.h"

enum constants {
	MAX_EVENTS = 16,
	POLL_TIMEOUT = AMTIME_SEC * 3,
};

typedef enum srv_type {
	SRV_UNSET = 0,
	QUEUEDSRV_QUEUED,
	SRV_THREADED,
	SRV_POOLED,
} srv_type_t;

typedef enum srv_state {
	SRV_STOPPED = 0,
	SRV_RUNNING,
} srv_state_t;

struct amserver {
	amserver_flags_t	flags;
	srv_type_t	type;
	volatile srv_state_t state;
	ambool_t	should_keep_running;
	amcqueue_t*	user_queue;
	amserver_threaded_cb_t	user_callback;
	void*		user_data;
	lam_thread_pool_t* user_pool;
	lam_thread_func_t pool_func;
	pthread_t	pthread;
	int		epollfd;
	int		epoll_count;
	int		epoll_set[MAX_EVENTS];
};


/* Allocates a server handle
 * A server, once started, will launch a thread to accept new connections on a set of sockets.
 * When a connection arrives, depending on server start, it will either be pushed to a queue,
 * 	a new thread will be started, or a thread pool will be called.
 * Returns pointer to server handle / NULL on errors */
amserver_t* srv_alloc(amserver_flags_t flags)
{
	amserver_t* srv;
	va_list valist;
	int i;
	const char* interface;
	amrc_t rc;
	amskt_addr* addr;

	if ((flags & AMSERVER_ABORT_ON_ERRORS) && (flags & AMSERVER_STOP_ON_ERRORS)) {
		debug_log("Cannot initialize server with both AMSERVER_ABORT_ON_ERRORS and AMSERVER_STOP_ON_ERRORS set\n");
		return NULL;
	}

	srv = malloc(sizeof(*srv));
	if (srv == NULL) {
		debug_log("Failed to allocate server memory\n");
		return NULL;
	}
	memset(srv, 0, sizeof(*srv));
	srv->should_keep_running = am_false;
	srv->flags = flags;

	assert(0); // TODO: add epoll mechanism

	return srv;
}

/* Frees all associated resources tied to the server handle.
 * If close_sockets is set, will also close all sockets that have been added to the server. */
amrc_t amserver_free(amserver_t* srv, ambool_t close_sockets)
{
	if (!srv)
		return AMRC_ERROR;
	amserver_stop(srv);
	assert(0); // TODO: add close_sockets code
	assert(0); // TODO: add epoll cleanup
	free(srv);
	return AMRC_SUCCESS;
}

/* Add a socket to work with when serving.
 * Socket MUST be a TCP (SOCK_STREAM) socket.
 * Internally, this will set the server socket to non-blocking mode.
 * This function cannot be called on a running server, and will fail if used so.
 * Returns AMRC_SUCCESS / AMRC_ERROR */
amrc_t amserver_add_socket(amserver_t* srv, amskt_t socket)
{
	assert(0); // TODO: Epoll.
	return AMRC_ERROR;
}

static void* server_thread_function(void* arg)
{
	amserver_t* srv = arg;
	struct epoll_event events[MAX_EVENTS];
	int rc;
	int i;
	amskt_t new_socket;
	struct sockaddr address;
	socklen_t address_len;
	pthread_t pt;

	debug_log("Server %p thread started\n", srv);
	while (srv->should_keep_running) {
		rc = epoll_wait(srv->epollfd, events, MAX_EVENTS, POLL_TIMEOUT);
		if (rc < 0) {
			debug_log("Server %p encountered error %d while running\n", srv, errno);
			if (srv->flags & AMSERVER_ABORT_ON_ERRORS)
				abort();
			if (srv->flags & AMSERVER_STOP_ON_ERRORS)
				break;
			continue;
		}
		/* rc == 0 means a timeout */

		for (i = 0; i < rc; i++) {
			if (events[i].events & EPOLLERR) {
				debug_log("Server %p encountered error for binding %d\n", srv, events[i].data.fd);
				/* TODO: Something more robust here. Remove from pollset? */
				if (srv->flags & AMSERVER_ABORT_ON_ERRORS)
					abort();
				if (srv->flags & AMSERVER_STOP_ON_ERRORS)
					break;
				continue;
			}

			address_len = sizeof(address);
			new_socket = accept(events[i].data.fd, &address, &address_len);
		        if (new_socket < 0) {
		        	debug_log("Server %p encountered error %d on binding %fd\n", srv, errno, events[i].data.fd);
		        	if (errno != EAGAIN && errno != EWOULDBLOCK)
					if (srv->flags & AMSERVER_ABORT_ON_ERRORS)
						abort();
					if (srv->flags & AMSERVER_STOP_ON_ERRORS)
						break;
					continue;
		        }

		        switch (srv->type) {
		        case QUEUEDSRV_QUEUED:
		        	rc = amcqueue_enq(srv->user_queue, (void*)new_socket);
		        	if (rc != AMRC_SUCCESS) {
		        		debug_log("Server %p failed to enqueue socket %d\n", srv, new_socket);
		        		close(new_socket);
					if (srv->flags & AMSERVER_ABORT_ON_ERRORS)
						abort();
					if (srv->flags & AMSERVER_STOP_ON_ERRORS)
						break;
		        	}
		        	break;

		        case SRV_THREADED:
		        	pt = 0;
		        	rc = pthread_create(&pt, NULL, srv->user_callback, srv->user_data);
		        	if (rc != 0) {
		        		debug_log("Server %p failed to start a new thread for %d\n", srv, new_socket);
		        		close(new_socket);
					if (srv->flags & AMSERVER_ABORT_ON_ERRORS)
						abort();
					if (srv->flags & AMSERVER_STOP_ON_ERRORS)
						break;
		        	}

		        	rc = pthread_detach(pt);
		        	assert(rc == 0);
		        	break;

		        case SRV_POOLED:
		        	rc = lam_thread_pool_run(srv->user_pool, srv->pool_func, (void*)new_socket, srv->user_data);
				if (rc != AMRC_SUCCESS) {
					debug_log("Server %p failed to schedule a new thread for %d\n", srv, new_socket);
					close(new_socket);
					if (srv->flags & AMSERVER_ABORT_ON_ERRORS)
						abort();
					if (srv->flags & AMSERVER_STOP_ON_ERRORS)
						break;
				}
		        	break;

		        default:
		        	if (rc != 0) {
					debug_log("Server %p in invalid configuration %d\n", srv, srv->type);
					close(new_socket);
					if (srv->flags & AMSERVER_ABORT_ON_ERRORS)
						abort();
					if (srv->flags & AMSERVER_STOP_ON_ERRORS)
						break;
				}
		        	break;
		        }
		}

	}
	debug_log("Server %p thread stopped\n", srv);
	return NULL;
}

static amrc_t listen_generic(amserver_t* srv, amserver_t* config)
{
	amrc_t rc;

	if (!amsync_swap(&srv->state, SRV_STOPPED, SRV_RUNNING)) {
		debug_log("Server %p already running\n", srv);
		return AMRC_ERROR;
	}
	srv->type = config->type;
	srv->user_queue = config->user_queue;
	srv->user_callback = config->user_callback;
	srv->user_data = config->user_data;
	srv->user_pool = config->user_pool;
	srv->pool_func = config->pool_func;
	srv->should_keep_running = am_true;

	rc = pthread_create(&srv->pthread, NULL, server_thread_function, srv);
	if (rc != 0) {
		debug_log("Server %p failed to start main serving thread\n", srv);
		goto error;
	}

	debug_log("Server %p thread started\n", srv);
	return AMRC_SUCCESS;
error:
	rc = amsync_swap(&srv->state, SRV_RUNNING, SRV_STOPPED);
	assert(rc);
	return AMRC_ERROR;
}

/* This will start the server listening in the backgroud as a separate thread.
 * New connections will be queued into <connection_queue> as is (convert the pointer into amskt_t).
 * THIS FLUNCTION IS NOT THREAD SAFE
 * Returns AMRC_SUCCESS / AMRC_ERROR if the server is in incompatible state. */
amrc_t amserver_listen_queued(amserver_t* srv, amcqueue_t* connection_queue)
{
	amserver_t config;

	if (srv == NULL || connection_queue == NULL) {
		debug_log("Arguments to amserver_listen_queued cannot be NULL\n");
		return AMRC_ERROR;
	}
	memset(&config, 0, sizeof(config));
	config.user_queue = connection_queue;
	config.type = QUEUEDSRV_QUEUED;

	return listen_generic(srv, &config);
}

/* This will start the server listening in the backgroud as a separate thread.
 * New for every new connection a new thread will be started and control handed to to <callback>, along with any custom data.
 * THIS FLUNCTION IS NOT THREAD SAFE
 * Returns AMRC_SUCCESS / AMRC_ERROR if the server is in incompatible state. */
amrc_t amserver_listen_threaded(amserver_t* srv, amserver_threaded_cb_t callback, void* data)
{
	amserver_t config;

	if (srv == NULL || callback == NULL) {
		debug_log("Arguments to amserver_listen_threaded cannot be NULL\n");
		return AMRC_ERROR;
	}
	memset(&config, 0, sizeof(config));
	config.user_callback = callback;
	config.user_data = data;
	config.type = QUEUEDSRV_QUEUED;

	return listen_generic(srv, &config);
}

/* This will start the server listening in the backgroud as a separate thread.
 * New for every new connection a thread from <pool> will be tasked and control handed to to <callback>, along with any custom data.
 * THIS FLUNCTION IS NOT THREAD SAFE
 * Returns AMRC_SUCCESS / AMRC_ERROR if the server is in incompatible state. */
amrc_t amserver_listen_pooled(amserver_t* srv, lam_thread_pool_t* pool, lam_thread_func_t pool_cb, amserver_threaded_cb_t callback, void* data)
{
	amserver_t config;

	if (srv == NULL || callback == NULL || pool == NULL) {
		debug_log("Arguments to amserver_listen_pooled cannot be NULL\n");
		return AMRC_ERROR;
	}
	memset(&config, 0, sizeof(config));
	config.user_callback = callback;
	config.user_data = data;
	config.user_pool = pool;
	config.pool_func = pool_cb;
	config.type = QUEUEDSRV_QUEUED;

	return listen_generic(srv, &config);
}

/* This will stop server from listening
 * THIS FLUNCTION IS NOT THREAD SAFE */
amrc_t amserver_stop(amserver_t* srv)
{
	amrc_t rc;

	if (srv == NULL) {
		debug_log("Server argument NULL\n");
		return AMRC_ERROR;
	}
	if (!srv->should_keep_running) {
		debug_log("Server %p Already stopped\n", srv);
		return AMRC_SUCCESS;
	}
	srv->should_keep_running = am_false;
	rc = pthread_join(srv->pthread, NULL);
	if (rc != 0) {
		debug_log("Sever %p failed to stop thread\n", srv);
		return AMRC_ERROR;
	}
	srv->pthread = 0;
	return amsync_swap(&srv->state, SRV_RUNNING, SRV_STOPPED);
}
