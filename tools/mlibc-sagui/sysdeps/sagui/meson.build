sysdep_supported_options = {
	'posix': true,
}

rtld_sources += files(
	'sysdeps.cpp',
	'syscall.cpp',
)
rtld_include_dirs += include_directories('include')

libc_sources += files(
	'entry.cpp',
	'sysdeps.cpp',
	'syscall.cpp',
)
libc_include_dirs += include_directories('include')

if not no_headers
	install_subdir('include/abi-bits', install_dir: get_option('includedir'))
endif

if not headers_only
	crt = custom_target('crt1',
		build_by_default: true,
		command: c_compiler.cmd_array() + freestanding_hdrs_args + ['-c', '-o', '@OUTPUT@', '@INPUT@'],
		input: 'crt1.S',
		output: 'crt1.o',
		install: true,
		install_dir: get_option('libdir')
	)
endif
