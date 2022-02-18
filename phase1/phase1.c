/* ------------------------------------------------------------------------
   Kyle AuBuchon
   phase1.c
   CSCV 452
   ------------------------------------------------------------------------ */
#include <stdlib.h>
 
#include <string.h>
 
#include <stdio.h>
 
#include <phase1.h>
 
#include "kernel.h"
 
#include "usloss.h"
 
/* ------------------------- Prototypes ----------------------------------- */
int sentinel(void * );
extern int start1(void * );
void dispatcher(void);
void launch();
void dump_processes();
 
static void kernelCheck(char * caller_name);
static void enableInterrupts(char * caller_name);
static void disableInterrupts(char * caller_name);
 
static void check_deadlock();
int zap(int pid);
int is_zapped(void);
 
proc_ptr find_process(int pid);
 
void clockHandler(int dev, int unit);
int read_cur_start_time(void);
void time_slice(void);
 
void empty_proc(int pid);
 
int block_me(int block_status);
int unblock_proc(int pid);
 
static void purge_ready(char * caller_name);
static void purge_ready_helper(proc_ptr * b_t, proc_ptr * q_t, proc_ptr * r_t, proc_ptr * z_t, proc_ptr found);
 
/* -------------------------- Globals ------------------------------------- */
/* Patrick's Debugging Global Variable... */
int debugflag = 0;
 
/* the process table */
proc_struct ProcTable[MAXPROC];
 
/* Process Lists  */
proc_ptr BlockedList;
proc_ptr QuitList;
proc_ptr ReadyList[SENTINELPRIORITY + 1];
proc_ptr ZappedList;
 
/* global Number of Processes */
int procCount;
 
/* current Process ID */
proc_ptr Current;
 
/* the next pid to be assigned */
unsigned int next_pid = SENTINELPID;
 
/*-- -- -- -- -- -- -- -- -- -- -- -- --Functions-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- - * /
/* ------------------------------------------------------------------------
   Name - startup
   Purpose - Initializes process lists and clock interrupt vector.
         Start up sentinel process and the test process.
   Parameters - none, called by USLOSS
   Returns - nothing
   Side Effects - lots, starts the whole thing
   ----------------------------------------------------------------------- */
void startup() {
  int i; /* loop index */
  int result; /* value returned by call to fork1() */
 
  /* initialize the Process Table */
  if (DEBUG && debugflag) {
    console("startup(): initilizing process table, ProcTable[] \n");
  }
  for (i = 1; i <= MAXPROC; i++) {
    ProcTable[i].pid = EMPTY;
    ProcTable[i].status = EMPTY;
    ProcTable[i].next_proc_ptr = NULL;
    ProcTable[i].child_proc_ptr = NULL;
    ProcTable[i].next_sibling_ptr = NULL;
  }
 
  /* initializes the Process Number */
  procCount = 0;
 
  /* initialize all Process Lists */
  if (DEBUG && debugflag) {
    console(" - startup(): initializing the blocked, quit, ready, and zapped lists -\n");
  }
  BlockedList = NULL;
  QuitList = NULL;
  for (i = MAXPRIORITY; i <= SENTINELPRIORITY; i++) {
    ReadyList[i] = NULL;
  }
  ZappedList = NULL;
 
  /* initialize the Clock Interrupt Handler */
  int_vec[CLOCK_INT] = clockHandler;
 
  /* startup a sentinel process */
  if (DEBUG && debugflag)
    console(" - startup(): calling fork1() for sentinel -\n");
  result = fork1("sentinel", sentinel, NULL, USLOSS_MIN_STACK, SENTINELPRIORITY);
  if (result < 0) {
    if (DEBUG && debugflag)
      console(" - startup(): fork1 of sentinel returned error, halting... -\n");
    halt(1);
  }
 
  /* start the test process */
  if (DEBUG && debugflag)
    console(" - startup(): calling fork1() for start1 -\n");
  result = fork1("start1", start1, NULL, 2 * USLOSS_MIN_STACK, 1);
  if (result < 0) {
    console(" - startup(): fork1 for start1 returned an error, halting... -\n");
    halt(1);
  }
 
  dispatcher();
 
  console(" - startup(): Should not see this message! -\n");
  console(" - Returned from fork1 call that created start1 -\n");
 
  return;
} /* startup */
 
/* ------------------------------------------------------------------------
   Name - finish
   Purpose - Required by USLOSS
   Parameters - none
   Returns - nothing
   Side Effects - none
   ----------------------------------------------------------------------- */
void finish() {
  if (debugflag)
    console("in finish...\n");
} /* finish */
 
int getpid() {
  return Current -> pid;
}
 
/* ------------------------------------------------------------------------
   Name - fork1
   Purpose - Gets a new process from the process table and initializes
             information of the process.  Updates information in the
             parent process to reflect this child process creation.
   Parameters - the process procedure address, the size of the stack and
                the priority to be assigned to the child process.
   Returns - the process id of the created child or -1 if no child could
             be created or if priority is not between max and min priority.
   Side Effects - ReadyList is changed, ProcTable is changed, Current
                  process information changed
   ------------------------------------------------------------------------ */
int fork1(char * name, int( * f)(char * ),
  char * arg, int stacksize, int priority) {
  int proc_slot;
  proc_ptr ready_temp;
 
  if (DEBUG && debugflag) {
    console(" - fork1(): creating process %s -\n", name);
  }
 
  /* test if in kernel mode, halts otherwise */
  kernelCheck("fork1");
  disableInterrupts("fork1");
 
  /* find an empty slot in the process table */
  proc_slot = 1;
  while ((ProcTable[proc_slot].pid != -1) && (proc_slot < MAXPROC))
    proc_slot += 1;
 
  /* return if stack size is too small */
  if (stacksize < USLOSS_MIN_STACK) {
 
    enableInterrupts("fork1");
    if (DEBUG && debugflag)
      console(" - fork1(): stack size is too small -\n");
    return (-2);
  }
 
  /* checks if no empty slots in the process table */
  if (proc_slot >= MAXPROC) {
    enableInterrupts("fork1");
    if (DEBUG && debugflag)
      console(" - fork1(): process table is full -\n");
    return (-1);
  }
  /* checks if process is sentinel */
  if (strcmp(name, "sentinel")) {
    if ((priority > MINPRIORITY) || (priority < MAXPRIORITY)) {
      enableInterrupts("fork1");
      if (DEBUG && debugflag)
        console(" - fork1(): priority out of range -\n");
      return (-1);
    }
  }
 
  if (f == NULL) {
    enableInterrupts("fork1");
    if (DEBUG && debugflag)
      console(" - fork1(): null function passed -\n");
    return (-1);
  }
 
  if (name == NULL) {
    enableInterrupts("fork1");
    if (DEBUG && debugflag)
      console(" - fork1(): null process name passed -\n");
    return (-1);
  }
 
  /* fill-in entry in process table */
  if (strlen(name) >= (MAXNAME - 1)) {
    if (DEBUG && debugflag)
      console(" - fork1(): process name is too long.  Halting... -\n");
    halt(1);
  }
  strcpy(ProcTable[proc_slot].name, name);
 
  ProcTable[proc_slot].start_func = f;
 
  if (arg == NULL)
    ProcTable[proc_slot].start_arg[0] = '\0';
  else if (strlen(arg) >= (MAXARG - 1)) {
    if (DEBUG && debugflag)
      console(" - fork1(): argument too long.  Halting... -\n");
    halt(1);
  } else
    strcpy(ProcTable[proc_slot].start_arg, arg);
 
  ProcTable[proc_slot].stacksize = stacksize;
 
  ProcTable[proc_slot].pid = next_pid;
  next_pid += 1;
 
  if (Current != NULL)
    ProcTable[proc_slot].parent_pid = Current -> pid;
  else
    ProcTable[proc_slot].parent_pid = 0;
 
  ProcTable[proc_slot].childCount = 0;
 
  ProcTable[proc_slot].status = READY;
 
  ProcTable[proc_slot].priority = priority;
 
  ProcTable[proc_slot].stack = (char * )(malloc(stacksize));
 
  /* initialize context for this process, but use launch function
   pointer for the initial value of the process's program
   counter (PC) */
  context_init( & (ProcTable[proc_slot].state), psr_get(), ProcTable[proc_slot].stack, ProcTable[proc_slot].stacksize, launch);
 
  /* updates the ReadyList */
  ready_temp = ReadyList[priority];
 
  /*check if list is empty*/
  if (ReadyList[priority] == NULL) {
    ReadyList[priority] = & ProcTable[proc_slot];
  } else {
    while (ready_temp != NULL)
      ready_temp = ready_temp -> next_proc_ptr;
    ready_temp = & ProcTable[proc_slot];
  }
 
  /* updates Current */
  if (Current != NULL) {
    Current -> child_proc_ptr = & ProcTable[proc_slot];
    Current -> childCount += 1;
  }
 
  /* for future phase(s) */
  p1_fork(ProcTable[proc_slot].pid);
 
  /* calls dispatcher if not sentinel */
  if (strcmp(name, "sentinel"))
    dispatcher();
 
  /* enable interrupts */
  enableInterrupts("fork1");
 
  /* updates Process Number */
  procCount += 1;
 
  return (ProcTable[proc_slot].pid);
} /* fork1 */
 
/* ------------------------------------------------------------------------
   Name - launch
   Purpose - Dummy function to enable interrupts and launch a given process
             upon startup.
   Parameters - none
   Returns - nothing
   Side Effects - enable interrupts
   ------------------------------------------------------------------------ */
void launch() {
  int result;
 
  if (DEBUG && debugflag)
    console(" - launch(): started -\n");
 
  kernelCheck("launch");
  disableInterrupts("launch");
 
  /* enable interrupts */
  enableInterrupts("launch");
 
  if (DEBUG && debugflag)
    console(" - Process %d returned to launch -\n", Current -> pid);
 
  /* call the function passed to fork1, and capture its return value */
  result = Current -> start_func(Current -> start_arg);
 
  quit(result);
} /* launch */
 
/* ------------------------------------------------------------------------
   Name - join
   Purpose - Wait for a child process (if one has been forked) to quit.  If 
             one has already quit, don't wait.
   Parameters - a pointer to an int where the termination code of the 
                quitting process is to be stored.
   Returns - the process id of the quitting child joined on.
        -1 if the process was zapped in the join
        -2 if the process has no children
   Side Effects - If no child process has quit before join is called, the 
                  parent is removed from the ready list and blocked.
   ------------------------------------------------------------------------ */
int join(int * code) {
  int child_pid = -1;
  proc_ptr join_temp;
 
  if (DEBUG && debugflag)
    console(" - join(): called -\n");
 
  /* test if in kernel mode, halts otherwise */
  kernelCheck("join");
 
  /* disbale interrupts */
  disableInterrupts("join");
 
  /* check if current process has children */
  if (!Current -> childCount) {
    /* enable interrupts */
    enableInterrupts("join");
    if (DEBUG && debugflag)
      console(" - join(): current process has no children -\n");
    return (-2);
  }
 
  /* check if current process has been zapped */
  if (Current -> status == ZAPPED) {
    /* enable interrupts */
    enableInterrupts("join");
    if (DEBUG && debugflag)
      console(" - join(): current process has been zapped -\n");
    return (-1);
  }
 
  /* check if current process has children in QuitList */
  if (Current -> status == QUIT) {
    console("Current status set to quit");
    child_pid = Current -> pid;
  }
 
  //join_temp = Current->child_proc_ptr;
  while (child_pid == -1) {
    join_temp = Current -> child_proc_ptr;
    while (join_temp != NULL) {
      if (join_temp -> status == QUIT) {
        child_pid = join_temp -> pid;
      }
      join_temp = join_temp -> next_sibling_ptr;
    }
    if (child_pid == -1) {
      Current -> status = BLOCKED;
      dispatcher();
    }
  }
 
  /* stores passed quit code */
  * code = find_process(child_pid) -> quit_code;
 
  /* enable interrupts */
  enableInterrupts("join");
  return child_pid;
} /* join */
 
/*----------------------------------------------------------------*
 * Name        : quit                                             *
 * Purpose     : Terminates the current process and returns the   *
 *               status to a join by its parent. If the current   *
 *               process has children instead prints appropriate  *
 *               error message and halts the program.             *
 * Parameters  : the code to return to the grieving parent        *
 * Returns     : nothing                                          *
 * Side Effects: changes the parent of pid child completion       *
 *               status list.                                     *
 *----------------------------------------------------------------*/
void quit(int status) {
  if (DEBUG && debugflag)
    console(" - quit(): started -\n");
 
  /* test if in kernel mode, halts otherwise */
  kernelCheck("quit");
 
  /* disbale interrupts */
  disableInterrupts("quit");
 
  /* checks if current process has any children */
  if (Current -> childCount != 0) {
    if (DEBUG && debugflag)
      console(" - quit(): current process has children and cannot quit. Halting... -\n");
    halt(1);
  }
 
  /* sets status of current process to quit */
 
  Current -> status = QUIT;
  Current -> quit_code = status;
  ReadyList[Current -> priority--] = NULL;
  Current -> quit_code = status;
 
  /* deals with the parent process */
  if (Current -> parent_pid) {
    proc_ptr parent_ptr = find_process(Current -> parent_pid);
    parent_ptr -> childCount--;
    parent_ptr -> status = READY;
    //parent_ptr->child_proc_ptr = NULL;
    //empty_proc(parent_ptr->child_proc_ptr);
  } else {
    empty_proc(Current -> pid);
  }
  /* for future phase(s) */
  p1_quit(Current -> pid);
  /* enable interrupts */
  enableInterrupts("quit");
 
  /* goes to dispatcher to decide who runs next */
  dispatcher();
 
} /* quit */
 
/* ------------------------------------------------------------------------
   Name - dispatcher
   Purpose - dispatches ready processes.  The process with the highest
             priority (the first on the ready list) is scheduled to
             run.  The old process is swapped out and the new process
             swapped in.
   Parameters - none
   Returns - nothing
   Side Effects - the context of the machine is changed
   ----------------------------------------------------------------------- */
void dispatcher(void) {
  proc_ptr next_process = NULL;
  proc_ptr prev_process = NULL;
  proc_ptr disp_temp = NULL;
  int i = MAXPRIORITY;
 
  if (DEBUG && debugflag)
    console(" - dispatcher(): called -\n");
 
  /* test if in kernel mode, halts otherwise */
  kernelCheck("dispatcher");
 
  /* disbale interrupts */
  disableInterrupts("dispatcher");
 
  purge_ready("dispatcher");
 
  /*if (DEBUG && debugflag)
      dump_processes();
 
 /* sets next_process to next highest priority process */
  while ((i <= MINPRIORITY) && (next_process == NULL)) {
    disp_temp = ReadyList[i];
    while ((disp_temp != NULL) && (next_process == NULL)) {
      next_process = disp_temp;
      disp_temp = disp_temp -> next_proc_ptr;
    }
    i += 1;
  }
 
  if (next_process == NULL) {
    if (BlockedList != NULL) {
      BlockedList -> status = READY;
      next_process = BlockedList;
      //next_process->status = READY;
      BlockedList = BlockedList -> next_proc_ptr;
 
      /* updates the ReadyList */
      disp_temp = ReadyList[next_process -> priority];
 
      if (ReadyList[next_process -> priority] == NULL) /* deals with an empty List */ {
        ReadyList[next_process -> priority] = next_process;
      } else /* deals with an non empty List */ {
        while (disp_temp != NULL)
          disp_temp = disp_temp -> next_proc_ptr;
        disp_temp = next_process;
      }
 
    } else
      next_process = & ProcTable[1];
 
  }
 
  /* context switch to next process */
  if (Current == NULL) /* First time Current is NULL */ {
    /* for future phase(s) */
    p1_switch(0, next_process -> pid);
    Current = next_process;
 
    context_switch(NULL, & next_process -> state);
  }
 
  /* for future phase(s) */
  p1_switch(Current -> pid, next_process -> pid);
  prev_process = Current;
  Current = next_process;
  Current -> startTime = sys_clock();
 
  enableInterrupts("dispatcher");
  context_switch( & prev_process -> state, & Current -> state);
 
  //Current->status = RUNNING;
  /* enable interrupts */
  enableInterrupts("dispatcher");
 
} /* dispatcher */
 
/* ------------------------------------------------------------------------
   Name - sentinel
   Purpose - The purpose of the sentinel routine is two-fold.  One
             responsibility is to keep the system going when all other
         processes are blocked.  The other is to detect and report
         simple deadlock states.
   Parameters - none
   Returns - nothing
   Side Effects -  if system is in deadlock, print appropriate error
           and halt.
   ----------------------------------------------------------------------- */
int sentinel(void * dummy) {
  if (DEBUG && debugflag)
    console("sentinel(): called -\n");
  while (1) {
    check_deadlock();
    waitint();
  }
} /* sentinel */
 
/* check to determine if deadlock has occurred... */
static void check_deadlock() {
  int numReady = 0;
  int numActive = 0;
 
  /* loops through Process Table */
  for (int i = 1; i < MAXPROC; i++) {
    if (ProcTable[i].status == READY) {
      /* increments both since Ready are always active */
      numReady++;
      numActive++;
    }
 
    if (ProcTable[i].status == BLOCKED || ProcTable[i].status == ZAPPED) {
      /* only increments active since not ready */
      numActive++;
    }
  }
 
  if (numReady == 1) {
    if (numActive >= 1) {
      console("All Processes have been completed.\n");
      halt(0);
    } else {
      console("checkdeadlock(): procCount = %d\n", numActive);
      console("checkdeadlock(): processes still present. Halting...\n");
      halt(1);
    }
  } else
    return;
} /* check_deadlock */
 
/*----------------------------------------------------------------*
 * Name        : dump_proces                                      *
 * Purpose     : Displays the PID, ParentPID, Priority, Status,   *
 *               # of Children, CPU Time Consumed, and Name of    *
 *               of every process                                 *
 * Parameters  : none                                             *
 * Returns     : nothing                                          *
 * Side Effects: the lists are all updated                        *
 *----------------------------------------------------------------*/
void dump_processes() {
  if (DEBUG && debugflag)
    console("    - dump_processes(): called -\n");
 
  int i;
  char * status[8] = {
    "\0",
    "RUNNING",
    "READY",
    "BLOCKED",
    "ZAPPED",
    "QUIT",
    "ZAP_BLOCK",
    "JOIN_BLOCK"
  };
 
  if (DEBUG && debugflag)
    console("    - dump_processes(): generating process list -\n");
 
  /* loop to run through all processes */
  if (procCount >= 1) {
    for (i = 1; i < MAXPROC; i++) {
      if (ProcTable[i].pid != EMPTY) /* checks if process is used */ {
        console("         +-------------------------------------------------------------------+\n");
        console("         | Name         : %-50s |\n", ProcTable[i].name);
        console("         | PID          : %-50d |\n", ProcTable[i].pid);
        console("         | Parent PID   : %-50d |\n", ProcTable[i].parent_pid);
        console("         | Priority     : %-50d |\n", ProcTable[i].priority);
        console("         | Status       : %-50s |\n", status[ProcTable[i].status]);
        console("         | # of Children: %-50d |\n", ProcTable[i].childCount);
        console("         | CPU Time Used: Currently blank                                    |\n");
      }
    }
    console("         +-------------------------------------------------------------------+\n");
  }
 
  console("         | Blocked %8p |\n", BlockedList);
 
} /* dump_processes */
 
/*----------------------------------------------------------------*
 * Name        : purge_ready                                      *
 * Purpose     : Removes all processes that aren't ready from the *
 *               ready lists                                      *
 * Parameters  : name of calling function                         *
 * Returns     : nothing                                          *
 * Side Effects: all lists updated                                *
 *----------------------------------------------------------------*/
static void purge_ready(char * caller_name) {
  if (DEBUG && debugflag)
    console("    - purge_ready(): called for function %s -\n", caller_name);
  int i;
  proc_ptr block_ptr = NULL;
  proc_ptr quit_ptr = NULL;
  proc_ptr ready_ptr = NULL;
  proc_ptr zapped_ptr = NULL;
  proc_ptr found_ptr = NULL;
  proc_ptr prev_ptr;
  proc_ptr temp_ptr;
 
  /* loop to check through Ready List at each priority level */
  for (i = MAXPRIORITY; i < MINPRIORITY; i++) {
    found_ptr = ReadyList[i];
    if (found_ptr != NULL) /* if the pointer is null skips it */ {
      prev_ptr = found_ptr;
      found_ptr = prev_ptr -> next_proc_ptr;
      if (prev_ptr -> status == READY) /* checks if the list head is ready */ {
        while (found_ptr != NULL) {
          if (found_ptr -> status != READY) /* loops through all members and chekc if ready */ {
            purge_ready_helper( & block_ptr, & quit_ptr, & ready_ptr, & zapped_ptr, found_ptr);
            prev_ptr -> next_proc_ptr = found_ptr -> next_proc_ptr;
            found_ptr -> next_proc_ptr = NULL;
          }
          prev_ptr = found_ptr;
          found_ptr = prev_ptr -> next_proc_ptr;
        }
      } else /* deals with the case where head is not ready */ {
        purge_ready_helper( & block_ptr, & quit_ptr, & ready_ptr, & zapped_ptr, prev_ptr);
        ReadyList[i] = prev_ptr -> next_proc_ptr;
      }
    }
  }
 
  /* stores blocked process in Blocked List */
  temp_ptr = BlockedList;
  if (BlockedList == NULL)
    BlockedList = block_ptr;
  else {
    while (temp_ptr != NULL)
      temp_ptr = temp_ptr -> next_proc_ptr;
    temp_ptr = block_ptr;
  }
 
  /* stores blocked proces>s in Quit List */
  temp_ptr = QuitList;
  if (QuitList == NULL)
    QuitList = block_ptr;
  else {
    while (temp_ptr != NULL)
      temp_ptr = temp_ptr -> next_proc_ptr;
    temp_ptr = block_ptr;
  }
 
  /* stores blocked process in Zapped List */
  temp_ptr = ZappedList;
  if (ZappedList == NULL)
    ZappedList = block_ptr;
  else {
    while (temp_ptr != NULL)
      temp_ptr = temp_ptr -> next_proc_ptr;
    temp_ptr = block_ptr;
  }
} /* purge_ready */
 
/*----------------------------------------------------------------*
 * Name        : purge_ready_helper                               *
 * Purpose     : Helper function to purge_ready                   *
 * Parameters  : temp lists from purge_ready and process to be    *
 *               added                                            *
 * Returns     : nothing                                          *
 * Side Effects: none                                             *
 *----------------------------------------------------------------*/
static void purge_ready_helper(proc_ptr * b_t, proc_ptr * q_t, proc_ptr * r_t, proc_ptr * z_t, proc_ptr found) {
  if (DEBUG && debugflag)
    console("       - purge_ready_helper(): called -\n");
 
  /* decides which list to add process to */
  //int status = found->status;
  proc_ptr helper_temp;
  switch (found -> status) {
  case READY:
    helper_temp = * r_t;
    if ( * r_t == NULL)
      *
      r_t = found;
    break;
  case BLOCKED:
    helper_temp = * b_t;
    if ( * b_t == NULL)
      *
      b_t = found;
    break;
  case ZAPPED:
    helper_temp = * z_t;
    if ( * z_t == NULL)
      *
      z_t = found;
    break;
  case QUIT:
    helper_temp = * q_t;
    if ( * q_t == NULL)
      *
      q_t = found;
    break;
  }
 
  /* adds process to proper sublist */
  while (helper_temp != NULL)
    helper_temp = helper_temp -> next_proc_ptr;
 
  helper_temp = found;
  found -> next_proc_ptr = NULL;
}
 
/*----------------------------------------------------------------*
 * Name        : kernelCheck                                *
 * Purpose     : Checks the current kernel mode.                  *
 * Parameters  : name of calling function                         *
 * Returns     : nothing                                          *
 * Side Effects: halts process if in user mode                    *
 *----------------------------------------------------------------*/
static void kernelCheck(char * caller_name) {
  union psr_values caller_psr; /* holds the current psr values */
  if (DEBUG && debugflag)
    console("kernelCheck(): called for function %s -\n", caller_name);
 
  /* checks if in kernel mode, halts otherwise */
  caller_psr.integer_part = psr_get(); /* stores current psr values into structure */
  if (caller_psr.bits.cur_mode != 1) {
    console("%s(): called while not in kernel mode, by process %d. Halting... -\n", caller_name, Current -> pid);
    halt(1);
  }
} /* kernelCheck */
 
/* Disables the interrupts */
void disableInterrupts(char * caller_name) {
  if (DEBUG && debugflag)
    console("disableInterrupts(): interupts turned off for %s -\n", caller_name);
 
  kernelCheck("disableInterrupts");
  psr_set(psr_get() & ~PSR_CURRENT_INT);
} /* disableInterrupts */
 
/* Enables interrupts */
void enableInterrupts(char * caller_name) {
  if (DEBUG && debugflag)
    console("enableInterrupts(): interupts turned on for %s -\n", caller_name);
  kernelCheck("enableInterrupts");
  psr_set(psr_get() | PSR_CURRENT_INT);
} /* enableInterrupts */
 
/*----------------------------------------------------------------*
 * Name        : find_process                                     *
 * Purpose     : Finds process pointer of given PID               *
 * Parameters  : PID to find                                      *
 * Returns     : pointer of found process -or-                    *
 *               NULL if none match                               *
 * Side Effects: none                                             *
 *----------------------------------------------------------------*/
proc_ptr find_process(int pid) {
  if (DEBUG && debugflag)
    console("       - find_process(): seeking process %d -\n", pid);
 
  int i = 1;
  proc_ptr found_ptr = NULL;
 
  while ((i < MAXPROC) && (found_ptr == NULL)) {
    if (ProcTable[i].pid == pid)
      found_ptr = & ProcTable[i];
    i += 1;
  }
 
  return (found_ptr);
} /* find_process */
 
void clockHandler(int dev, int unit) {
  if (DEBUG && debugflag)
    console("    - clockHandler(): ClockHandler called\n");
  time_slice();
} /* clockHandler */
 
int zap(int pid) {
  if (Current -> pid == pid) {
    console("Process cannot zap itself! Halting.\n");
    halt(1);
  }
  for (int i = 0; i < MAXPROC; i++) {
    if (ProcTable[i].pid == pid) {
      ProcTable[i].status = ZAPPED;
      return 0;
    }
  }
  console("Process does not exist. Halting...\n");
  halt(1);
}
 
int is_zapped() {
  if (Current -> status == ZAPPED) {
    return 1;
  }
  return 0;
}
 
int read_cur_start_time() {
  /*Returns the time in microseconds at which the currently executing process
  began its current time slice */
  return Current -> startTime;
}
 
void time_slice() {
  disableInterrupts("time_slice");
  int run_time = sys_clock() - read_cur_start_time();
 
  if (run_time >= TIMESLICE) {
    Current -> sliceTime = 0;
    dispatcher();
    /*Calls dispatcher if currently executing process
            has exceeded its time slice*/
  }
}
 
void empty_proc(int pid) {
  kernelCheck("empty_proc");
  disableInterrupts("empty_proc");
  int i = pid % MAXPROC;
 
  ProcTable[i].pid = EMPTY;
  ProcTable[i].status = EMPTY;
  ProcTable[i].next_proc_ptr = NULL;
  ProcTable[i].child_proc_ptr = NULL;
  ProcTable[i].next_sibling_ptr = NULL;
 
  procCount--;
  enableInterrupts("empty_proc");
}
 
int block_me(int block_status) {
  if (DEBUG && debugflag)
    console("block(): entering the block_me function\n");
 
  kernelCheck("block_me");
  disableInterrupts("block_me");
 
  if (block_status < 10) {
    if (DEBUG && debugflag)
      console("block(): Invalid block status. Halting\n");
    halt(1);
  }
 
  Current -> status = block_status;
  if (block_status < 10) {
    if (DEBUG && debugflag)
      console("block(): Invalid block status. Halting\n");
    halt(1);
  }
  if (Current -> status == ZAPPED) {
    return -1;
  }
 
  enableInterrupts("block_me");
  return 0;
}
int unblock_proc(int pid) {
  if (DEBUG && debugflag)
    console("unblock_proc(): entering the unblock_proc function.\n");
 
  kernelCheck("unblock_proc");
  disableInterrupts("ublock_proc");
 
  int i = pid % MAXPROC;
  if (Current -> pid == pid)
    return -2;
 
  ProcTable[i].status = READY;
  ReadyList[ProcTable[i].priority--] = & ProcTable[i];
  dispatcher();
 
  return 0;
}