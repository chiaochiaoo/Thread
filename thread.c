#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include "thread.h"
#include "interrupt.h"

// Constants for thread status
#define EMPTY -1
#define READY 0
#define RUNNING 1
#define BLOCKED 2
#define KILLED 3
#define EXITED 4

// Constants for lock status
#define FREE 5
#define LOCKED 6

// Thread control block
struct wait_thread;
struct wait_queue;
struct thread;

struct wait_thread{
    struct thread * thread;
    struct wait_thread * next;
};

struct wait_queue {
    struct wait_thread * head_thread;
    struct wait_thread * tail_thread;
};

struct thread {
    Tid id;
    int status;
    void * stack;
    ucontext_t context;
    struct wait_queue thread_wait_queue;
    int is_waited;
    int exit_code;
};

// Global variables
int num_threads = 1;
struct thread thread_id_map[THREAD_MAX_THREADS];
struct thread* curr_thread;
int thread_ready_queue[THREAD_MAX_THREADS];
int ready_queue_size = 0;

/* --------------------------- Helper functions -------------------------- */ 

int thread_find_empty() {
    for (int i = 0; i < THREAD_MAX_THREADS; i++) {
        if (thread_id_map[i].status == EMPTY){
            return thread_id_map[i].id;
        }       
    }
    return -1;
}

int queue_find_tid(int tid){
    for (int i = 0; i < THREAD_MAX_THREADS; i++) {
        if (thread_ready_queue[i] == tid){
            return i;
        }
    }
    return -1;
}

void queue_cleanup(){
    ready_queue_size = 0;
    for (int i = 0; i < THREAD_MAX_THREADS; i++) {
        if (thread_ready_queue[i] != -1){
            thread_ready_queue[ready_queue_size] = thread_ready_queue[i];
            ready_queue_size++;
        }
    }
    for (int i = ready_queue_size; i < THREAD_MAX_THREADS; i++) {
        thread_ready_queue[i] = -1;
    }
}

int queue_first_thread(){
    for (int i = 0; i < THREAD_MAX_THREADS; i++) {
        if (thread_ready_queue[i] != -1){
            return i;
        }
    }
    return -1;
}

void enqueue_tid(int tid){
    if (queue_find_tid(tid)==-1){
        thread_ready_queue[ready_queue_size] = tid;
        ready_queue_size++;   
    }
    queue_cleanup();
}

void free_killed(){
    int enabled = interrupts_set(0);
    for (int i = 0; i < ready_queue_size; i++){
        if (((thread_id_map[thread_ready_queue[i]].status == KILLED)) && 
        thread_id_map[thread_ready_queue[i]].id != curr_thread->id){
            num_threads--;
            thread_id_map[thread_ready_queue[i]].status = EMPTY;
            free(thread_id_map[thread_ready_queue[i]].stack);
            thread_ready_queue[i] = -1;
        }
        if (thread_id_map[thread_ready_queue[i]].status == BLOCKED){
            thread_ready_queue[i] = -1;
        }
    }
    queue_cleanup();
    interrupts_set(enabled);
}

int
wait_queue_size(struct wait_queue *wq){
    int i = 0;
    struct wait_thread *curr = wq->head_thread;

    while (curr != NULL){
        i++;
        curr = curr->next;
    }  
    return i;
}

void wait_queue_enqueue(struct wait_queue *wq)
{
    if (wq->head_thread == NULL && wq->tail_thread==NULL){
        wq->head_thread = malloc(sizeof(struct wait_thread));
        wq->head_thread->thread = curr_thread;
        wq->tail_thread = wq->head_thread;
    } else {
        wq->tail_thread->next = malloc(sizeof(struct wait_thread));
        wq->tail_thread->next->thread = curr_thread; 
        wq->tail_thread = wq->tail_thread->next;
    }
}

void wait_queue_dequeue(struct wait_queue *wq)
{
    assert(wq->head_thread != NULL);
    if (wq->head_thread == wq->tail_thread){
        free(wq->head_thread);
        wq->head_thread = NULL;
        wq->tail_thread = NULL;
    } else {
        struct wait_thread *thread_removed = wq->head_thread;
        wq->head_thread = wq->head_thread->next;
        free(thread_removed);
    }
}

/* --------------------------- Thread Implementation -------------------------- */ 

void
thread_init(void)
{
    for (int i = 0; i < THREAD_MAX_THREADS; i++) {
        thread_ready_queue[i] = -1;
        thread_id_map[i].id = i;
        thread_id_map[i].status = EMPTY;      
    }
    ready_queue_size = 0;
    curr_thread = &thread_id_map[0];
    curr_thread->status = RUNNING;
    getcontext(&(curr_thread->context));
    thread_id_map[0].context.uc_link = NULL;
    thread_id_map[0].context.uc_stack.ss_flags = 0;
}

Tid
thread_id()
{
    int enabled = interrupts_set(0);
    if (curr_thread!=NULL){
        interrupts_set(enabled);
        return curr_thread->id;
    } else {
        interrupts_set(enabled);
        return THREAD_INVALID;
    }
}

/* New thread starts by calling thread_stub. The arguments to thread_stub are
 * the thread_main() function, and one argument to the thread_main() function. 
 */
void
thread_stub(void (*thread_main)(void *), void *arg)
{
    interrupts_on();
    thread_main(arg); // call thread_main() function with arg
    thread_exit(0);
}


Tid
thread_yield(Tid want_tid)
{
    int enabled = interrupts_set(0);
    free_killed();
    int want_index = -1;
    if (want_tid < THREAD_FAILED || want_tid >= THREAD_MAX_THREADS){
        interrupts_set(enabled);
        return THREAD_INVALID;
    }

    if (want_tid == THREAD_ANY && ready_queue_size == 0) {
        interrupts_set(enabled);
        return THREAD_NONE;
    }

    if (want_tid == THREAD_SELF || want_tid == curr_thread->id) {
        interrupts_set(enabled);
        return curr_thread->id;
    }

    if (want_tid == THREAD_ANY){
        want_index = queue_first_thread();
    } else {
        want_index = queue_find_tid(want_tid);
    }

    if (want_index == -1){
        interrupts_set(enabled);
        return THREAD_INVALID;
    }

    want_tid = thread_id_map[thread_ready_queue[want_index]].id;

    volatile int setcontext_called = 0;
    getcontext(&(curr_thread->context));
    if (setcontext_called){
        free_killed();
        setcontext_called = 0;
        interrupts_set(enabled);
        return want_tid;
    } else {
        setcontext_called = 1;
        if (curr_thread->status!=BLOCKED){
            enqueue_tid(curr_thread->id);
        }
        curr_thread = &thread_id_map[thread_ready_queue[want_index]];
        thread_ready_queue[want_index] = -1;
        setcontext(&(curr_thread->context));
    }

    interrupts_set(enabled);
    return curr_thread->id;
}


Tid
thread_create(void (*fn) (void *), void *parg)
{
    int enabled = interrupts_set(0);
    if (num_threads >= THREAD_MAX_THREADS){
        interrupts_set(enabled);
        return THREAD_NOMORE;
    }
    void * stack = malloc(THREAD_MIN_STACK);
    if (stack == NULL) {
        interrupts_set(enabled);
        return THREAD_NOMEMORY; 
    }

    int available_id = thread_find_empty();

    thread_id_map[available_id].stack = stack;
    thread_id_map[available_id].status = READY;

    getcontext(&(thread_id_map[available_id].context));

    thread_id_map[available_id].context.uc_link = NULL;
    thread_id_map[available_id].context.uc_stack.ss_sp = stack;
    thread_id_map[available_id].context.uc_stack.ss_size = THREAD_MIN_STACK;
    thread_id_map[available_id].context.uc_stack.ss_flags = 0;

    thread_id_map[available_id].context.uc_mcontext.gregs[REG_RBP] = (unsigned long long) stack + THREAD_MIN_STACK - 8;
    thread_id_map[available_id].context.uc_mcontext.gregs[REG_RSP] = (unsigned long long) stack + THREAD_MIN_STACK - 8;
    thread_id_map[available_id].context.uc_mcontext.gregs[REG_RIP] = (unsigned long long) &thread_stub;
    thread_id_map[available_id].context.uc_mcontext.gregs[REG_RDI] = (unsigned long long) fn;
    thread_id_map[available_id].context.uc_mcontext.gregs[REG_RSI] = (unsigned long long) parg;

    enqueue_tid(thread_id_map[available_id].id);
    num_threads++;

    interrupts_set(enabled);
    return available_id;
}

void
thread_exit(int exit_code)
{
    int enabled = interrupts_set(0);

    if (num_threads==1){
        interrupts_set(enabled);
        exit(exit_code);
    }

    curr_thread->status = KILLED;

    if (exit_code){
        curr_thread->exit_code = exit_code;
    } else {
        curr_thread->exit_code = THREAD_MAX_THREADS+curr_thread->id-1;
    }
    
    thread_wakeup(&curr_thread->thread_wait_queue,1);
    thread_yield(THREAD_ANY);
    
    interrupts_set(enabled);
    exit(exit_code);
}

Tid
thread_kill(Tid tid)
{
    int enabled = interrupts_set(0);

    if (tid < 0 || tid > THREAD_MAX_THREADS || tid == thread_id()){
        interrupts_set(enabled);
        return THREAD_INVALID;
    } else {
        if (queue_find_tid(tid) != -1){
            thread_id_map[tid].status = KILLED;
        } else if (thread_id_map[tid].status == BLOCKED){
            thread_id_map[tid].status = KILLED;
        } else{
            interrupts_set(enabled);
            return THREAD_INVALID;
        }      
    }
    interrupts_set(enabled);
    return tid;

}


struct wait_queue *
wait_queue_create()
{
    struct wait_queue *wq;
    wq = malloc(sizeof(struct wait_queue));
    assert(wq);
    return wq;
}

void
wait_queue_destroy(struct wait_queue *wq)
{
    struct wait_thread *curr = wq->head_thread;
    while (wq->head_thread != NULL)
    {
       curr = wq->head_thread;
       wq->head_thread = wq->head_thread->next;
       free(curr);
    }
    free(wq);
}

Tid
thread_sleep(struct wait_queue *queue)
{
    int enabled = interrupts_set(0);
    if (queue == NULL){
        interrupts_set(enabled);
        return THREAD_INVALID;
    }
    if (ready_queue_size==0){
        interrupts_set(enabled);
        return THREAD_NONE;
    } else {
        curr_thread->status = BLOCKED;
        wait_queue_enqueue(queue);
    }
    interrupts_set(enabled);
    ready_queue_size--;
    return thread_yield(THREAD_ANY);
}

/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int
thread_wakeup(struct wait_queue *queue, int all)
{
    int enabled = interrupts_set(0);

    if (queue == NULL || queue->head_thread == NULL) {
        interrupts_set(enabled);
        return 0;
    }

    if (all == 1) {
        int num_wake = 0;
        while (thread_wakeup(queue,0)){
            num_wake++;
        }
        interrupts_set(enabled);
        return num_wake;

    } else {

        struct wait_thread * head_thread = queue->head_thread;

        assert(head_thread);

        if (head_thread->thread->status != KILLED){
            head_thread->thread->status = READY;
        }

        enqueue_tid(head_thread->thread->id);
        wait_queue_dequeue(queue); 

        interrupts_set(enabled);
        return 1;

    }
}

/* suspend current thread until Thread tid exits */
Tid
thread_wait(Tid tid, int *exit_code)
{
    int enabled = interrupts_set(0);

    if (tid < 0 || tid >= THREAD_MAX_THREADS || tid == thread_id() || thread_id_map[tid].status == EMPTY){
        if (exit_code){
            *exit_code = THREAD_INVALID;
        }
        interrupts_set(enabled);
        return THREAD_INVALID;
    }

    if (thread_id_map[tid].status == KILLED || curr_thread->status == BLOCKED 
    || thread_id_map[tid].status == EMPTY || thread_id_map[tid].is_waited == 1) {
        if (exit_code){
            *exit_code = THREAD_KILLED;
        }
        interrupts_set(enabled);
        return THREAD_INVALID;
    }

    Tid next_thread_id = thread_sleep(&(thread_id_map[tid].thread_wait_queue));

    if (thread_id_map[tid].is_waited==0){
        thread_id_map[tid].is_waited=1;
    } else {
        interrupts_set(enabled);
        return THREAD_INVALID;
    }

    if (next_thread_id == THREAD_NONE) {
        if (exit_code){
            *exit_code = THREAD_NONE;
        }
        interrupts_set(enabled);
        return THREAD_NONE;
    }

    if (exit_code){
        *exit_code = thread_id_map[tid].exit_code;
    }

    interrupts_set(enabled);
    return tid;
}

/* --------------------------- Lock Implementation -------------------------- */ 

struct lock {
    struct wait_queue * lock_wait_queue;
    int status; 
    Tid thread_id;

};

struct lock *
lock_create()
{
    int enabled = interrupts_set(0);
    struct lock *lock;
    lock = malloc(sizeof(struct lock));
    assert(lock);
    lock->lock_wait_queue = wait_queue_create();
    assert(lock->lock_wait_queue);
    lock->status = FREE;
    interrupts_set(enabled);
    return lock;
}

void
lock_destroy(struct lock *lock)
{
    int enabled = interrupts_set(0);
    assert(lock != NULL);
    wait_queue_destroy(lock->lock_wait_queue);
    interrupts_set(enabled);
}

void
lock_acquire(struct lock *lock)
{
    int enabled = interrupts_set(0);
    assert(lock != NULL);
    while(lock->status==LOCKED){
        thread_sleep(lock->lock_wait_queue);
    }  
    lock->status = LOCKED;
    lock->thread_id = thread_id();
    interrupts_set(enabled);
}

void
lock_release(struct lock *lock)
{
    int enabled = interrupts_set(0);
    assert(lock != NULL);
    lock->status = FREE;
    thread_wakeup(lock->lock_wait_queue,1);
    interrupts_set(enabled);
}

/* ----------------------- Condition Variables Implementation ---------------------- */ 

struct cv {
    struct wait_queue * cv_wait_queue;
};

struct cv *
cv_create()
{
    int enabled = interrupts_set(0);
    struct cv *cv = malloc(sizeof(struct cv));
    assert(cv);
    cv->cv_wait_queue = wait_queue_create();
    assert(cv->cv_wait_queue);
    interrupts_set(enabled);
    return cv;
}

void
cv_destroy(struct cv *cv)
{
    assert(cv != NULL);
    wait_queue_destroy(cv->cv_wait_queue);
    free(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
    int enabled = interrupts_set(0);
    assert(cv != NULL);
    assert(lock != NULL);
    if (lock->thread_id == thread_id()){
        lock_release(lock);
        thread_sleep(cv->cv_wait_queue);
        interrupts_set(enabled);
        lock_acquire(lock);

    }   
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
    int enabled = interrupts_set(0);
    assert(cv != NULL);
    assert(lock != NULL); 
    if (lock->thread_id == thread_id()){
        thread_wakeup(cv->cv_wait_queue,0);
    }
    interrupts_set(enabled);
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
    int enabled = interrupts_set(0);
    assert(cv != NULL);
    assert(lock != NULL);
    if (lock->thread_id == thread_id()){
        thread_wakeup(cv->cv_wait_queue,1);
    }
    interrupts_set(enabled);
}