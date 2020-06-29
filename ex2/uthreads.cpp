#include <deque>
#include <unordered_set>
#include "uthreads.h"
#include <stdio.h>
#include <signal.h>
#include <sys/time.h>
#include <setjmp.h>
#include <unistd.h>
#include<bits/stdc++.h>


#define JB_SP 6
#define JB_PC 7

typedef struct thread
{
    int id;
    int priority;
    char stack[STACK_SIZE];

    void (*f)(void);

    sigjmp_buf env;
    int quantums;
} thread;

thread *current_thread;

sigset_t block_mask{};
typedef unsigned long address_t;
struct itimerval timer;
struct sigaction sa;

int *quantum_usecs_arr;
int size_quantum_usecs;
int num_quantoms;

std::unordered_set<int> ids;
std::deque<thread *> ready_queue;
std::unordered_set<thread *> blocked_set;

// prototype
void del_all();

int get_new_id();

bool check_contains_neg(int const *arr, int size);

thread *find_thread_in_ready(int tid);

thread *find_thread(int tid);

thread *find_thread_in_blocked(int tid);

address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
                 "rol    $0x11,%0\n"
    : "=g" (ret)
    : "0" (addr));
    return ret;
}

/**
 * the method that handles an alarm,  using sigsetjmp and siglongjmp.
 * this method is also encharge of time managing
 * @param x an int represents how to handle the alarm.
 */
void handle_alarm(int x)
{
    int ret_val = sigsetjmp(current_thread->env, 1);
    if (ret_val == 1)
    {
        return;
    }
    if (x == 0 || x == 26)
    {
        ready_queue.push_back(current_thread);
    }
    else if (x == 1)
    {
        blocked_set.insert(current_thread);
    }
    else
    {
        ids.erase(current_thread->id);
        delete (current_thread);
    }
    thread *t = ready_queue.front();
    if (t != nullptr)
    {
        ready_queue.pop_front();
        timer.it_value.tv_usec = quantum_usecs_arr[t->priority];
        timer.it_value.tv_sec = 0;
        current_thread = t;
        t->quantums += 1;
        num_quantoms += 1;
        if (setitimer(ITIMER_VIRTUAL, &timer, NULL))
        {
            std::cerr << "system error: timer error\n";
        }
        siglongjmp(t->env, 1);
    }
}


/*
 * Description: This function initializes the thread library.
 * You may assume that this function is called before any other thread library
 * function, and that it is called exactly once. The input to the function is
 * an array of the length of a quantum in micro-seconds for each priority.
 * It is an error to call this function with an array containing non-positive integer.
 * size - is the size of the array.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_init(int *quantum_usecs, int size)
{
    address_t sp, pc;
    if (size <= 0 || check_contains_neg(quantum_usecs, size))
    {
        std::cerr << "thread library error: illegal input\n";
        return -1;
    }
    quantum_usecs_arr = quantum_usecs;
    size_quantum_usecs = size;
    ready_queue = std::deque<thread *>();
    ids = std::unordered_set<int>();
    blocked_set = std::unordered_set<thread *>();
    num_quantoms = 0;
    sa = {0};
    sa.sa_handler = &handle_alarm;
    if (sigaction(SIGVTALRM, &sa, NULL) < 0)
    {
        printf("sigaction error.");
    }
    timer.it_value.tv_sec = 0;        // first time interval, seconds part
    timer.it_value.tv_usec = quantum_usecs[0];
    if (setitimer(ITIMER_VIRTUAL, &timer, NULL))
    {
        std::cerr << "system error: timer error\n";
        return -1;
    }
    thread *first_thread = new thread;
    first_thread->id = 0;
    first_thread->priority = 0;
    first_thread->f = []()
    {
        while (true)
        {
        }
    };
    sp = (address_t) first_thread->stack + STACK_SIZE - sizeof(address_t);
    pc = (address_t) first_thread->f;
    sigsetjmp(first_thread->env, 1);
    ((first_thread->env)->__jmpbuf)[JB_SP] = translate_address(sp);
    (first_thread->env->__jmpbuf)[JB_PC] = translate_address(pc);
    sigemptyset(&(first_thread->env)->__saved_mask);
    sigaddset(&block_mask, SIGVTALRM);
    current_thread = first_thread;
    current_thread->quantums = 1;
    num_quantoms = 1;
    return 0;
}


/*
 * Description: This function creates a new thread, whose entry point is the
 * function f with the signature void f(void). The thread is added to the end
 * of the READY threads list. The uthread_spawn function should fail if it
 * would cause the number of concurrent threads to exceed the limit
 * (MAX_THREAD_NUM). Each thread should be allocated with a stack of size
 * STACK_SIZE bytes.
 * priority - The priority of the new thread.
 * Return value: On success, return the ID of the created thread.
 * On failure, return -1.
*/
int uthread_spawn(void (*f)(void), int priority)
{
    if (sigprocmask(SIG_BLOCK, &block_mask, nullptr) < 0)
    {
        std::cerr << "system error: masking fail\n";
        return -1;
    }
    if (priority >= size_quantum_usecs || priority < 0)
    {
        std::cerr << "thread library error: illegal input\n";
        if (sigprocmask(SIG_UNBLOCK, &block_mask, nullptr) < 0)
        {
            std::cerr << "system error: masking fail\n";
        }
        return -1;
    }
    address_t sp, pc;
    if (ready_queue.size() + blocked_set.size() + 1 < MAX_THREAD_NUM)
    {
        auto *new_thread = new thread;
        if (new_thread == nullptr)
        {
            std::cerr << "system error: memory allocation error\n";
            return -1;
        }
        new_thread->priority = priority;
        new_thread->f = f;
        new_thread->id = get_new_id();
        new_thread->quantums = 0;
        sp = (address_t) new_thread->stack + STACK_SIZE - sizeof(address_t);
        pc = (address_t) f;
        sigsetjmp(new_thread->env, 1);
        ((new_thread->env)->__jmpbuf)[JB_SP] = translate_address(sp);
        (new_thread->env->__jmpbuf)[JB_PC] = translate_address(pc);
        sigemptyset(&(new_thread->env)->__saved_mask);
        ready_queue.push_back(new_thread);
        if (sigprocmask(SIG_UNBLOCK, &block_mask, nullptr) < 0)
        {
            std::cerr << "system error: masking fail\n";
            return -1;
        }
        return new_thread->id;
    }
    std::cerr << "thread library error: illegal input\n";
    if (sigprocmask(SIG_UNBLOCK, &block_mask, nullptr) < 0)
    {
        std::cerr << "system error: masking fail\n";
        return -1;
    }
    return -1;
}


/*
 * Description: This function changes the priority of the thread with ID tid.
 * If this is the current running thread, the effect should take place only the
 * next time the thread gets scheduled.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_change_priority(int tid, int priority)
{
    if (sigprocmask(SIG_BLOCK, &block_mask, nullptr) < 0)
    {
        std::cerr << "system error: masking fail\n";
        return -1;
    }
    thread *t;
    t = find_thread(tid);
    if (t != nullptr)
    {
        t->priority = priority;
        if (sigprocmask(SIG_UNBLOCK, &block_mask, nullptr) < 0)
        {
            std::cerr << "system error: masking fail\n";
            return -1;
        }
        return 0;
    }
    std::cerr << "thread library error: illegal input\n";
    if (sigprocmask(SIG_UNBLOCK, &block_mask, nullptr) < 0)
    {
        std::cerr << "system error: masking fail\n";
    }
    return -1;
}


/*
 * Description: This function terminates the thread with ID tid and deletes
 * it from all relevant control structures. All the resources allocated by
 * the library for this thread should be released. If no thread with ID tid
 * exists it is considered an error. Terminating the main thread
 * (tid == 0) will result in the termination of the entire process using
 * exit(0) [after releasing the assigned library memory].
 * Return value: The function returns 0 if the thread was successfully
 * terminated and -1 otherwise. If a thread terminates itself or the main
 * thread is terminated, the function does not return.
*/
int uthread_terminate(int tid)
{
    if (sigprocmask(SIG_BLOCK, &block_mask, nullptr) < 0)
    {
        std::cerr << "system error: masking fail\n";
        return -1;
    }
    if (tid == 0)
    {
        delete (current_thread);
        del_all();
        if (sigprocmask(SIG_UNBLOCK, &block_mask, nullptr) < 0)
        {
            std::cerr << "system error: masking fail\n";
            return -1;
        }
        exit(0);
    }
    if (tid == current_thread->id)
    {
        handle_alarm(2);
        if (sigprocmask(SIG_UNBLOCK, &block_mask, nullptr) < 0)
        {
            std::cerr << "system error: masking fail\n";
            return -1;
        }
        return 0;
    }
    thread *t = find_thread_in_ready(tid);
    if (t != nullptr)
    {
        ready_queue.erase(std::remove(ready_queue.begin(), ready_queue.end(), t), ready_queue.end());
        ids.erase(t->id);
        delete t;
        if (sigprocmask(SIG_UNBLOCK, &block_mask, nullptr) < 0)
        {
            std::cerr << "system error: masking fail\n";
            return -1;
        }
        return 0;
    }
    t = find_thread_in_blocked(tid);
    if (t != nullptr)
    {
        blocked_set.erase(t);
        ids.erase(t->id);
        delete t;
        if (sigprocmask(SIG_UNBLOCK, &block_mask, nullptr) < 0)
        {
            std::cerr << "system error: masking fail\n";
            return -1;
        }
        return 0;
    }
    std::cerr << "thread library error: invalid id\n";
    if (sigprocmask(SIG_UNBLOCK, &block_mask, nullptr) < 0)
    {
        std::cerr << "system error: masking fail\n";
    }
    return -1;
}

/*
 * Description: This function blocks the thread with ID tid. The thread may
 * be resumed later using uthread_resume. If no thread with ID tid exists it
 * is considered as an error. In addition, it is an error to try blocking the
 * main thread (tid == 0). If a thread blocks itself, a scheduling decision
 * should be made. Blocking a thread in BLOCKED state has no
 * effect and is not considered an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_block(int tid)
{
    if (sigprocmask(SIG_BLOCK, &block_mask, nullptr) < 0)
    {
        std::cerr << "system error: masking fail\n";
        return -1;
    }
    if (tid == 0)
    {
        std::cerr << "thread library error: blocking main thread\n";
        if (sigprocmask(SIG_UNBLOCK, &block_mask, nullptr) < 0)
        {
            std::cerr << "system error: masking fail\n";
        }
        return -1;
    }
    else if (tid == current_thread->id)
    {
        handle_alarm(1);
        if (sigprocmask(SIG_UNBLOCK, &block_mask, nullptr) < 0)
        {
            std::cerr << "system error: masking fail\n";
            return -1;
        }
        return 0;
    }
    thread *t = find_thread_in_blocked(tid);
    if (t != nullptr)
    {
        if (sigprocmask(SIG_UNBLOCK, &block_mask, nullptr) < 0)
        {
            std::cerr << "system error: masking fail\n";
            return -1;
        }
        return 0;
    }
    else
    {
        t = find_thread_in_ready(tid);
        if (t == nullptr)
        {
            std::cerr << "thread library error: invalid id\n";
            if (sigprocmask(SIG_UNBLOCK, &block_mask, nullptr) < 0)
            {
                std::cerr << "system error: masking fail\n";
            }
            return -1;
        }
        ready_queue.erase(std::remove(ready_queue.begin(), ready_queue.end(), t), ready_queue.end());
        blocked_set.insert(t);
        if (sigprocmask(SIG_UNBLOCK, &block_mask, nullptr) < 0)
        {
            std::cerr << "system error: masking fail\n";
            return -1;
        }
        return 0;
    }
}


/*
 * Description: This function resumes a blocked thread with ID tid and moves
 * it to the READY state if it's not synced. Resuming a thread in a RUNNING or READY state
 * has no effect and is not considered as an error. If no thread with
 * ID tid exists it is considered an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_resume(int tid)
{
    if (sigprocmask(SIG_BLOCK, &block_mask, nullptr) < 0)
    {
        std::cerr <<"system error: masking fail\n";
        return -1;
    }
    if (current_thread->id == tid || find_thread_in_ready(tid) != nullptr)
    {
        if (sigprocmask(SIG_UNBLOCK, &block_mask, nullptr) < 0)
        {
            std::cerr << "system error: masking fail\n";
            return -1;
        }
        return 0;
    }
    thread *t = find_thread_in_blocked(tid);
    if (t == nullptr)
    {
        std::cerr << "thread library error: invalid id\n";
        if (sigprocmask(SIG_UNBLOCK, &block_mask, nullptr) < 0)
        {
            std::cerr << "system error: masking fail\n";
        }
        return -1;
    }
    blocked_set.erase(t);
    ready_queue.push_back(t);
    if (sigprocmask(SIG_UNBLOCK, &block_mask, nullptr) < 0)
    {
        std::cerr << "system error: masking fail\n";
        return -1;
    }
    return 0;
}


/*
 * Description: This function returns the thread ID of the calling thread.
 * Return value: The ID of the calling thread.
*/
int uthread_get_tid()
{
    return current_thread->id;
}


/*
 * Description: This function returns the total number of quantums since
 * the library was initialized, including the current quantum.
 * Right after the call to uthread_init, the value should be 1.
 * Each time a new quantum starts, regardless of the reason, this number
 * should be increased by 1.
 * Return value: The total number of quantums.
*/
int uthread_get_total_quantums()
{
    return num_quantoms;
}


/*
 * Description: This function returns the number of quantums the thread with
 * ID tid was in RUNNING state. On the first time a thread runs, the function
 * should return 1. Every additional quantum that the thread starts should
 * increase this value by 1 (so if the thread with ID tid is in RUNNING state
 * when this function is called, include also the current quantum). If no
 * thread with ID tid exists it is considered an error.
 * Return value: On success, return the number of quantums of the thread with ID tid.
 * 			     On failure, return -1.
*/
int uthread_get_quantums(int tid)
{
    thread *t = find_thread(tid);
    if (t == nullptr)
    {
        std::cerr << "thread library error: invalid id\n";
        return -1;
    }
    return t->quantums;
}

/**
 * a function that finds a thread in the blocked set.
 * @param id the id to find in the set.
 * @return a pointer to the thread if in the blocked, nullptr otherwise.
 */
thread *find_thread_in_blocked(int id)
{
    for (thread *t : blocked_set)
    {
        if (t->id == id)
        {
            return t;
        }
    }
    return nullptr;
}

/**
 * a function that finds a thread in the blocked set, or in the ready queue or if it is the current thread.
 * @param id the id to find in the set.
 * @return a pointer to the thread if in the blocked, nullptr otherwise.
 */
thread *find_thread(int id)
{
    if (current_thread->id == id)
    {
        return current_thread;
    }
    thread *t = find_thread_in_ready(id);
    if (t != nullptr)
    {
        return t;
    }
    return find_thread_in_blocked(id);
}

/**
 * a function that finds a thread in the ready queue.
 * @param id the id to find in the set.
 * @return a pointer to the thread if in the blocked, nullptr otherwise.
 */
thread *find_thread_in_ready(int id)
{
    for (thread *t : ready_queue)
    {
        if (t->id == id)
        {
            return t;
        }
    }
    return nullptr;
}

/**
 * a method that gives a new id according to the used ids.
 * @return a new id.
 */
int get_new_id()
{
    int i = 1;
    while (ids.find(i) != ids.end())
    {
        i++;
    }
    ids.insert(i);
    return i;
}


/**
 * a method that checks if a given int array contains a negative number.
 * @param arr an array of ints
 * @param size the size of the given array
 * @return true if it contains negative, false otherwise.
 */
bool check_contains_neg(int const *arr, int size)
{
    for (int i = 0; i < size; ++i)
    {
        if (arr[i] <= 0)
        {
            return true;
        }
    }
    return false;
}

/**
 * a function that deletes all allocated memory.
 */
void del_all()
{
    for (thread *t: ready_queue)
    {
        delete (t);
    }
    for (thread *t: blocked_set)
    {
        delete (t);
    }
}