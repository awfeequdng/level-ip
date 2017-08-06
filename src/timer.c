#include "syshead.h"
#include "timer.h"
#include "socket.h"

static LIST_HEAD(timers);
static int tick = 0;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_rwlock_t rwlock = PTHREAD_RWLOCK_INITIALIZER;

static void timer_free(struct timer *t)
{
    int rc = 0;
    if ((rc = pthread_mutex_trylock(&lock)) != 0) {
        if (rc != EBUSY) {
            print_err("Timer free mutex lock: %s\n", strerror(rc));
        }
        return;
    }

    list_del(&t->list);
    free(t);

    pthread_mutex_unlock(&lock);
}

static struct timer *timer_alloc()
{
    struct timer *t = calloc(sizeof(struct timer), 1);

    return t;
}

static void timers_tick()
{
    struct list_head *item, *tmp = NULL;
    struct timer *t = NULL;

    list_for_each_safe(item, tmp, &timers) {
        if (!item) continue;
        
        t = list_entry(item, struct timer, list);

        if (!t->cancelled && t->expires < tick) {
            t->cancelled = 1;
            t->handler(tick, t->arg);
        }

        if (t->cancelled && t->refcnt == 0) {
            timer_free(t);
        }
    }
}

void timer_oneshot(uint32_t expire, void (*handler)(uint32_t, void *), void *arg)
{
    struct timer *t = timer_alloc();

    t->refcnt = 0;
    t->expires = tick + expire;
    t->cancelled = 0;

    if (t->expires < tick) {
        print_err("ERR: Timer expiry integer wrap around\n");
    }
     
    t->handler = handler;
    t->arg = arg;

    pthread_mutex_lock(&lock);
    list_add_tail(&t->list, &timers);
    pthread_mutex_unlock(&lock);
}

struct timer *timer_add(uint32_t expire, void (*handler)(uint32_t, void *), void *arg)
{
    struct timer *t = timer_alloc();

    int tick = timer_get_tick();

    t->refcnt = 1;
    t->expires = tick + expire;
    t->cancelled = 0;

    if (t->expires < tick) {
        print_err("ERR: Timer expiry integer wrap around\n");
    }
     
    t->handler = handler;
    t->arg = arg;

    pthread_mutex_lock(&lock);
    list_add_tail(&t->list, &timers);
    pthread_mutex_unlock(&lock);
    
    return t;
}

void timer_release(struct timer *t)
{
    int rc = 0;
    
    if ((rc = pthread_mutex_lock(&lock)) != 0) {
        print_err("Timer release lock: %s\n", strerror(rc));
        return;
    };

    if (t) {
        t->refcnt--;
    }
    
    pthread_mutex_unlock(&lock);
}

void timer_cancel(struct timer *t)
{
    int rc = 0;
    
    if ((rc = pthread_mutex_lock(&lock)) != 0) {
        print_err("Timer cancel lock: %s\n", strerror(rc));
        return;
    };

    if (t) {
        t->refcnt--;
        t->cancelled = 1;
    }
    
    pthread_mutex_unlock(&lock);
}

void *timers_start()
{
    while (1) {
        if (usleep(1000) != 0) {
            perror("Timer usleep");
        }

        pthread_rwlock_wrlock(&rwlock);
        tick++;
        pthread_rwlock_unlock(&rwlock);
        timers_tick();

        if (tick % 5000 == 0) {
            socket_debug();
        } 
    }
}

int timer_get_tick()
{
    int copy = 0;
    pthread_rwlock_rdlock(&rwlock);
    copy = tick;
    pthread_rwlock_unlock(&rwlock);
    return copy;
}
