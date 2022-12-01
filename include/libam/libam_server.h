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
 * intf - A list of interfaces. Acceptable values:
 * 	"ANY" - Must be the only interface in the list. All other values will be ignored.
 * 	"<IP>" - E.g. "127.0.0.1" - A specific IP to bind to and listen on.
 * 		Any failures to bind even one of the addresses provided will abort.
 * 	"<path>" - In the case of AF_UNIX, a path to listen on
 * Returns pointer to server handle.
 */
amserver_t* srv_alloc(amserver_flags_t flags, uint16_t port, const char *intf, ...);
amrc_t amserver_free(amserver_t* srv);

/* This will start the server listening in the backgroud as a separate thread.
 * New connections will be queued into <connection_queue> as is (convert the pointer into amskt_t).
 * THIS FLUNCTION IS NOT THREAD SAFE
 * Returns AMRC_SUCCESS / AMRC_ERROR if the server is in incompatible state. */
amrc_t amserver_listen_queued(amserver_t* srv, amcqueue_t* connection_queue);

/* This will start the server listening in the backgroud as a separate thread.
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
