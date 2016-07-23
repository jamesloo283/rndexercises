#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include <ucontext.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <limits.h>

#define MAX(a,b) ((a) > (b) ? a : b)
#define MIN(a,b) ((a) < (b) ? a : b)
#define GETTID() (pid_t)syscall(SYS_gettid)
/*
 * Multiple ways to see running threads of this process
 * 1) Nice tree structure of parent and children but not much details
 * *  pstree -p `pidof find_minmax`
 *
 * 2) Customized with details
 * *  ps -L -o pid,lwp,pri,psr,nice,start,stat,bsdtime,cmd,comm -C find_minmax
 * * A simpler customized details
 * *  ps H -C find_minmax -o 'pid tid cmd comm'
 *
 * Stopping process
 * 1) kill -l: lists avilable signals on the system
 * 2) kill -s SIGTERM|SIGHUP|SIGINT pid where pid is parent PID obtained above
 *
 * Looking for open files and ports
 * 1) sudo lsof -i
 * 2) sudo netstat -lptu
 * 3) sudo netstat -tulpn
 *
 * 2 ways to measure execution time:
 * 1) With bash built-in cmd 'time' for basic results
 *    time find_minmax
 * 2) With system cmd '/usr/bin/time -v' for detailed results
 *    /usr/bin/time -v find_minmax
 *
 * To check installed glibc version: ldd --version
 *
 * To compile:
 * * gcc -pthread -lrt -g -o find_minmax find_minmax.c
 *
 * Usage of this program
 * Usage: ./find_minmax dataset_size thread_count
 * * dataset_size: desired number of integers, min = 2, max = INT_MAX
 * * thread_count: desired number of threads, 0-1 = single threaded, 2+ = multi-threaded with this number of threads
 */

typedef struct {
	int min;
	int max;
	int thr_count;
	int *dataset;
	int dataset_size;
	int from;
	int to;
	int err;
} rlstarg_t;

static int sigs_init(void);
static void sigterm_hdl(int sig, siginfo_t *siginfo, void *ctx);
static void sigusr1_hdl(int sig, siginfo_t *siginfo, void *ctx);
static void sigusr2_hdl(int sig, siginfo_t *siginfo, void *ctx);
static void sigintr_hdl(int sig, siginfo_t *siginfo, void *ctx);
static void* sighdl_multithr(void *arg);
static void usage(char *arg);
static void* find_minmax_single_thr(void *arg);
static void find_minmax_multi_thr(void* arg);

/* to do: get rid of printf in sig handlers */
static int termsig = 0;
static int usr1sig = 0;
static int usr2sig = 0;
static int intsig = 0;	/* ctrl-C interrupt */
static int intrsig = 0;	/* SIGSTOP follow by SIGCONT interrupt */
static int sigwaitinfo_err = 0;
static int sigmisshdl_err = 0;


static void
sigusr1_hdl(int sig, siginfo_t *siginfo, void *ctx)
{
#if 0
	/* future use */
	ucontext_t *uctx;
	if (ctx) {
		uctx = ctx;
	}
#endif
	++usr1sig;
}

static void
sigusr2_hdl(int sig, siginfo_t *siginfo, void *ctx)
{
#if 0
	/* future use */
	ucontext_t *uctx;
	if (ctx) {
		uctx = ctx;
	}
#endif
	++usr2sig;
}

/* ctrl-C interrupt */
static void
sigintr_hdl(int sig, siginfo_t *siginfo, void *ctx)
{
#if 0
	/* future use */
	ucontext_t *uctx;
	if (ctx) {
		uctx = ctx;
	}
#endif
	++intsig;
}

static void
sigterm_hdl(int sig, siginfo_t *siginfo, void *ctx)
{
#if 0
	/* future use */
	ucontext_t *uctx;
	if (ctx) {
		uctx = ctx;
	}
#endif
	termsig = 1;
}

static void*
sighdl_multithr(void *arg)
{
	sigset_t s;
	siginfo_t sf;
	int sig;
	while (1) {
		/* include all sigs and wait on them */
		sigfillset(&s);
		sig = sigwaitinfo(&s, &sf);
		if (-1 == sig) {
			if (EINTR == errno) {
				/* SIGSTOP follow by SIGCONT */
				sigintr_hdl(sig, &sf, NULL);
				++intrsig;
				continue;
			} else {
				perror("sigwaitinfo(...)");
				++sigwaitinfo_err;
			}
		}

		switch (sig) {
		case SIGTERM:	/* terminate */
		case SIGHUP:	/* hang up */
		case SIGINT:	/* ctrl-C interrupt */
			/* terminate for all these sigs */
			sigterm_hdl(sig, &sf, NULL);
			break;
		case SIGUSR1:
			sigusr1_hdl(sig, &sf, NULL);
			break;
		case SIGUSR2:
			sigusr2_hdl(sig, &sf, NULL);
			break;
		default:
			printf("Miss-handled signal: %d: \n", sig);
			++sigmisshdl_err;
			break;
		}
	}
}

static int
sigs_init(void)
{
	sigset_t allset;
	/* block all sigs initially */
	sigfillset(&allset);
	int ret = pthread_sigmask(SIG_BLOCK, &allset, NULL);
	if (ret) {
		perror("pthread_sigmask(...)");
		return -1;
	}

	/* create the signal handler thread */
	pthread_t sigh_thr;
	ret = pthread_create(&sigh_thr, NULL, &sighdl_multithr, NULL);
	if (ret) {
		perror("pthread_create(..., sighandler, ...)");
		return -1;
	}
	if (pthread_setname_np(sigh_thr, "sighdl") ) {
		perror("pthread_setname_np(..., sighandler, ...)");
	}

#if 0
	/*
	 * the following method of configuring signal action is used
	 * in single threaded apps. it cannot be used for this app
	 */
	struct sigaction act;
	memset(&act, 0, sizeof(act));
	/* sets up signal handler */
	act.sa_sigaction = &sigterm_hdl;
	/*
	 * this flag configures the use of more portable sigaction as
	 * opposed to the older and not portable signal/sa_handler
	 */
	act.sa_flags = SA_SIGINFO;
	/* now, add all desired sigs to be handled by this action */
	if (sigaction(SIGTERM, &act, NULL) < 0) {
		perror ("sigaction(..., SIGTERM, ...)");
		exit(1);
	}
	/* ctrl-C */
	if (sigaction(SIGINT, &act, NULL) < 0) {
		perror ("sigaction(..., SIGINT, ...)");
		exit(1);
	}
	if (sigaction(SIGHUP, &act, NULL) < 0) {
		perror ("sigaction(..., SIGHUP, ...)");
		exit(1);
	}
#endif
	return 0;
}

int dataset_init(int *container, unsigned int size)
{
	if (!container) {
		return 0;
	}

	int i;
	int prev = 0;
	int curr = 0;
	time_t t;

	/* seed random first
	 *
	 * seed with the same value to get the same sequence of
	 * of values everytime, this is good for testing
	 */
	 //srandom((unsigned int) 0);
	 /*
	 * seed with current time to get different sequence for
	 * production code
	 */
	srandom((unsigned int) time(&t));
	for (i = 0; i < size; i++) {
		while ((curr = random() % size) == prev);
		prev = curr;
		container[i] = curr;
	}

/*
	// test output
	for (i = 0; i < size; i++) {
		fprintf(stdout, "Data: %d\n", container[i]);
	}
*/
	return i;
}

static void* find_minmax_single_thr(void *arg)
{
	if (!arg) {
		return;
	}
	rlstarg_t *rlstarg = arg;
	rlstarg->min = INT_MAX;
	rlstarg->max = 0;
	int i;
	/* note the loop includes rlstarg->to */
	for (i = rlstarg->from; i <= rlstarg->to; i++) {
		rlstarg->min = MIN(rlstarg->min, rlstarg->dataset[i]);
		rlstarg->max = MAX(rlstarg->max, rlstarg->dataset[i]);
	}
}

static void find_minmax_multi_thr(void* arg)
{
	if (!arg) {
		return;
	}
	sigset_t allset;
	sigfillset(&allset);
	int ret = pthread_sigmask(SIG_BLOCK, &allset, NULL);
	if (ret) {
		perror("pthread_sigmask(...)");
		/* no need to exit for this failure */
	}
	rlstarg_t *rlstarg = arg;
	// this will store pthread_t*
	pthread_t **threads = malloc(rlstarg->thr_count *
				     sizeof(pthread_t*));
	if (!threads) {
		perror("malloc(...)");
		rlstarg->err = 1;
		return;
	}
	char thrname[32];
	// this will store rlstarg_t* for each thread
	rlstarg_t **thrargs = malloc(rlstarg->thr_count * sizeof(rlstarg_t*));
	if (!thrargs) {
		free(threads);
		perror("malloc(...)");
		rlstarg->err = 1;
		return;
	}
	int i;
	for (i = 0; i < rlstarg->thr_count; i++) {
		/* allocate each thread */
		threads[i] = malloc(sizeof(pthread_t));
		if (!threads[i]) {
			continue;
		}
		/* now each arg */
		thrargs[i] = malloc(sizeof(rlstarg_t));
		if (!thrargs[i]) {
			free(threads[i]);
			continue;
		}
		/* reference dataset, note no copying */
		thrargs[i]->dataset = rlstarg->dataset;
		/* starting offset in dataset for this thread */
		thrargs[i]->from = i*(rlstarg->dataset_size/rlstarg->thr_count);
		/* end offset is the minimum of these 2 since dataset_size may not be a multiple of thr_count */
		thrargs[i]->to = MIN(thrargs[i]->from +
			(rlstarg->dataset_size / rlstarg->thr_count) - 1,
			rlstarg->dataset_size / rlstarg->thr_count);
		/* now create the thread with find_minmax_single_thr */
		ret = pthread_create(threads[i], NULL,
				&find_minmax_single_thr, thrargs[i]);
		if (ret) {
			perror("pthread_create(...)");
			free(threads[i]);
			free(thrargs[i]);
			continue;
		}
		/* assign each thread a name, easier to dbg */
		snprintf(thrname, 32, "minmaxthr-%d", i);
		if (pthread_setname_np(threads[i], thrname)) {
			/* this error is seen if dataset_size is too
			 * small that the threads completed their job
			 * before we ge here
			 */
			if (errno != ENOENT) {
				perror("pthread_setname_np(...)");
			}
		}
	}
	for (i = 0; i < rlstarg->thr_count; i++) {
		if (threads[i]) {
			pthread_join(*threads[i], NULL);
		}
	}

	for (i = 0; i < rlstarg->thr_count; i++) {
		rlstarg->min = MIN(rlstarg->min, thrargs[i]->min);
		rlstarg->max = MAX(rlstarg->max, thrargs[i]->max);
		free(thrargs[i]);
	}
	if (thrargs) free(thrargs);

	for (i = 0; i < rlstarg->thr_count; i++) {
		if (threads[i]) free(threads[i]);
	}
	if (threads) free(threads);
}

static void usage(char *arg)
{
	fprintf(stdout, "Usage: %s dataset_size thread_count\n", arg);
}
int
main(int argc, char **argv)
{
	if (sigs_init()) {
		printf("Failed configuring signals, terminating\n");
		exit(1);
	}

	if (argc < 2) {
		usage(argv[0]);
		exit(1);
	}

	/* 0 or 1 = single threaded, 2 or more = multi-threaded */
	int dataset_size = atoi(argv[1]);
	rlstarg_t rlst;
	rlst.err = 0;
	if (dataset_size < 2) {
		fprintf(stdout, "Dataset size is too small\n");
		exit(1);
	}
	rlst.dataset_size = dataset_size;
	rlst.from = 0;
	rlst.to = dataset_size;
	/* default to single threaded mode initially */
	rlst.thr_count = 1;
	if (argc > 2) {
		rlst.thr_count = atoi(argv[2]);
	}

	/* allocate dataset */
	rlst.dataset = malloc(dataset_size * sizeof(int));
	if (!rlst.dataset) {
		perror("Failed allocating dataset, terminating");
		exit(1);
	}
	if (!dataset_init(rlst.dataset, dataset_size)) {
		perror("Failed initing data set, terminating\n");
		free(rlst.dataset);
		exit(1);
	}

	if (rlst.thr_count < 2) {
		fprintf(stdout, "Running in single threaded mode\n");
		find_minmax_single_thr(&rlst);
	} else {
		fprintf(stdout, "Running in multi-threaded mode with %d threads\n", rlst.thr_count);
		find_minmax_multi_thr(&rlst);
	}

	if (!rlst.err) {
		fprintf(stdout, "***** Min: %d *****\n", rlst.min);
		fprintf(stdout, "***** Max: %d *****\n", rlst.max);
	} else {
		fprintf(stdout, "Errors encountered");
	}

	if (rlst.dataset) {
		free(rlst.dataset);
	}

	return 0;
}
