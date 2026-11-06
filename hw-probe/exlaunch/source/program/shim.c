/* C library functions that Rust's std references (aarch64 Linux musl target, built with
 * ../../ggrs-spike) but that neither the game's main module nor the SDK module exports. The GGRS
 * path never reaches them (sockets, processes, signals, files); they're here so the module links
 * with nothing left unresolved. A stub that does get called returns -1, which std treats as an
 * error. Generated from the symbol list; the few real ones are written by hand below. */
#include <stddef.h>
#include <stdint.h>

void* __dso_handle = 0;

/* exlaunch's init hands newlib a fake heap; without newlib (see config.mk: -nodefaultlibs, every libc
 * call binds to the game's own at load time) nothing reads these. */
char* fake_heap_start;
char* fake_heap_end;

static inline uint64_t tick(void) { uint64_t t; __asm__ volatile("mrs %0, cntpct_el0" : "=r"(t)); return t; }

/* nn::os::GenerateRandomBytes once net.cpp has found it (WebRTC's DTLS keys and ICE credentials come from here). */
void (*shim_random_bytes)(void*, size_t) = 0;

/* std seeds HashMap's hasher from this. Before shim_random_bytes is set: xorshift64* from the system tick,
 * not secure (fine for the hasher, which only needs different seeds per run). */
long getrandom(void* buf, size_t n, unsigned flags) {
    (void)flags;
    if (n == 0) return 0;   /* the getrandom crate probes with (dangling pointer 0x1, 0): the SDK's generator would still write there */
    if (shim_random_bytes) { shim_random_bytes(buf, n); return (long)n; }
    static uint64_t s = 0;
    if (!s) s = tick() | 1;
    uint8_t* p = (uint8_t*)buf;
    for (size_t i = 0; i < n; i++) {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        p[i] = (uint8_t)((s * 0x2545F4914F6CDD1DULL) >> 56);
    }
    return (long)n;
}

long sysconf(int name) { return name == 30 /* _SC_PAGESIZE */ ? 4096 : 1; }
int dl_iterate_phdr(void* cb, void* data) { (void)cb; (void)data; return 0; }
int __cxa_thread_atexit_impl(void* f, void* a, void* d) { (void)f; (void)a; (void)d; return 0; }
/* Linux syscalls reached by Rust crates that bypass libc: getrandom (278 on aarch64) and futex (98).
 * futex (2026-10-18): Rust's std Mutex / Condvar / Once wait with FUTEX_WAIT(_BITSET) under contention. It used to fail at once, which made
 * every contended lock a spinlock; on Horizon a waiter of higher priority (the module's threads run at 12) spinning on the core of a
 * lower-priority holder (the game thread's ggrs_ffi pump, the Connect worker in netplay_init) never lets the holder run: the console froze
 * at the start of a lobby match's netplay_init (ffisock's socket lock). Now a wait sleeps 50 us while the word still holds the expected
 * value and returns (a spurious wake-up, which every futex caller allows); a wake is a no-op (the sleeping waiter re-checks). */
extern void svcSleepThread(long nano);
#include <stdarg.h>
long syscall(long n, ...) {
    va_list ap;
    va_start(ap, n);
    if (n == 278) {
        void* buf = va_arg(ap, void*);
        size_t len = va_arg(ap, size_t);
        unsigned flags = va_arg(ap, unsigned);
        va_end(ap);
        return getrandom(buf, len, flags);
    }
    if (n == 98) {   /* futex(uaddr, op, val, timeout, ...) */
        volatile uint32_t* uaddr = va_arg(ap, volatile uint32_t*);
        int op = va_arg(ap, int) & 0x7f;   /* (FUTEX_PRIVATE_FLAG off) */
        uint32_t val = va_arg(ap, uint32_t);
        va_end(ap);
        if (op == 0 || op == 9) {          /* FUTEX_WAIT, FUTEX_WAIT_BITSET */
            if (*uaddr == val) svcSleepThread(50000);
            return 0;
        }
        return 0;                          /* FUTEX_WAKE(_BITSET), others: nothing to do (waiters poll) */
    }
    va_end(ap);
    return -1;
}
__attribute__((noreturn)) void _exit(int c) { (void)c; for (;;) {} }

long accept4(void) { return -1; }
/* (Newer Rust std, for tfx-rtc.) */
long dup(void) { return -1; }
long fchmodat(void) { return -1; }
long fstatat(void) { return -1; }
long gethostname(void) { return -1; }
long gettid(void) { return -1; }
long killpg(void) { return -1; }
long mkdirat(void) { return -1; }
long mkfifo(void) { return -1; }
long renameat(void) { return -1; }
long setsid(void) { return -1; }
long utimensat(void) { return -1; }
long bind(void) { return -1; }
long chmod(void) { return -1; }
long chown(void) { return -1; }
long chroot(void) { return -1; }
long connect(void) { return -1; }
long copy_file_range(void) { return -1; }
long dlsym(void) { return -1; }
long dup2(void) { return -1; }
long execvp(void) { return -1; }
long fchmod(void) { return -1; }
long fchown(void) { return -1; }
long fcntl(void) { return -1; }
long fdatasync(void) { return -1; }
long flock(void) { return -1; }
long fork(void) { return -1; }
long freeaddrinfo(void) { return -1; }
long fsync(void) { return -1; }
long futimens(void) { return -1; }
long gai_strerror(void) { return -1; }
long getaddrinfo(void) { return -1; }
/* AT_HWCAP (16): FP | ASIMD only, so Rust's feature detection (std, cpufeatures in the crypto crates) never picks instructions the
 * Switch's Cortex-A57 may lack (the stub's -1 claimed every feature: SHA-3, SHA-512, SVE...). Anything else: 0. */
unsigned long getauxval(unsigned long type) { return type == 16 ? 0x3 : 0; }
long getpeername(void) { return -1; }
long getppid(void) { return -1; }
long getpwuid_r(void) { return -1; }
long getsockname(void) { return -1; }
long getsockopt(void) { return -1; }
long getuid(void) { return -1; }
long ioctl(void) { return -1; }
long kill(void) { return -1; }
long lchown(void) { return -1; }
long linkat(void) { return -1; }
long listen(void) { return -1; }
long lstat(void) { return -1; }
long mmap(void) { return -1; }
long mprotect(void) { return -1; }
long munmap(void) { return -1; }
long openat(void) { return -1; }
long pause(void) { return -1; }
long pidfd_getpid(void) { return -1; }
long pidfd_spawnp(void) { return -1; }
long pipe2(void) { return -1; }
long poll(void) { return -1; }
long posix_spawn_file_actions_addchdir_np(void) { return -1; }
long posix_spawn_file_actions_adddup2(void) { return -1; }
long posix_spawn_file_actions_destroy(void) { return -1; }
long posix_spawn_file_actions_init(void) { return -1; }
long posix_spawnattr_destroy(void) { return -1; }
long posix_spawnattr_init(void) { return -1; }
long posix_spawnattr_setflags(void) { return -1; }
long posix_spawnattr_setpgroup(void) { return -1; }
long posix_spawnattr_setsigdefault(void) { return -1; }
long posix_spawnp(void) { return -1; }
long preadv(void) { return -1; }
int pthread_atfork(void* a, void* b, void* c) { (void)a; (void)b; (void)c; return 0; } /* rand's reseeding hook; nothing forks */
long pthread_attr_getguardsize(void) { return -1; }
long pthread_getattr_np(void) { return -1; }
long pwritev(void) { return -1; }
long readlink(void) { return -1; }
long readv(void) { return -1; }
long realpath(void) { return -1; }
long recv(void) { return -1; }
long recvfrom(void) { return -1; }
long recvmsg(void) { return -1; }
long send(void) { return -1; }
long sendfile(void) { return -1; }
long sendmsg(void) { return -1; }
long sendto(void) { return -1; }
long setgid(void) { return -1; }
long setgroups(void) { return -1; }
long setpgid(void) { return -1; }
long setsockopt(void) { return -1; }
long setuid(void) { return -1; }
long shutdown(void) { return -1; }
long sigaction(void) { return -1; }
long sigaddset(void) { return -1; }
long sigaltstack(void) { return -1; }
long sigemptyset(void) { return -1; }
long signal(void) { return -1; }
long socket(void) { return -1; }
long socketpair(void) { return -1; }
long splice(void) { return -1; }
long symlink(void) { return -1; }
long unlinkat(void) { return -1; }
long waitid(void) { return -1; }
long waitpid(void) { return -1; }

/* ---- pthread basics -----------------------------------------------------------------------------
 * Rust's std (built with has-thread-local=false, see ggrs-spike/) keeps its thread-locals behind pthread keys. The SDK's
 * pthread_getspecific aborts for keys created this way, so they live here: one value table PER THREAD (keyed by Horizon's
 * thread-local register tpidrro_el0). Two threads run Rust now (the game thread: GGRS; the network thread: WebRTC), and a
 * shared table made them use each other's std thread state, hash keys and RNG: the console crashed starting a lobby match. */
typedef unsigned pthread_key_t_;
#define KEYS 128
#define THREADS 8
static void (*g_dtors[KEYS])(void*);
static unsigned char g_keyUsed[KEYS];
static struct { volatile uintptr_t owner; void* v[KEYS]; } g_tl[THREADS];

static void** ThreadValues(void) {
    uintptr_t self;
    __asm__ volatile("mrs %0, tpidrro_el0" : "=r"(self));
    for (int i = 0; i < THREADS; i++) if (g_tl[i].owner == self) return g_tl[i].v;
    for (int i = 0; i < THREADS; i++) {
        uintptr_t none = 0;
        if (__atomic_compare_exchange_n(&g_tl[i].owner, &none, self, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return g_tl[i].v;
    }
    return 0;   /* more Rust threads than THREADS: no thread-locals for this one */
}
static volatile int g_keyLock = 0;
int pthread_key_create(pthread_key_t_* key, void (*dtor)(void*)) {
    while (__atomic_exchange_n(&g_keyLock, 1, __ATOMIC_ACQUIRE)) {}
    int r = 11; /* EAGAIN */
    for (unsigned i = 1; i < KEYS; i++) {
        if (!g_keyUsed[i]) {
            g_keyUsed[i] = 1; g_dtors[i] = dtor;
            for (int t = 0; t < THREADS; t++) g_tl[t].v[i] = 0;
            *key = i; r = 0; break;
        }
    }
    __atomic_store_n(&g_keyLock, 0, __ATOMIC_RELEASE);
    return r;
}
int pthread_key_delete(pthread_key_t_ key) { if (key < KEYS) g_keyUsed[key] = 0; return 0; }
void* pthread_getspecific(pthread_key_t_ key) { void** v = ThreadValues(); return key < KEYS && v ? v[key] : 0; }
int pthread_setspecific(pthread_key_t_ key, const void* val) { void** v = ThreadValues(); if (key >= KEYS || !v) return 22; v[key] = (void*)val; return 0; }
int pthread_once(int* once, void (*fn)(void)) {
    int st = 0;
    if (__atomic_compare_exchange_n(once, &st, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) { fn(); __atomic_store_n(once, 2, __ATOMIC_RELEASE); }
    else while (__atomic_load_n(once, __ATOMIC_ACQUIRE) != 2) {}
    return 0;
}
int pthread_mutex_lock(void* m) { (void)m; return 0; }
int pthread_mutex_unlock(void* m) { (void)m; return 0; }

/* Rust's stderr (panic messages) goes to the probe's log. */
extern void net_raw_log(const char* p, size_t n);
struct iovec_ { void* base; size_t len; };
long write(int fd, const void* buf, size_t n) {
    if (fd == 1 || fd == 2) { net_raw_log((const char*)buf, n); return (long)n; }
    return -1;
}
long writev(int fd, const struct iovec_* iov, int cnt) {
    long total = 0;
    for (int i = 0; i < cnt; i++) {
        long r = write(fd, iov[i].base, iov[i].len);
        if (r < 0) return total ? total : -1;
        total += r;
    }
    return total;
}

/* ---- time ----------------------------------------------------------------------------------------
 * The SDK's clock_gettime rejects the Linux clock ids Rust passes. Every clock here is the system tick
 * (19.2 MHz) since boot: GGRS only needs differences. */
struct ts_ { long sec; long nsec; };
extern void svcSleepThread(long nano);

int clock_gettime(int id, struct ts_* t) {
    (void)id;
    uint64_t k = tick();
    t->sec = (long)(k / 19200000u);
    t->nsec = (long)((k % 19200000u) * 625u / 12u);
    return 0;
}
int nanosleep(const struct ts_* req, struct ts_* rem) {
    (void)rem;
    svcSleepThread(req->sec * 1000000000L + req->nsec);
    return 0;
}
int sched_yield(void) { svcSleepThread(0); return 0; }
/* Newer Rust std sleeps with this (relative only: flags 0). */
int clock_nanosleep(int id, int flags, const struct ts_* req, struct ts_* rem) {
    (void)id; (void)flags; (void)rem;
    svcSleepThread(req->sec * 1000000000L + req->nsec);
    return 0;
}
