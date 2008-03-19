// Copyright (C) 2006-2007 David Sugar, Tycho Softworks.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.

#include <config.h>
#include <ucommon/string.h>
#include <ucommon/memory.h>
#include <ucommon/shell.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <ctype.h>

#ifndef	_MSWINDOWS_
#include <sys/wait.h>
#include <fcntl.h>
#endif

#ifdef	HAVE_SYS_RESOURCE_H
#include <sys/time.h>
#include <sys/resource.h>
#endif

using namespace UCOMMON_NAMESPACE;

#ifdef _MSWINDOWS_

void shell::expand(void)
{
	first = last = NULL;
	const char *fn;
	char dirname[128];
	WIN32_FIND_DATA entry;
	bool skipped = false, flagged = true;
	args *arg;
	int len;
	fd_t dir;

	first = last = NULL;

	while(_argv && *_argv) {
		if(skipped) {
skip:
			arg = (args *)mempager::alloc(sizeof(args));
			arg->item = *(_argv++);
			arg->next = NULL;
			if(last) {
				last->next = arg;	
				last = arg;
			}
			else
				last = first = arg;
			continue;
		}
		if(!strncmp(*_argv, "-*", 2)) {
			++_argv;
			skipped = true;
			continue;
		}
		if(!strcmp(*_argv, "--")) {
			flagged = false;
			goto skip;
		}
		if(**_argv == '-' && flagged) 
			goto skip;
		fn = strrchr(*_argv, '/');
		if(!fn)
			fn = strrchr(*_argv, '\\');
		if(!fn)
			fn = strrchr(*_argv, ':');
		if(fn)
			++fn;
		else
			fn = *_argv;
		if(!*fn)
			goto skip;
		if(*fn != '*' && fn[strlen(fn) - 1] != '*' && !strchr(fn, '?'))
			goto skip;
		if(!strcmp(fn, "*"))
			fn = "*.*";
		len = fn - *_argv;
		if(len >= sizeof(dirname))
			len = sizeof(dirname) - 1;
		if(len == 0)
			dirname[0] = 0;
		else
			String::set(dirname, ++len, *_argv);	
		len = strlen(dirname);
		if(len)
			String::set(dirname + len, sizeof(dirname) - len, fn);
		else
			String::set(dirname, sizeof(dirname), fn);
		dir = FindFirstFile(dirname, &entry);
		if(dir == INVALID_HANDLE_VALUE)
			goto skip;
		do {
			if(len)
				String::set(dirname + len, sizeof(dirname) - len, fn);
			else
				String::set(dirname, sizeof(dirname), fn);
			arg = (args *)mempager::alloc(sizeof(args));
			arg->item = mempager::dup(dirname);
			arg->next = NULL;
			if(last) {
				last->next = arg;	
				last = arg;
			}
			else
				last = first = arg;
		} while(FindNextFile(dir, &entry));
		CloseHandle(dir);
		++*_argv;
	}
}

#else

void shell::expand(void)
{
	first = last = NULL;
}

#endif

void shell::collapse(void)
{
	char **argv = _argv = (char **)mempager::alloc(sizeof(char **) * (_argc + 1));
	while(first) {
		*(argv++) = first->item;
		first = first->next;
	}
	*argv = NULL;
	first = last = NULL;
}

shell::shell(size_t pagesize) :
mempager(pagesize)
{
	_argv = NULL;
	_argc = 0;
}

shell::shell(const char *string, size_t pagesize) :
mempager(pagesize)
{
	parse(string);
}

shell::shell(char **argv, size_t pagesize) :
mempager(pagesize)
{
	int tmp;
	expand(&tmp, &argv);
}

char **shell::parse(const char *string)
{
	assert(string != NULL);

	args *arg;
	char quote = 0;
	char *cp = mempager::dup(string);
	bool active = false;
	first = last = NULL;

	_argc = 0;

	while(*cp) {
		if(isspace(*cp) && active && !quote) {
inactive:
			active = false;
			*(cp++) = 0;
			continue;
		}
		if(*cp == '\'' && !active) {
			quote = *cp;
			goto argument;
		}
		if(*cp == '\"' && !active) {
			quote = *(cp++);
			goto argument;
		}
		if(*cp == quote && active) {
			if(quote == '\"')
				goto inactive;
			if(isspace(cp[1])) {
				++cp;
				goto inactive;
			}
		}
		if(!isspace(*cp) && !active) {
argument:
			++_argc;
			active = true;
			arg = (args *)mempager::alloc(sizeof(args));
			arg->item = (cp++);
			arg->next = NULL;
			if(last) {
				last->next = arg;
				last = arg;
			}
			else
				first = last = arg;
			continue;
		}
		++cp;
	}
	collapse();
	return _argv;
}

int shell::systemf(const char *format, ...)
{
	va_list args;
	char buffer[1024];
	
	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	return system(buffer);
}

int shell::expand(int *argc, char ***argv)
{
	_argc = *argc;
	_argv = *argv;

	expand();
	if(first) {
		collapse();
		*argv = _argv;
		*argc = _argc;
	}
	return _argc;
}

#ifdef _MSWINDOWS_

int shell::system(const char *cmd, const char **envp)
{
	char cmdspec[128];
	DWORD code;
	PROCESS_INFORMATION pi;
	char *ep = NULL;
	unsigned len = 0;

	if(envp)
		ep = new char[4096];

	while(envp && *envp && len < 4090) {
		String::set(ep + len, 4094 - len, *envp);
		len += strlen(*(envp++)) + 1;
	}

	if(ep)
		ep[len] = 0;

	GetEnvironmentVariable("ComSpec", cmdspec, sizeof(cmdspec));
	
	if(!CreateProcess((CHAR *)cmdspec, (CHAR *)cmd, NULL, NULL, TRUE, CREATE_NEW_CONSOLE, ep, NULL, NULL, &pi)) {
		delete[] ep;
		return 127;
	}
	delete[] ep;
	
	if(WaitForSingleObject(pi.hProcess, INFINITE) == WAIT_FAILED) {
		return -1;
	}

	GetExitCodeProcess(pi.hProcess, &code);
	return (int)code;
}

#else

int shell::system(const char *cmd, const char **envp)
{
	assert(cmd != NULL);
	
	char symname[129];
	const char *cp;
	char *ep;
	int status;
	int max = sizeof(fd_set) * 8;

#ifdef	RLIMIT_NOFILE
	struct rlimit rlim;

	if(!getrlimit(RLIMIT_NOFILE, &rlim))
		max = rlim.rlim_max;
#endif

	pid_t pid = fork();
	if(pid < 0)
		return -1;

	if(pid > 0) {
		if(::waitpid(pid, &status, 0) != pid)
			status = -1;
		return status;
	}
	for(int fd = 3; fd < max; ++fd)
		::close(fd);

	while(envp && *envp) {
		String::set(symname, sizeof(symname), *envp);
		ep = strchr(symname, '=');
		if(ep)
			*ep = 0;
		cp = strchr(*envp, '=');
		if(cp)
			++cp;
		::setenv(symname, cp, 1);
		++envp;
	}

	::signal(SIGQUIT, SIG_DFL);
	::signal(SIGINT, SIG_DFL);
	::signal(SIGCHLD, SIG_DFL);
	::signal(SIGPIPE, SIG_DFL);
	::execlp("/bin/sh", "sh", "-c", cmd, NULL);
	exit(127);
}

#endif
