/* ------------------------------------------------------------------------
   Kyle AuBuchon & Juan Gonzalez
   phase1.c
   CSCV 452
   ------------------------------------------------------------------------ */
#include <stdlib.h>

#include <string.h>

#include <stdio.h>

#include <phase1.h>

#include "kernel.h"

/* ------------------------- Prototypes ----------------------------------- */
void 	startup();
void 	finish();
int 	fork1(char * , int( * )(char * ), char * , int, int);

int 		sentinel(char * );
extern int 	start1 (char *);
void 		dispatcher(void);
void 		launch();
static void enableInterrupts();
static void check_deadlock();

void 	kernelChecker(void);
int 	join(int * );
void 	quit(int);
void 	dispatcher(void);

int 	block_me(int);
int 	unblock_proc(int);
int 	getpid();
int 	zap(int);
int 	is_zapped();
int 	readtime();
void 	time_slice();
void 	clockhandler();

static void check_deadlock();
void 		dump_processes();

extern int start1(char * );

void 	createProcTable(int);
void 	addToReadyList(proc_ptr);
void 	addToQuitList(proc_ptr);

void 	popFromReadyList(proc_ptr);
void 	popFromQuitList(proc_ptr);
void 	popFromChildList(proc_ptr);


/* -------------------------- Globals ------------------------------------- */

/* Debugging global variable */
int debugflag = 0;

/* Process table */
proc_struct ProcTable[MAXPROC];

/* Process lists  */
static proc_ptr ReadyList;

/* Current process ID */
proc_ptr Current;

/* Next pid to be assigned */
unsigned int next_pid = SENTINELPID;

/* -------------------------- Functions ----------------------------------- */

/* ------------------------------------------------------------------------
   Name         - startup
   Purpose      - Initializes process lists and clock interrupt vector.
	              Start up sentinel process and the test process.
   Parameters   - none, called by USLOSS
   Returns      - nothing
   Side Effects - lots, starts the whole thing
   ----------------------------------------------------------------------- */
void startup() {
  int i; /* loop index */
  int result; /* value returned by call to fork1() */

  /* initialize the process table */
  if (DEBUG && debugflag) {
    console("startup(): initializing the Process Table\n");
  }

  for (i = 0; i < MAXPROC; i++)
    createProcTable(i);

  /* Initialize the Ready list, etc. */
  if (DEBUG && debugflag) {
    console("startup(): initializing the Ready & Blocked lists\n");
  }
  ReadyList = NULL;

  /* Initialize the clock interrupt handler */
  int_vec[CLOCK_INT] = clockhandler;

  /* startup a sentinel process */
  if (DEBUG && debugflag)
    console("startup(): calling fork1() for sentinel\n");

  result = fork1("sentinel", sentinel, NULL, USLOSS_MIN_STACK,
    SENTINELPRIORITY);

  if (result < 0) {
    if (DEBUG && debugflag)
      console("startup(): fork1 of sentinel returned error, halting...\n");
    halt(1);
  }

  /* start the test process */
  if (DEBUG && debugflag)
    console("startup(): calling fork1() for start1\n");

  result = fork1("start1", start1, NULL, 2 * USLOSS_MIN_STACK, 1);
  if (result < 0) {
    console("startup(): fork1 for start1 returned an error, halting...\n");
    halt(1);
  }

  console("startup(): Should not see this message! ");
  console("Returned from fork1 call that created start1\n");

  return;
} /* startup */

/* ------------------------------------------------------------------------
   Name         - finish
   Purpose      - Required by USLOSS
   Parameters   - none
   Returns      - nothing
   Side Effects - none
   ----------------------------------------------------------------------- */
void finish() {
  if (DEBUG && debugflag)
    console("finishing...\n");
} /* finish */

/* ------------------------------------------------------------------------
   Name         - fork1
   Purpose      - Gets a new process from the process table and initializes
                  information of the process.  Updates information in the
                  parent process to reflect this child process creation.
   Parameters   - the process procedure address, the size of the stack and
                  the priority to be assigned to the child process.
   Returns      - the process id of the created child or -1 if no child could
                  be created or if priority is not between max and min priority.
   Side Effects - ReadyList is changed, ProcTable is changed, Current
                  process information changed
   ------------------------------------------------------------------------ */
int fork1(char * name, int( * f)(char * ), char * arg, int stacksize, int priority) {
  int proc_slot = -1;

  if (DEBUG && debugflag)
    console("fork1(): creating process %s\n", name);

  /* test if in kernel mode; halt if in user mode */
  kernelChecker();
  disableInterrupts();

  /* Return if stack size is too small */
  if (stacksize < USLOSS_MIN_STACK) {
    return -2;
  }

  /*check priority*/
  if ((next_pid != SENTINELPID) && (priority > MINPRIORITY || priority < MAXPRIORITY)) {
    if (DEBUG && debugflag) {
      console("fork1(): Process %s priority is out of bounds\n", name);
    }
    return -1;
  }

  /* Return if process name is too long */
  if (strlen(name) >= (MAXNAME - 1)) {
    console("fork1(): Process name is too long.  Halting...\n");
    halt(1);
  }

  /* find an empty slot in the process table */
  proc_slot = findProcSlot();

  if (proc_slot == -1) {
    if (DEBUG && debugflag) {
      console("fork1(): process table full\n");
    }
    return -1;
  }

  /* fill-in entry in process table */
  ProcTable[proc_slot].pid = next_pid;
  strcpy(ProcTable[proc_slot].name, name);
  ProcTable[proc_slot].start_func = f;

  /* Check if arg is correct */
  if (arg == NULL)
    ProcTable[proc_slot].start_arg[0] = '\0';
  else if (strlen(arg) >= (MAXARG - 1)) {
    console("fork1(): argument too long.  Halting...\n");
    halt(1);
  } else
    strcpy(ProcTable[proc_slot].start_arg, arg);

  ProcTable[proc_slot].stacksize = stacksize;
  /*TODO check malloc error*/
  if ((ProcTable[proc_slot].stack = malloc(stacksize)) == NULL) {
    console("fork1(): malloc failed.  Halting...\n");
    halt(1);
  }

  /*set new priority and pointers*/
  ProcTable[proc_slot].priority = priority;
  if (Current != NULL) {
    Current -> childCount++;

    // check if no children
    if (Current -> child_proc_ptr == NULL) {
      Current -> child_proc_ptr = & ProcTable[proc_slot];
    }else {
      proc_ptr child = Current -> child_proc_ptr;
      while (child -> next_sibling_ptr != NULL) {
        child = child -> next_sibling_ptr;
      }
      child -> next_sibling_ptr = & ProcTable[proc_slot];
    }
  }
  ProcTable[proc_slot].parent_ptr = Current;

  /* Initialize context for this process, but use launch function pointer for
   * the initial value of the process's program counter (PC)
   */
  context_init( & (ProcTable[proc_slot].state), psr_get(),
    ProcTable[proc_slot].stack,
    ProcTable[proc_slot].stacksize, launch);

  /* for future phase(s) */
  p1_fork(ProcTable[proc_slot].pid);

  ProcTable[proc_slot].status = READY;

  addToReadyList( & ProcTable[proc_slot]);
  next_pid++;

//checking for sentinel 
  if (ProcTable[proc_slot].pid != SENTINELPID) {
    dispatcher();
  }
  return ProcTable[proc_slot].pid;

} /* fork1 */

/* ------------------------------------------------------------------------
   Name         - launch
   Purpose      - Dummy function to enable interrupts and launch a given process
                  upon startup.
   Parameters   - none
   Returns      - nothing
   Side Effects - enable interrupts
   ------------------------------------------------------------------------ */
void launch() {
  int result;

  if (DEBUG && debugflag)
    console("launch(): started\n");

  /* Enable interrupts */
  enableInterrupts();

  /* Call the function passed to fork1, and capture its return value */
  result = Current -> start_func(Current -> start_arg);

  if (DEBUG && debugflag)
    console("Process %d returned to launch\n", Current -> pid);

  quit(result);

} /* launch */

/* ------------------------------------------------------------------------
   Name         - join
   Purpose      - Wait for a child process (if one has been forked) to quit.  If
                  one has already quit, don't wait.
   Parameters   - a pointer to an int where the termination code of the
                  quitting process is to be stored.
   Returns      - the process id of the quitting child joined on.
		          -1 if the process was zapped in the join
		          -2 if the process has no children
		          -3 if child quit before join occurred
   Side Effects - If no child process has quit before join is called, the 
                  parent is removed from the ready list and blocked.
   ------------------------------------------------------------------------ */
int join(int * code) {
  int child_pid = -3;
  proc_ptr child;

  /* check kernel mode */
  kernelChecker();
  disableInterrupts();

  /* first case no children */
  if (Current -> child_proc_ptr == NULL && Current -> quit_child_ptr == NULL) {
    return -2;
  }

  /* case 2 children have not yet quit	 */
  if (Current -> quit_child_ptr == NULL) {
    Current -> status = JOIN_BLOCK;
    popFromReadyList(Current);

    if (DEBUG && debugflag) {
      console("join(): %s is blocked.\n", Current -> name);
    }
    dispatcher();
  }

  /* case 3 child has already quit */
  child = Current -> quit_child_ptr;

  child_pid = child -> pid;
  * code = child -> quit;
  popFromQuitList(child);
  createProcTable(child_pid);

  /* case 4 process was zapped */
  if (is_zapped())
    return -1;

  return child_pid;
} /* join */

/* ------------------------------------------------------------------------
   Name         - quit
   Purpose      - Stops the child process and notifies the parent of the death by
                  putting child quit info on the parents child completion code
                  list.
   Parameters   - the code to return to the grieving parent
   Returns      - nothing
   Side Effects - changes the parent of pid child completion status list.
   ------------------------------------------------------------------------ */
void quit(int code) {
  if (DEBUG && debugflag){
	  console("quit started\n");
  }
  
  int curr_pid = -1;
  proc_ptr temp1;

  /* check kernel halt if not*/
  kernelChecker();

  /* disable interrupts */
  disableInterrupts();

  /* checks for children */
  if (Current -> child_proc_ptr != NULL) {
    console("quit(): process %s has active children. Halting...\n", Current -> name);
    halt(1);
  }

  Current -> quit = code;
  Current -> status = QUIT;
  popFromReadyList(Current);

  /* Unblock all process that zapped this process */
  if (is_zapped()) {
    temp1 = Current -> zapped;
    while (temp1 != NULL) {
      temp1 -> status = READY;
      addToReadyList(temp1);
      temp1 = temp1 -> next_zapped_proc;
    }
  }

  /* Parent has quit children  */
  if (Current -> parent_ptr != NULL && Current -> quit_child_ptr != NULL) {

    /* Clean up children on quit list */
    while (Current -> quit_child_ptr != NULL) {
      int child_pid = Current -> quit_child_ptr -> pid;
      popFromQuitList(Current -> quit_child_ptr);
      createProcTable(child_pid);
    }

    /* Clean up Current */
    Current -> parent_ptr -> status = READY;
    popFromChildList(Current);
    addToQuitList(Current -> parent_ptr);
    addToReadyList(Current -> parent_ptr);
    curr_pid = Current -> pid;

  } else if (Current -> parent_ptr != NULL) {

    /* Process is a child */
    addToQuitList(Current -> parent_ptr);
    popFromChildList(Current);

    if (Current -> parent_ptr -> status == JOIN_BLOCK) {
      addToReadyList(Current -> parent_ptr);
      Current -> parent_ptr -> status = READY;
    }

  } else {
    /* Process is a parent */
    while (Current -> quit_child_ptr != NULL) {
      int child_pid = Current -> quit_child_ptr -> pid;
      popFromQuitList(Current -> quit_child_ptr);
      createProcTable(child_pid);
    }
    curr_pid = Current -> pid;
    createProcTable(Current -> pid);

  }
  if (Current -> parent_ptr != NULL) {
    Current -> parent_ptr -> childCount--;
  }
  p1_quit(curr_pid);
  dispatcher();
} /* quit */

/* ------------------------------------------------------------------------
   Name         - dispatcher
   Purpose      - dispatches ready processes.  The process with the highest
                  priority (the first on the ready list) is scheduled to
                  run.  The old process is swapped out and the new process
                  swapped in.
   Parameters   - none
   Returns      - nothing
   Side Effects - the context of the machine is changed
   ----------------------------------------------------------------------- */
void dispatcher(void) {
  if (DEBUG && debugflag) {
    console("dispatcher(): started.\n");
  }

  /* Dispatch is called for the first time by start1() */
  if (Current == NULL) {
    Current = ReadyList;
    Current -> startTime = sys_clock();

    /*Enable interrupts and change context */
    enableInterrupts();
    context_switch(NULL, & Current -> state);

  } else {
    proc_ptr temp = Current;

    /*Previous process overran timesplice */
    if (temp -> status == RUNNING) {
      temp -> status = READY;
    }
    Current -> cpuTime += (sys_clock() - readtime());

    Current = ReadyList;
    popFromReadyList(Current);
    Current -> status = RUNNING;

    // Put current in correct priority
    addToReadyList(Current);

    Current -> startTime = sys_clock();
    p1_switch(temp -> pid, Current -> pid);

    /*Enable interrupts and change context */
    enableInterrupts();
    context_switch( & temp -> state, & Current -> state);
  }

} /* dispatcher */

/* ------------------------------------------------------------------------
   Name         - zap
   Purpose      - Marks a process as zapped.
   Parameters   - int pid - the process id to be zapped
   Returns      - -0 The zapped process called quit();
   	   	   	   	  -1 The calling process was zapped
   Side Effects - Process calling zap is added to the process's zapped
   	   	   	   	  list
   ----------------------------------------------------------------------- */
int zap(int pid) {
  proc_ptr zap;

  /* Make sure kernel is active */
  kernelChecker();

  if (DEBUG && debugflag) {
    console("zap(): Process %s is disabling interrupts.\n", Current -> name);
  }
  disableInterrupts();

  /* Process tried to zap itself*/
  if (Current -> pid == pid) {
    console("zap(): Process %s tried to zap itself. Halting...\n", Current -> name);
    halt(1);
  }

  /* Process to be zapped does not exist */
  if (ProcTable[pid % MAXPROC].status == EMPTY || ProcTable[pid % MAXPROC].pid != pid) {
    console("zap(): Process being zapped does not exist.  Halting...\n");
    halt(1);
  }

  /* Process to zap has finished, waiting on parent to finish */
  if (ProcTable[pid % MAXPROC].status == QUIT) {
    if (is_zapped()) {
      return -1;
    }
    return 0;
  }

  Current -> status = ZAP_BLOCK;
  popFromReadyList(Current);
  zap = & ProcTable[pid % MAXPROC];
  zap -> zap = 1;

  /* Add process to list of process zapped by this process */
  if (zap -> zapped == NULL) {
    zap -> zapped = Current;
  } else {
    proc_ptr temp = zap -> zapped;
    zap -> zapped = Current;
    zap -> zapped -> next_zapped_proc = temp;
  }

  dispatcher();
  if (is_zapped()) {
    return -1;
  }
  return 0;

}

/* ------------------------------------------------------------------------
   Name         - sentinel
   Purpose      - The purpose of the sentinel routine is two-fold.  One
                  responsibility is to keep the system going when all other
	              processes are blocked.  The other is to detect and report
	              simple deadlock states.
   Parameters   - none
   Returns      - nothing
   Side Effects - if system is in deadlock, print appropriate error
		          and halt.
   ----------------------------------------------------------------------- */
int sentinel(char * dummy) {
  if (DEBUG && debugflag)
    console("sentinel(): called\n");
  while (1) {
    check_deadlock();
    waitint();
  }
} /* sentinel */


/* ------------------------------------------------------------------------
   Name         - dump_processes
   Purpose      - Prints out processes
   ----------------------------------------------------------------------- */
void dump_processes() {
  console("PID	Parent		Priority	Status		# childCount  cpuTime Name\n");

  for (int i = 0; i < MAXPROC; i++) {
    console(" %d	", ProcTable[i].pid);
    if (ProcTable[i].parent_ptr != NULL) {
      console("  %d    ", ProcTable[i].parent_ptr -> pid);
    } else {
      console("   -1   ");
    }
    console("   %d		", ProcTable[i].priority);

    if (Current -> pid == ProcTable[i].pid) {
      console("RUNNING		");
	} else if (ProcTable[i].status == 7) {
      console("ZAP_BLOCK	  ");
	} else if (ProcTable[i].status == 6) {
      console("JOIN_BLOCK	  ");
    } else if (ProcTable[i].status == 5) {
      console("EMPTY		  ");
    } else if (ProcTable[i].status == 4) {
      console("BLOCKED	  ");
    } else if (ProcTable[i].status == 3) {
      console("QUIT		  ");
    } else if (ProcTable[i].status == 1) {
      console("READY		  ");
    } else {
      console("%d			  ", ProcTable[i].status);
    }

    console("  %d  ", ProcTable[i].childCount);
    console("   %d", ProcTable[i].cpuTime);

    if (strcmp(ProcTable[i].name, "") != 0) {
      console("	%s", ProcTable[i].name);
    }
    console("\n");
  }
}
/*TODO init empty proctTable*/
void createProcTable(int pid) {
  int i = pid % MAXPROC;
  ProcTable[i].next_proc_ptr = NULL;
  ProcTable[i].parent_ptr = NULL;

  ProcTable[i].child_proc_ptr = NULL;
  ProcTable[i].quit_child_ptr = NULL;

  ProcTable[i].next_sibling_ptr = NULL;
  ProcTable[i].next_quit_sibling_ptr = NULL;

  ProcTable[i].next_zapped_proc = NULL;
  ProcTable[i].zapped = NULL;

  ProcTable[i].name[0] = '\0';
  ProcTable[i].start_arg[0] = '\0';

  ProcTable[i].pid = -1;
  ProcTable[i].priority = -1;
  ProcTable[i].start_func = NULL;
  ProcTable[i].stack = NULL;
  ProcTable[i].stacksize = -1;
  ProcTable[i].status = EMPTY;
  ProcTable[i].childCount = 0;
  ProcTable[i].cpuTime = -1;
  ProcTable[i].quit = -1;
} /* createProcTable */

void addToReadyList(proc_ptr proc) {
  proc_ptr next = NULL;
  proc_ptr temp = NULL;

  if (ReadyList == NULL) {
    ReadyList = proc;
  } else {
    // check for highest priority 
    if (ReadyList -> priority > proc -> priority) {
      proc_ptr temp = ReadyList;
      ReadyList = proc;
      proc -> next_proc_ptr = temp;
    }
    // Add proc to list after first greater priority
    else {
      next = ReadyList -> next_proc_ptr;
      temp = ReadyList;

      while (next -> priority <= proc -> priority) {
        temp = next;
        next = next -> next_proc_ptr;
      }
      temp -> next_proc_ptr = proc;
      proc -> next_proc_ptr = next;
    }

  }
} /* addToReadyList */

/* remove process from ready list */
void popFromReadyList(proc_ptr proc) {
  if (DEBUG && debugflag) {
    console("popFromReadyList(): Removing process %s from Ready list\n", proc -> name);
  }
  /* Process is head of ready list */
  if (proc == ReadyList) {
    ReadyList = ReadyList -> next_proc_ptr;
  } else {
    /* iterate through ReadyList until proc is found */
    proc_ptr temp = ReadyList;
    while (temp -> next_proc_ptr != proc) {
      temp = temp -> next_proc_ptr;
    }
    temp -> next_proc_ptr = temp -> next_proc_ptr -> next_proc_ptr;
  }

  if (DEBUG && debugflag) {
    console("popFromReadyList(): Removed process %s from Ready List", proc -> name);
  }

} /* popFromReadyList */

/* pop from parent quit_list */
void popFromQuitList(proc_ptr proc) {
  proc -> parent_ptr -> quit_child_ptr = proc -> next_quit_sibling_ptr;
}

void addToQuitList(proc_ptr proc) {
  if (proc -> quit_child_ptr == NULL) {
    proc -> quit_child_ptr = Current;
    return;
  }

  proc_ptr temp = proc -> quit_child_ptr;
  while (temp -> next_quit_sibling_ptr != NULL) {
    temp = temp -> next_quit_sibling_ptr;
  }
  temp -> next_quit_sibling_ptr = Current;
}

void popFromChildList(proc_ptr proc) {

  if (proc == proc -> parent_ptr -> child_proc_ptr) {
    proc -> parent_ptr -> child_proc_ptr = proc -> next_sibling_ptr;

  } else {
    proc_ptr temp = proc -> parent_ptr -> child_proc_ptr;

    while (temp -> next_sibling_ptr != proc) {
      temp = temp -> next_sibling_ptr;
    }

    temp -> next_sibling_ptr = temp -> next_sibling_ptr -> next_sibling_ptr;

  }

} 
int block_me(int status) {
  kernelChecker();
  disableInterrupts();

  /* confirm status */
  if (status > 7) {
    halt(1);
  }

  /* main blocking code */

  Current -> status = status;
  popFromReadyList(Current);
  dispatcher();

  if (is_zapped()) {
    return -1;
  }
  return 0;
} /*block_me*/

/* unblock_proc */
int unblock_proc(int pid) {
  /*first case is indicated process was not blocked, dne etc */
  if (ProcTable[pid % MAXPROC].pid != pid) {
    return -2;
  }

  if (Current -> pid == pid) {
    return -2;
  }
  
  /*check if process status is greater than or equal to 10*/
  if (ProcTable[pid % MAXPROC].status < 10) {
    return -2;
  }
 
  /*check if process was zapped*/
  if (is_zapped()) {
    return -1;
  }

  ProcTable[pid % MAXPROC].status = READY;
  addToReadyList( & ProcTable[pid % MAXPROC]);
  dispatcher();
  return 0;
}

int findProcSlot() {
  int index = next_pid % MAXPROC;
  int count = 0;

  while (ProcTable[index].status != EMPTY) {
    next_pid++;
    index = next_pid % MAXPROC;

    if (count >= MAXPROC) {
      return -1;
    }
    count++;
  }
  return index;
}
/*
 * Disables the interrupts.
 */
void disableInterrupts() {
  /* check kernel mode */
  kernelChecker();

  /* perform bitwise to set disable */
  psr_set(psr_get() & ~PSR_CURRENT_INT);
} /* disableInterrupts */

/*
 * Enables the interrupts.
 */
void enableInterrupts() {
  /* Turn the interrupts ON if we are in kernel mode */
  kernelChecker();

  psr_set(psr_get() | PSR_CURRENT_INT);
} /*enableInterrupts*/

/* check if deadlock has occurred... */

static void check_deadlock() {

  int numProc = 0;

  /* check each process status */
  for (int i = 0; i < MAXPROC; i++) {
    if (ProcTable[i].status != EMPTY)
      numProc++;
  }
  
  if (numProc > 1) {
    console("check_deadlock(): numProc = %d", numProc);
    console("check_deadlock(): processes still present.   Halting...\n");
    halt(1);
  }

  console("All processes completed.\n");
  halt(0);

} /* check_deadlock */


void clockhandler() {
  time_slice();
}


void time_slice() {
  if ((sys_clock() - readtime()) >= TIME_SLICE) {
    dispatcher();
  }
  return;
}

int readtime() {
  int curr_time = Current -> startTime;
  return curr_time;
}

int is_zapped() {
  return Current -> zap;
}

int getpid() {
  return Current -> pid;
}

void kernelChecker(void) {
  if ((PSR_CURRENT_MODE & psr_get()) == 0) {
    console("fork1(): called while in user mode, by process %d.  Halting...\n", Current -> pid);
    halt(1);
  }
}