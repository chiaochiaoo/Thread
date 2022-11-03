#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include <stdlib.h>
#include "thread.h"
#include "interrupt.h"


/* ------------------------------ Constants ------------------------------ */

typedef enum {
    READY = 0,
    RUNNING = 1,
    SLEEP = 2,
    EXIT = 3
} state;

typedef enum {
    FREE = 4,
    LOCKED = 5
} mutex_state;

/* ------------------------------- Structs ------------------------------ */

/* Linked list data structure for queue implementation */
typedef struct node {
    struct thread* thread;
    struct node* next;
} node;

/* This is the wait queue structure */
typedef struct wait_queue {
    int size;
    struct node* start_node;
    struct node* end_node;
    
} wait_queue;

/* This is the thread control block */
typedef struct thread {
    int t_id;
    ucontext_t t_context;
    void* t_stack;
    state t_state;
    wait_queue* t_queue;
    void* t_exit_code;
} thread;


/* -------------------------- Global Variables ------------------------- */

static wait_queue* thread_ready_queue = NULL;
static thread* thread_id_map[THREAD_MAX_THREADS] = {NULL};


/* -------------------------- Helper Functions-------------------------- */

wait_queue* create_queue() {
    wait_queue* queue = (wait_queue *) malloc(sizeof(wait_queue));
    queue->size = 0;
    queue->start_node = NULL;
    queue->end_node = NULL;
    return queue;
}

void enqueue(wait_queue* queue, thread* thread) {
    node* new_node = (node *) malloc(sizeof(node));
    new_node->thread = thread;
    new_node->next = NULL;
    if (queue->size == 0) {
        queue->start_node = new_node;
        queue->end_node = new_node;
    } else {
        queue->end_node->next = new_node;
        queue->end_node = new_node;
    }
    queue->size += 1;
}

void* dequeue(wait_queue* queue) {
    if (queue->start_node == NULL) {
        return NULL;
    } else if (queue->start_node->next == NULL) {
        node* head = queue->start_node;
        thread* thread = head->thread;
        queue->start_node = NULL;
        queue->size -=1;
        free(head);
        return thread;
    } else {
        node* head = queue->start_node;
        thread* thread = head->thread;
        node* next = queue->start_node->next;
        queue->start_node = next;
        queue->size -= 1;
        free(head);
        return thread;
    }
}

void* queue_head_thread(wait_queue* queue) {
    if (queue == NULL || queue->start_node == NULL) {
        return NULL;
    } else {
        return queue->start_node->thread;
    }
}

void* queue_move_to_head(wait_queue* queue, Tid tid) {
    if (queue == NULL) {
        return NULL;
    } else if (tid == queue->start_node->thread->t_id) {
        return queue->start_node->thread;
    } else if (queue->size == 1) {
        return NULL;
    } else {
        node* curr = queue->start_node;
        while (curr->next != NULL && curr->next->thread->t_id != tid) {
            curr = curr->next;
        }
        if (curr->next == NULL) {
            return NULL;
        } else {
            node* wanted_node = curr->next;
            thread* wanted_thread = wanted_node->thread;
            assert(wanted_thread->t_id == tid);
            curr->next = wanted_node->next;
            wanted_node->next = queue->start_node;
            queue->start_node = wanted_node;
            return wanted_thread;
        }
    }   
}

void queue_cleanup(wait_queue* queue) {
    node* curr = queue->start_node;
    // if (curr->thread->t_state == EXIT) {  
    //     node* exited_node = curr;      
    //     thread** exited_thread = &curr->thread;
    //     Tid tid = (*exited_thread)->t_id;
    //     queue->start_node = curr->next;
    //     queue->size -= 1;
    //     thread_id_map[tid] = NULL;
    //     free((*exited_thread)->t_stack);
    //     free(*exited_thread);
    //     free(exited_node);
    // }
    while (curr->next != NULL && curr->next->thread->t_state != EXIT) {
        curr = curr->next;
    }
    if (curr->next != NULL) {
        node* exited_node = curr->next;
        thread** exited_thread = &curr->next->thread;
        if ((*exited_thread)->t_exit_code == NULL) {
            Tid tid = (*exited_thread)->t_id;
            if (curr->next->next == NULL) {
                thread_ready_queue->end_node = curr;
            }
            curr->next = curr->next->next;  
            queue->size -= 1;  
            thread_id_map[tid] = NULL;       
            free((*exited_thread)->t_stack);
            free(*exited_thread);
            free(exited_node);
        }
    }
}


/* ---------------------------- Implementations -------------------------- */

void
thread_init(void)
{
    /* Add necessary initialization for your threads library here. */
	/* Initialize the thread control block for the first thread */
    thread_ready_queue = create_queue();
    thread* first_thread = (thread *) malloc(sizeof(thread));
    // getcontext(&first_thread->t_context);
    first_thread->t_id = 0;
    first_thread->t_state = RUNNING;
    first_thread->t_queue = wait_queue_create();
    first_thread->t_exit_code = NULL;
    thread_id_map[0] = first_thread;
    enqueue(thread_ready_queue, first_thread);
}

Tid
thread_id()
{
    if (thread_ready_queue == NULL || thread_ready_queue->start_node == NULL) {
        return THREAD_INVALID;
    } else {
        thread* running_thread = (thread *) queue_head_thread(thread_ready_queue);
        return running_thread->t_id;
    }
    
}

/* New thread starts by calling thread_stub. The arguments to thread_stub are
 * the thread_main() function, and one argument to the thread_main() function. 
 */
void
thread_stub(void (*thread_main)(void *), void *arg)
{
    int interrupt_enabled = interrupts_set(1);
    queue_cleanup(thread_ready_queue);
    thread_main(arg); // call thread_main() function with arg
    interrupts_set(interrupt_enabled);
	thread_exit(0);
    exit(0);
}

Tid
thread_create(void (*fn) (void *), void *parg)
{
    int interrupt_enabled = interrupts_set(0);
    queue_cleanup(thread_ready_queue);
    thread* new_thread = (thread *) malloc(sizeof(thread));
    if (new_thread == NULL) {
        free(new_thread);
        interrupts_set(interrupt_enabled);
        return THREAD_NOMEMORY;
    }
    int i;
    for (i = 1; i < THREAD_MAX_THREADS; i++) {
        if (thread_id_map[i] == NULL) {
            new_thread->t_id = i;
            thread_id_map[i] = new_thread;
            void* new_stack = malloc(THREAD_MIN_STACK);
            if (new_stack == NULL) {
                free(new_stack);
                free(new_thread);
                interrupts_set(interrupt_enabled);
                return THREAD_NOMEMORY;
            }
            enqueue(thread_ready_queue, new_thread);
            int err = getcontext(&(new_thread->t_context));
            assert(!err);
            new_thread->t_state = READY;
            new_thread->t_stack = new_stack;
            new_thread->t_context.uc_stack.ss_sp = new_stack;
            new_thread->t_context.uc_stack.ss_size = THREAD_MIN_STACK;
            new_thread->t_context.uc_mcontext.gregs[REG_RIP] = (unsigned long) thread_stub;
            new_thread->t_context.uc_mcontext.gregs[REG_RSP] = (unsigned long) new_stack + THREAD_MIN_STACK - sizeof(long);
            new_thread->t_context.uc_mcontext.gregs[REG_RDI] = (unsigned long) fn;
            new_thread->t_context.uc_mcontext.gregs[REG_RSI] = (unsigned long) parg;
            new_thread->t_context.uc_mcontext.gregs[REG_RBP] = 0;          
            interrupts_set(interrupt_enabled); 
            return new_thread->t_id;
        }    
    }
    if (i == THREAD_MAX_THREADS) {
        interrupts_set(interrupt_enabled); 
        return THREAD_NOMORE;
    }
    interrupts_set(interrupt_enabled); 
    return THREAD_FAILED;
}

Tid
thread_yield(Tid want_tid)
{
    int interrupt_enabled = interrupts_set(0);
    if (want_tid == THREAD_SELF) {
        queue_cleanup(thread_ready_queue);
        interrupts_set(interrupt_enabled);
        return thread_id();
    } else if (want_tid == THREAD_ANY) {

        if (thread_ready_queue->size <= 1) {
            interrupts_set(interrupt_enabled);
            return THREAD_NONE;
        }

        /* Move current head to tail, and run the next ready thread. */
        enqueue(thread_ready_queue, dequeue(thread_ready_queue));     
        if (thread_ready_queue->end_node->thread->t_state == RUNNING) {
            thread_ready_queue->end_node->thread->t_state = READY;
        }   
        thread_ready_queue->start_node->thread->t_state = RUNNING;
        
        /* Switch context */
        volatile int setcontext_called = 0;
        getcontext(&(thread_ready_queue->end_node->thread->t_context));      
        if (!setcontext_called) {
            setcontext_called = 1;
            int err = setcontext(&(thread_ready_queue->start_node->thread->t_context));
            assert(!err);
        }     
        queue_cleanup(thread_ready_queue);
        interrupts_set(interrupt_enabled);
        return thread_ready_queue->start_node->thread->t_id;

    } else {

        if (want_tid == thread_id()){
            queue_cleanup(thread_ready_queue);
            interrupts_set(interrupt_enabled);
            return want_tid;
        }

        if (want_tid >= 0 && want_tid < THREAD_MAX_THREADS && thread_id_map[want_tid] != NULL && 
            thread_id_map[want_tid]->t_state != EXIT) {    

            /* Move current head to tail, and move wanted thread to head. */
            enqueue(thread_ready_queue, dequeue(thread_ready_queue));
            queue_move_to_head(thread_ready_queue, want_tid);

            if (thread_ready_queue->end_node->thread->t_state == RUNNING) {
                thread_ready_queue->end_node->thread->t_state = READY;
            }            
            thread_ready_queue->start_node->thread->t_state = RUNNING;

            /* Switch context */
            volatile int setcontext_called = 0;
            getcontext(&(thread_ready_queue->end_node->thread->t_context));            
            if (!setcontext_called) {
                setcontext_called = 1;
                int err = setcontext(&(thread_ready_queue->start_node->thread->t_context));
                assert(!err);
            }       
            queue_cleanup(thread_ready_queue);
            interrupts_set(interrupt_enabled);
            return want_tid;

        } else {
            queue_cleanup(thread_ready_queue);
            interrupts_set(interrupt_enabled);
            return THREAD_INVALID;

        }
    }
}

void
thread_exit(int exit_code)
{
    int interrupt_enabled = interrupts_set(0);
    node* curr_node = thread_ready_queue->start_node;
    thread* curr_thread = curr_node->thread;
    curr_thread->t_state = EXIT;  
    if (curr_node->next == NULL) {
        if (curr_thread->t_queue->size > 0) {
            curr_thread->t_exit_code = (void *) &exit_code;
            thread_wakeup(curr_thread->t_queue, 1); 
        }     
        interrupts_set(interrupt_enabled);
        exit(exit_code);
    } else {
        int err = thread_yield(THREAD_ANY);
        assert(err >= 0);
        interrupts_set(interrupt_enabled);
    }
}

Tid
thread_kill(Tid tid)
{
    int interrupt_enabled = interrupts_set(0);
    if (tid <= 0 || tid > THREAD_MAX_THREADS || thread_id_map[tid] == NULL || thread_id_map[tid]->t_state != READY) {
        queue_cleanup(thread_ready_queue);
        interrupts_set(interrupt_enabled);
        return THREAD_INVALID;
    } else {
        thread_id_map[tid]->t_state = EXIT;
        if (thread_id_map[tid]->t_queue->size > 0) {
            thread_id_map[tid]->t_exit_code = (void *) THREAD_KILLED;
            thread_wakeup(thread_id_map[tid]->t_queue, 1); 
        }
        queue_cleanup(thread_ready_queue);
        interrupts_set(interrupt_enabled);
        return tid;
    }
}

/**************************************************************************
 * Important: The rest of the code should be implemented in Assignment 3. *
 **************************************************************************/

/* make sure to fill the wait_queue structure defined above */
struct wait_queue *
wait_queue_create()
{
    struct wait_queue *wq;
    wq = create_queue();
    assert(wq);

    return wq;
}

void
wait_queue_destroy(struct wait_queue *wq)
{
    while (wq->size != 0) {
        thread* thread = dequeue(wq);
        free(thread->t_stack);
        wait_queue_destroy(thread->t_queue);
        free(thread);
    }
    free(wq);
}

Tid
thread_sleep(struct wait_queue *queue)
{
    int interrupt_enabled = interrupts_set(0);
    if (queue == NULL) {
        interrupts_set(interrupt_enabled);
        return THREAD_FAILED;
    }
    if (thread_ready_queue->size == 1) {
        interrupts_set(interrupt_enabled);
        return THREAD_NONE;
    }
    thread* current_thread = thread_ready_queue->start_node->thread;
    thread* control_thread = thread_ready_queue->start_node->next->thread;
    int control_thread_id = control_thread->t_id;
    getcontext(&(current_thread->t_context));
    dequeue(thread_ready_queue);
    current_thread->t_state = SLEEP;
    enqueue(queue, current_thread);
    control_thread->t_state = RUNNING;
    setcontext(&(control_thread->t_context));
    interrupts_set(interrupt_enabled);
    return control_thread_id;
}

/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int
thread_wakeup(struct wait_queue *queue, int all)
{
    int interrupt_enabled = interrupts_set(0);
    if (queue == NULL || queue->size == 0) {
        interrupts_set(interrupt_enabled);
        return 0;
    }
    if (all == 1) {
        int size = queue->size;
        while (queue->size != 0) {
            enqueue(thread_ready_queue, dequeue(queue));
            thread_ready_queue->end_node->thread->t_state = READY;
        }
        interrupts_set(interrupt_enabled);
        return size;
    } else {
        enqueue(thread_ready_queue, dequeue(queue));
        thread_ready_queue->end_node->thread->t_state = READY;
        interrupts_set(interrupt_enabled);
        return 1;
    }
}

/* suspend current thread until Thread tid exits */
Tid
thread_wait(Tid tid, int *exit_code)
{
    int interrupt_enabled = interrupts_set(0);
    if (tid < 0 || tid >= THREAD_MAX_THREADS || tid == thread_id() || thread_id_map[tid] == NULL) {
        interrupts_set(interrupt_enabled);
        return THREAD_INVALID;
    }
    thread_sleep(thread_id_map[tid]->t_queue);
    if (thread_id_map[tid] == NULL || thread_id_map[tid]->t_exit_code == NULL) {
        interrupts_set(interrupt_enabled);
        return THREAD_INVALID;
    }
    if (exit_code != NULL) {
        exit_code = thread_id_map[tid]->t_exit_code;
    }
    thread_id_map[tid]->t_exit_code = NULL;
    interrupts_set(interrupt_enabled);
    return tid;
}

struct lock {
    thread* locked_thread;
    mutex_state state;
    wait_queue* queue;
};

struct lock *
lock_create()
{
    int interrupt_enabled = interrupts_set(0);
    struct lock *lock;
    lock = malloc(sizeof(struct lock));
    lock->locked_thread = NULL;
    lock->state = FREE;
    lock->queue = wait_queue_create();
    assert(lock);
    interrupts_set(interrupt_enabled);
    return lock;
}

void
lock_destroy(struct lock *lock)
{   
    int interrupt_enabled = interrupts_set(0);
    assert(lock != NULL);
    wait_queue_destroy(lock->queue);
    free(lock);
    interrupts_set(interrupt_enabled);
}

void
lock_acquire(struct lock *lock)
{
    int interrupt_enabled = interrupts_set(0);
    assert(lock != NULL);
    thread_sleep(lock->queue);
    lock->locked_thread = thread_ready_queue->start_node->thread;
    lock->state = LOCKED;
    interrupts_set(interrupt_enabled);
}

void
lock_release(struct lock *lock)
{
    int interrupt_enabled = interrupts_set(0);
    assert(lock != NULL);
    lock->state = FREE;
    thread_wakeup(lock->queue, 1);
    lock->locked_thread = NULL;
    interrupts_set(interrupt_enabled);
}

struct cv {
    thread* current_thread;
    wait_queue* queue;
};

struct cv *
cv_create()
{
    struct cv *cv;
    cv = malloc(sizeof(struct cv));
    cv->queue = wait_queue_create();
    assert(cv);
    return cv;
}

void
cv_destroy(struct cv *cv)
{
    assert(cv != NULL);
    wait_queue_destroy(cv->queue);
    free(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
    int interrupt_enabled = interrupts_set(0);
    assert(cv != NULL);
    assert(lock != NULL);
    thread* current_thread = thread_ready_queue->start_node->thread;
    if (lock->locked_thread == current_thread && lock->state == LOCKED) {
        lock_release(lock);
        thread_sleep(cv->queue);
        lock_acquire(lock);
    }
    interrupts_set(interrupt_enabled);
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
    int interrupt_enabled = interrupts_set(0);
    assert(cv != NULL);
    assert(lock != NULL);
    thread* current_thread = thread_ready_queue->start_node->thread;
    if (lock->locked_thread == current_thread && lock->state == LOCKED) {
        thread_wakeup(cv->queue, 0);
    }
    interrupts_set(interrupt_enabled);
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
    int interrupt_enabled = interrupts_set(0);
    assert(cv != NULL);
    assert(lock != NULL);
    thread* current_thread = thread_ready_queue->start_node->thread;
    if (lock->locked_thread == current_thread && lock->state == LOCKED) {
        thread_wakeup(cv->queue, 1);
    }
    interrupts_set(interrupt_enabled);
}
