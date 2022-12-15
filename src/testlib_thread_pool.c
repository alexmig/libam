#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/sysinfo.h>

#include "libam/libam_thread_pool.h"

#include "libam/libam_types.h"
#include "libam/libam_time.h"
#include "libam/libam_atomic.h"
#include "libam/libam_replace.h"

#ifdef NDEBUG
#include <stdio.h>
#undef assert
#define assert(cond) do {if (!(cond)) { fprintf(stderr, "Assertion '" #cond "' failed at %s:%d\n", __FILE__, __LINE__); fflush(stderr); abort(); }} while(0)
#else
#include <assert.h>
#endif

typedef enum task_flags {
	FLAG_NONE	= 0 << 0,
	FLAG_RETURN	= 1 << 0, /* Set & check return code */
	FLAG_FUNC	= 1 << 1, /* Set & check custom function */
} task_flags_t;

typedef struct task {
	uint64_t	id;

	task_flags_t	flags;
	amtime_t	sleep_for;

	void*		check_ret;
	ambool_t 	check_custom_func;
	ambool_t	check_done;
} task_t;

typedef struct stats {
	volatile uint64_t scheduled;
	volatile uint64_t returned;
	volatile uint64_t custom_func;
} stats_t;

typedef struct stat_set {
	stats_t expected;
	stats_t received;
} stat_set_t;


static void* task_function_default(void* arg)
{
	struct timespec sleep_time = { .tv_sec = 0, .tv_nsec = 0 };
	task_t* task = arg;

	if (task->sleep_for > 0) {
		sleep_time.tv_nsec = task->sleep_for;
		nanosleep(&sleep_time, NULL);
	}

	task->check_done = am_true;
	return (void*)(task->id + (uint64_t)task);
}

static void* task_function_custom(void* arg)
{
	task_t* task = arg;
	task->check_custom_func = am_true;
	return task_function_default(arg);
}


/* Assumes task->id is set */
static void task_parametrize(task_t* task, uint64_t id, stats_t* stats)
{
	memset(task, 0, sizeof(*task));
	task->id = id;
	stats->scheduled++;

	if (id & 0x1) {
		task->flags |= FLAG_RETURN;
		stats->returned++;
	}
	id = id >> 1;

	if (id & 0x1) {
		task->flags |= FLAG_FUNC;
		stats->custom_func++;
	}
	id = id >> 1;

	task->sleep_for = AMTIME_USEC * (id & 0xF);
	id = id >> 4;
}

static amrc_t task_check(task_t* task, stats_t* stats)
{
	assert(task->check_done);
	if (stats) stats->scheduled++;

	if (task->flags & FLAG_RETURN) {
		assert(task->check_ret == (void*)(task->id + (uint64_t)task));
		if (stats) stats->returned++;
	}
	else
		assert(task->check_ret == NULL);

	if (task->flags & FLAG_FUNC) {
		assert(task->check_custom_func);
		if (stats) stats->custom_func++;
	}
	else
		assert(!task->check_custom_func);

	return AMRC_SUCCESS;
}

static amrc_t task_schedule(lam_thread_pool_t* tp, task_t* task)
{
	void** ret_ptr = NULL;
	lam_thread_func_t func = NULL;

	if (task->flags & FLAG_RETURN)
		ret_ptr = &task->check_ret;
	if (task->flags & FLAG_FUNC)
		func = task_function_custom;

	return lam_thread_pool_run(tp, func, task, ret_ptr);
}


static amrc_t check_default_func()
{
	lam_thread_pool_stats_t stats;
	lam_thread_pool_t* tp;
	task_t task1;
	task_t task2;
	amrc_t rc;

	tp = lam_thread_pool_create(NULL);
	assert(tp != NULL);

	memset(&task1, 0, sizeof(task1));
	task1.id = 1;
	memset(&task2, 0, sizeof(task2));
	task2.id = 2;
	task2.flags = FLAG_FUNC;

	rc = task_schedule(tp, &task1);
	assert(rc == AMRC_ERROR);
	rc = task_schedule(tp, &task2);
	assert(rc == AMRC_SUCCESS);
	rc = lam_thread_pool_set_default_func(tp, task_function_default);
	assert(rc == AMRC_SUCCESS);
	rc = task_schedule(tp, &task1);
	assert(rc == AMRC_SUCCESS);

	rc = lam_thread_pool_destroy(tp, &stats);
	assert(rc == AMRC_SUCCESS);
	rc = task_check(&task1, NULL);
	assert(rc == AMRC_SUCCESS);
	rc = task_check(&task2, NULL);
	assert(rc == AMRC_SUCCESS);

	assert(stats.threads_created >= 1);
	assert(stats.busy_task_num.sum == 2);
	return AMRC_SUCCESS;
}

static amrc_t check_functional_tests()
{
	/* Check basic operations */
	check_default_func();
	/* TODO */

	/* Check flags function */
	/* TODO */
	return AMRC_SUCCESS;
}

enum thread_defaults {
	MAX_OBJECTS_PER_LIST = 8192,
};

typedef struct thread_ctx {
	lam_thread_pool_t* tp;
	task_t tasks[MAX_OBJECTS_PER_LIST];
	uint64_t num_tasks;
	uint64_t sent;
	stats_t expected;
} thread_ctx_t;

static void thread_ctx_init(thread_ctx_t* ctx, lam_thread_pool_t* tp, uint64_t num)
{
	uint64_t i;

	memset(ctx, 0, sizeof(*ctx));
	ctx->tp = tp;
	ctx->num_tasks = num;
	for (i = 0; i < num; i++) {
		task_parametrize(&ctx->tasks[i], i, &ctx->expected);
	}
}

static void thread_ctx_list_init(thread_ctx_t* ctx, uint64_t len, lam_thread_pool_t* tp, uint64_t num)
{
	uint64_t i;

	memset(ctx, 0, sizeof(*ctx));
	ctx->tp = tp;
	ctx->num_tasks = num;
	for (i = 0; i < len; i++)
		thread_ctx_init(&ctx[i], tp, num);
}

static void thread_ctx_check(thread_ctx_t* ctx)
{
	stats_t received;
	uint64_t i;

	memset(&received, 0, sizeof(received));
	assert(ctx->sent == ctx->num_tasks);
	for (i = 0; i < ctx->num_tasks; i++) {
		task_check(&ctx->tasks[i], &received);

	}
	assert(memcmp(&received, &ctx->expected, sizeof(received)) == 0);
}

static void thread_ctx_list_check(thread_ctx_t* ctx, uint64_t num, lam_thread_pool_stats_t* stats)
{
	stats_t received;

	memset(&received, 0, sizeof(received));
	for (; num > 0; num--, ctx++) {
		thread_ctx_check(ctx);
		received.scheduled += ctx->expected.scheduled;
		received.custom_func += ctx->expected.custom_func;
		received.returned += ctx->expected.returned;
	}
	assert(stats->tasks_created == received.scheduled);
	assert(stats->busy_task_num.sum == received.scheduled);
}

static void* thread_func_worker(void* arg)
{
	thread_ctx_t* ctx = arg;
	lam_thread_pool_t* tp = ctx->tp;
	task_t* tasks = ctx->tasks;
	uint64_t num_tasks = ctx->num_tasks;
	amrc_t rc;

	for (; num_tasks > 0; num_tasks--, tasks++) {
		rc = task_schedule(tp, tasks);
		assert(rc == AMRC_SUCCESS);
		ctx->sent++;
	}

	return NULL;
}

void stats_print(lam_thread_pool_stats_t* stats)
{
	char out_buffer[128];

	printf("Threads started: %lu\n", stats->threads_created);
	printf("Tasks processed: %lu\n", stats->tasks_created);

	amstat_2str(&stats->active_thread_count, out_buffer, sizeof(out_buffer));
	printf("Active thread distribution.: %s\n", out_buffer);

	amstat_2str(&stats->idle_thread_count, out_buffer, sizeof(out_buffer));
	printf("Idle thread distribution...: %s\n", out_buffer);

	amstat_2str(&stats->task_delay, out_buffer, sizeof(out_buffer));
	printf("Task execution delay.......: %s\n", out_buffer);

	amstat_2str(&stats->tasks_processed, out_buffer, sizeof(out_buffer));
	printf("Tasks before idle timeout..: %s\n", out_buffer);

	amstat_2str(&stats->busy_task_num, out_buffer, sizeof(out_buffer));
	printf("Continuous task streak.....: %s\n", out_buffer);

	amstat_2str(&stats->queue_depth, out_buffer, sizeof(out_buffer));
	printf("Queue depth at schedule....: %s\n", out_buffer);

	printf("\n");
}

static void run_threaded_test(uint64_t workers, uint64_t thread_min, uint64_t thread_max)
{
	thread_ctx_t ctx[2];
	lam_thread_pool_t* worker_tp;
	lam_thread_pool_config_t worker_config;
	lam_thread_pool_stats_t worker_stats;
	lam_thread_pool_t* test_tp;
	lam_thread_pool_config_t test_config;
	lam_thread_pool_stats_t test_stats;
	struct timespec poll_time = { .tv_sec = 0, .tv_nsec = AMTIME_MSEC * 5 };
	uint64_t i;
	amrc_t rc;

	assert(workers == 1 || workers == 2);

	memset(&worker_config, 0, sizeof(worker_config));
	worker_config.min_threads = workers;
	worker_config.max_threads = workers;
	worker_config.default_func = thread_func_worker;
	worker_tp = lam_thread_pool_create(&worker_config);
	assert(worker_tp != NULL);

	memset(&test_config, 0, sizeof(test_config));
	test_config.flags = LIBAM_THREAD_POOL_FUNC_OVERRIDE;
	test_config.poll_freq = AMTIME_MSEC * 1;
	test_config.min_threads = thread_min;
	test_config.max_threads = thread_max;
	test_config.default_func = task_function_default;
	test_config.backlog = MAX_OBJECTS_PER_LIST * workers;
	test_tp = lam_thread_pool_create(&test_config);
	assert(test_tp != NULL);

	thread_ctx_list_init(ctx, workers, test_tp, MAX_OBJECTS_PER_LIST);

	while (lam_thread_pool_get_idle_thread_count(test_tp) < thread_min)
		nanosleep(&poll_time, NULL);

	for (i = 0; i < workers; i++) {
		rc = lam_thread_pool_run(worker_tp, NULL, &ctx[i], NULL);
		assert(rc == AMRC_SUCCESS);
	}

	rc = lam_thread_pool_destroy(worker_tp, &worker_stats);
	assert(rc == AMRC_SUCCESS);
	assert(worker_stats.threads_created == workers);
	assert(worker_stats.tasks_created == workers);
	assert(worker_stats.busy_task_num.sum == workers);

	rc = lam_thread_pool_destroy(test_tp, &test_stats);
	assert(rc == AMRC_SUCCESS);

	/*printf("\nStats from run with %lu workers, %lu min, %lu max\n", workers, thread_min, thread_max);*/
	/*stats_print(&test_stats);*/

	thread_ctx_list_check(ctx, workers, &test_stats);
}

static void add_cpu_number(uint64_t* cpu_number_array, uint64_t array_len, uint64_t ent)
{
	while (1) {
		assert(array_len > 1);
		if (*cpu_number_array == UINT64_MAX) {
			*cpu_number_array = ent;
			return;
		}

		if (*cpu_number_array == ent)
			return;

		array_len--;
		cpu_number_array++;
	}

	abort();
}

static void init_cpu_array(uint64_t* cpu_number_array, uint64_t array_len)
{
	int procs;

	memset(cpu_number_array, -1, sizeof(*cpu_number_array) * array_len);

	procs = get_nprocs();
	if (procs <= 2)
		procs = 2;

	add_cpu_number(cpu_number_array, array_len, 1);
	add_cpu_number(cpu_number_array, array_len, procs);
	add_cpu_number(cpu_number_array, array_len, procs * 2);
}

int main()
{
	amtime_t start;
	uint64_t cpu_numbers[10];
	uint64_t* num_cpu;
	uint64_t i;

	init_cpu_array(cpu_numbers, ARRAY_SIZE(cpu_numbers));

	start = amtime_now();
	srandom(start);

	printf("libam testing of thread_pool starting.");
	fflush(stdout);

	check_functional_tests();

	for (i = 1; i <= 2; i++) {
		num_cpu = cpu_numbers;
		while (*num_cpu != UINT64_MAX) {
			run_threaded_test(i, *num_cpu, *num_cpu);
			run_threaded_test(i, 0, *num_cpu);
			run_threaded_test(i, *num_cpu, 0);
			num_cpu++;
		}
		printf(".");
		fflush(stdout);
	}

	printf("\nlibam testing of thread_pool done successfully (%.2lf seconds)!\n", ((double)amtime_now() - start) / ((double)AMTIME_SEC));

	return 0;
}
