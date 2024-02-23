#include "shell.h"

typedef struct proc {
  pid_t pid;    /* process identifier */
  int state;    /* RUNNING or STOPPED or FINISHED */
  int exitcode; /* -1 if exit status not yet received */
} proc_t;

typedef struct job {
  pid_t pgid;            /* 0 if slot is free */
  proc_t *proc;          /* array of processes running in as a job */
  struct termios tmodes; /* saved terminal modes */
  int nproc;             /* number of processes */
  int state;             /* changes when live processes have same state */
  char *command;         /* textual representation of command line */
} job_t;

static job_t *jobs = NULL;          /* array of all jobs */
static int njobmax = 1;             /* number of slots in jobs array */
static int tty_fd = -1;             /* controlling terminal file descriptor */
static struct termios shell_tmodes; /* saved shell terminal modes */

static void sigchld_handler(int sig) {
  int old_errno = errno;
  pid_t pid;
  int status;
  /* TODO: Change state (FINISHED, RUNNING, STOPPED) of processes and jobs.
   * Bury all children that finished saving their status in jobs. */
#ifdef STUDENT

  while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
    for (int i = 0; i < njobmax; i++) {
      bool flag_stopped = false;
      bool flag_running = false;
      int nproc = jobs[i].nproc;

      for (int j = 0; j < nproc; j++) {
        proc_t *actproc = &jobs[i].proc[j];

        if (actproc->state == FINISHED) {
          continue; // nothing to do here
        }

        // update exitcode and state for pid returned by waitpid
        if (pid == actproc->pid) {
          actproc->exitcode = -1;

          if (WIFCONTINUED(status)) {
            actproc->state = RUNNING;
          } else if (WIFSTOPPED(status)) {
            actproc->state = STOPPED;
          } else if (WIFEXITED(status)) {
            actproc->state = FINISHED;
            actproc->exitcode = status;
          } else if (WIFSIGNALED(status)) {
            actproc->state = FINISHED;
            actproc->exitcode = status;
          }
        }

        if (actproc->state == RUNNING) {
          flag_running = true;
        } else if (actproc->state == STOPPED) {
          flag_stopped = true;
        }
      }

      // update job status (if any process in group set flag)
      if (flag_running) {
        jobs[i].state = RUNNING;
      } else if (flag_stopped) {
        jobs[i].state = STOPPED;
      } else {
        jobs[i].state = FINISHED;
      }
    }
  }

#endif /* !STUDENT */
  errno = old_errno;
}

/* When pipeline is done, its exitcode is fetched from the last process. */
static int exitcode(job_t *job) {
  return job->proc[job->nproc - 1].exitcode;
}

static int allocjob(void) {
  /* Find empty slot for background job. */
  for (int j = BG; j < njobmax; j++)
    if (jobs[j].pgid == 0)
      return j;

  /* If none found, allocate new one. */
  jobs = realloc(jobs, sizeof(job_t) * (njobmax + 1));
  memset(&jobs[njobmax], 0, sizeof(job_t));
  return njobmax++;
}

static int allocproc(int j) {
  job_t *job = &jobs[j];
  job->proc = realloc(job->proc, sizeof(proc_t) * (job->nproc + 1));
  return job->nproc++;
}

int addjob(pid_t pgid, int bg) {
  int j = bg ? allocjob() : FG;
  job_t *job = &jobs[j];
  /* Initial state of a job. */
  job->pgid = pgid;
  job->state = RUNNING;
  job->command = NULL;
  job->proc = NULL;
  job->nproc = 0;
  job->tmodes = shell_tmodes;
  return j;
}

static void deljob(job_t *job) {
  assert(job->state == FINISHED);
  free(job->command);
  free(job->proc);
  job->pgid = 0;
  job->command = NULL;
  job->proc = NULL;
  job->nproc = 0;
}

static void movejob(int from, int to) {
  assert(jobs[to].pgid == 0);
  memcpy(&jobs[to], &jobs[from], sizeof(job_t));
  memset(&jobs[from], 0, sizeof(job_t));
}

static void mkcommand(char **cmdp, char **argv) {
  if (*cmdp)
    strapp(cmdp, " | ");

  for (strapp(cmdp, *argv++); *argv; argv++) {
    strapp(cmdp, " ");
    strapp(cmdp, *argv);
  }
}

void addproc(int j, pid_t pid, char **argv) {
  assert(j < njobmax);
  job_t *job = &jobs[j];

  int p = allocproc(j);
  proc_t *proc = &job->proc[p];
  /* Initial state of a process. */
  proc->pid = pid;
  proc->state = RUNNING;
  proc->exitcode = -1;
  mkcommand(&job->command, argv);
}

/* Returns job's state.
 * If it's finished, delete it and return exitcode through statusp. */
static int jobstate(int j, int *statusp) {
  assert(j < njobmax);
  job_t *job = &jobs[j];
  int state = job->state;

  /* TODO: Handle case where job has finished. */
#ifdef STUDENT
  if (state == FINISHED) {
    *statusp = exitcode(job);
    deljob(job);
  }
#endif /* !STUDENT */

  return state;
}

char *jobcmd(int j) {
  assert(j < njobmax);
  job_t *job = &jobs[j];
  return job->command;
}

/* Continues a job that has been stopped. If move to foreground was requested,
 * then move the job to foreground and start monitoring it. */
bool resumejob(int j, int bg, sigset_t *mask) {
  if (j < 0) {
    for (j = njobmax - 1; j > 0 && jobs[j].state == FINISHED; j--)
      continue;
  }

  if (j >= njobmax || jobs[j].state == FINISHED)
    return false;

    /* TODO: Continue stopped job. Possibly move job to foreground slot. */
#ifdef STUDENT

  if (!bg) {
    movejob(j, FG);

    // set job as FG for terminal and restore saved modes
    Tcsetpgrp(tty_fd, jobs[FG].pgid);
    Tcsetattr(tty_fd, TCSADRAIN, &jobs[FG].tmodes);

    Kill(-jobs[FG].pgid, SIGCONT);

    // wait for SIGCONT to be handled
    while (jobs[FG].state != RUNNING) {
      Sigsuspend(mask);
    }
    printf("[%d] continue '%s'\n", j, jobs[FG].command);
    monitorjob(mask);
  } else {
    Kill(-jobs[j].pgid, SIGCONT);
    printf("[%d] continue '%s'\n", j, jobs[j].command);
  }

#endif /* !STUDENT */

  return true;
}

/* Kill the job by sending it a SIGTERM. */
bool killjob(int j) {
  if (j >= njobmax || jobs[j].state == FINISHED)
    return false;
  debug("[%d] killing '%s'\n", j, jobs[j].command);

  /* TODO: I love the smell of napalm in the morning. */
#ifdef STUDENT

  Kill(-jobs[j].pgid, SIGTERM);
  // in case some jobs are stopped
  Kill(-jobs[j].pgid, SIGCONT);

#endif /* !STUDENT */

  return true;
}

/* Report state of requested background jobs. Clean up finished jobs. */
void watchjobs(int which) {
  for (int j = BG; j < njobmax; j++) {
    if (jobs[j].pgid == 0)
      continue;

      /* TODO: Report job number, state, command and exit code or signal. */
#ifdef STUDENT

    int exitc = 0;

    if (which == ALL || which == jobs[j].state) {
      switch (jobs[j].state) {
        case RUNNING:
          printf("[%d] running '%s'\n", j, jobs[j].command);
          break;

        case STOPPED:
          printf("[%d] suspended '%s'\n", j, jobs[j].command);
          break;

        case FINISHED:
          exitc = exitcode(&jobs[j]);

          if (WIFSIGNALED(exitc)) {
            printf("[%d] killed '%s' by signal %d\n", j, jobs[j].command,
                   WTERMSIG(exitc));
          } else {
            printf("[%d] exited '%s', status=%d\n", j, jobs[j].command,
                   WEXITSTATUS(exitc));
          }

          deljob(&jobs[j]);
          break;

        default:
          app_error("[watchjobs]: Wrong job state!\n");
      }
    }

#endif /* !STUDENT */
  }
}

/* Monitor job execution. If it gets stopped move it to background.
 * When a job has finished or has been stopped move shell to foreground. */
int monitorjob(sigset_t *mask) {
  int exitcode = 0, state;

  /* TODO: Following code requires use of Tcsetpgrp of tty_fd. */
#ifdef STUDENT

  // wait for job to stop or finish
  state = jobstate(FG, &exitcode);
  while (state == RUNNING) {
    Sigsuspend(mask);
    state = jobstate(FG, &exitcode);
  }

  // if stopped move it to background
  if (state == STOPPED) {
    int new_job = allocjob();
    movejob(FG, new_job);
    Tcsetattr(tty_fd, TCSADRAIN, &jobs[new_job].tmodes);
  }

  // move shell to FG and restore modes
  setfgpgrp(getpgrp());
  Tcsetattr(tty_fd, TCSADRAIN, &shell_tmodes);

#endif /* !STUDENT */

  return exitcode;
}

/* Called just at the beginning of shell's life. */
void initjobs(void) {
  struct sigaction act = {
    .sa_flags = SA_RESTART,
    .sa_handler = sigchld_handler,
  };

  /* Block SIGINT for the duration of `sigchld_handler`
   * in case `sigint_handler` does something crazy like `longjmp`. */
  sigemptyset(&act.sa_mask);
  sigaddset(&act.sa_mask, SIGINT);
  Sigaction(SIGCHLD, &act, NULL);

  jobs = calloc(sizeof(job_t), 1);

  /* Assume we're running in interactive mode, so move us to foreground.
   * Duplicate terminal fd, but do not leak it to subprocesses that execve. */
  assert(isatty(STDIN_FILENO));
  tty_fd = Dup(STDIN_FILENO);
  fcntl(tty_fd, F_SETFD, FD_CLOEXEC);

  /* Take control of the terminal. */
  Tcsetpgrp(tty_fd, getpgrp());

  /* Save default terminal attributes for the shell. */
  Tcgetattr(tty_fd, &shell_tmodes);
}

/* Called just before the shell finishes. */
void shutdownjobs(void) {
  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);

  /* TODO: Kill remaining jobs and wait for them to finish. */
#ifdef STUDENT

  for (int j = 0; j < njobmax; j++) {
    if (jobs[j].state == FINISHED) {
      continue;
    }

    killjob(j);

    // wait for signal to be handled
    while (jobs[j].state != FINISHED) {
      Sigsuspend(&mask);
    }
  }

#endif /* !STUDENT */

  watchjobs(FINISHED);

  Sigprocmask(SIG_SETMASK, &mask, NULL);

  Close(tty_fd);
}

/* Sets foreground process group to `pgid`. */
void setfgpgrp(pid_t pgid) {
  Tcsetpgrp(tty_fd, pgid);
}
