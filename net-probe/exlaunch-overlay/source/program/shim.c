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

/* std seeds HashMap's hasher from this. xorshift64* from the system tick: not secure, and it
 * doesn't need to be (the hasher only needs different seeds per run). */
long getrandom(void* buf, size_t n, unsigned flags) {
    (void)flags;
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
/* Linux syscalls reached by Rust crates that bypass libc: getrandom (278 on aarch64) is real; the rest
 * (futex waits, only reached under lock contention) fail. */
#include <stdarg.h>
long syscall(long n, ...) {
    if (n == 278) {
        va_list ap;
        va_start(ap, n);
        void* buf = va_arg(ap, void*);
        size_t len = va_arg(ap, size_t);
        unsigned flags = va_arg(ap, unsigned);
        va_end(ap);
        return getrandom(buf, len, flags);
    }
    return -1;
}
__attribute__((noreturn)) void _exit(int c) { (void)c; for (;;) {} }

long accept4(void) { return -1; }
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
long getauxval(void) { return -1; }
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

/* ---- pthread basics, single-threaded -----------------------------------------------------------
 * Rust's std (built with has-thread-local=false, see ggrs-spike/) keeps its thread-locals behind
 * pthread keys. The SDK's pthread_getspecific aborts for keys created this way, and only one
 * thread (the probe's) ever runs Rust code, so a plain table does. Mutexes can't contend either. */
typedef unsigned pthread_key_t_;
static void* g_keys[128];
static void (*g_dtors[128])(void*);
static unsigned char g_keyUsed[128];

int pthread_key_create(pthread_key_t_* key, void (*dtor)(void*)) {
    for (unsigned i = 1; i < 128; i++) {
        if (!g_keyUsed[i]) { g_keyUsed[i] = 1; g_dtors[i] = dtor; g_keys[i] = 0; *key = i; return 0; }
    }
    return 11; /* EAGAIN */
}
int pthread_key_delete(pthread_key_t_ key) { if (key < 128) { g_keyUsed[key] = 0; g_keys[key] = 0; } return 0; }
void* pthread_getspecific(pthread_key_t_ key) { return key < 128 ? g_keys[key] : 0; }
int pthread_setspecific(pthread_key_t_ key, const void* v) { if (key >= 128) return 22; g_keys[key] = (void*)v; return 0; }
int pthread_once(int* once, void (*fn)(void)) { if (!*once) { *once = 1; fn(); } return 0; }
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
