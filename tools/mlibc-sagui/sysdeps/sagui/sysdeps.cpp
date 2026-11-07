#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/statvfs.h>
#include <sys/uio.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <mlibc/tcb.hpp>
#include <mlibc/all-sysdeps.hpp>
#include <sagui-abi.h>

extern "C" long __sagui_syscall0(long);
extern "C" long __sagui_syscall1(long, long);
extern "C" long __sagui_syscall2(long, long, long);
extern "C" long __sagui_syscall3(long, long, long, long);

namespace mlibc {

namespace {

constexpr size_t kPageSize = 4096;
constexpr int kFdMax = 16;
constexpr int kDirBase = 1000;
constexpr int kDirMax = 16;
constexpr int kPathMax = 256;

unsigned char g_fd_used[kFdMax] = {};
int g_fd_flags[kFdMax] = {};
char g_fd_path[kFdMax][kPathMax] = {};

char g_dir_path[kDirMax][kPathMax] = {};
int g_dir_index[kDirMax] = {};
unsigned char g_dir_used[kDirMax] = {};

mode_t g_umask = 022;

struct sg_winsize {
	unsigned short ws_row;
	unsigned short ws_col;
	unsigned short ws_xpixel;
	unsigned short ws_ypixel;
};

#define SG_TIOCGWINSZ 0x5413UL

int sg_errno(long r) {
	switch (-r) {
	case 1: return EINVAL;
	case 2: return ENOENT;
	case 3: return EEXIST;
	case 4: return ENOMEM;
	case 5: return EFAULT;
	case 6: return ENOTDIR;
	case 7: return EISDIR;
	case 8: return ENOTEMPTY;
	case 9: return EMFILE;
	case 10: return EBADF;
	case 11: return EPIPE;
	case 12: return ESRCH;
	case 13: return ENOEXEC;
	case 14: return EACCES;
	case 15: return EBUSY;
	default: return EIO;
	}
}

int sg_errno_io(long r) {
	if (r == ERRO_ACESSO)
		return EBADF;
	return sg_errno(r);
}

size_t sg_copy_path(char *dst, const char *src) {
	size_t n = 0;
	while (src[n] && n < (size_t)kPathMax - 1) n++;
	memcpy(dst, src, n);
	dst[n] = 0;
	return n;
}

void fd_set(int fd, int flags, const char *path) {
	if (fd < 0 || fd >= kFdMax)
		return;
	g_fd_used[fd] = 1;
	g_fd_flags[fd] = flags;
	if (path)
		sg_copy_path(g_fd_path[fd], path);
}

void fd_clear(int fd) {
	if (fd < 0 || fd >= kFdMax)
		return;
	g_fd_used[fd] = 0;
	g_fd_flags[fd] = 0;
	g_fd_path[fd][0] = 0;
}

int dir_slot(int fd) {
	int i = fd - kDirBase;
	return (i >= 0 && i < kDirMax && g_dir_used[i]) ? i : -1;
}

int dir_alloc(const char *path) {
	for (int i = 0; i < kDirMax; i++) {
		if (!g_dir_used[i]) {
			g_dir_used[i] = 1;
			g_dir_index[i] = 0;
			sg_copy_path(g_dir_path[i], path);
			return kDirBase + i;
		}
	}
	return -1;
}

int sg_info(const char *path, sg_info_arquivo_t *out) {
	long r = __sagui_syscall2(SIS_INFO_ARQUIVO, (long)path, (long)out);
	return sg_errno(r);
}

uintptr_t hash_path(const char *path) {
	uintptr_t h = 5381;
	while (*path) {
		h = h * 33 + (unsigned char)*path;
		path++;
	}
	return h;
}

void fill_stat_from_info(const sg_info_arquivo_t *info, const char *path, struct stat *st) {
	memset(st, 0, sizeof(*st));
	st->st_dev = 1;
	st->st_ino = (ino_t)(hash_path(path) & 0x00ffffff);
	st->st_nlink = (info->tipo == TIPO_PASTA) ? 2 : 1;
	st->st_uid = 0;
	st->st_gid = 0;
	if (info->tipo == TIPO_PASTA)
		st->st_mode = S_IFDIR | 0755;
	else if (info->tipo == TIPO_PROGRAMA)
		st->st_mode = S_IFREG | 0755;
	else if (info->tipo == TIPO_TERMINAL)
		st->st_mode = S_IFCHR | 0620;
	else
		st->st_mode = S_IFREG | 0644;
	st->st_size = (off64_t)info->tamanho;
	st->st_blksize = 512;
	st->st_blocks = 0;
}

int fill_stat_path(const char *path, struct stat *st) {
	sg_info_arquivo_t info;
	int e = sg_info(path, &info);
	if (e)
		return e;
	fill_stat_from_info(&info, path, st);
	return 0;
}

int fill_stat_fd(int fd, struct stat *st) {
	if (fd >= kDirBase) {
		int slot = dir_slot(fd);
		if (slot < 0)
			return EBADF;
		sg_info_arquivo_t info = { TIPO_PASTA, 0 };
		fill_stat_from_info(&info, g_dir_path[slot], st);
		return 0;
	}
	if (fd < 0 || fd >= kFdMax)
		return EBADF;
	if (fd <= 2) {
		memset(st, 0, sizeof(*st));
		st->st_dev = 1;
		st->st_ino = (ino_t)(0x100 + fd);
		st->st_nlink = 1;
		st->st_mode = S_IFCHR | 0620;
		st->st_rdev = 0;
		return 0;
	}
	if (!g_fd_used[fd])
		return EBADF;
	if (!g_fd_path[fd][0]) {
		memset(st, 0, sizeof(*st));
		st->st_dev = 1;
		st->st_ino = (ino_t)(0x200 + fd);
		st->st_nlink = 1;
		st->st_mode = S_IFREG | 0644;
		return 0;
	}
	return fill_stat_path(g_fd_path[fd], st);
}

int sg_seek(int fd, off_t offset, int whence, off_t *result) {
	long r = __sagui_syscall3(SIS_POSICIONAR, fd, (long)offset, whence);
	if (r < 0) {
		if (r == ERRO_ACESSO)
			return ESPIPE;
		if (r == ERRO_INVALIDO)
			return EINVAL;
		return sg_errno(r);
	}
	*result = (off_t)r;
	return 0;
}

int sg_read(int fd, void *buf, size_t count, ssize_t *out) {
	long r = __sagui_syscall3(SIS_LER, fd, (long)buf, (long)count);
	if (r == ERRO_PIPE_QUEBRADO) {
		*out = 0;
		return 0;
	}
	if (r < 0)
		return sg_errno_io(r);
	*out = (ssize_t)r;
	return 0;
}

int sg_write(int fd, const void *buf, size_t count, ssize_t *out) {
	long r = __sagui_syscall3(SIS_ESCREVER, fd, (long)buf, (long)count);
	if (r < 0)
		return sg_errno_io(r);
	*out = (ssize_t)r;
	return 0;
}

int sg_write_all(int fd, const char *buf, size_t n) {
	while (n) {
		ssize_t w;
		int e = sg_write(fd, buf, n, &w);
		if (e)
			return e;
		if (w == 0)
			break;
		buf += (size_t)w;
		n -= (size_t)w;
	}
	return 0;
}

long sg_open_mode(const char *path, int flags) {
	int acc = flags & 3;
	int sm;
	if (acc == O_WRONLY)
		sm = ABRIR_ESCREVER;
	else if (acc == O_RDWR)
		sm = ABRIR_LER | ABRIR_ESCREVER;
	else
		sm = ABRIR_LER;
	if (flags & O_CREAT)
		sm |= ABRIR_CRIAR;
	if (flags & O_TRUNC)
		sm |= ABRIR_TRUNCAR;
	if (flags & O_APPEND)
		sm |= ABRIR_ACRESCENTAR;
	if (flags & O_DIRECTORY) {
		sg_info_arquivo_t info;
		if (int e = sg_info(path, &info))
			return -e;
		if (info.tipo != TIPO_PASTA)
			return -ENOTDIR;
		return -2;
	}
	return __sagui_syscall2(SIS_ABRIR, (long)path, sm);
}

int sg_dup2_raw(int fd, int target, int *result) {
	long r = __sagui_syscall2(SIS_DUPLICAR, fd, target);
	if (r < 0)
		return sg_errno_io(r);
	if (target >= 0 && target < kFdMax) {
		fd_set(target, g_fd_flags[fd], g_fd_path[fd][0] ? g_fd_path[fd] : nullptr);
	}
	if (result)
		*result = target;
	return 0;
}

int sg_find_free_fd(int min) {
	if (min < 0)
		min = 0;
	for (int i = min; i < kFdMax; i++) {
		if (!g_fd_used[i])
			return i;
	}
	return -1;
}

} // namespace

// Memória: SIS_AUMENTAR_MEM(0) devolve o fim atual; pedir o restante alinha
// o próximo alargamento. O kernel não tem munmap; liberar é no-op.

int Sysdeps<AnonAllocate>::operator()(size_t size, void **pointer) {
	if (!size) {
		*pointer = nullptr;
		return 0;
	}
	long cur = __sagui_syscall1(SIS_AUMENTAR_MEM, 0);
	if (cur < 0)
		return sg_errno(cur);
	size_t pad = (kPageSize - ((uintptr_t)cur & (kPageSize - 1))) & (kPageSize - 1);
	size_t rounded = (size + kPageSize - 1) & ~(kPageSize - 1);
	long r = __sagui_syscall1(SIS_AUMENTAR_MEM, (long)(pad + rounded));
	if (r < 0)
		return sg_errno(r);
	*pointer = (void *)((uintptr_t)r + pad);
	return 0;
}

int Sysdeps<AnonFree>::operator()(void *, size_t) {
	return 0;
}

int Sysdeps<VmMap>::operator()(void *hint, size_t size, int, int, int fd, off_t, void **window) {
	if (fd != -1 || hint != nullptr)
		return ENOSYS;
	return sysdep<AnonAllocate>(size, window);
}

int Sysdeps<VmUnmap>::operator()(void *, size_t) {
	return 0;
}

int Sysdeps<VmProtect>::operator()(void *, size_t, int) {
	return 0;
}

int Sysdeps<TcbSet>::operator()(void *pointer) {
	long r = __sagui_syscall1(SIS_TLS, (long)pointer);
	return r < 0 ? sg_errno(r) : 0;
}

int Sysdeps<FutexWait>::operator()(int *, int, const struct timespec *) {
	return 0;
}

int Sysdeps<FutexWake>::operator()(int *, bool) {
	return 0;
}

pid_t Sysdeps<FutexTid>::operator()() {
	return (pid_t)__sagui_syscall0(SIS_PID);
}

pid_t Sysdeps<GetPid>::operator()() {
	return (pid_t)__sagui_syscall0(SIS_PID);
}

pid_t Sysdeps<GetTid>::operator()() {
	return (pid_t)__sagui_syscall0(SIS_PID);
}

pid_t Sysdeps<GetPpid>::operator()() {
	sg_info_processo_t procs[64];
	long n = __sagui_syscall3(SIS_INFO_SISTEMA, INFO_PROCESSOS, (long)procs, 64);
	if (n <= 0)
		return 0;
	pid_t me = (pid_t)__sagui_syscall0(SIS_PID);
	for (long i = 0; i < n; i++) {
		if (procs[i].pid == me)
			return procs[i].pai;
	}
	return 0;
}

int Sysdeps<Kill>::operator()(pid_t pid, int) {
	long r = __sagui_syscall1(SIS_MATAR, pid);
	return r < 0 ? sg_errno(r) : 0;
}

void Sysdeps<Exit>::operator()(int status) {
	__sagui_syscall1(SIS_SAIR, status);
	__builtin_unreachable();
}

void Sysdeps<ThreadExit>::operator()() {
	__sagui_syscall1(SIS_SAIR, 0);
	__builtin_unreachable();
}

void Sysdeps<LibcLog>::operator()(const char *message) {
	if (message)
		__sagui_syscall1(SIS_REGISTRAR, (long)message);
}

void Sysdeps<LibcPanic>::operator()() {
	sysdep<LibcLog>("!!! mlibc panic !!!");
	sysdep<Exit>(-1);
	__builtin_unreachable();
}

int Sysdeps<Isatty>::operator()(int fd) {
	return (fd >= 0 && fd <= 2) ? 0 : ENOTTY;
}

int Sysdeps<Open>::operator()(const char *pathname, int flags, mode_t, int *fd) {
	if (!pathname || !fd)
		return EFAULT;
	if (flags & O_DIRECTORY) {
		sg_info_arquivo_t info;
		if (int e = sg_info(pathname, &info))
			return e;
		if (info.tipo != TIPO_PASTA)
			return ENOTDIR;
		int h = dir_alloc(pathname);
		if (h < 0)
			return EMFILE;
		*fd = h;
		return 0;
	}
	long r = sg_open_mode(pathname, flags);
	if (r < 0)
		return sg_errno(r);
	fd_set((int)r, flags, pathname);
	*fd = (int)r;
	return 0;
}

int Sysdeps<Openat>::operator()(int dirfd, const char *path, int flags, mode_t mode, int *fd) {
	if (dirfd != AT_FDCWD)
		return ENOSYS;
	return sysdep<Open>(path, flags, mode, fd);
}

int Sysdeps<Read>::operator()(int fd, void *buf, size_t count, ssize_t *bytes_read) {
	if (dir_slot(fd) >= 0)
		return EISDIR;
	return sg_read(fd, buf, count, bytes_read);
}

int Sysdeps<Write>::operator()(int fd, const void *buf, size_t count, ssize_t *bytes_written) {
	if (dir_slot(fd) >= 0)
		return EISDIR;
	return sg_write(fd, buf, count, bytes_written);
}

int Sysdeps<Pread>::operator()(int fd, void *buf, size_t n, off_t off, ssize_t *bytes_read) {
	off_t old_pos, tmp;
	int e = sg_seek(fd, 0, SEEK_CUR, &old_pos);
	if (e)
		return e;
	if (e = sg_seek(fd, off, SEEK_SET, &tmp))
		return e;
	e = sg_read(fd, buf, n, bytes_read);
	off_t back;
	sg_seek(fd, old_pos, SEEK_SET, &back);
	return e;
}

int Sysdeps<Pwrite>::operator()(int fd, const void *buf, size_t n, off_t off, ssize_t *bytes_read) {
	off_t old_pos, tmp;
	int e = sg_seek(fd, 0, SEEK_CUR, &old_pos);
	if (e)
		return e;
	if (e = sg_seek(fd, off, SEEK_SET, &tmp))
		return e;
	e = sg_write(fd, buf, n, bytes_read);
	off_t back;
	sg_seek(fd, old_pos, SEEK_SET, &back);
	return e;
}

int Sysdeps<Readv>::operator()(int fd, const struct iovec *iovs, int iovc, ssize_t *bytes_read) {
	ssize_t total = 0;
	for (int i = 0; i < iovc; i++) {
		ssize_t n;
		int e = sg_read(fd, iovs[i].iov_base, iovs[i].iov_len, &n);
		if (e) {
			if (total)
				break;
			return e;
		}
		total += n;
		if (n == 0)
			break;
	}
	*bytes_read = total;
	return 0;
}

int Sysdeps<Writev>::operator()(int fd, const struct iovec *iovs, int iovc, ssize_t *bytes_written) {
	ssize_t total = 0;
	for (int i = 0; i < iovc; i++) {
		ssize_t n;
		int e = sg_write(fd, iovs[i].iov_base, iovs[i].iov_len, &n);
		if (e) {
			if (total)
				break;
			return e;
		}
		total += n;
		if (n == 0)
			break;
	}
	*bytes_written = total;
	return 0;
}

int Sysdeps<Seek>::operator()(int fd, off_t offset, int whence, off_t *new_offset) {
	if (dir_slot(fd) >= 0)
		return ESPIPE;
	return sg_seek(fd, offset, whence, new_offset);
}

int Sysdeps<Close>::operator()(int fd) {
	int slot = dir_slot(fd);
	if (slot >= 0) {
		g_dir_used[slot] = 0;
		return 0;
	}
	long r = __sagui_syscall1(SIS_FECHAR, fd);
	if (r < 0)
		return sg_errno(r);
	fd_clear(fd);
	return 0;
}

int Sysdeps<Stat>::operator()(fsfd_target fsfdt, int fd, const char *path, int, struct stat *statbuf) {
	if (!statbuf)
		return EFAULT;
	if (fsfdt == fsfd_target::path) {
		if (!path)
			return EFAULT;
		return fill_stat_path(path, statbuf);
	}
	if (fsfdt == fsfd_target::fd || fsfdt == fsfd_target::fd_path) {
		if (fd >= 0) {
			if (dir_slot(fd) >= 0 || (fd < kFdMax && g_fd_used[fd]) || fd <= 2)
				return fill_stat_fd(fd, statbuf);
			if (fsfdt == fsfd_target::fd)
				return EBADF;
		}
		if (path)
			return fill_stat_path(path, statbuf);
		return EBADF;
	}
	return EINVAL;
}

int Sysdeps<ClockGet>::operator()(int clock, time_t *secs, long *nanos) {
	if (clock == CLOCK_MONOTONIC
#ifdef CLOCK_PROCESS_CPUTIME_ID
			|| clock == CLOCK_PROCESS_CPUTIME_ID || clock == CLOCK_THREAD_CPUTIME_ID
#endif
	) {
		unsigned ms;
		if (clock == CLOCK_MONOTONIC) {
			long r = __sagui_syscall0(SIS_TEMPO);
			if (r < 0)
				return sg_errno(r);
			ms = (unsigned)r;
		} else {
			sg_info_processo_t procs[64];
			long n = __sagui_syscall3(SIS_INFO_SISTEMA, INFO_PROCESSOS, (long)procs, 64);
			if (n <= 0)
				return ESRCH;
			pid_t me = (pid_t)__sagui_syscall0(SIS_PID);
			ms = 0;
			for (long i = 0; i < n; i++) {
				if (procs[i].pid == me) {
					ms = procs[i].cpu_ms;
					break;
				}
			}
		}
		*secs = (time_t)(ms / 1000u);
		*nanos = (long)(ms % 1000u) * 1000000L;
		return 0;
	}
	if (clock != CLOCK_REALTIME)
		return EINVAL;
	sg_data_t d;
	long r = __sagui_syscall1(SIS_DATA, (long)&d);
	if (r < 0)
		return sg_errno(r);
	int ano = d.ano;
	if (ano < 1970 || ano > 2038 || d.dia < 1 || d.mes < 1 || d.mes > 12 || d.hora > 23 || d.minuto > 59 || d.segundo > 59)
		return EOVERFLOW;
	static const unsigned char days_in_month[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	unsigned dias = 0;
	for (int y = 1970; y < ano; y++) {
		int leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
		dias += leap ? 366u : 365u;
	}
	for (int m = 1; m < d.mes; m++) {
		int leap = (ano % 4 == 0 && (ano % 100 != 0 || ano % 400 == 0));
		dias += (unsigned)days_in_month[m - 1] + (m == 2 && leap ? 1u : 0u);
	}
	dias += (unsigned)d.dia - 1u;
	unsigned long seconds = (unsigned long)dias * 86400UL + (unsigned long)d.hora * 3600UL + (unsigned long)d.minuto * 60UL + d.segundo;
	*secs = (time_t)seconds;
	*nanos = 0;
	return 0;
}

int Sysdeps<ClockGetres>::operator()(int clock, time_t *secs, long *nanos) {
	if (clock != CLOCK_REALTIME && clock != CLOCK_MONOTONIC)
		return EINVAL;
	*secs = 0;
	*nanos = 1000000;
	return 0;
}

int Sysdeps<Sleep>::operator()(time_t *secs, long *nanos) {
	if (*secs < 0 || *nanos < 0)
		return EINVAL;
	long long ms = (long long)(*secs) * 1000LL + (*nanos + 999999L) / 1000000LL;
	while (ms > 0) {
		long chunk = ms > 40000000LL ? 40000000L : (long)ms;
		long r = __sagui_syscall1(SIS_DORMIR, chunk);
		if (r < 0)
			return sg_errno(r);
		ms -= chunk;
	}
	*secs = 0;
	*nanos = 0;
	return 0;
}

int Sysdeps<GetEntropy>::operator()(void *buffer, size_t length) {
	static uint32_t state = 0x9e3779b9u;
	long t = __sagui_syscall0(SIS_TEMPO);
	long p = __sagui_syscall0(SIS_PID);
	state ^= (uint32_t)t + 0x9e3779b9u + (state << 6) + (state >> 2) + (uint32_t)p;
	auto out = (uint8_t *)buffer;
	for (size_t i = 0; i < length; i++) {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		out[i] = (uint8_t)(state & 0xff);
	}
	return 0;
}

int Sysdeps<Access>::operator()(const char *path, int) {
	sg_info_arquivo_t info;
	if (!path)
		return EFAULT;
	return sg_info(path, &info);
}

int Sysdeps<Faccessat>::operator()(int dirfd, const char *pathname, int mode, int) {
	if (dirfd != AT_FDCWD)
		return ENOSYS;
	return sysdep<Access>(pathname, mode);
}

int Sysdeps<Rmdir>::operator()(const char *path) {
	long r = __sagui_syscall1(SIS_REMOVER_PASTA, (long)path);
	return r < 0 ? sg_errno(r) : 0;
}

int Sysdeps<Unlinkat>::operator()(int, const char *path, int flags) {
	long r;
	if (flags & AT_REMOVEDIR)
		r = __sagui_syscall1(SIS_REMOVER_PASTA, (long)path);
	else
		r = __sagui_syscall1(SIS_APAGAR, (long)path);
	return r < 0 ? sg_errno(r) : 0;
}

int Sysdeps<Rename>::operator()(const char *path, const char *new_path) {
	long r = __sagui_syscall2(SIS_RENOMEAR, (long)path, (long)new_path);
	return r < 0 ? sg_errno(r) : 0;
}

int Sysdeps<Renameat>::operator()(int olddirfd, const char *old_path, int newdirfd, const char *new_path) {
	if (olddirfd != AT_FDCWD || newdirfd != AT_FDCWD)
		return ENOSYS;
	return sysdep<Rename>(old_path, new_path);
}

int Sysdeps<Mkdir>::operator()(const char *path, mode_t) {
	long r = __sagui_syscall1(SIS_CRIAR_PASTA, (long)path);
	return r < 0 ? sg_errno(r) : 0;
}

int Sysdeps<Mkdirat>::operator()(int dirfd, const char *path, mode_t mode) {
	if (dirfd != AT_FDCWD)
		return ENOSYS;
	return sysdep<Mkdir>(path, mode);
}

int Sysdeps<Chdir>::operator()(const char *path) {
	long r = __sagui_syscall1(SIS_MUDAR_PASTA, (long)path);
	return r < 0 ? sg_errno(r) : 0;
}

int Sysdeps<GetCwd>::operator()(char *buffer, size_t size) {
	long r = __sagui_syscall2(SIS_PASTA_ATUAL, (long)buffer, (long)size);
	return r < 0 ? sg_errno(r) : 0;
}

int Sysdeps<OpenDir>::operator()(const char *path, int *handle) {
	if (!path || !handle)
		return EFAULT;
	sg_info_arquivo_t info;
	if (int e = sg_info(path, &info))
		return e;
	if (info.tipo != TIPO_PASTA)
		return ENOTDIR;
	int h = dir_alloc(path);
	if (h < 0)
		return EMFILE;
	*handle = h;
	return 0;
}

int Sysdeps<ReadEntries>::operator()(int handle, void *buffer, size_t max_size, size_t *bytes_read) {
	int slot = dir_slot(handle);
	if (slot < 0)
		return EBADF;
	size_t used = 0;
	while (used + sizeof(struct dirent) <= max_size && max_size >= sizeof(struct dirent)) {
		sg_entrada_pasta_t ent;
		long r = __sagui_syscall3(SIS_LISTAR, (long)g_dir_path[slot], g_dir_index[slot], (long)&ent);
		if (r < 0)
			return sg_errno(r);
		if (r == 0)
			break;
		g_dir_index[slot]++;
		size_t namelen = 0;
		while (namelen < NOME_MAX && ent.nome[namelen])
			namelen++;
		struct dirent *de = (struct dirent *)((char *)buffer + used);
		memset(de, 0, sizeof(*de));
		de->d_ino = (ino_t)(g_dir_index[slot]);
		de->d_off = (off_t)(g_dir_index[slot]);
		de->d_type = (ent.tipo == TIPO_PASTA) ? DT_DIR : DT_REG;
		de->d_reclen = (reclen_t)(offsetof(struct dirent, d_name) + namelen + 1);
		memcpy(de->d_name, ent.nome, namelen);
		de->d_name[namelen] = 0;
		used += de->d_reclen;
		if (used > max_size)
			break;
	}
	*bytes_read = used;
	return 0;
}

int Sysdeps<Dup>::operator()(int fd, int, int *newfd) {
	if (dir_slot(fd) >= 0)
		return EBADF;
	int target = sg_find_free_fd(0);
	if (target < 0)
		return EMFILE;
	int e = sg_dup2_raw(fd, target, newfd);
	return e;
}

int Sysdeps<Dup2>::operator()(int fd, int, int newfd) {
	if (fd == newfd)
		return dir_slot(fd) >= 0 ? EINVAL : 0;
	if (dir_slot(fd) >= 0)
		return EBADF;
	return sg_dup2_raw(fd, newfd, nullptr);
}

int Sysdeps<Fcntl>::operator()(int fd, int request, va_list args, int *result) {
	switch (request) {
	case F_GETFD:
		*result = 0;
		return 0;
	case F_SETFD:
		return 0;
	case F_GETFL:
		*result = (fd >= 0 && fd < kFdMax && g_fd_used[fd]) ? g_fd_flags[fd] : O_RDWR;
		return 0;
	case F_SETFL:
		return 0;
	case F_DUPFD:
#ifdef F_DUPFD_CLOEXEC
	case F_DUPFD_CLOEXEC:
#endif
	{
		int min = va_arg(args, int);
		if (dir_slot(fd) >= 0)
			return EBADF;
		int target = sg_find_free_fd(min);
		if (target < 0)
			return EMFILE;
		*result = target;
		return sg_dup2_raw(fd, target, nullptr);
	}
	default:
		return EINVAL;
	}
}

int Sysdeps<Pipe>::operator()(int *fds, int) {
	long r = __sagui_syscall1(SIS_PIPE, (long)fds);
	if (r < 0)
		return sg_errno(r);
	fd_set(fds[0], O_RDONLY, nullptr);
	fd_set(fds[1], O_WRONLY, nullptr);
	return 0;
}

int Sysdeps<Fork>::operator()(pid_t *child) {
	*child = -1;
	return ENOSYS;
}

int Sysdeps<Execve>::operator()(const char *, char *const *, char *const *) {
	return ENOSYS;
}

int Sysdeps<PrepareStack>::operator()(void **, void *, void *, void *, size_t *, size_t *, void **) {
	return ENOSYS;
}

int Sysdeps<Clone>::operator()(void *, pid_t *, void *) {
	return ENOSYS;
}

int Sysdeps<Waitpid>::operator()(pid_t pid, int *status, int flags, struct rusage *, pid_t *ret_pid) {
	int code = 0;
	long r = __sagui_syscall3(SIS_ESPERAR, pid, (long)&code, (flags & WNOHANG) ? 1 : 0);
	if (r < 0)
		return sg_errno(r);
	if (r == 0) {
		if (ret_pid)
			*ret_pid = 0;
		return 0;
	}
	if (status) {
		if (code == SAIDA_ENCERRADO_PELO_KERNEL)
			*status = SIGKILL;
		else
			*status = (code & 0xff) << 8;
	}
	if (ret_pid)
		*ret_pid = (pid_t)r;
	return 0;
}

void Sysdeps<Sync>::operator()() {}

int Sysdeps<Fsync>::operator()(int) {
	return 0;
}

int Sysdeps<Fdatasync>::operator()(int) {
	return 0;
}

int Sysdeps<Fadvise>::operator()(int, off_t, off_t, int) {
	return 0;
}

int Sysdeps<Flock>::operator()(int, int) {
	return 0;
}

int Sysdeps<Chmod>::operator()(const char *pathname, mode_t) {
	sg_info_arquivo_t info;
	if (!pathname)
		return EFAULT;
	return sg_info(pathname, &info);
}

int Sysdeps<Fchmod>::operator()(int fd, mode_t) {
	struct stat st;
	return fill_stat_fd(fd, &st);
}

int Sysdeps<Fchmodat>::operator()(int fd, const char *pathname, mode_t mode, int) {
	if (pathname)
		return sysdep<Chmod>(pathname, mode);
	struct stat st;
	return fill_stat_fd(fd, &st);
}

gid_t Sysdeps<GetGid>::operator()() {
	return 0;
}

gid_t Sysdeps<GetEgid>::operator()() {
	return 0;
}

uid_t Sysdeps<GetUid>::operator()() {
	return 0;
}

uid_t Sysdeps<GetEuid>::operator()() {
	return 0;
}

int Sysdeps<GetHostname>::operator()(char *buffer, size_t bufsize) {
	const char *name = "sagui";
	size_t n = strlen(name);
	if (n + 1 > bufsize)
		return ENAMETOOLONG;
	memcpy(buffer, name, n);
	buffer[n] = 0;
	return 0;
}

int Sysdeps<Umask>::operator()(mode_t mode, mode_t *old) {
	if (old)
		*old = g_umask;
	g_umask = mode & 0777;
	return 0;
}

int Sysdeps<Uname>::operator()(struct utsname *buf) {
	auto copy = [](char *dst, size_t n, const char *src) {
		size_t i = 0;
		while (src[i] && i + 1 < n) {
			dst[i] = src[i];
			i++;
		}
		dst[i] = 0;
	};
	copy(buf->sysname, sizeof(buf->sysname), "SaguiOS");
	copy(buf->nodename, sizeof(buf->nodename), "sagui");
	copy(buf->release, sizeof(buf->release), "0.1");
	copy(buf->version, sizeof(buf->version), "mlibc");
	copy(buf->machine, sizeof(buf->machine), "i686");
	return 0;
}

int Sysdeps<Ioctl>::operator()(int, unsigned long request, void *arg, int *result) {
	if (request == SG_TIOCGWINSZ) {
		auto ws = (struct sg_winsize *)arg;
		ws->ws_row = 25;
		ws->ws_col = 80;
		ws->ws_xpixel = 0;
		ws->ws_ypixel = 0;
		if (result)
			*result = 0;
		return 0;
	}
	return ENOTTY;
}

int Sysdeps<Ttyname>::operator()(int, char *, size_t) {
	return ENOTTY;
}

int Sysdeps<Readlink>::operator()(const char *, void *, size_t, ssize_t *) {
	return EINVAL;
}

int Sysdeps<Readlinkat>::operator()(int, const char *, void *, size_t, ssize_t *) {
	return EINVAL;
}

int Sysdeps<Truncate>::operator()(const char *, off_t) {
	return ENOSYS;
}

int Sysdeps<Ftruncate>::operator()(int, size_t) {
	return ENOSYS;
}

int Sysdeps<Statvfs>::operator()(const char *path, struct statvfs *out) {
	sg_info_arquivo_t info;
	if (int e = sg_info(path, &info))
		return e;
	memset(out, 0, sizeof(*out));
	out->f_bsize = 512;
	out->f_frsize = 512;
	out->f_namemax = NOME_MAX;
	return 0;
}

int Sysdeps<Fstatvfs>::operator()(int fd, struct statvfs *out) {
	struct stat st;
	if (int e = fill_stat_fd(fd, &st))
		return e;
	memset(out, 0, sizeof(*out));
	out->f_bsize = 512;
	out->f_frsize = 512;
	out->f_namemax = NOME_MAX;
	return 0;
}

int Sysdeps<Link>::operator()(const char *, const char *) {
	return ENOSYS;
}

int Sysdeps<Symlink>::operator()(const char *, const char *) {
	return ENOSYS;
}

int Sysdeps<Chroot>::operator()(const char *) {
	return ENOSYS;
}

int Sysdeps<Fchdir>::operator()(int) {
	return ENOSYS;
}

int Sysdeps<Sysconf>::operator()(int num, long *ret) {
	switch (num) {
	case _SC_PAGE_SIZE:
		*ret = 4096;
		return 0;
	case _SC_CLK_TCK:
		*ret = 100;
		return 0;
	case _SC_OPEN_MAX:
		*ret = 16;
		return 0;
	case _SC_NPROCESSORS_ONLN:
		*ret = 1;
		return 0;
	case _SC_ARG_MAX:
		*ret = 131072;
		return 0;
	case _SC_CHILD_MAX:
		*ret = 64;
		return 0;
	case _SC_HOST_NAME_MAX:
		*ret = 255;
		return 0;
	case _SC_LOGIN_NAME_MAX:
		*ret = 255;
		return 0;
	case _SC_LINE_MAX:
		*ret = 2048;
		return 0;
	case _SC_IOV_MAX:
		*ret = 1024;
		return 0;
	default:
		return ENOSYS;
	}
}

} // namespace mlibc
