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
#include <ucommon/thread.h>
#include <ucommon/timers.h>
#include <ucommon/linked.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>

#ifndef	_MSWINDOWS_
#include <sched.h>
#endif

using namespace UCOMMON_NAMESPACE;

unsigned Conditional::max_sharing = 0;

#ifndef	_MSWINDOWS_
Conditional::attribute Conditional::attr;
#endif

ReusableAllocator::ReusableAllocator() :
Conditional()
{
	freelist = NULL;
	waiting = 0;
}

void ReusableAllocator::release(ReusableObject *obj)
{
	LinkedObject **ru = (LinkedObject **)freelist;

	obj->retain();
	obj->release();

	lock();
	obj->enlist(ru);
	if(waiting)
		signal();
	unlock();
}

void Conditional::gettimeout(timeout_t msec, struct timespec *ts)
{
#if defined(HAVE_PTHREAD_CONDATTR_SETCLOCK) && defined(_POSIX_MONOTONIC_CLOCK)
	clock_gettime(CLOCK_MONOTONIC, ts);
#elif _POSIX_TIMERS > 0
	clock_gettime(CLOCK_REALTIME, ts);
#else
	timeval tv;
	gettimeofday(&tv, NULL);
	ts->tv_sec = tv.tv_sec;
	ts->tv_nsec = tv.tv_usec * 1000l;
#endif
	ts->tv_sec += msec / 1000;
	ts->tv_nsec += (msec % 1000) * 1000000l;
	while(ts->tv_nsec > 1000000000l) {
		++ts->tv_sec;
		ts->tv_nsec -= 1000000000l;
	}
}

semaphore::semaphore(unsigned limit) : 
Conditional()
{
	count = limit;
	waits = 0;
	used = 0;
}

void semaphore::Shlock(void)
{
	wait();
}

void semaphore::Unlock(void)
{
	release();
}

unsigned semaphore::getUsed(void)
{
	unsigned rtn;
	lock();
	rtn = used;
	unlock();
	return rtn;
}

unsigned semaphore::getCount(void)
{
    unsigned rtn;
	lock();
    rtn = count;
	unlock();
    return rtn;
}

bool semaphore::wait(timeout_t timeout)
{
	return request(1, timeout);
}

bool semaphore::request(unsigned size, timeout_t timeout)
{
	bool result = true;
	struct timespec ts;

	if(timeout && timeout != Timer::inf)
		gettimeout(timeout, &ts);

	lock();
	if(used + size > count) {
		++waits;
		while(used + size > count && result) {
			if(timeout == Timer::inf)
				Conditional::wait();
			else if(timeout)
				result = Conditional::wait(&ts);
			else
				result = false;
		}
		--waits;
	}
	if(result)
		used += size;
	unlock();
	return result;
}

void semaphore::wait(void)
{
	lock();
	if(used >= count) {
		++waits;
		while(used >= count)
			Conditional::wait();
		--waits;
	}
	++used;
	unlock();
}

void semaphore::request(unsigned size)
{
	lock();
	if(used + size > count) {
		++waits;
		while(used + size > count)
			Conditional::wait();
		--waits;
	}
	used += size;
	unlock();
}

void semaphore::release(unsigned size)
{
	while(size--)
		release();
}

void semaphore::release(void)
{
	lock();
	if(used)
		--used;
	if(waits)
		signal();
	unlock();
}

void semaphore::set(unsigned value)
{
	unsigned diff;

	lock();
	count = value;
	if(used >= count || !waits) {
		unlock();
		return;
	}
	diff = count - used;
	if(diff > waits)
		diff = waits;
	unlock();
	while(diff--) {
		lock();
		signal();
		unlock();
	}
}

#ifdef	_MSWINDOWS_

void Conditional::wait(void)
{
	int result;

	EnterCriticalSection(&mlock);
	++waiting;
	LeaveCriticalSection(&mlock);
	LeaveCriticalSection(&mutex);
	result = WaitForMultipleObjects(2, events, FALSE, INFINITE);
	EnterCriticalSection(&mlock);
	--waiting;
	result = ((result == WAIT_OBJECT_0 + BROADCAST) && (waiting == 0));
	LeaveCriticalSection(&mlock);
	if(result)
		ResetEvent(&events[BROADCAST]);
	EnterCriticalSection(&mutex);
}

void Conditional::signal(void)
{
	EnterCriticalSection(&mlock);
	if(waiting)
		SetEvent(&events[SIGNAL]);
	LeaveCriticalSection(&mlock);
}

void Conditional::broadcast(void)
{
	EnterCriticalSection(&mlock);
	if(waiting)
		SetEvent(&events[BROADCAST]);
	LeaveCriticalSection(&mlock);

}

Conditional::Conditional()
{
	waiting = 0;

	InitializeCriticalSection(&mutex);
	InitializeCriticalSection(&mlock);
	events[SIGNAL] = CreateEvent(NULL, FALSE, FALSE, NULL);
	events[BROADCAST] = CreateEvent(NULL, TRUE, FALSE, NULL);	
}

Conditional::~Conditional()
{
	DeleteCriticalSection(&mlock);
	DeleteCriticalSection(&mutex);
	CloseHandle(events[SIGNAL]);
	CloseHandle(events[BROADCAST]);
}

bool Conditional::wait(timeout_t timeout)
{
	int result;
	bool rtn = true;

	if(!timeout)
		return false;

	EnterCriticalSection(&mlock);
	++waiting;
	LeaveCriticalSection(&mlock);
	LeaveCriticalSection(&mutex);
	result = WaitForMultipleObjects(2, events, FALSE, timeout);
	EnterCriticalSection(&mlock);
	--waiting;
	if(result == WAIT_OBJECT_0 || result == WAIT_OBJECT_0 + BROADCAST)
		rtn = true;
	result = ((result == WAIT_OBJECT_0 + BROADCAST) && (waiting == 0));
	LeaveCriticalSection(&mlock);
	if(result)
		ResetEvent(&events[BROADCAST]);
	EnterCriticalSection(&mutex);
	return rtn;
}

bool Conditional::wait(struct timespec *ts)
{
	return wait(ts->tv_sec * 1000 + (ts->tv_nsec / 1000000l));
}

#else
Conditional::attribute::attribute()
{
	Thread::init();
	pthread_condattr_init(&attr);
#if _POSIX_TIMERS > 0 && defined(HAVE_PTHREAD_CONDATTR_SETCLOCK)
#if defined(_POSIX_MONOTONIC_CLOCK)
	pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
#else
	pthread_condattr_setclock(&attr, CLOCK_REALTIME);
#endif
#endif
}

Conditional::Conditional()
{
	crit(pthread_cond_init(&cond, &attr.attr) == 0, "conditional init failed");
	crit(pthread_mutex_init(&mutex, NULL) == 0, "mutex init failed");
}

Conditional::~Conditional()
{
	pthread_cond_destroy(&cond);
	pthread_mutex_destroy(&mutex);
}

bool Conditional::wait(timeout_t timeout)
{
	struct timespec ts;
	gettimeout(timeout, &ts);
	return wait(&ts);
}

bool Conditional::wait(struct timespec *ts)
{
	if(pthread_cond_timedwait(&cond, &mutex, ts) == ETIMEDOUT)
		return false;

	return true;
}

#endif

rexlock::rexlock() :
Conditional()
{
	lockers = 0;
	waiting = 0;
}

unsigned rexlock::getWaiting(void)
{
	unsigned count;
	Conditional::lock();
	count = waiting;
	Conditional::unlock();
	return count;
}

unsigned rexlock::getLocking(void)
{
	unsigned count;
	Conditional::lock();
	count = lockers;
	Conditional::unlock();
	return count;
}



void rexlock::Exlock(void)
{
	lock();
}

void rexlock::Unlock(void)
{
	release();
}

void rexlock::lock(void)
{
	Conditional::lock();
	while(lockers) {
		if(pthread_equal(locker, pthread_self()))
			break;
		++waiting;
		Conditional::wait();
		--waiting;
	}
	if(!lockers)
		locker = pthread_self();
	++lockers;
	Conditional::unlock();
	return;
}

void rexlock::release(void)
{
	Conditional::lock();
	--lockers;
	if(!lockers && waiting)
		Conditional::signal();
	Conditional::unlock();
}

rwlock::rwlock() :
Conditional()
{
	writers = 0;
	reading = 0;
	pending = 0;
	waiting = 0;
}

unsigned rwlock::getAccess(void)
{
	unsigned count;
	Conditional::lock();
	count = reading;
	Conditional::unlock();
	return count;
}

unsigned rwlock::getModify(void)
{
	unsigned count;
	Conditional::lock();
	count = writers;
	Conditional::unlock();
	return count;
}

unsigned rwlock::getWaiting(void)
{
	unsigned count;
	Conditional::lock();
	count = waiting + pending;
	Conditional::unlock();
	return count;
}

void rwlock::Exlock(void)
{
	modify();
}

void rwlock::Shlock(void)
{
	access();
}

void rwlock::Unlock(void)
{
	release();
}

bool rwlock::modify(timeout_t timeout)
{
	bool rtn = true;
	struct timespec ts;

	if(timeout && timeout != Timer::inf)
		gettimeout(timeout, &ts);
	
	lock();
	while((writers || reading) && rtn) {
		if(writers && pthread_equal(writer, pthread_self()))
			break;
		++pending;
		if(timeout == Timer::inf)
			wait();
		else if(timeout)
			rtn = wait(&ts);
		else
			rtn = false;
		--pending;
	}
	assert(!max_sharing || writers < max_sharing);
	if(rtn) {
		if(!writers)
			writer = pthread_self();
		++writers;
	}
	unlock();
	return rtn;
}

bool rwlock::access(timeout_t timeout)
{
	struct timespec ts;
	bool rtn = true;

	if(timeout && timeout != Timer::inf)
		gettimeout(timeout, &ts);

	lock();
	while((writers || pending) && rtn) {
		++waiting;
		if(timeout == Timer::inf)
			wait();
		else if(timeout)
			rtn = wait(&ts);
		else
			rtn = false;
		--waiting;
	}
	assert(!max_sharing || reading < max_sharing);
	if(rtn)
		++reading;
	unlock();
	return rtn;
}

void rwlock::release(void)
{
	lock();
	assert(reading || writers);

	if(writers) {
		assert(!reading);
		--writers;
		if(waiting && !writers)
			broadcast();
		else if(pending && !writers)
			signal();
		unlock();
		return;
	}
	if(reading) {
		assert(!writers);
		--reading;
		if(waiting && (!pending || !reading))
			broadcast();
		else if(!reading && pending)
			signal();
	}
	unlock();
}

StepLock::StepLock(mutex *base)
{
	parent = base;
	stepping = false;
	crit(pthread_mutex_init(&mlock, NULL) == 0, "mutex init failed");
}

StepLock::~StepLock()
{
	pthread_mutex_destroy(&mlock);
	if(stepping)
		parent->unlock();
}

void StepLock::release(void)
{
	if(stepping) {
		stepping = false;
		parent->unlock();
	}
	else
		pthread_mutex_unlock(&mlock);
}

void StepLock::lock(void)
{
	parent->lock();
	stepping = true;
}

void StepLock::access(void)
{
	pthread_mutex_lock(&mlock);
	if(stepping)
		parent->unlock();
	stepping = false;
}

void StepLock::Exlock(void)
{
	lock();
}

void StepLock::Shlock(void)
{
	access();
}

void StepLock::Unlock(void)
{
	release();
}

mutex::mutex()
{
	crit(pthread_mutex_init(&mlock, NULL) == 0, "mutex init failed");
}

mutex::~mutex()
{
	pthread_mutex_destroy(&mlock);
}

void mutex::Exlock(void)
{
	pthread_mutex_lock(&mlock);
}

void mutex::Unlock(void)
{
	pthread_mutex_unlock(&mlock);
}

#ifdef	_MSWINDOWS_

TimedEvent::TimedEvent() : 
Timer()
{
	event = CreateEvent(NULL, FALSE, FALSE, NULL);
	InitializeCriticalSection(&mutex);
}

TimedEvent::~TimedEvent()
{
	if(event != INVALID_HANDLE_VALUE) {
		CloseHandle(event);
		event = INVALID_HANDLE_VALUE;
	}
	DeleteCriticalSection(&mutex);
}

TimedEvent::TimedEvent(timeout_t timeout) :
Timer(timeout)
{
	event = CreateEvent(NULL, FALSE, FALSE, NULL);
	InitializeCriticalSection(&mutex);
}

TimedEvent::TimedEvent(time_t timer) :
Timer(timer)
{
	event = CreateEvent(NULL, FALSE, FALSE, NULL);
	InitializeCriticalSection(&mutex);
}

void TimedEvent::signal(void)
{
	SetEvent(event);
}

bool TimedEvent::expire(void) 
{
	int result;
	timeout_t timeout;

	timeout = get();
	if(!timeout)
		return false;

	LeaveCriticalSection(&mutex);
	result = WaitForSingleObject(event, timeout);
	EnterCriticalSection(&mutex);
	if(result != WAIT_OBJECT_0)
		return true;
	return false;
}

bool TimedEvent::wait(void) 
{
	int result;
	timeout_t timeout;

	timeout = get();
	if(!timeout)
		return false;

	result = WaitForSingleObject(event, timeout);
	if(result == WAIT_OBJECT_0)
		return true;
	return false;
}

void TimedEvent::lock(void)
{
	EnterCriticalSection(&mutex);
}

void TimedEvent::release(void)
{
	LeaveCriticalSection(&mutex);
}

#else

TimedEvent::TimedEvent() : 
Timer()
{
}

TimedEvent::TimedEvent(timeout_t timeout) :
Timer(timeout)
{
}

TimedEvent::TimedEvent(time_t timer) :
Timer(timer)
{
}

void TimedEvent::signal(void)
{
	cond.signal();
}

bool TimedEvent::expire(void) 
{
	timeout_t timeout = get();

	if(!timeout)
		return true;

	if(cond.wait(timeout))
		return false;

	return true;
}

bool TimedEvent::wait(void) 
{
	bool result;
	timeout_t timeout = get();

	cond.lock();
	if(!timeout)
		result = false;
	else
		result = cond.wait(timeout);

	cond.unlock();
	return result;
}

void TimedEvent::lock(void)
{
	cond.lock();
}

void TimedEvent::release(void)
{
	cond.unlock();
}

#endif
	
ConditionalLock::ConditionalLock() :
Conditional()
{
	waiting = 0;
	pending = 0;
	sharing = 0;
}

unsigned ConditionalLock::getReaders(void)
{
	unsigned count;

	Conditional::lock();
	count = sharing;
	Conditional::unlock();
	return count;
}

unsigned ConditionalLock::getWaiters(void)
{
	unsigned count;

	Conditional::lock();
	count = pending + waiting;
	Conditional::unlock();
	return count;
}

void ConditionalLock::Shlock(void)
{
	access();
}

void ConditionalLock::Unlock(void)
{
	release();
}

void ConditionalLock::Exclusive(void)
{
	exclusive();
}

void ConditionalLock::Share(void)
{
	share();
}

void ConditionalLock::modify(void)
{
	Conditional::lock();
	while(sharing) {
		++pending;
		Conditional::wait();
		--pending;
	}
}

void ConditionalLock::commit(void)
{
	if(waiting)
		Conditional::broadcast();
	else if(pending)
		Conditional::signal();
	Conditional::unlock();
}

void ConditionalLock::release(void)
{
	Conditional::lock();
	assert(sharing);
	--sharing;
	if(waiting && (!pending || !sharing))
		Conditional::broadcast();
	else if(pending && !sharing)
		Conditional::signal();
	Conditional::unlock();
}

void ConditionalLock::protect(void)
{
	Conditional::lock();
	assert(!max_sharing || sharing < max_sharing);

	// reschedule if pending exclusives to make sure modify threads are not
	// starved.

	while(pending && !sharing) {
		++waiting;
		wait();
		--waiting;
	}
	++sharing;
	Conditional::unlock();
}

void ConditionalLock::access(void)
{
	Conditional::lock();
	assert(!max_sharing || sharing < max_sharing);

	// reschedule if pending exclusives to make sure modify threads are not
	// starved.

	while(pending) {
		++waiting;
		wait();
		--waiting;
	}
	++sharing;
	Conditional::unlock();
}

void ConditionalLock::exclusive(void)
{
	lock();
	assert(sharing);
	--sharing;
	while(sharing) {
		++pending;
		wait();
		--pending;
	}
}

void ConditionalLock::share(void)
{
	assert(!sharing);
	++sharing;
	unlock();
}

barrier::barrier(unsigned limit) :
Conditional()
{
	count = limit;
	waits = 0;
}

barrier::~barrier()
{
	for(;;)
	{
		lock();
		if(waits)
			broadcast();
		unlock();
	}
}

void barrier::set(unsigned limit)
{
	lock();
	count = limit;
	if(count <= waits) {
		waits = 0;
		broadcast();
	}
	unlock();
}

bool barrier::wait(timeout_t timeout)
{
	bool result;

	Conditional::lock();
	if(!count) {
		Conditional::unlock();
		return true;
	}
	if(++waits >= count) {
		waits = 0;
		Conditional::broadcast();
		Conditional::unlock();
		return true;
	}
	result = Conditional::wait(timeout);
	Conditional::unlock();
	return result;
}

void barrier::wait(void)
{
	Conditional::lock();
	if(!count) {
		Conditional::unlock();
		return;
	}
	if(++waits >= count) {
		waits = 0;
		Conditional::broadcast();
		Conditional::unlock();
		return;
	}
	Conditional::wait();
	Conditional::unlock();
}

ConditionalIndex::ConditionalIndex() :
OrderedIndex(), Conditional()
{
}

void ConditionalIndex::lock_index(void)
{
	Conditional::lock();
}

void ConditionalIndex::unlock_index(void)
{
	Conditional::unlock();
}

LockedIndex::LockedIndex() :
OrderedIndex()
{
	pthread_mutex_init(&mutex, NULL);
}

void LockedIndex::lock_index(void)
{
	pthread_mutex_lock(&mutex);
}

void LockedIndex::unlock_index(void)
{
	pthread_mutex_unlock(&mutex);
}
	
LockedPointer::LockedPointer()
{
#ifdef	_MSWINDOWS_
	InitializeCriticalSection(&mutex);
#else
	static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
	memcpy(&mutex, &lock, sizeof(mutex));
#endif
	pointer = NULL;
}

void LockedPointer::replace(Object *obj)
{
	pthread_mutex_lock(&mutex);
	obj->retain();
	if(pointer)
		pointer->release();
	pointer = obj;
	pthread_mutex_unlock(&mutex);
}

Object *LockedPointer::dup(void)
{
	Object *temp;
	pthread_mutex_lock(&mutex);
	temp = pointer;
	if(temp)
		temp->retain();
	pthread_mutex_unlock(&mutex);
	return temp;
}

SharedObject::~SharedObject()
{
}

SharedPointer::SharedPointer() :
ConditionalLock()
{
	pointer = NULL;
}

SharedPointer::~SharedPointer()
{
}

void SharedPointer::replace(SharedObject *ptr)
{
	ConditionalLock::modify();
	if(pointer)
		delete pointer;
	pointer = ptr;
	if(ptr)
		ptr->commit(this);
	ConditionalLock::commit();
}

SharedObject *SharedPointer::share(void)
{
	ConditionalLock::access();
	return pointer;
}

Thread::Thread(size_t size)
{
	stack = size;
}

#if defined(_MSWINDOWS_)
void Thread::lowerPriority(void)
{
	HANDLE hThread = GetCurrentThread();
	SetThreadPriority(hThread, THREAD_PRIORITY_LOWEST);
}

void Thread::raisePriority(unsigned adj)
{
	HANDLE hThread = GetCurrentThread();
	if(adj == 0) {
		SetThreadPriority(hThread, THREAD_PRIORITY_NORMAL);
	}
	else if(adj == 1) {
		SetThreadPriority(hThread, THREAD_PRIORITY_ABOVE_NORMAL);
	}
	else
		SetThreadPriority(hThread, THREAD_PRIORITY_HIGHEST);
}
#elif _POSIX_PRIORITY_SCHEDULING > 0

void Thread::resetPriority(struct sched_param *sparam)
{	
	pthread_t tid = pthread_self();
#ifdef	HAVE_PTHREAD_SETSCHEDPRIO
	pthread_setschedprio(tid, sparam->sched_priority);
#else
	int policy;
	struct sched_param lp;
	pthread_getschedparam(tid, &policy, &lp);
	pthread_setschedparam(tid, policy, sparam);
#endif
}

void Thread::lowerPriority(void)
{
	int policy;
	struct sched_param sp;
	pthread_t tid = pthread_self();

	if(pthread_getschedparam(tid, &policy, &sp))
		return;

#ifdef	HAVE_PTHREAD_SETSCHEDPRIO
	pthread_setschedprio(tid, sched_get_priority_min(policy));
#else
	sp.sched_priority = sched_get_priority_min(policy);
	pthread_setschedparam(tid, policy, &sp);
#endif
}

void Thread::raisePriority(unsigned adj, struct sched_param *sparam)
{
	int policy;
	struct sched_param lp;
	pthread_t tid = pthread_self();
	int pri;

	if(!sparam)
		sparam = &lp;

	if(pthread_getschedparam(tid, &policy, sparam))
		return;

	pri = sparam->sched_priority + adj;
	if(pri > sched_get_priority_max(policy))
		pri = sched_get_priority_max(policy);

#ifdef	HAVE_PTHREAD_SETSCHEDPRIO
	pthread_setschedprio(tid, pri);
#else
	if(sparam != &lp)
		memcpy(&lp, sparam, sizeof(lp));
	lp.sched_priority = pri;
	pthread_setschedparam(tid, policy, &lp);
#endif
}
	
#endif

JoinableThread::JoinableThread(size_t size)
{
#ifdef	_MSWINDOWS_
	joining = INVALID_HANDLE_VALUE;
#else
	running = false;
#endif
	stack = size;
}

DetachedThread::DetachedThread(size_t size)
{
	stack = size;
}

#include <stdio.h>

void Thread::sleep(timeout_t timeout)
{
	timespec ts;
	ts.tv_sec = timeout / 1000l;
	ts.tv_nsec = (timeout % 1000l) * 1000000l;
#if defined(HAVE_PTHREAD_DELAY)
	pthread_delay(&ts);
#elif defined(HAVE_PTHREAD_DELAY_NP)
	pthread_delay_np(&ts);
#elif defined(_MSWINDOWS_)
	::Sleep(timeout);
#else
	usleep(timeout * 1000);
#endif
}

void Thread::yield(void)
{
#if defined(_MSWINDOWS_)
	SwitchToThread();
#elif defined(HAVE_PTHREAD_YIELD)
	pthread_yield();
#else
	sched_yield();
#endif
}

void PooledThread::exit(void)
{
	--poolused;
	if(!poolused)
		delete this;
	pthread_exit(NULL);
}

void DetachedThread::exit(void)
{
	delete this;
	pthread_exit(NULL);
}

void PooledThread::sync(void)
{
	lock();
	if(poolused < 2) {
		unlock();
		return;
	}
	if(++waits == poolused) {
		Conditional::broadcast();
		waits = 0;
	}
	else
		Conditional::wait();
	unlock();
}

bool PooledThread::suspend(timeout_t timeout)
{
	bool result;
	struct timespec ts;

	gettimeout(timeout, &ts);

    lock();
    ++waits;
	result = Conditional::wait(&ts);
    --waits;
    unlock();
	return result;
}

void PooledThread::suspend(void)
{
	lock();
	++waits;
	Conditional::wait();
	--waits;
	unlock();
}

unsigned PooledThread::wakeup(unsigned limit)
{
	unsigned waiting = 0;

	lock();
	while(waits >= limit) {
		++waiting;
		Conditional::signal();
		unlock();
		yield();
		lock();
	}
	unlock();
	return waiting;
}

PooledThread::PooledThread(size_t size) :
DetachedThread(size), Conditional()
{
	waits = 0;
	poolsize = 0;
	poolused = 0;
}

Thread::~Thread()
{
}

JoinableThread::~JoinableThread()
{
	join();
}

DetachedThread::~DetachedThread()
{
}
		
extern "C" {
#ifdef	_MSWINDOWS_
	static unsigned __stdcall exec_thread(void *obj) {
		Thread *th = static_cast<Thread *>(obj);
		th->run();
		th->exit();
		return 0;
	}
#else
	static void *exec_thread(void *obj)
	{
		Thread *th = static_cast<Thread *>(obj);
		th->run();
		th->exit();
		return NULL;
	};
#endif
}

void PooledThread::start(void)
{
	lock();
	++poolsize;
	++poolused;
	DetachedThread::start();
	unlock();
}

void PooledThread::start(unsigned count)
{
	lock();
	poolsize = count;
	while(poolused < poolsize) {
		DetachedThread::start();
		++poolused;
	}
	unlock();
}

#ifdef	_MSWINDOWS_
void JoinableThread::start(void)
{
	if(joining != INVALID_HANDLE_VALUE)
		return;

	if(stack == 1)
		stack = 1024;

	joining = (HANDLE)_beginthreadex(NULL, stack, &exec_thread, this, 0, (unsigned int *)&tid);
	if(!joining)
		joining = INVALID_HANDLE_VALUE;
}

void DetachedThread::start(void)
{
	HANDLE hThread;
	unsigned temp;
	
	if(stack == 1)
		stack = 1024;

	hThread = (HANDLE)_beginthreadex(NULL, stack, &exec_thread, this, 0, (unsigned int *)&tid);
	CloseHandle(hThread);
}		

void JoinableThread::join(void)
{
	pthread_t self = pthread_self();
	int rc;

	if(joining && pthread_equal(tid, self))
		Thread::exit();

	// already joined, so we ignore...
	if(joining == INVALID_HANDLE_VALUE)
		return;

	rc = WaitForSingleObject(joining, INFINITE);
	if(rc == WAIT_OBJECT_0 || rc == WAIT_ABANDONED) {
		CloseHandle(joining);
		joining = INVALID_HANDLE_VALUE;
	}	
}

#else

void JoinableThread::start(void)
{
	int result;

	pthread_attr_t attr;
	if(running)
		return;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE); 
	pthread_attr_setinheritsched(&attr, PTHREAD_INHERIT_SCHED);
// we typically use "stack 1" for min stack...
#ifdef	PTHREAD_STACK_MIN
	if(stack && stack < PTHREAD_STACK_MIN)
		stack = PTHREAD_STACK_MIN;
#else
	if(stack && stack < 2)
		stack = 0;
#endif
	if(stack)
		pthread_attr_setstacksize(&attr, stack);
	result = pthread_create(&tid, &attr, &exec_thread, this);
	pthread_attr_destroy(&attr);
	if(!result)
		running = true;
}

void DetachedThread::start(void)
{
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_attr_setinheritsched(&attr, PTHREAD_INHERIT_SCHED);
// we typically use "stack 1" for min stack...
#ifdef	PTHREAD_STACK_MIN
	if(stack && stack < PTHREAD_STACK_MIN)
		stack = PTHREAD_STACK_MIN;
#else
	if(stack && stack < 2)
		stack = 0;
#endif
	if(stack)
		pthread_attr_setstacksize(&attr, stack);
	pthread_create(&tid, &attr, &exec_thread, this);
	pthread_attr_destroy(&attr);
}

void JoinableThread::join(void)
{
	pthread_t self = pthread_self();

	if(running && pthread_equal(tid, self))
		Thread::exit();

	// already joined, so we ignore...
	if(!running)
		return;

	if(!pthread_join(tid, NULL))
		running = false;
}

#endif

void Thread::exit(void)
{
	pthread_exit(NULL);
}

queue::member::member(queue *q, Object *o) :
OrderedObject(q)
{
	o->retain();
	object = o;
}

queue::queue(mempager *p, size_t size) :
OrderedIndex(), Conditional()
{
	pager = p;
	freelist = NULL;
	used = 0;
	limit = size;
}

bool queue::remove(Object *o)
{
	bool rtn = false;
	linked_pointer<member> node;
	lock();
	node = begin();
	while(node) {
		if(node->object == o)
			break;
		node.next();
	}
	if(node) {
		--used;
		rtn = true;
		node->object->release();
		node->delist(this);
		node->LinkedObject::enlist(&freelist);			
	}
	unlock();
	return rtn;
}

Object *queue::lifo(timeout_t timeout)
{
	struct timespec ts;
	bool rtn = true;
    member *member;
    Object *obj = NULL;

	if(timeout && timeout != Timer::inf)
		gettimeout(timeout, &ts);

	lock();
	while(!tail && rtn) {
		if(timeout == Timer::inf)
			Conditional::wait();
		else if(timeout)
			rtn = Conditional::wait(&ts);
		else
			rtn = false;
	}
    if(rtn && tail) {
        --used;
        member = static_cast<queue::member *>(head);
        obj = member->object;
		member->delist(this);
        member->LinkedObject::enlist(&freelist);
    }
	if(rtn)
		signal();
	unlock();
    return obj;
}

Object *queue::fifo(timeout_t timeout)
{
	bool rtn = true;
	struct timespec ts;
	linked_pointer<member> node;
	Object *obj = NULL;

	if(timeout && timeout != Timer::inf)
		gettimeout(timeout, &ts);

	lock();
	while(rtn && !head) {
		if(timeout == Timer::inf)
			Conditional::wait();
		else if(timeout)
			rtn = Conditional::wait(&ts);
		else
			rtn = false;
	}

	if(rtn && head) {
		--used;
		node = begin();
		obj = node->object;
		head = head->getNext();
		if(!head)
			tail = NULL;
		node->LinkedObject::enlist(&freelist);
	}
	if(rtn)
		signal();
	unlock();
	return obj;
}

bool queue::post(Object *object, timeout_t timeout)
{
	bool rtn = true;
	struct timespec ts;
	member *node;
	LinkedObject *mem;

	if(timeout && timeout != Timer::inf)
		gettimeout(timeout, &ts);

	lock();
	while(rtn && limit && used == limit) {
		if(timeout == Timer::inf)
			Conditional::wait();
		else if(timeout)
			rtn = Conditional::wait(&ts);
		else
			rtn = false;
	}

	if(!rtn) {
		unlock();
		return false;
	}

	++used;
	if(freelist) {
		mem = freelist;
		freelist = freelist->getNext();
		unlock();
		node = new((caddr_t)mem) member(this, object);
	}
	else {
		unlock();
		if(pager)
			node = new((caddr_t)(pager->alloc(sizeof(member)))) member(this, object);
		else
			node = new member(this, object);
	}
	lock();
	signal();
	unlock();
	return true;
}

size_t queue::getCount(void)
{
	size_t count;
	lock();
	count = used;
	unlock();
	return count;
}


stack::member::member(stack *S, Object *o) :
LinkedObject((&S->usedlist))
{
	o->retain();
	object = o;
}

stack::stack(mempager *p, size_t size) :
Conditional()
{
	pager = p;
	freelist = usedlist = NULL;
	limit = size;
	used = 0;
}

bool stack::remove(Object *o)
{
	bool rtn = false;
	linked_pointer<member> node;
	lock();
	node = static_cast<member*>(usedlist);
	while(node) {
		if(node->object == o)
			break;
		node.next();
	}
	if(node) {
		--used;
		rtn = true;
		node->object->release();
		node->delist(&usedlist);
		node->enlist(&freelist);			
	}
	unlock();
	return rtn;
}

Object *stack::pull(timeout_t timeout)
{
	bool rtn = true;
	struct timespec ts;
    member *member;
    Object *obj = NULL;

	if(timeout && timeout != Timer::inf)
		gettimeout(timeout, &ts);

	lock();
	while(rtn && !usedlist) {
		if(timeout == Timer::inf)
			Conditional::wait();
		else if(timeout)
			rtn = Conditional::wait(&ts);
		else
			rtn = false;
	} 
	if(!rtn) {
		unlock();
		return NULL;
	}
    if(usedlist) {
        member = static_cast<stack::member *>(usedlist);
        obj = member->object;
		usedlist = member->getNext();
        member->enlist(&freelist);
	}
	if(rtn)
		signal();
	unlock();
    return obj;
}

bool stack::push(Object *object, timeout_t timeout)
{
	bool rtn = true;
	struct timespec ts;
	member *node;
	LinkedObject *mem;

	if(timeout && timeout != Timer::inf)
		gettimeout(timeout, &ts);

	lock();
	while(rtn && limit && used == limit) {
		if(timeout == Timer::inf)
			Conditional::wait();
		else if(timeout)
			rtn = Conditional::wait(&ts);
		else
			rtn = false;
	}

	if(!rtn) {
		unlock();
		return false;
	}

	++used;
	if(freelist) {
		mem = freelist;
		freelist = freelist->getNext();
		unlock();
		node = new((caddr_t)mem) member(this, object);
	}
	else {
		unlock();
		if(pager) {
			caddr_t ptr = (caddr_t)pager->alloc(sizeof(member));
			node = new(ptr) member(this, object);
		}
		else
			node = new member(this, object);
	}
	lock();
	signal();
	unlock();
	return true;
}

size_t stack::getCount(void)
{
	size_t count;
	lock();
	count = used;
	unlock();
	return count;
}

Buffer::Buffer(size_t osize, size_t c) : 
Conditional()
{
	size = osize * count;
	objsize = osize;
	count = 0;
	limit = c;

	if(osize) {
		buf = (char *)malloc(size);
		crit(buf != NULL, "buffer alloc failed");
	}
	else 
		buf = NULL;

	head = tail = buf;
}

Buffer::~Buffer()
{
	if(buf)
		free(buf);
	buf = NULL;
}

unsigned Buffer::getCount(void)
{
	unsigned bcount = 0;

	lock();
	if(tail > head) 
		bcount = (unsigned)((size_t)(tail - head) / objsize);
	else if(head > tail)
		bcount = (unsigned)((((buf + size) - head) + (tail - buf)) / objsize);
	unlock();
	return bcount;
}

unsigned Buffer::getSize(void)
{
	return size / objsize;
}

void *Buffer::get(void)
{
	caddr_t dbuf;

	lock();
	while(!count)
		wait();
	dbuf = head;
	unlock();
	return dbuf;
}

void *Buffer::get(timeout_t timeout)
{
	caddr_t dbuf = NULL;
	struct timespec ts;
	bool rtn = true;

	if(timeout && timeout != Timer::inf)
		gettimeout(timeout, &ts);

	lock();
	while(!count && rtn) {
		if(timeout == Timer::inf)
			wait();
		else if(timeout)
			rtn = wait(&ts);
		else
			rtn = false;
	}
	if(count && rtn)
		dbuf = head;
	unlock();
	return dbuf;
}

void Buffer::release(void)
{
	lock();
	head += objsize;
	if(head >= buf + size)
		head = buf;
	--count;
	signal();
	unlock();
}

void Buffer::put(void *dbuf)
{
	lock();
	while(count == limit)
		wait();
	memcpy(tail, dbuf, objsize);
	tail += objsize;
	if(tail >= (buf + size))
		tail = 0;
	++count;
	signal();
	unlock();
}

bool Buffer::put(void *dbuf, timeout_t timeout)
{
	bool rtn = true;
	struct timespec ts;

	if(timeout && timeout != Timer::inf)
		gettimeout(timeout, &ts);

	lock();
	while(count == limit && rtn) {
		if(timeout == Timer::inf)
			wait();
		else if(timeout)
			rtn = wait(&ts);
		else
			rtn = false;
	}
	if(rtn && count < limit) {
		memcpy(tail, dbuf, objsize);
		tail += objsize;
		if(tail >= (buf + size))
			tail = 0;
		++count;
		signal();
	}
	unlock();
	return rtn;
}


Buffer::operator bool()
{
	bool rtn = false;

	lock();
	if(buf && head != tail)
		rtn = true;
	unlock();
	return rtn;
}

bool Buffer::operator!()
{
	bool rtn = false;

	lock();
	if(!buf || head == tail)
		rtn = true;
	unlock();
	return rtn;
}

locked_release::locked_release(const locked_release &copy)
{
	object = copy.object;
	object->retain();
}

locked_release::locked_release()
{
	object = NULL;
}

locked_release::locked_release(LockedPointer &p)
{
	object = p.dup();
}

locked_release::~locked_release()
{
	release();
}

void locked_release::release(void)
{
	if(object)
		object->release();
	object = NULL;
}

locked_release &locked_release::operator=(LockedPointer &p)
{
	release();
	object = p.dup();
	return *this;
}

shared_release::shared_release(const shared_release &copy)
{
	ptr = copy.ptr;
}

shared_release::shared_release()
{
	ptr = NULL;
}

SharedObject *shared_release::get(void)
{
	if(ptr)
		return ptr->pointer;
	return NULL;
}

void SharedObject::commit(SharedPointer *pointer)
{
}

shared_release::shared_release(SharedPointer &p)
{
	ptr = &p;
	p.share(); // create rdlock
}

shared_release::~shared_release()
{
	release();
}

void shared_release::release(void)
{
	if(ptr)
		ptr->release();
	ptr = NULL;
}

shared_release &shared_release::operator=(SharedPointer &p)
{
	release();
	ptr = &p;
	p.share();
	return *this;
}

void Thread::init(void)
{
}



