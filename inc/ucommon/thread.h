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

/**
 * Thread classes and sychronization objects.
 * The theory behind ucommon thread classes is that they would be used
 * to create derived classes where thread-specific data can be stored as
 * member data of the derived class.  The run method is called when the
 * context is executed.  Since we use a pthread foundation, we support
 * both detached threads and joinable threads.  Objects based on detached
 * threads should be created with new, and will automatically delete when
 * the thread context exits.  Joinable threads will be joined with deleted.
 *
 * The theory behind ucommon sychronization objects is that all upper level
 * sychronization objects can be formed directly from a mutex and conditional.
 * This includes semaphores, barriers, rwlock, our own specialized conditional
 * lock, resource-bound locking, and recurive exclusive locks.  Using only
 * conditionals means we are not dependent on platform specific pthread
 * implimentations that may not impliment some of these, and hence improves
 * portability and consistency.  Given that our rwlocks are recursive access
 * locks, one can safely create read/write threading pairs where the read
 * threads need not worry about deadlocks and the writers need not either if
 * they only write-lock one instance at a time to change state.
 * @file ucommon/thread.h
 */

#ifndef _UCOMMON_THREAD_H_
#define	_UCOMMON_THREAD_H_

#ifndef _UCOMMON_ACCESS_H_
#include <ucommon/access.h>
#endif

#ifndef	_UCOMMON_TIMERS_H_
#include <ucommon/timers.h>
#endif

#ifndef _UCOMMON_MEMORY_H_
#include <ucommon/memory.h>
#endif

NAMESPACE_UCOMMON

class SharedPointer;

/**
 * The conditional is a common base for other thread synchronizing classes.
 * Many of the complex sychronization objects, including barriers, semaphores,
 * and various forms of read/write locks are all built from the conditional.
 * This assures that the minimum functionality to build higher order thread
 * synchronizing objects is a pure conditional, and removes dependencies on
 * what may be optional features or functions that may have different
 * behaviors on different pthread implimentations and platforms.
 * @author David Sugar <dyfet@gnutelephony.org>
 */
class __EXPORT Conditional 
{
private:
#ifdef	_MSWINDOWS_
	enum {SIGNAL = 0, BROADCAST = 1};
	HANDLE events[2];
	unsigned waiting;
	CRITICAL_SECTION mlock;
	CRITICAL_SECTION mutex;
#else
	class __LOCAL attribute
	{
	public:
		pthread_condattr_t attr;
		attribute();
	};

	__LOCAL static attribute attr;

	pthread_cond_t cond;
	pthread_mutex_t mutex;
#endif

protected:
	friend class TimedEvent;

	/**
	 * Convert a millisecond timeout into use for high resolution
	 * conditional timers.
	 * @param timeout to convert.
	 * @param hires timespec representation to fill.
	 */
	static void gettimeout(timeout_t timeout, struct timespec *hires);

	/**
	 * Conditional wait for signal on millisecond timeout.
	 * @param timeout in milliseconds.
	 * @return true if signalled, false if timer expired.
	 */
	bool wait(timeout_t timeout);

	/**
	 * Conditional wait for signal on timespec timeout.
	 * @param timeout as a high resolution timespec.
	 * @return true if signalled, false if timer expired.
	 */
	bool wait(struct timespec *timeout);

#ifdef	_MSWINDOWS_
	inline void lock(void)
		{EnterCriticalSection(&mutex);};

	inline void unlock(void)
		{LeaveCriticalSection(&mutex);};

	void wait(void);
	void signal(void);
	void broadcast(void);

#else
	/**
	 * Lock the conditional's supporting mutex.
	 */
	inline void lock(void)
		{pthread_mutex_lock(&mutex);};

	/**
	 * Unlock the conditional's supporting mutex.
	 */
	inline void unlock(void)
		{pthread_mutex_unlock(&mutex);};

	/**
	 * Wait (block) until signalled.
	 */
	inline void wait(void)
		{pthread_cond_wait(&cond, &mutex);};

	/**
	 * Signal the conditional to release one waiting thread.
	 */
	inline void signal(void)
		{pthread_cond_signal(&cond);};

	/**
	 * Signal the conditional to release all waiting threads.
	 */
	inline void broadcast(void)
		{pthread_cond_broadcast(&cond);};
#endif

	/**
	 * Initialize and construct conditional.
	 */
	Conditional();

	/**
	 * Destroy conditional, release any blocked threads.
	 */
	~Conditional();

public:
	/**
	 * Specify a maximum sharing (concurrency) limit.  This can be used
	 * to detect locking errors, such as when aquiring locks that are
	 * not released.
	 */
	static unsigned max_sharing;

#ifndef	_MSWINDOWS_
	/**
	 * Support function for getting conditional attributes for realtime
	 * scheduling.
	 * @return attributes to use for creating realtime conditionals.
	 */ 
	static inline pthread_condattr_t *initializer(void)
		{return &attr.attr;};
#endif

};

/**
 * Event notification to manage scheduled realtime threads.  The timer
 * is advanced to sleep threads which then wakeup either when the timer
 * has expired or they are notified through the signal handler.  This can
 * be used to schedule and signal one-time completion handlers or for time
 * synchronized events signaled by an asychrononous I/O or event source.
 * @author David Sugar <dyfet@gnutelephony.org>
 */
class __EXPORT TimedEvent : public Timer
{
private:
#ifdef _MSWINDOWS_
	HANDLE event;
	CRITICAL_SECTION mutex;
#else
	Conditional cond;
#endif

protected:
	/**
	 * Lock the object for wait or to manipulate derived data.  This is
	 * relevant to manipulations in a derived class.
	 */
	void lock(void);

	/**
	 * Release the object lock after waiting.  This is relevent to
	 * manipulations in a derived class.
	 */
	void release(void);

	/**
	 * Wait while locked.  This can be used in more complex derived
	 * objects where we are concerned with synchronized access between
	 * the signaling and event thread.  This can be used in place of
	 * wait, but lock and release methods must be used around it.
	 * @return true if time expired. 
	 */
	bool expire(void);

public:
	/**
	 * Create event handler and timer for timing of events.
	 */
	TimedEvent(void);

	/**
	 * Create event handler and timer set to trigger a timeout.
	 * @param timeout in milliseconds.
	 */
	TimedEvent(timeout_t timeout);

	/**
	 * Create event handler and timer set to trigger a timeout.
	 * @param timeout in seconds.
	 */
	TimedEvent(time_t timeout);

	/**
	 * Destroy timer and release pending events.
	 */
	~TimedEvent();

	/**
	 * Signal pending event.  Object may be locked or unlocked.  The
	 * signalling thread may choose to lock and check a condition in
	 * a derived class before signalling.
	 */
	void signal(void);

	/**
	 * Wait to be signalled or until timer expires.  This is for simple
	 * completion events.
	 * @return true if signaled, false if timeout.
	 */
	bool wait(void);
};

/**
 * Portable recursive exclusive lock.  This class is built from the
 * conditional and hence does not require support for non-standard and 
 * platform specific extensions to pthread mutex to support recrusive
 * style mutex locking.  The exclusive protocol is implimented to support
 * exclusive_lock referencing.
 */
class __EXPORT rexlock : private Conditional, public Exclusive
{
private:
	unsigned waiting;
	unsigned lockers;
	pthread_t locker;

	__LOCAL void Exlock(void);
	__LOCAL void Unlock(void);

public:
	/**
	 * Create rexlock.
	 */
	rexlock();

	/**
	 * Acquire or increase locking.
	 */
	void lock(void);

	/**
	 * Release or decrease locking.
	 */
	void release(void);

	/**
	 * Get the number of recursive locking levels.
	 * @return locking level.
	 */
	unsigned getLocking(void);

	/**
	 * Get the number of threads waiting on lock.
	 * @return wating thread count.
	 */
	unsigned getWaiting(void);

	/**
	 * Convenience method to lock a recursive lock.
	 * @param rex lock to lock.
	 */
	inline static void lock(rexlock &rex)
		{rex.lock();};

	/**
	 * Convenience method to unlock a recursive lock.
	 * @param rex lock to release.
	 */
	inline static void release(rexlock &rex)
		{rex.release();};
};

/**
 * A generic and portable implimentation of Read/Write locking.  This
 * class impliments classical read/write locking, including "timed" locks.
 * Support for scheduling threads to avoid writer starvation is also provided
 * for.  By building read/write locks from a conditional, we make them
 * available on pthread implimetations and other platforms which do not
 * normally include optional pthread rwlock's.  We also do not restrict
 * the number of threads that may use the lock.  Finally, both the exclusive 
 * and shared protocols are implimented to support exclusive_lock and
 * shared_lock referencing.
 * @author David Sugar <dyfet@gnutelephony.org>
 */
class __EXPORT rwlock : private Conditional, public Exclusive, public Shared
{
private:
	unsigned waiting;
	unsigned reading;
	unsigned pending;
	unsigned writers;
	pthread_t writer;

	__LOCAL void Exlock(void);
	__LOCAL void Shlock(void);
	__LOCAL void Unlock(void);

public:
	/**
	 * Create an instance of a rwlock.
	 */
	rwlock();

	/**
	 * Request modify (write) access through the lock.
	 * @param timeout in milliseconds to wait for lock.
	 * @return true if locked, false if timeout.
	 */
	bool modify(timeout_t timeout = Timer::inf);

	/**
	 * Request shared (read) access through the lock.
	 * @param timeout in milliseconds to wait for lock.
	 * @return true if locked, false if timeout.
	 */
	bool access(timeout_t timeout = Timer::inf);

	/**
	 * Release the lock.
	 */
	void release(void);

	/**
	 * Get the number of threads in shared access mode.
	 * @return number of accessing threads.
	 */
	unsigned getAccess(void);

	/**
	 * Get the number of threads waiting to modify the lock.
	 * @return number of pending write threads.
	 */
	unsigned getModify(void);

	/**
	 * Get the number of threads waiting to access after writer completes.
	 * @return number of waiting access threads.
	 */
	unsigned getWaiting(void);

	/**
	 * Convenience function to modify (write lock) a rwlock.
	 * @param lock to modify.
	 * @param timeout to wait for lock.
	 * @return true if successful, false if timeout.
	 */
	inline static bool modify(rwlock &lock, timeout_t timeout = Timer::inf)
		{return lock.modify(timeout);};

	/**
	 * Convenience function to access (read lock) a rwlock.
	 * @param lock to access.
	 * @param timeout to wait for lock.
	 * @return true if successful, false if timeout.
	 */
	inline static bool access(rwlock &lock, timeout_t timeout = Timer::inf)
		{return lock.access(timeout);};

	/**
	 * Convenience function to release a rwlock.
	 * @param lock to release.
	 */
	inline static void release(rwlock &lock)
		{lock.release();};
};

/**
 * Class for resource bound memory pools between threads.  This is used to 
 * support a memory pool allocation scheme where a pool of reusable objects 
 * may be allocated, and the pool renewed by releasing objects or back.
 * When the pool is used up, a pool consuming thread then must wait for
 * a resource to be freed by another consumer (or timeout).  This class is
 * not meant to be used directly, but rather to build the synchronizing
 * control between consumers which might be forced to wait for a resource.
 * @author David Sugar <dyfet@gnutelephony.org>
 */
class __EXPORT ReusableAllocator : protected Conditional
{
protected:
	ReusableObject *freelist;
	unsigned waiting;

	/**
	 * Initialize reusable allocator through a conditional.  Zero free list.
	 */
	ReusableAllocator();

	/**
	 * Get next reusable object in the pool.
	 * @param object from list.
	 * @return next object.
	 */ 
	inline ReusableObject *next(ReusableObject *object)
		{return object->getNext();};
	
	/**
	 * Release resuable object
	 * @param object being released.
	 */
	void release(ReusableObject *object);
};

/**
 * An optimized and convertable shared lock.  This is a form of read/write
 * lock that has been optimized, particularly for shared access.  Support
 * for scheduling access around writer starvation is also included.  The
 * other benefits over traditional read/write locks is that the code is
 * a little lighter, and read (shared) locks can be converted to exclusive
 * (write) locks to perform brief modify operations and then returned to read
 * locks, rather than having to release and re-aquire locks to change mode.
 * @author David Sugar <dyfet@gnutelephony.org>
 */
class __EXPORT ConditionalLock : protected Conditional, public Shared
{
private:
	unsigned pending, sharing, waiting;

	__LOCAL void Shlock(void);
	__LOCAL void Unlock(void);
	__LOCAL void Exclusive(void);
	__LOCAL void Share(void);

protected:
	/**
	 * This is used in place of access when one is not concerned with writer
	 * starvation.  It is faster, and may be safely used recursivily, such as
	 * to protect a method call which might be called by a larger protected
	 * method or might be exposed directly.
	 */
	void protect(void);

public:
	/**
	 * Construct conditional lock.
	 */
	ConditionalLock();

	/**
	 * Acquire write (exclusive modify) lock.
	 */
	void modify(void);

	/**
	 * Commit changes / release a modify lock.
	 */
	void commit(void);

	/**
	 * Acquire access (shared read) lock.
	 */
	void access(void);

	/**
	 * Release a shared lock.
	 */
	void release(void);

	/**
	 * Convert read lock into exclusive (write/modify) access.  Schedule
	 * when other readers sharing.
	 */
	void exclusive(void);

	/**
	 * Return an exclusive access lock back to share mode.
	 */
	void share(void);

	/**
	 * Get the number of threads reading (sharing) the lock.
	 */
	unsigned getReaders(void);

	/**
	 * Get the number of threads waiting to share the lock.
	 */
	unsigned getWaiters(void);

	/**
	 * Acquire/recursive read lock operator.
	 */
	inline void operator++()
		{protect();};

	/**
	 * Release read lock operator.
	 */
	inline void operator--()
		{release();};

	/**
	 * Convenience function to modify lock.
	 * @param lock to acquire in write exclusive mode.
	 */
	inline static void modify(ConditionalLock &lock)
		{lock.modify();};

	/**
	 * Convenience function to commit a modify lock.
	 * @param lock to commit.
	 */
	inline static void commit(ConditionalLock &lock)
		{lock.commit();};

	/**
	 * Convenience function to release a shared lock.
	 * @param lock to release.
	 */
	inline static void release(ConditionalLock &lock)
		{lock.release();};

	/**
	 * Convenience function to aqcuire a shared lock.
	 * @param lock to share.
	 */
	inline static void access(ConditionalLock &lock)
		{lock.access();};

	/**
	 * Convenience function to convert lock to exclusive mode.
	 * @param lock to convert.
	 */
	inline static void exclusive(ConditionalLock &lock)
		{lock.exclusive();};

	/**
	 * Convenience function to convert lock to shared access.
	 * @param lock to convert.
	 */
	inline static void share(ConditionalLock &lock)
		{lock.share();};
};	

/**
 * A portable implimentation of "barrier" thread sychronization.  A barrier
 * waits until a specified number of threads have all reached the barrier,
 * and then releases all the threads together.  This implimentation works
 * regardless of whether the thread library supports barriers since it is
 * built from conditional.  It also differs in that the number of threads 
 * required can be changed dynamically at runtime, unlike pthread barriers
 * which, when supported, have a fixed limit defined at creation time.  Since
 * we use conditionals, another feature we can add is optional support for a
 * wait with timeout.
 */
class __EXPORT barrier : private Conditional 
{
private:
	unsigned count;
	unsigned waits;

public:
	/**
	 * Construct a barrier with an initial size.
	 * @param count of threads required.
	 */
	barrier(unsigned count);

	/**
	 * Destroy barrier and release pending threads.
	 */
	~barrier();

	/**
	 * Dynamically alter the number of threads required.  If the size is
	 * set below the currently waiting threads, then the barrier releases.
	 * @param count of threads required.
	 */
	void set(unsigned count);

	/**
	 * Wait at the barrier until the count of threads waiting is reached.
	 */
	void wait(void);

	/**
	 * Wait at the barrier until either the count of threads waiting is
	 * reached or a timeout has occurred.
	 * @param timeout to wait in milliseconds.
	 * @return true if barrier reached, false if timer expired.
	 */
	bool wait(timeout_t timeout);

	/**
	 * Convenience function to wait at a barrier.
	 * @param sync object to wait at.
	 */
	inline static void wait(barrier &sync)
		{sync.wait();};

	/**
	 * Convenience function to wait at a barrier with a timeout.
	 * @param sync object to wait at.
	 * @param timeout to wait in milliseconds.
	 * @return false if timer expired.
	 */
	inline static bool wait(barrier &sync, timeout_t timeout)
		{return sync.wait(timeout);};


	/**
	 * Convenience function to set a barrier count.
	 * @param sync object to set.
	 * @param count of threads to set.
	 */
	inline static void set(barrier &sync, unsigned count)
		{sync.set(count);};
};

class __EXPORT semaphore : public Shared, private Conditional
{
private:
	unsigned count, waits, used;

	__LOCAL void Shlock(void);
	__LOCAL void Unlock(void);

public:
	semaphore(unsigned limit = 0);

	void request(unsigned size);
	bool request(unsigned size, timeout_t timeout);
	void wait(void);
	bool wait(timeout_t timeout);
	unsigned getCount(void);
	unsigned getUsed(void);
	void set(unsigned limit);
	void release(void);
	void release(unsigned size);

	inline static void wait(semaphore &s)
		{s.wait();};

	inline static bool wait(semaphore &s, timeout_t timeout)
		{return s.wait(timeout);};

	inline static void release(semaphore &s)
		{s.release();};
};

class __EXPORT mutex : public Exclusive
{
private:
	pthread_mutex_t mlock;

	__LOCAL void Exlock(void);
	__LOCAL void Unlock(void);
		
public:
	mutex();
	~mutex();

	inline void acquire(void)
		{pthread_mutex_lock(&mlock);};

	inline void lock(void)
		{pthread_mutex_lock(&mlock);};

	inline void unlock(void)
		{pthread_mutex_unlock(&mlock);};

	inline void release(void)
		{pthread_mutex_unlock(&mlock);};

	inline static void acquire(mutex &m)
		{pthread_mutex_lock(&m.mlock);};

	inline static void lock(mutex &m)
		{pthread_mutex_lock(&m.mlock);};

	inline static void unlock(mutex &m)
		{pthread_mutex_unlock(&m.mlock);};

	inline static void release(mutex &m)
		{pthread_mutex_unlock(&m.mlock);};

	inline static void acquire(pthread_mutex_t *lock)
		{pthread_mutex_lock(lock);};

	inline static void lock(pthread_mutex_t *lock)
		{pthread_mutex_lock(lock);};

	inline static void unlock(pthread_mutex_t *lock)
		{pthread_mutex_unlock(lock);};

	inline static void release(pthread_mutex_t *lock)
		{pthread_mutex_unlock(lock);};
};

class __EXPORT StepLock : public Exclusive, public Shared
{
private:
	pthread_mutex_t mlock;
	mutex *parent;
	bool stepping;

	__LOCAL void Exlock(void);
	__LOCAL void Shlock(void);
	__LOCAL void Unlock(void);

public:
	StepLock(mutex *base);
	~StepLock();

	void lock(void);
	void access(void);
	void release(void);
	
	inline static void lock(StepLock &sl)
		{sl.lock();};

	inline static void access(StepLock &sl)
		{sl.access();};

	inline static void release(StepLock &sl)
		{sl.release();};
};

class __EXPORT ConditionalIndex : public OrderedIndex, protected Conditional
{
public:
	ConditionalIndex();

protected:
	void lock_index(void);
	void unlock_index(void);
};

class __EXPORT LockedIndex : public OrderedIndex
{
public:
	LockedIndex();

private:
	pthread_mutex_t mutex;

protected:
	void lock_index(void);
	void unlock_index(void);
};

class __EXPORT LockedPointer
{
private:
	friend class locked_release;
	pthread_mutex_t mutex;
	Object *pointer;

protected:
	LockedPointer();

	void replace(Object *ptr);
	Object *dup(void);

	LockedPointer &operator=(Object *o);
};

class __EXPORT SharedObject
{
protected:
	friend class SharedPointer;
	
	virtual void commit(SharedPointer *pointer);

public:
	virtual ~SharedObject();
};

class __EXPORT SharedPointer : protected ConditionalLock
{
private:
	friend class shared_release;
	SharedObject *pointer;

protected:
	SharedPointer();
	~SharedPointer();

	void replace(SharedObject *ptr);
	SharedObject *share(void);
};

class __EXPORT Thread
{
protected:
	pthread_t tid;
	size_t stack;

	Thread(size_t stack = 0);

public:
	static void yield(void);

	static void sleep(timeout_t timeout);

	virtual void run(void) = 0;
	
	virtual ~Thread();

	virtual void exit(void);

	static void init(void);

#if defined(_MSWINDOWS_)
	static void lowerPriority(void);
	static void raisePriority(unsigned pri);
#elif _POSIX_PRIORITY_SCHEDULING > 0
	static void raisePriority(unsigned pri, struct sched_param *sparam = NULL);
	static void resetPriority(struct sched_param *sparam);
	static void lowerPriority(void);
#else
	inline static void lowerPriority(void) {};
	inline static void raisePriority(unsigned pri) {};
#endif

	inline static bool equal(pthread_t t1, pthread_t t2)
		{return pthread_equal(t1, t2);};
};

class __EXPORT JoinableThread : protected Thread
{
private:
#ifdef	_MSWINDOWS_
	HANDLE joining;
#else
	volatile bool running;
#endif

protected:
	JoinableThread(size_t size = 0);
	virtual ~JoinableThread();
	void join(void);

public:
#ifdef	_MSWINDOWS_
	inline bool isRunning(void)
		{joining != INVALID_HANDLE_VALUE;};
#else
	inline bool isRunning(void)
		{return running;};
#endif

	inline bool isDetached(void)
		{return false;};

	void start(void);
};

class __EXPORT DetachedThread : protected Thread
{
protected:
	DetachedThread(size_t size = 0);
	~DetachedThread();

	void exit(void);

public:
	void start(void);

	inline bool isDetached(void)
		{return true;};

	inline bool isRunning(void)
		{return true;};
};

class __EXPORT PooledThread : public DetachedThread, protected Conditional
{
protected:
	unsigned volatile poolsize, poolused, waits;

	PooledThread(size_t stack = 0);
	void suspend(void);
	bool suspend(timeout_t timeout);
	void sync(void);
	
	void exit(void);

public:
	unsigned wakeup(unsigned limit = 1);
	void start(void);
	void start(unsigned count);
};
	
class __EXPORT queue : protected OrderedIndex, protected Conditional
{
private:
	mempager *pager;
	LinkedObject *freelist;
	size_t used;

	class __LOCAL member : public OrderedObject
	{
	public:
		member(queue *q, Object *obj);
		Object *object;
	};

protected:
	size_t limit;

public:
	queue(mempager *mem, size_t size);

	bool remove(Object *obj);
	bool post(Object *obj, timeout_t timeout = 0);
	Object *fifo(timeout_t timeout = 0);
	Object *lifo(timeout_t timeout = 0);
	size_t getCount(void);

	static bool remove(queue &q, Object *obj)
		{return q.remove(obj);};

	static bool post(queue &q, Object *obj, timeout_t timeout = 0)
		{return q.post(obj, timeout);};

	static Object *fifo(queue &q, timeout_t timeout = 0)
		{return q.fifo(timeout);};

	static Object *lifo(queue &q, timeout_t timeout = 0)
		{return q.lifo(timeout);};

	static size_t count(queue &q)
		{return q.getCount();};
};

class __EXPORT stack : protected Conditional
{
private:
	LinkedObject *freelist, *usedlist;
	mempager *pager;
	size_t used;

	class __LOCAL member : public LinkedObject
	{
	public:
		member(stack *s, Object *obj);
		Object *object;
	};

	friend class member;

protected:
	size_t limit;

public:
	stack(mempager *pager, size_t size);

	bool remove(Object *obj);
	bool push(Object *obj, timeout_t timeout = 0);
	Object *pull(timeout_t timeout = 0);
	size_t getCount(void);

	static inline bool remove(stack &stack, Object *obj)
		{return stack.remove(obj);};

	static inline bool push(stack &stack, Object *obj, timeout_t timeout = 0)
		{return stack.push(obj, timeout);};

	static inline Object *pull(stack &stack, timeout_t timeout = 0)
		{return stack.pull(timeout);};  

	static inline size_t count(stack &stack)
		{return stack.getCount();};
};

class __EXPORT Buffer : protected Conditional
{
private:
	size_t size, objsize;
	caddr_t buf, head, tail;
	unsigned count, limit;

public:
	Buffer(size_t objsize, size_t count);
	virtual ~Buffer();

	unsigned getSize(void);
	unsigned getCount(void);

	void *get(timeout_t timeout);
	void *get(void);
	void put(void *data);
	bool put(void *data, timeout_t timeout);
	void release(void);	// release lock from get

	operator bool();

	virtual bool operator!();
};

class __EXPORT locked_release
{
protected:
	Object *object;

	locked_release();
	locked_release(const locked_release &copy);

public:
	locked_release(LockedPointer &p);
	~locked_release();

	void release(void);

	locked_release &operator=(LockedPointer &p);
};

class __EXPORT shared_release
{
protected:
	SharedPointer *ptr;

	shared_release();
	shared_release(const shared_release &copy);

public:
	shared_release(SharedPointer &p);
	~shared_release();

	void release(void);

	SharedObject *get(void);

	shared_release &operator=(SharedPointer &p);
};

template<class T, mempager *P = NULL, size_t L = 0>
class queueof : public queue
{
public:
	inline queueof() : queue(P, L) {};

	inline bool remove(T *obj)
		{return queue::remove(obj);};	

	inline bool post(T *obj, timeout_t timeout = 0)
		{return queue::post(obj);};

	inline T *fifo(timeout_t timeout = 0)
		{return static_cast<T *>(queue::fifo(timeout));};

    inline T *lifo(timeout_t timeout = 0)
        {return static_cast<T *>(queue::lifo(timeout));};
};

template<class T, mempager *P = NULL, size_t L = 0>
class stackof : public stack
{
public:
	inline stackof() : stack(P, L) {};

	inline bool remove(T *obj)
		{return stack::remove(obj);};	

	inline bool push(T *obj, timeout_t timeout = 0)
		{return stack::push(obj);};

	inline T *pull(timeout_t timeout = 0)
		{return static_cast<T *>(stack::pull(timeout));};
};

template<class T>
class bufferof : public Buffer
{
public:
	inline bufferof(unsigned count) :
		Buffer(sizeof(T), count) {};

	inline T *get(void)
		{return static_cast<T*>(get());};

	inline T *get(timeout_t timeout)
		{return static_cast<T*>(get(timeout));};

	inline void put(T *obj)
		{put(obj);};

	inline bool put(T *obj, timeout_t timeout)
		{return put(obj, timeout);};
};
 
template<class T>
class shared_pointer : public SharedPointer
{
public:
	inline shared_pointer() : SharedPointer() {};

	inline shared_pointer(void *p) : SharedPointer(p) {};

	inline const T *dup(void)
		{return static_cast<const T*>(SharedPointer::share());};

	inline void replace(T *p)
		{SharedPointer::replace(p);};
};	

template<class T>
class locked_pointer : public LockedPointer
{
public:
	inline locked_pointer() : LockedPointer() {};

	inline T* dup(void)
		{return static_cast<T *>(LockedPointer::dup());};

	inline void replace(T *p)
		{LockedPointer::replace(p);};

	inline locked_pointer<T>& operator=(T *obj)
		{LockedPointer::operator=(obj); return *this;};

	inline T *operator*()
		{return dup();};
};

template<class T>
class locked_instance : public locked_release
{
public:
    inline locked_instance() : locked_release() {};

    inline locked_instance(locked_pointer<T> &p) : locked_release(p) {};

    inline T& operator*() const
        {return *(static_cast<T *>(object));};

    inline T* operator->() const
        {return static_cast<T*>(object);};

    inline T* get(void) const
        {return static_cast<T*>(object);};
};

template<class T>
class shared_instance : public shared_release
{
public:
	inline shared_instance() : shared_release() {};

	inline shared_instance(shared_pointer<T> &p) : shared_release(p) {};

	inline const T& operator*() const
		{return *(static_cast<const T *>(ptr->pointer));};

	inline const T* operator->() const
		{return static_cast<const T*>(ptr->pointer);};

	inline const T* get(void) const
		{return static_cast<const T*>(ptr->pointer);};
};

inline void start(JoinableThread *th)
	{th->start();};

inline void start(DetachedThread *th)
    {th->start();};

typedef	StepLock steplock_t;
typedef ConditionalLock condlock_t;
typedef TimedEvent timedevent_t;
typedef	mutex mutex_t;
typedef rwlock rwlock_t;
typedef	rexlock rexlock_t;
typedef semaphore semaphore_t;
typedef	barrier barrier_t;
typedef stack stack_t;
typedef	queue queue_t;

inline void wait(barrier_t &b)
	{b.wait();};

inline void wait(semaphore_t &s, timeout_t timeout = Timer::inf)
	{s.wait(timeout);};

inline void acquire(mutex_t &ml)
	{ml.lock();};

inline void release(mutex_t &ml)
	{ml.release();};

inline void lock(steplock_t &sl)
	{sl.lock();};

inline void access(steplock_t &sl)
	{sl.access();};

inline void release(steplock_t &sl)
	{sl.release();};

inline void exclusive(condlock_t &cl)
	{cl.exclusive();};

inline void share(condlock_t &cl)
	{cl.share();};

inline void modify(condlock_t &cl)
	{cl.modify();};

inline void commit(condlock_t &cl)
	{cl.commit();};

inline void access(condlock_t &cl)
	{cl.access();};

inline void release(condlock_t &cl)
	{cl.release();};

inline bool modify(rwlock_t &rw, timeout_t timeout = Timer::inf)
	{return rw.modify(timeout);};

inline bool access(rwlock_t &rw, timeout_t timeout = Timer::inf)
	{return rw.access(timeout);};

inline void release(rwlock_t &rw)
	{rw.release();};

inline void lock(rexlock_t &rex)
	{rex.lock();};

inline void release(rexlock_t &rex)
	{rex.release();};

inline void push(stack_t &s, Object *obj)
	{s.push(obj);};

inline Object *pull(stack_t &s, timeout_t timeout = Timer::inf)
	{return s.pull(timeout);};

inline void remove(stack_t &s, Object *obj)
	{s.remove(obj);};

inline void push(queue_t &s, Object *obj)
	{s.post(obj);};

inline Object *pull(queue_t &s, timeout_t timeout = Timer::inf)
	{return s.fifo(timeout);};

inline void remove(queue_t &s, Object *obj)
	{s.remove(obj);};

END_NAMESPACE

#if _POSIX_PRIORITY_SCHEDULING > 0

#define RAISE_PRIORITY(x) \
	do { struct sched_param __sparam_; \
		Thread::raisePriority(&__sparam__, x);

#define	END_PRIORITY \
		Thread::resetPriority(&__sparam); \
	} while(0);

#else
#define	SET_PRIORITY(x)
#define	END_PRIORITY
#endif

#define	ENTER_EXCLUSIVE	\
	do { static pthread_mutex_t __sync__ = PTHREAD_MUTEX_INITIALIZER; \
		pthread_mutex_lock(&__sync__);

#define LEAVE_EXCLUSIVE \
	pthread_mutex_unlock(&__sync__);} while(0);

#endif
