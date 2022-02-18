/* ------------------------------------------------------------------------
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
int sentinel (char *);
extern int start1 (char *);
void dispatcher(void);
void disableInterrupts();
void launch();
static void enableInterrupts();
static void check_deadlock();
static void insertReadyList(proc_ptr proc);
static void removeFromReadyList(int PID);


/* -------------------------- Globals ------------------------------------- */

/* Patrick's debugging global variable... */
int debugflag = 1;

/* the process table */
proc_struct ProcTable[MAXPROC];

/* Process lists  */
proc_ptr ReadyList[SENTINELPRIORITY + 1];
proc_ptr BlockedList;
proc_ptr QuitList;
proc_ptr ZappedList;

/* current process ID */
proc_ptr Current;

/* the next pid to be assigned */
unsigned int next_pid = SENTINELPID;

/*global to keep track number of processes*/
int numProc;

/* -------------------------- Functions ----------------------------------- */
/* ------------------------------------------------------------------------
   Name - startup
   Purpose - Initializes process lists and clock interrupt vector.
	     Start up sentinel process and the test process.
   Parameters - none, called by USLOSS
   Returns - nothing
   Side Effects - lots, starts the whole thing
   ----------------------------------------------------------------------- */
void startup()
{
   int i;      /* loop index */
   int result; /* value returned by call to fork1() */

   /* initialize the process table */
   //if (DEBUG && debugflag)
      console("startup(): initilizing process table, ProcTable[] \n");
   
   //init all procTables 
   for (i=0; i < MAXPROC; i++){
      ProcTable[i].pid = EMPTY;
      ProcTable[i].status = EMPTY;
      ProcTable[i].next_proc_ptr = NULL;
      ProcTable[i].child_proc_ptr = NULL;
      ProcTable[i].next_sibling_ptr = NULL;
      ProcTable[i].parent_pid = NULL;
      strcpy(ProcTable[i].name, " ");
      ProcTable[i].start_arg[0] = '\0';
   }

   /*init trackers readylist etc*/
   if (DEBUG && debugflag)
      console("startup(): initializing the Ready & Blocked lists\n");
   numProc = 0;
   BlockedList = NULL;
   QuitList = NULL;
   for (int i = MAXPRIORITY; i <= SENTINELPRIORITY; i++){
      ReadyList[i] = NULL;
   }
   ZappedList = NULL;
 

   /* Initialize the clock interrupt handler */
   //TODO int_vec[CLOCK_INT] = clockHandler()

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

   dispatcher();

   console("startup(): Should not see this message! ");
   console("Returned from fork1 call that created start1\n");

   return;
} /* startup */

/* ------------------------------------------------------------------------
   Name - finish
   Purpose - Required by USLOSS
   Parameters - none
   Returns - nothing
   Side Effects - none
   ----------------------------------------------------------------------- */
void finish()
{
   if (debugflag)
      console("in finish...\n");
} /* finish */

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
int fork1(char *name, int (*f)(char *), 
          char *arg, int stacksize, int priority)
{
   int proc_slot;
   proc_ptr temp_proc;

   if (debugflag)
      console("fork1(): creating process %s\n", name);

   /* test if in kernel mode; halt if in user mode */
   if ((PSR_CURRENT_MODE & psr_get())==0){
      console("fork is currently in user mode, halting\n");
      halt(1);
   }

   /* Return if stack size is too small */
   if (stacksize < USLOSS_MIN_STACK){
      console("stack size is too small\n");
      return -2;
   }

   /* find an empty slot in the process table */
   for (int i=0; i <= MAXPROC; i++){
      if (ProcTable[i].status == EMPTY){
         proc_slot = i;
         break;
      }
   //return -1 if none found
      if (i >= MAXPROC){
         enableInterrupts();
         if (DEBUG && debugflag){
            console("fork func Process table is full\n");  
         }
         return -1;
      }
   } 

   /*check if sentinel*/
   if(strcmp(name, "sentinel")){
      if((priority>MINPRIORITY) || (priority<MAXPRIORITY)){
         if(debugflag && DEBUG){
            console("fork1() priority out of range\n");
         }
      return -1;
      }
   }

   /*make sure function and name is not null*/
   if ((f == NULL) || (name == NULL)){
      return -1;
   }
   

   /* fill-in entry in process table */
   if ( strlen(name) >= (MAXNAME - 1) ) {
      console("fork1(): Process name is too long.  Halting...\n");
      halt(1);
   }

   strcpy(ProcTable[proc_slot].name, name);
   ProcTable[proc_slot].start_func = f;
   if ( arg == NULL )
      ProcTable[proc_slot].start_arg[0] = '\0';
   else if ( strlen(arg) >= (MAXARG - 1) ) {
      if(debugflag && DEBUG){
      console("fork1(): argument too long.  Halting...\n");
      }
      halt(1);
   }else{
      strcpy(ProcTable[proc_slot].start_arg, arg);
   }
   ProcTable[proc_slot].stacksize = stacksize;
   ProcTable[proc_slot].stack = (char *)(malloc(stacksize));
   ProcTable[proc_slot].pid = next_pid++;
   ProcTable[proc_slot].priority = priority;
   ProcTable[proc_slot].status = READY;
   if (Current != NULL){
      ProcTable[proc_slot].parent_pid = Current;
   }else{
      ProcTable[proc_slot].parent_pid = 0;
   }
   ProcTable[proc_slot].childrenCount = 0;
   
      

   /* Initialize context for this process, but use launch function pointer for
    * the initial value of the process's program counter (PC)
    */
   context_init(&(ProcTable[proc_slot].state), psr_get(),
                ProcTable[proc_slot].stack, 
                ProcTable[proc_slot].stacksize, launch);

   /* for future phase(s) */
   p1_fork(ProcTable[proc_slot].pid);
   return 0;
} /* fork1 */

/* ------------------------------------------------------------------------
   Name - launch
   Purpose - Dummy function to enable interrupts and launch a given process
             upon startup.
   Parameters - none
   Returns - nothing
   Side Effects - enable interrupts
   ------------------------------------------------------------------------ */
void launch()
{
   int result;

   if (DEBUG && debugflag)
      console("launch(): started\n");

   /* Enable interrupts */
   enableInterrupts();

   /* Call the function passed to fork1, and capture its return value */
   result = Current->start_func(Current->start_arg);

   if (DEBUG && debugflag)
      console("Process %d returned to launch\n", Current->pid);

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
int join(int *code)
{
   return 0;
} /* join */


/* ------------------------------------------------------------------------
   Name - quit
   Purpose - Stops the child process and notifies the parent of the death by
             putting child quit info on the parents child completion code
             list.
   Parameters - the code to return to the grieving parent
   Returns - nothing
   Side Effects - changes the parent of pid child completion status list.
   ------------------------------------------------------------------------ */
void quit(int code)
{
   p1_quit(Current->pid);
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
void dispatcher(void)
{
   /*need to check current proccess priority*/
   //if((Current !=NULL) && (Current->priority <= ReadyList->priority) && (Current->status == RUNNING)){ //} && readtime() < 80){
   //   return;
   //}

   proc_ptr next_process = NULL;
   proc_ptr prev_process = NULL;
   proc_ptr temp = NULL;
   int i = MAXPRIORITY;

   if (DEBUG && debugflag){
      console("dispatcher called\n");
   }
   /*Check kernel mode*/
   if((PSR_CURRENT_MODE & psr_get()) == 0){
      //not in kernel mode
      if (debugflag && DEBUG){
         console("Dispatcher Kernel Error: Not in kernel mode\n");
      }
      halt(1);
   }
   disableInterrupts();
   /* Pick the next process to run */
   while ((i<MINPRIORITY) && (next_process == NULL)){
      temp = ReadyList[i];
      while((temp !=NULL) && (next_process == NULL)){
         next_process = temp;
         temp = temp->next_proc_ptr;
      }
      i+=1;
   }
   if (next_process == NULL){
      if(BlockedList !=NULL){
         BlockedList->status = READY;
         next_process = BlockedList;
         BlockedList = BlockedList->next_proc_ptr;
      }
      /*update readyList*/
      temp = ReadyList[next_process->priority];
      if (ReadyList[next_process->priority]){
         ReadyList[next_process->priority] = next_process;
      }else{
         while (temp != NULL){
            temp = temp->next_proc_ptr;
         }
      }
   }else{
      next_process = &ProcTable[1];
   }

   /*TODO context switching*/
   if (Current == NULL){
      p1_switch(0, next_process->pid);
      Current = next_process;

      context_switch(NULL, &next_process->state);
   }
   p1_switch(Current->pid, next_process->pid);
   prev_process= Current;
   Current = next_process;
   Current->startTime = sys_clock();

   enableInterrupts();
   context_switch(&prev_process->state, &Current->state);
   enableInterrupts();


   /*
   if (prev_process == NULL){
      next_process->status = RUNNING;
      removeFromReadyList(next_process->pid);
      if(debugflag && DEBUG){
         console("dispatcher(): %s", next_process->name);
      }
      next_process->startTime = sys_clock();
      context_switch(NULL, &next_process->state);      
   }else if(prev_process->status == QUIT){
      next_process->status = RUNNING;
      removeFromReadyList(next_process->pid);
      //prev_process->CPUtime = prev_process->CPUtime + readtime();
      next_process->startTime = sys_clock();
      context_switch(&prev_process->state, &next_process->state);
   }
   else{
      next_process->status = RUNNING;
      removeFromReadyList(next_process->pid);

      //check if process is blocked, if not inster into readyList
      if(prev_process->status != BLOCKED){
         prev_process->status = READY;
         insertReadyList(prev_process);
      }
   }
*/
}   /* dispatcher */


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
int sentinel (char * dummy)
{
   if (DEBUG && debugflag)
      console("sentinel(): called\n");
   while (1)
   {
      check_deadlock();
      waitint();
   }
} /* sentinel */


/* check to determine if deadlock has occurred... */
static void check_deadlock()
{
} /* check_deadlock */


/*
 * Disables the interrupts.
 */
void disableInterrupts()
{
  /* turn the interrupts OFF iff we are in kernel mode */
  if((PSR_CURRENT_MODE & psr_get()) == 0) {
    //not in kernel mode
    console("Kernel Error: Not in kernel mode, may not disable interrupts\n");
    halt(1);
  } else
    /* We ARE in kernel mode */
    psr_set( psr_get() & PSR_CURRENT_INT);
} /* disableInterrupts */

/*
 * Enables interrupts
 */
void enableInterrupts(){
   if((PSR_CURRENT_MODE & psr_get()) ==0){
      console("Not in kernel mode\n");
      halt(1);
   }else
      psr_set(psr_get() | PSR_CURRENT_INT);
}
/* -------------------------------------------------------------------------------
   Name - insertReadyList() 
   Purpose - inserts entries into the ReadyList in by priority
   Parameters - a process pointer 
   Returns -  Nothing
   -------------------------------------------------------------------------------*/
static void insertReadyList(proc_ptr proc)
{
   proc_ptr walker=ReadyList;
   proc_ptr previous=NULL;

   /*walk through readyList until priority matches*/
   while (walker != NULL && walker->priority <= proc->priority) {
      previous = walker;
      walker = walker->next_proc_ptr;
   }
   if (previous == NULL) {
      /* process goes at front of ReadyList */
      proc->next_proc_ptr = ReadyList;
      //ReadyList = proc;
   }
   else {
      /* process goes after previous */
      previous->next_proc_ptr = proc;
      proc->next_proc_ptr = walker;
   }
   return;
} /* insertReadyList */

/* --------------------------------------------------------------------------------
   Name - removeFromReadyList()
   Purpose - removes entry from the ReadyList
   Parameters - Accepts a PID of the process to be removed
   Returns -  Nothing
   --------------------------------------------------------------------------------*/
static void removeFromReadyList(int PID)
{
   proc_ptr walker = ReadyList;
   proc_ptr tmp = walker;

   /*check if sentinel is the only process in the ready list, don't do anything*/
   if(walker->next_proc_ptr == NULL){
      return;
   }

   /*check if process is the first item in the ready list*/
   if(walker->pid == PID){
      //ReadyList = walker->next_proc_ptr;
      walker->next_proc_ptr = NULL;
      return;
   }

   /*check if process is already in the readyList*/
   while (walker != NULL && walker->pid != PID){
      tmp = walker;
      walker = walker->next_proc_ptr;
   }
   if (walker->pid == PID){
      tmp->next_proc_ptr = walker->next_proc_ptr;
      walker->next_proc_ptr = NULL;
      return;
   }
   return;
} /* removeFromReadyList */