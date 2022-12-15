#ifndef _LIBAM_SERVER_H_
#define _LIBAM_SERVER_H_

#include <sys/socket.h>

#include <libam/libam_types.h>
#include <libam/libam_cqueue.h>
#include <libam/libam_fdopers.h>
#include <libam/libam_thread_pool.h>
#include <libam/libam_stats.h>

struct amserver;
typedef struct amserver amserver_t;

typedef enum amserver_flags {
	AMSERVER_ABORT_ON_ERRORS = 1 << 0,	/* Issue abort() on encountering errors while serving. Mutually exclusive with AMSERVER_STOP_ON_ERRORS */
	AMSERVER_STOP_ON_ERRORS = 1 << 1,	/* Stop server on encountering errors while serving. Mutually exclusive with AMSERVER_ABORT_ON_ERRORS */
} amserver_flags_t;

typedef amrc_t (*amserver_threaded_cb_t)(amskt_t socket, void* data);

/* Allocates a server handle
 * A server, once started, will launch a thread to accept new connections on a set of sockets.
 * When a connection arrives, depending on server start, it will either be pushed to a queue,
 * 	a new thread will be started, or a thread pool will be called.
 * Returns pointer to server handle / NULL on errors */
amserver_t* srv_alloc(amserver_flags_t flags);

/* Frees all associated resources tied to the server handle.
 * If close_sockets is set, will also close all sockets that have been added to the server. */
amrc_t amserver_free(amserver_t* srv, ambool_t close_sockets);

/* Add a socket to work with when serving.
 * Socket MUST be a TCP (SOCK_STREAM) socket.
 * Internally, this will set the server socket to non-blocking mode.
 * This function cannot be called on a running server, and will fail if used so.
 * Returns AMRC_SUCCESS / AMRC_ERROR */
amrc_t amserver_add_socket(amserver_t* srv, amskt_t socket);

/* This will start the server listening in the background as a separate thread.
 * New connections will be queued into <connection_queue> as is (convert the pointer into amskt_t).
 * THIS FLUNCTION IS NOT THREAD SAFE
 * Returns AMRC_SUCCESS / AMRC_ERROR if the server is in incompatible state. */
amrc_t amserver_listen_queued(amserver_t* srv, amcqueue_t* connection_queue);

/* This will start the server listening in the background as a separate thread.
 * New for every new connection a new thread will be started and control handed to to <callback>, along with any custom data.
 * THIS FLUNCTION IS NOT THREAD SAFE
 * Returns AMRC_SUCCESS / AMRC_ERROR if the server is in incompatible state. */
amrc_t amserver_listen_threaded(amserver_t* srv, amserver_threaded_cb_t callback, void* data);

/* This will start the server listening in the backgroud as a separate thread.
 * New for every new connection a thread from <pool> will be tasked and control handed to to <callback>, along with any custom data.
 * THIS FLUNCTION IS NOT THREAD SAFE
 * Returns AMRC_SUCCESS / AMRC_ERROR if the server is in incompatible state. */
amrc_t amserver_listen_pooled(amserver_t* srv, lam_thread_pool_t* pool, lam_thread_func_t pool_cb, amserver_threaded_cb_t callback, void* data);

/* This will stop server from listening
 * THIS FLUNCTION IS NOT THREAD SAFE */
amrc_t amserver_stop(amserver_t* srv);

#endif /* _LIBAM_SERVER_H_ */
