#define DEBUG 1

typedef struct proc_struct proc_struct;

typedef struct proc_struct * proc_ptr;

struct proc_struct {
    proc_ptr next_proc_ptr;
    proc_ptr parent_ptr;

    proc_ptr child_proc_ptr;
    proc_ptr quit_child_ptr;

    proc_ptr next_sibling_ptr;
    proc_ptr next_quit_sibling_ptr;

    proc_ptr zapped;
    proc_ptr next_zapped_proc;

    char name[MAXNAME]; /* process's name */
    char start_arg[MAXARG]; /* args passed to process */
    context state; /* current context for process */
    short pid; /* process id */
    int priority;
    int( * start_func)(char * ); /* function where process begins -- launch */
    char * stack;
    unsigned int stacksize;
    int status; /* READY = 2, BLOCKED, QUIT, etc. */
    int quit;
    int startTime;
    int zap;
    int childCount;
    int cpuTime;
    /* other fields as needed... */
};

struct psr_bits {
    unsigned int cur_mode: 1;
    unsigned int cur_int_enable: 1;
    unsigned int prev_mode: 1;
    unsigned int prev_int_enable: 1;
    unsigned int unused: 28;
};

union psr_values {
    struct psr_bits bits;
    unsigned int integer_part;
};

/* Some useful constants.  Add more as needed... */
#define NO_CURRENT_PROCESS NULL

#define READY 1
#define RUNNING 2
#define QUIT 3
#define BLOCKED 4
#define EMPTY 5
#define JOIN_BLOCK 6
#define ZAP_BLOCK 7

#define MINPRIORITY 5
#define MAXPRIORITY 1
#define SENTINELPID 1
#define SENTINELPRIORITY 6
#define TIME_SLICE 80000