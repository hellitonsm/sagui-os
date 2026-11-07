#ifndef MLIBC_SAGUI_SYSDEP_TAGS
#define MLIBC_SAGUI_SYSDEP_TAGS

#include <mlibc/sysdep-signatures.hpp>

namespace mlibc {

struct SaguiSysdepTags
    : LibcPanic, LibcLog, AnonAllocate, AnonFree, Stat, VmMap, VmUnmap,
      VmProtect, TcbSet, Exit, ThreadExit, FutexWait, FutexWake, FutexTid,
      Open, Openat, OpenDir, ReadEntries, Read, Write, Pread, Pwrite, Readv,
      Writev, Seek, Close, ClockGet, ClockGetres, Sleep, Isatty, Rmdir,
      Unlinkat, Rename, Renameat, Waitpid, Fork, Execve, GetPid, GetPpid,
      GetTid, Kill, Access, Faccessat, Dup, Dup2, Fcntl, Sync, Fsync,
      Fdatasync, Fadvise, Chmod, Fchmod, Fchmodat, GetGid, GetEgid, GetUid,
      GetEuid, GetHostname, Umask, Uname, GetEntropy, Pipe, Ioctl, Ttyname,
      Readlink, Readlinkat, Truncate, Ftruncate, Flock, Statvfs, Fstatvfs,
      Link, Symlink, Chroot, Fchdir, GetCwd, Chdir, Mkdir, Mkdirat,
      PrepareStack, Clone, Sysconf {};

template <typename Tag>
using Sysdeps = SysdepOf<SaguiSysdepTags, Tag>;

struct SysdepTraits {
	static constexpr bool usesRtNetlink = false;
};

} // namespace mlibc

#endif /* MLIBC_SAGUI_SYSDEP_TAGS */
