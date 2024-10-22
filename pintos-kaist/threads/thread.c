#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
//add
#include "threads/fixed-point.h"

#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;
static struct list sleep_list;
static struct list mlfqs_list;


/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };



//add
// 여기와 헤더 모두에 일단 선언해둠
//default for numbers - mlfqs
#define NICE_DEFAULT 0
#define RECENT_CPU_DEFAULT 0
#define LOAD_AVG_DEFAULT 0

//add
//확인
int load_avg;

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* Init the globla thread context */
	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init (&sleep_list);
	list_init (&mlfqs_list); //advanced 에서 현재 돌고있는 list 알아야해서 필요
	list_init (&destruction_req);

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);

	//add
	//nice, recent_cpu 값 init
	initial_thread->nice = NICE_DEFAULT; // 헤더에서 선언해둔 고정값
	initial_thread->recent_cpu = RECENT_CPU_DEFAULT; //0으로 시작

	//mlfqs recalculate 위해서 필요함
	list_push_back(&mlfqs_list, &initial_thread->mlfqs_elem);

	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */


void
thread_start (void) {
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);

	//add
	//우선순위 선점 하기 전에 실행시간 변수 저장해줌
	//부팅할 때 0으로 설정됨
	load_avg = LOAD_AVG_DEFAULT;

	/* Start preemptive thread scheduling. */
	intr_enable ();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started);
}

void
thread_sleep (int64_t ticks)
{
  // disable interrupt
  enum intr_level old_level;
  old_level = intr_disable ();	
  
  // 핵심 코드, 현재 쓰레드 sleep list에 push back 하고서 thread_block
  struct thread *t;
  t = thread_current ();

  t->wakeup = ticks;		
  list_push_back (&sleep_list, &t->elem);	
  thread_block ();

  // set interrupt level as before
  intr_set_level (old_level);	
}



void
thread_awake (int64_t ticks)
{
  // sleep_list 제일 앞 단에서 호출 
  struct list_elem *e = list_begin (&sleep_list); 
  struct thread *t;
  while (e != list_tail (&sleep_list)){
    t = list_entry (e, struct thread, elem);
	// go through list and see if there is thread to wake up
    if (t->wakeup <= ticks){	
      e = list_remove (e);	//remove with c pointers
      thread_unblock (t);	
    }
	/* 현재 쓰레드가 sleep 해야 되는 경우, 다음 쓰레드로 update (순회)*/
    else 
      e = list_next (e);
  }
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce pre=emptn. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;
	
#ifdef USERPROG
	/* IMPLELEMENTED IN PROJECT 2-3. Initializes data structures for file descriptor table. */
	t->fd_table = (struct file **)palloc_get_page(PAL_ZERO);
	if (t->fd_table == NULL)
	{
		palloc_free_page(t);
		return TID_ERROR;
	}
	t->fd_table[0] = 0; /* For stdin. */
	t->fd_table[1] = 1; /* For stdout. */
#endif


	/* Initialize thread. */
	init_thread (t, name, priority);
	
	//1007
	//mlfqs handling
	if (thread_mlfqs) {
		if (function != idle){ //idle 은 제외하고 생각
			t->recent_cpu = thread_current() -> recent_cpu;
			t->nice = thread_current() -> nice;
			mlfqs_priority_cal(t);
			list_push_back(&mlfqs_list, &t->mlfqs_elem);
		}
	}
	
	tid = t->tid = allocate_tid ();

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* Add to run queue. */
	thread_unblock (t);
	// added. just pre-empt
	thread_pre_empt ();
	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

void
test_max_priority(void){
	if (list_empty(&ready_list)) return;
	if (!intr_context() && cmp_priority(list_front(&ready_list), &thread_current()->elem,NULL)) thread_yield();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  //list_push_back (&ready_list, &t->elem);
  // Reference : Hints from EE415 Lecture
  list_insert_ordered (&ready_list, &t->elem, cmp_priority, 0); 
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();

	//mlfqs list 에서 지워줌
	if (thread_mlfqs){
		//struct list_elem *
		// list_remove (struct list_elem *elem) {
		// 	ASSERT (is_interior (elem));
		// 	elem->prev->next = elem->next;
		// 	elem->next->prev = elem->prev;
		// 	return elem->next;
		// }
		list_remove(&thread_current()->mlfqs_elem);
	}

	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}


//get priority of a thread
int
return_priority (struct list_elem *e){
	return list_entry (e, struct thread,elem)->priority;
}


//compare priority of two threads
bool
cmp_priority (struct list_elem *a, struct list_elem *b, void *aux UNUSED)
{
    return return_priority(a) > return_priority(b);
}


//compare when priority donation
bool
cmp_priority_donate (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{	
	int priority_a = list_entry (a, struct thread, d_elem)->priority;
	int priority_b = list_entry (b, struct thread, d_elem)->priority;
	if (priority_a > priority_b) return 1;
	else return 0;
}


void donate_recursive(struct thread *curr) {
    // 현재 스레드가 wait 하는 lock 없으면 종료함
    if (curr->wait_on_lock == NULL) {
        return;
    }

    // 현재 스레드가 대기 중인 락의 holder를 호출해서 가져옴
    struct thread *holder = curr->wait_on_lock->holder;

    // 만약 holder가 NULL이면 return;
    if (holder == NULL) {
        return;
    }

    // 현재 스레드의 순위가 holder의 우선순위보다 높은 경우 donate.
    if (holder->priority < curr->priority) {
        // donation 진행
        holder->priority = curr->priority;
    }
    // 현재 스레드를 holder로 변경. 연속 donate 위해 recursive calling 함.
    donate_recursive(holder);
}

//if a thread ask for lock --> check and see if you can donate and wait for lock
void do_donate (void) {
    struct thread *th = thread_current();
    donate_recursive(th);
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) 
    //list_push_back (&ready_list, &cur->elem);
	// also we considers priority
    list_insert_ordered (&ready_list, &cur->elem, cmp_priority, 0);
  do_schedule (THREAD_READY);
  intr_set_level (old_level);
}

void 
thread_pre_empt (void) // ≈ oprate. 
// Even though current thread is running, if front ready state thread's priority is higher, we yield 
{
    if (list_empty(&ready_list)!=true){
		int current_thread_priority = thread_get_priority();
		int front_thread_priority = list_entry(list_front (&ready_list), struct thread, elem) ->priority;
		if ( current_thread_priority<front_thread_priority){
		thread_yield ();
		}
		else{
			;
		}
	}
}

void
thread_set_priority(int new_priority) 
{
	if (thread_mlfqs) return ;
	else{
    struct thread *cur = thread_current();
    // new priority giving
    cur->priority_before_donations = new_priority;
    cur->priority = new_priority;
    if (!list_empty(&cur->donations)) {
        struct list_elem *e;
        // donations list 순회 하면서, 제일 높은애를 할당함
        for (e = list_begin(&cur->donations); e != list_end(&cur->donations); e = list_next(e)) {
			if (list_entry(e, struct thread, d_elem)->priority > cur->priority) cur->priority = list_entry(e, struct thread, d_elem)->priority;
        }
    }
    thread_pre_empt();
	}
}


/* Returns the current thread's priority. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) {
	/* TODO: Your implementation goes here */
	//Sets the current thread's nice value to new nice and recalculates the thread's priority based on the new value (see Calculating Priority). 
	//If the running thread no longer has the highest priority, yields.

    enum intr_level old_level; //인터럽트 중지시킴
    old_level = intr_disable ();
	thread_current()->nice = nice;
	mlfqs_priority_cal(thread_current()); //nice 가 바뀌었으니 우선순위 다시 계산
	
	//priority 다시 계산해서 바뀌면 yield
	thread_pre_empt ();
	
    intr_set_level (old_level);
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	/* TODO: Your implementation goes here */
    enum intr_level old_level;
    old_level = intr_disable ();
	int nice_got = thread_current() -> nice;

    intr_set_level (old_level);
	return nice_got;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	/* TODO: Your implementation goes here */
    enum intr_level old_level;
    old_level = intr_disable ();
	int load_avg_got = round_fp_to_n(mul_fp_n(load_avg, 100));

    intr_set_level (old_level);
	return load_avg_got;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
	/* TODO: Your implementation goes here */
    enum intr_level old_level;
    old_level = intr_disable ();
	int recent_cpu_current = thread_current()-> recent_cpu;
	int recent_cpu_got = round_fp_to_n(mul_fp_n(recent_cpu_current, 100));

    intr_set_level (old_level);
	return recent_cpu_got;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;

	
	t->priority_before_donations = priority;
  	t->wait_on_lock = NULL;
  	list_init (&t->donations);
	

}


/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

static void
schedule (void) {
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}


void
mlfqs_priority_cal (struct thread *t) {
	//test if idle
	if (t == idle_thread) return;
	// PRI_MAX (- recent_cpu /4 ) - ( nice *2 )
	// (- recent_cpu /4 ) + (PRI_MAX - ( nice *2 ))
	// -div_fp_n(recent_cpu, -4) mul_fp_n(nice, 2)
	int priority_calculated;
	int term2 = div_fp_n(t->recent_cpu, -4); //fp
	int term3 = t->nice * 2; //n
	int result_fp = add_fp_n(term2, PRI_MAX - term3); //PRI_MAX = 63 (n)
	priority_calculated = fp_to_n(result_fp);
	t->priority = priority_calculated;
}

// increment recent_cpu on timer
void
mlfqs_recent_cpu_increment (void){
	struct thread *curr = thread_current();
	if (curr != idle_thread){
		curr->recent_cpu = add_fp_n(curr->recent_cpu, 1);
	}
}


void
mlfqs_recent_cpu_cal (struct thread *t){
	//test if idle
	if (t == idle_thread) return;

    // decay factor 계산: (2 * load_avg) / (2 * load_avg + 1)
    int decay = div_fp_fp(mul_fp_n(load_avg, 2), add_fp_n(mul_fp_n(load_avg, 2), 1));

    // recent_cpu 계산: decay * recent_cpu + nice
    t->recent_cpu = add_fp_fp(mul_fp_fp(decay, t->recent_cpu), n_to_fp(t->nice));

	// t->recent_cpu;
	//recent_cpu = decay * recent_cpu + nice
	//decay = (2*load_avg) / (2*load_avg+1)
}


//load_avg
//Returns 100 times the current system load average, rounded to the nearest integer.
//load_avg = (59/60) * load_avg + (1/60) * ready_threads,
void
mlfqs_load_avg_cal (void){
	int ready_threads_num = list_size(&ready_list);
	if (thread_current() != idle_thread) ready_threads_num++;
	
    // (59 / 60) * load_avg 
    int factor1 = div_fp_fp(n_to_fp(59), n_to_fp(60));  
    int term1 = mul_fp_fp(factor1, load_avg); 

    // (1 / 60) * ready_threads_num
    int factor2 = div_fp_fp(n_to_fp(1), n_to_fp(60)); 
    int term2 = mul_fp_n(factor2, ready_threads_num);  

    // load_avg = term1 + term2
    load_avg = add_fp_fp(term1, term2);  
	}




//recent cpu again 
// for all threads in the system
void
mlfqs_recal_recent_cpu (void){
    struct list_elem *e = list_begin(&mlfqs_list);  // 리스트의 시작 지점
	
// 	struct list_elem *
// list_begin (struct list *list) {
// 	ASSERT (list != NULL);
// 	return list->head.next;
// }

    while (e != list_end(&mlfqs_list)) { 
        struct thread *t = list_entry(e, struct thread, mlfqs_elem);
        
        mlfqs_recent_cpu_cal(t);

        e = list_next(e);
    }
}


//check priority for all threads in list
void
mlfqs_recal_priority (void){
    struct list_elem *e = list_begin(&mlfqs_list);  

    while (e != list_end(&mlfqs_list)) {  
        struct thread *t = list_entry(e, struct thread, mlfqs_elem);
        
        mlfqs_priority_cal(t);

        e = list_next(e);
    }
}
