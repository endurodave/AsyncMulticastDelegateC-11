#ifndef _DELEGATE_ASYNC_WAIT_H
#define _DELEGATE_ASYNC_WAIT_H

// DelegateAsyncWait.h
// @see https://www.codeproject.com/Articles/1160934/Asynchronous-Multicast-Delegates-in-Cplusplus
// David Lafreniere, Oct 2022.

#include "Delegate.h"
#include "DelegateThread.h"
#include "DelegateInvoker.h"
#include "Semaphore.h"

/// @brief Asynchronous member delegate that invokes the target function on the specified thread of control
/// and waits for the function to be executed or a timeout occurs. Use IsSuccess() to determine if asynchronous 
/// call succeeded.

namespace DelegateLib {

const int WAIT_INFINITE = -1;

// Declare DelegateMemberAsyncWaitBase as a class template. It will be specialized for all number of arguments.
template <typename Signature>
class DelegateMemberAsyncWaitBase;

// Declare DelegateMemberAsyncWait as a class template. It will be specialized for all number of arguments.
template <typename Signature>
class DelegateMemberAsyncWait;

// N=0 abstract base
template <class TClass, class RetType> 
class DelegateMemberAsyncWaitBase<RetType(TClass(void))> : public DelegateMember<RetType(TClass(void))>, public IDelegateInvoker {
public:
	typedef TClass* ObjectPtr;
	typedef RetType (TClass::*MemberFunc)();
	typedef RetType (TClass::*ConstMemberFunc)() const;
	
	// Contructors take a class instance, member function, and delegate thread
	DelegateMemberAsyncWaitBase(ObjectPtr object, MemberFunc func, DelegateThread* thread, int timeout) : m_success(false), m_timeout(timeout), m_refCnt(0) {
		Bind(object, func, thread); 
		LockGuard::Create(&m_lock);
	}
	DelegateMemberAsyncWaitBase(ObjectPtr object, ConstMemberFunc func, DelegateThread* thread, int timeout) : m_success(false), m_timeout(timeout), m_refCnt(0) {
		Bind(object, func, thread);
		LockGuard::Create(&m_lock);
	}
	DelegateMemberAsyncWaitBase(const DelegateMemberAsyncWaitBase& rhs) : DelegateMember<RetType(TClass(void))>(rhs), m_refCnt(0) {
		LockGuard::Create(&m_lock);
		Swap(rhs);
	}
	DelegateMemberAsyncWaitBase() : m_thread(0), m_success(false), m_timeout(0), m_refCnt(0) { LockGuard::Create(&m_lock); }
	virtual ~DelegateMemberAsyncWaitBase() { LockGuard::Destroy(&m_lock); }

	/// Bind a member function to a delegate. 
	void Bind(ObjectPtr object, MemberFunc func, DelegateThread* thread) {
		m_thread = thread; 
		DelegateMember<RetType(TClass(void))>::Bind(object, func); }

	/// Bind a const member function to a delegate. 
	void Bind(ObjectPtr object, ConstMemberFunc func, DelegateThread* thread)	{
		m_thread = thread;
		DelegateMember<RetType(TClass(void))>::Bind(object, func); }

	virtual bool operator==(const DelegateBase& rhs) const 	{
		const DelegateMemberAsyncWaitBase<RetType(TClass(void))>* derivedRhs = dynamic_cast<const DelegateMemberAsyncWaitBase<RetType(TClass(void))>*>(&rhs);
		return derivedRhs &&
			m_thread == derivedRhs->m_thread && 
			DelegateMember<RetType(TClass(void))>::operator==(rhs); }

	DelegateMemberAsyncWaitBase& operator=(const DelegateMemberAsyncWaitBase& rhs) {
		if (&rhs != this) {
			DelegateMember<RetType(TClass(void))>::operator=(rhs);
			Swap(rhs);
		}
		return *this;
	}

	/// Returns true if asynchronous function successfully invoked on target thread
	bool IsSuccess() { return m_success; }	

protected:
	DelegateThread* m_thread;		// Target thread to invoke the delegate function
	bool m_success;					// Set to true if async function succeeds
	int m_timeout;					// Time in mS to wait for async function to invoke
	Semaphore m_sema;				// Semaphore to signal waiting thread
	LOCK m_lock;					// Lock to synchronize threads
	int m_refCnt;					// Ref count to determine when to delete objects

private:
	void Swap(const DelegateMemberAsyncWaitBase& s) {
		m_thread = s.m_thread;
		m_timeout = s.m_timeout;
		m_success = s.m_success;
	}
};

// N=0 with return type
template <class TClass, class RetType>
class DelegateMemberAsyncWait<RetType(TClass(void))> : public DelegateMemberAsyncWaitBase<RetType(TClass(void))> {
public:
	typedef TClass* ObjectPtr;
	typedef RetType (TClass::*MemberFunc)();
	typedef RetType (TClass::*ConstMemberFunc)() const;
	
	DelegateMemberAsyncWait(ObjectPtr object, MemberFunc func, DelegateThread* thread, int timeout) : 
		DelegateMemberAsyncWaitBase<RetType(TClass(void))>(object, func, thread, timeout) { }
	DelegateMemberAsyncWait(ObjectPtr object, ConstMemberFunc func, DelegateThread* thread, int timeout) : 
		DelegateMemberAsyncWaitBase<RetType(TClass(void))>(object, func, thread, timeout) { }
	DelegateMemberAsyncWait() { }
	virtual DelegateMemberAsyncWait<RetType(TClass(void))>* Clone() const {	
		return new DelegateMemberAsyncWait<RetType(TClass(void))>(*this); }
	
	/// Invoke delegate function asynchronously
	virtual RetType operator()() {
		if (this->m_thread == 0)
			return DelegateMemberAsyncWaitBase<RetType(TClass(void))>::operator()();
		else {
			// Create a clone instance of this delegate 
			auto delegate = Clone();
			delegate->m_refCnt = 2;
			delegate->m_sema.Create();
			delegate->m_sema.Reset();

			// Create a new message instance 
			DelegateMsgBase* msg = new DelegateMsgBase(delegate);

			// Dispatch message onto the callback destination thread. DelegateInvoke()
			// will be called by the target thread. 
			this->m_thread->DispatchDelegate(msg);

			// Wait for target thread to execute the delegate function
			if ((this->m_success = delegate->m_sema.Wait(this->m_timeout)))
				m_retVal = delegate->m_retVal;

			bool deleteData = false;
			{
				LockGuard lockGuard(&delegate->m_lock);
				if (--delegate->m_refCnt == 0)
					deleteData = true;
			}
			if (deleteData) {
				delete msg;
				delete delegate;
			}
			return m_retVal;
		}
	}

	/// Called by the target thread to invoke the delegate function 
	virtual void DelegateInvoke(DelegateMsgBase** msg) {
		bool deleteData = false;
		{
			LockGuard lockGuard(&this->m_lock);
			if (this->m_refCnt == 2) {
				// Invoke the delegate function then signal the waiting thread
				m_retVal = DelegateMemberAsyncWaitBase<RetType(TClass(void))>::operator()();
				this->m_sema.Signal();
			}

			// If waiting thread is no longer waiting then delete heap data
			if (--this->m_refCnt == 0)
				deleteData = true;
		}
		if (deleteData) {
			delete *msg;
			*msg = 0;
			delete this;
		}
	}

	DelegateMemberAsyncWait& operator=(const DelegateMemberAsyncWait& rhs) {
		DelegateMemberAsyncWaitBase<RetType(TClass(void))>::operator=(rhs);
		return *this;
	}

	RetType GetRetVal() { return m_retVal; }
private:
	RetType m_retVal;				// The delegate return value
};

// N=0 void return type
template <class TClass>
class DelegateMemberAsyncWait<void(TClass(void))> : public DelegateMemberAsyncWaitBase<void(TClass(void))> {
public:
	typedef TClass* ObjectPtr;
	typedef void (TClass::*MemberFunc)();
	typedef void (TClass::*ConstMemberFunc)() const;
	
	DelegateMemberAsyncWait(ObjectPtr object, MemberFunc func, DelegateThread* thread, int timeout) :
		DelegateMemberAsyncWaitBase<void(TClass(void))>(object, func, thread, timeout) { }
	DelegateMemberAsyncWait(ObjectPtr object, ConstMemberFunc func, DelegateThread* thread, int timeout) :
		DelegateMemberAsyncWaitBase<void(TClass(void))>(object, func, thread, timeout) { }
	DelegateMemberAsyncWait() { }
	virtual DelegateMemberAsyncWait<void(TClass(void))>* Clone() const {
		return new DelegateMemberAsyncWait<void(TClass(void))>(*this);	}

	/// Invoke delegate function asynchronously
	virtual void operator()() {
		if (this->m_thread == 0)
			DelegateMemberAsyncWaitBase<void(TClass(void))>::operator()();
		else {
			// Create a clone instance of this delegate 
			auto delegate = Clone();
			delegate->m_refCnt = 2;
			delegate->m_sema.Create();
			delegate->m_sema.Reset();

			// Create a new message instance 
			DelegateMsgBase* msg = new DelegateMsgBase(delegate);

			// Dispatch message onto the callback destination thread. DelegateInvoke()
			// will be called by the target thread. 
			this->m_thread->DispatchDelegate(msg);

			// Wait for target thread to execute the delegate function
			if ((this->m_success = delegate->m_sema.Wait(this->m_timeout))) {
				// No return or param arguments
			}

			bool deleteData = false;
			{
				LockGuard lockGuard(&delegate->m_lock);
				if (--delegate->m_refCnt == 0)
					deleteData = true;
			}
			if (deleteData) {
				delete msg;
				delete delegate;
			}
		}
	}

	/// Called by the target thread to invoke the delegate function 
	virtual void DelegateInvoke(DelegateMsgBase** msg) {
		bool deleteData = false;
		{
			LockGuard lockGuard(&this->m_lock);
			if (this->m_refCnt == 2) {
				// Invoke the delegate function then signal the waiting thread
				DelegateMemberAsyncWaitBase<void(TClass(void))>::operator()();
				this->m_sema.Signal();
			}

			// If waiting thread is no longer waiting then delete heap data
			if (--this->m_refCnt == 0)
				deleteData = true;
		}
		if (deleteData) {
			delete *msg;
			*msg = 0;
			delete this;
		}
	}

	DelegateMemberAsyncWait& operator=(const DelegateMemberAsyncWait& rhs) {
		DelegateMemberAsyncWaitBase<void(TClass(void))>::operator=(rhs);
		return *this;
	}
};

// N=1 abstract base
template <class TClass, class RetType, class Param1>
class DelegateMemberAsyncWaitBase<RetType(TClass(Param1))> : public DelegateMember<RetType(TClass(Param1))>, public IDelegateInvoker {
public:
	typedef TClass* ObjectPtr;
	typedef RetType (TClass::*MemberFunc)(Param1);
	typedef RetType (TClass::*ConstMemberFunc)(Param1) const;
	
	// Contructors take a class instance, member function, and delegate thread
	DelegateMemberAsyncWaitBase(ObjectPtr object, MemberFunc func, DelegateThread* thread, int timeout) : m_success(false), m_timeout(timeout), m_refCnt(0) {
		Bind(object, func, thread);
		LockGuard::Create(&m_lock);
	}
	DelegateMemberAsyncWaitBase(ObjectPtr object, ConstMemberFunc func, DelegateThread* thread, int timeout) : m_success(false), m_timeout(timeout), m_refCnt(0) {
		Bind(object, func, thread);
		LockGuard::Create(&m_lock);
	}
	DelegateMemberAsyncWaitBase(const DelegateMemberAsyncWaitBase& rhs) : DelegateMember<RetType(TClass(Param1))>(rhs), m_refCnt(0) {
		LockGuard::Create(&m_lock);
		Swap(rhs);
	}
	DelegateMemberAsyncWaitBase() : m_thread(0), m_success(false), m_timeout(0), m_refCnt(0) { LockGuard::Create(&m_lock); }
	virtual ~DelegateMemberAsyncWaitBase() { LockGuard::Destroy(&m_lock); }

	/// Bind a member function to a delegate. 
	void Bind(ObjectPtr object, MemberFunc func, DelegateThread* thread) {
		m_thread = thread;
		DelegateMember<RetType(TClass(Param1))>::Bind(object, func);
	}

	/// Bind a const member function to a delegate. 
	void Bind(ObjectPtr object, ConstMemberFunc func, DelegateThread* thread) {
		m_thread = thread;
		DelegateMember<RetType(TClass(Param1))>::Bind(object, func);
	}

	virtual bool operator==(const DelegateBase& rhs) const {
		const DelegateMemberAsyncWaitBase<RetType(TClass(Param1))>* derivedRhs = dynamic_cast<const DelegateMemberAsyncWaitBase<RetType(TClass(Param1))>*>(&rhs);
		return derivedRhs &&
			m_thread == derivedRhs->m_thread &&
			DelegateMember<RetType(TClass(Param1))>::operator==(rhs);
	}

	DelegateMemberAsyncWaitBase& operator=(const DelegateMemberAsyncWaitBase& rhs) {
		if (&rhs != this) {
			DelegateMember<RetType(TClass(Param1))>::operator=(rhs);
			Swap(rhs);
		}
		return *this;
	}

	/// Returns true if asynchronous function successfully invoked on target thread
	bool IsSuccess() { return m_success; }

protected:
	DelegateThread* m_thread;		// Target thread to invoke the delegate function
	bool m_success;					// Set to true if async function succeeds
	int m_timeout;					// Time in mS to wait for async function to invoke
	Semaphore m_sema;				// Semaphore to signal waiting thread
	LOCK m_lock;					// Lock to synchronize threads
	int m_refCnt;					// Ref count to determine when to delete objects

private:
	void Swap(const DelegateMemberAsyncWaitBase& s) {
		m_thread = s.m_thread;
		m_timeout = s.m_timeout;
		m_success = s.m_success;
	}
};

// N=1 with return type 
template <class TClass, class RetType, class Param1>
class DelegateMemberAsyncWait<RetType(TClass(Param1))> : public DelegateMemberAsyncWaitBase<RetType(TClass(Param1))> {
public:
	typedef TClass* ObjectPtr;
	typedef RetType (TClass::*MemberFunc)(Param1);
	typedef RetType (TClass::*ConstMemberFunc)(Param1) const;
	
	DelegateMemberAsyncWait(ObjectPtr object, MemberFunc func, DelegateThread* thread, int timeout) :
		DelegateMemberAsyncWaitBase<RetType(TClass(Param1))>(object, func, thread, timeout) { }
	DelegateMemberAsyncWait<RetType(TClass(Param1))>(ObjectPtr object, ConstMemberFunc func, DelegateThread* thread, int timeout) :
		DelegateMemberAsyncWaitBase<RetType(TClass(Param1))>(object, func, thread, timeout) { }
	DelegateMemberAsyncWait() { }
	virtual DelegateMemberAsyncWait<RetType(TClass(Param1))>* Clone() const {
		return new DelegateMemberAsyncWait<RetType(TClass(Param1))>(*this);
	}

	/// Invoke delegate function asynchronously
	virtual RetType operator()(Param1 p1) {
		if (this->m_thread == 0)
			return DelegateMemberAsyncWaitBase<RetType(TClass(Param1))>::operator()(p1);
		else {
			// Create a clone instance of this delegate 
			auto delegate = Clone();
			delegate->m_refCnt = 2;
			delegate->m_sema.Create();
			delegate->m_sema.Reset();

			// Create a new message instance 
			DelegateMsg1<Param1>* msg = new DelegateMsg1<Param1>(delegate, p1);

			// Dispatch message onto the callback destination thread. DelegateInvoke()
			// will be called by the target thread. 
			this->m_thread->DispatchDelegate(msg);

			// Wait for target thread to execute the delegate function
			if ((this->m_success = delegate->m_sema.Wait(this->m_timeout)))
				m_retVal = delegate->m_retVal;

			bool deleteData = false;
			{
				LockGuard lockGuard(&delegate->m_lock);
				if (--delegate->m_refCnt == 0)
					deleteData = true;
			}
			if (deleteData) {
				delete msg;
				delete delegate;
			}
			return m_retVal;
		}
	}

	/// Called by the target thread to invoke the delegate function 
	virtual void DelegateInvoke(DelegateMsgBase** msg) {
		bool deleteData = false;
		{
			// Typecast the base pointer to back to the templatized instance
			DelegateMsg1<Param1>* delegateMsg = static_cast<DelegateMsg1<Param1>*>(*msg);

			// Get the function parameter data
			Param1 param1 = delegateMsg->GetParam1();

			LockGuard lockGuard(&this->m_lock);
			if (this->m_refCnt == 2) {
				// Invoke the delegate function then signal the waiting thread
				m_retVal = DelegateMemberAsyncWaitBase<RetType(TClass(Param1))>::operator()(param1);
				this->m_sema.Signal();
			}

			// If waiting thread is no longer waiting then delete heap data
			if (--this->m_refCnt == 0)
				deleteData = true;
		}
		if (deleteData) {
			delete *msg;
			*msg = 0;
			delete this;
		}
	}

	DelegateMemberAsyncWait& operator=(const DelegateMemberAsyncWait& rhs) {
		DelegateMemberAsyncWaitBase<RetType(TClass(Param1))>::operator=(rhs);
		return *this;
	}

	RetType GetRetVal() { return m_retVal; }
private:
	RetType m_retVal;				// The delegate return value
};

// N=1 void return type
template <class TClass, class Param1>
class DelegateMemberAsyncWait<void(TClass(Param1))> : public DelegateMemberAsyncWaitBase<void(TClass(Param1))> {
public:
	typedef TClass* ObjectPtr;
	typedef void (TClass::*MemberFunc)(Param1);
	typedef void (TClass::*ConstMemberFunc)(Param1) const;
	
	DelegateMemberAsyncWait(ObjectPtr object, MemberFunc func, DelegateThread* thread, int timeout) :
		DelegateMemberAsyncWaitBase<void(TClass(Param1))>(object, func, thread, timeout) { }
	DelegateMemberAsyncWait(ObjectPtr object, ConstMemberFunc func, DelegateThread* thread, int timeout) :
		DelegateMemberAsyncWaitBase<void(TClass(Param1))>(object, func, thread, timeout) { }
	DelegateMemberAsyncWait() { }
	virtual DelegateMemberAsyncWait<void(TClass(Param1))>* Clone() const {
		return new DelegateMemberAsyncWait<void(TClass(Param1))>(*this);
	}

	/// Invoke delegate function asynchronously
	virtual void operator()(Param1 p1) {
		if (this->m_thread == 0)
			DelegateMemberAsyncWaitBase<void(TClass(Param1))>::operator()(p1);
		else {
			// Create a clone instance of this delegate 
			auto delegate = Clone();
			delegate->m_refCnt = 2;
			delegate->m_sema.Create();
			delegate->m_sema.Reset();

			// Create a new message instance 
			DelegateMsg1<Param1>* msg = new DelegateMsg1<Param1>(delegate, p1);

			// Dispatch message onto the callback destination thread. DelegateInvoke()
			// will be called by the target thread. 
			this->m_thread->DispatchDelegate(msg);

			// Wait for target thread to execute the delegate function
			this->m_success = delegate->m_sema.Wait(this->m_timeout);

			bool deleteData = false;
			{
				LockGuard lockGuard(&delegate->m_lock);
				if (--delegate->m_refCnt == 0)
					deleteData = true;
			}
			if (deleteData) {
				delete msg;
				delete delegate;
			}
		}
	}

	/// Called by the target thread to invoke the delegate function 
	virtual void DelegateInvoke(DelegateMsgBase** msg) {
		bool deleteData = false;
		{
			// Typecast the base pointer to back to the templatized instance
			DelegateMsg1<Param1>* delegateMsg = static_cast<DelegateMsg1<Param1>*>(*msg);

			// Get the function parameter data
			Param1 param1 = delegateMsg->GetParam1();

			LockGuard lockGuard(&this->m_lock);
			if (this->m_refCnt == 2) {
				// Invoke the delegate function then signal the waiting thread
				DelegateMemberAsyncWaitBase<void(TClass(Param1))>::operator()(param1);
				this->m_sema.Signal();
			}

			// If waiting thread is no longer waiting then delete heap data
			if (--this->m_refCnt == 0)
				deleteData = true;
		}
		if (deleteData) {
			delete *msg;
			*msg = 0;
			delete this;
		}
	}

	DelegateMemberAsyncWait& operator=(const DelegateMemberAsyncWait& rhs) {
		DelegateMemberAsyncWaitBase<void(TClass(Param1))>::operator=(rhs);
		return *this;
	}
};

// N=2 abstract base
template <class TClass, class RetType, class Param1, class Param2>
class DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2))> : public DelegateMember<RetType(TClass(Param1, Param2))>, public IDelegateInvoker {
public:
	typedef TClass* ObjectPtr;
	typedef RetType (TClass::*MemberFunc)(Param1, Param2);
	typedef RetType (TClass::*ConstMemberFunc)(Param1, Param2) const;
	
	// Contructors take a class instance, member function, and delegate thread
	DelegateMemberAsyncWaitBase(ObjectPtr object, MemberFunc func, DelegateThread* thread, int timeout) : m_success(false), m_timeout(timeout), m_refCnt(0) {
		Bind(object, func, thread);
		LockGuard::Create(&m_lock);
	}
	DelegateMemberAsyncWaitBase(ObjectPtr object, ConstMemberFunc func, DelegateThread* thread, int timeout) : m_success(false), m_timeout(timeout), m_refCnt(0) {
		Bind(object, func, thread);
		LockGuard::Create(&m_lock);
	}
	DelegateMemberAsyncWaitBase(const DelegateMemberAsyncWaitBase& rhs) : DelegateMember<RetType(TClass(Param1, Param2))>(rhs), m_refCnt(0) {
		LockGuard::Create(&m_lock);
		Swap(rhs);
	}
	DelegateMemberAsyncWaitBase() : m_thread(0), m_success(false), m_timeout(0), m_refCnt(0) { LockGuard::Create(&m_lock); }
	virtual ~DelegateMemberAsyncWaitBase() { LockGuard::Destroy(&m_lock); }

	/// Bind a member function to a delegate. 
	void Bind(ObjectPtr object, MemberFunc func, DelegateThread* thread) {
		m_thread = thread;
		DelegateMember<RetType(TClass(Param1, Param2))>::Bind(object, func);
	}

	/// Bind a const member function to a delegate. 
	void Bind(ObjectPtr object, ConstMemberFunc func, DelegateThread* thread) {
		m_thread = thread;
		DelegateMember<RetType(TClass(Param1, Param2))>::Bind(object, func);
	}

	virtual bool operator==(const DelegateBase& rhs) const {
		const DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2))>* derivedRhs = dynamic_cast<const DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2))>*>(&rhs);
		return derivedRhs &&
			m_thread == derivedRhs->m_thread &&
			DelegateMember<RetType(TClass(Param1, Param2))>::operator==(rhs);
	}

	DelegateMemberAsyncWaitBase& operator=(const DelegateMemberAsyncWaitBase& rhs) {
		if (&rhs != this) {
			DelegateMember<RetType(TClass(Param1, Param2))>::operator=(rhs);
			Swap(rhs);
		}
		return *this;
	}

	/// Returns true if asynchronous function successfully invoked on target thread
	bool IsSuccess() { return m_success; }

protected:
	DelegateThread* m_thread;		// Target thread to invoke the delegate function
	bool m_success;					// Set to true if async function succeeds
	int m_timeout;					// Time in mS to wait for async function to invoke
	Semaphore m_sema;				// Semaphore to signal waiting thread
	LOCK m_lock;					// Lock to synchronize threads
	int m_refCnt;					// Ref count to determine when to delete objects

private:
	void Swap(const DelegateMemberAsyncWaitBase& s) {
		m_thread = s.m_thread;
		m_timeout = s.m_timeout;
		m_success = s.m_success;
	}
};

// N=2 with return type 
template <class TClass, class RetType, class Param1, class Param2>
class DelegateMemberAsyncWait<RetType(TClass(Param1, Param2))> : public DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2))> {
public:
	typedef TClass* ObjectPtr;
	typedef RetType (TClass::*MemberFunc)(Param1, Param2);
	typedef RetType (TClass::*ConstMemberFunc)(Param1, Param2) const;
	
	DelegateMemberAsyncWait(ObjectPtr object, MemberFunc func, DelegateThread* thread, int timeout) :
		DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2))>(object, func, thread, timeout) { }
	DelegateMemberAsyncWait(ObjectPtr object, ConstMemberFunc func, DelegateThread* thread, int timeout) :
		DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2))>(object, func, thread, timeout) { }
	DelegateMemberAsyncWait() { }
	virtual DelegateMemberAsyncWait<RetType(TClass(Param1, Param2))>* Clone() const {
		return new DelegateMemberAsyncWait<RetType(TClass(Param1, Param2))>(*this);
	}

	/// Invoke delegate function asynchronously
	virtual RetType operator()(Param1 p1, Param2 p2) {
		if (this->m_thread == 0)
			return DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2))>::operator()(p1, p2);
		else {
			// Create a clone instance of this delegate 
			auto delegate = Clone();
			delegate->m_refCnt = 2;
			delegate->m_sema.Create();
			delegate->m_sema.Reset();

			// Create a new message instance 
			DelegateMsg2<Param1, Param2>* msg = new DelegateMsg2<Param1, Param2>(delegate, p1, p2);

			// Dispatch message onto the callback destination thread. DelegateInvoke()
			// will be called by the target thread. 
			this->m_thread->DispatchDelegate(msg);

			// Wait for target thread to execute the delegate function
			if ((this->m_success = delegate->m_sema.Wait(this->m_timeout)))
				m_retVal = delegate->m_retVal;

			bool deleteData = false;
			{
				LockGuard lockGuard(&delegate->m_lock);
				if (--delegate->m_refCnt == 0)
					deleteData = true;
			}
			if (deleteData) {
				delete msg;
				delete delegate;
			}
			return m_retVal;
		}
	}

	/// Called by the target thread to invoke the delegate function 
	virtual void DelegateInvoke(DelegateMsgBase** msg) {
		bool deleteData = false;
		{
			// Typecast the base pointer to back to the templatized instance
			DelegateMsg2<Param1, Param2>* delegateMsg = static_cast<DelegateMsg2<Param1, Param2>*>(*msg);

			// Get the function parameter data
			Param1 param1 = delegateMsg->GetParam1();
			Param2 param2 = delegateMsg->GetParam2();

			LockGuard lockGuard(&this->m_lock);
			if (this->m_refCnt == 2) {
				// Invoke the delegate function then signal the waiting thread
				m_retVal = DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2))>::operator()(param1, param2);
				this->m_sema.Signal();
			}

			// If waiting thread is no longer waiting then delete heap data
			if (--this->m_refCnt == 0)
				deleteData = true;
		}
		if (deleteData) {
			delete *msg;
			*msg = 0;
			delete this;
		}
	}

	DelegateMemberAsyncWait& operator=(const DelegateMemberAsyncWait& rhs) {
		DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2))>::operator=(rhs);
		return *this;
	}

	RetType GetRetVal() { return m_retVal; }
private:
	RetType m_retVal;				// The delegate return value
};

// N=2 void return type
template <class TClass, class Param1, class Param2>
class DelegateMemberAsyncWait<void(TClass(Param1, Param2))> : public DelegateMemberAsyncWaitBase<void(TClass(Param1, Param2))> {
public:
	typedef TClass* ObjectPtr;
	typedef void (TClass::*MemberFunc)(Param1, Param2);
	typedef void (TClass::*ConstMemberFunc)(Param1, Param2) const;
	
	DelegateMemberAsyncWait(ObjectPtr object, MemberFunc func, DelegateThread* thread, int timeout) :
		DelegateMemberAsyncWaitBase<void(TClass(Param1, Param2))>(object, func, thread, timeout) { }
	DelegateMemberAsyncWait(ObjectPtr object, ConstMemberFunc func, DelegateThread* thread, int timeout) :
		DelegateMemberAsyncWaitBase<void(TClass(Param1, Param2))>(object, func, thread, timeout) { }
	DelegateMemberAsyncWait() { }
	virtual DelegateMemberAsyncWait<void(TClass(Param1, Param2))>* Clone() const {
		return new DelegateMemberAsyncWait<void(TClass(Param1, Param2))>(*this);
	}

	/// Invoke delegate function asynchronously
	virtual void operator()(Param1 p1, Param2 p2) {
		if (this->m_thread == 0)
			DelegateMemberAsyncWaitBase<void(TClass(Param1, Param2))>::operator()(p1, p2);
		else {
			// Create a clone instance of this delegate 
			auto delegate = Clone();
			delegate->m_refCnt = 2;
			delegate->m_sema.Create();
			delegate->m_sema.Reset();

			// Create a new message instance 
			DelegateMsg2<Param1, Param2>* msg = new DelegateMsg2<Param1, Param2>(delegate, p1, p2);

			// Dispatch message onto the callback destination thread. DelegateInvoke()
			// will be called by the target thread. 
			this->m_thread->DispatchDelegate(msg);

			// Wait for target thread to execute the delegate function
			this->m_success = delegate->m_sema.Wait(this->m_timeout);

			bool deleteData = false;
			{
				LockGuard lockGuard(&delegate->m_lock);
				if (--delegate->m_refCnt == 0)
					deleteData = true;
			}
			if (deleteData) {
				delete msg;
				delete delegate;
			}
		}
	}

	/// Called by the target thread to invoke the delegate function 
	virtual void DelegateInvoke(DelegateMsgBase** msg) {
		bool deleteData = false;
		{
			// Typecast the base pointer to back to the templatized instance
			DelegateMsg2<Param1, Param2>* delegateMsg = static_cast<DelegateMsg2<Param1, Param2>*>(*msg);

			// Get the function parameter data
			Param1 param1 = delegateMsg->GetParam1();
			Param2 param2 = delegateMsg->GetParam2();

			LockGuard lockGuard(&this->m_lock);
			if (this->m_refCnt == 2) {
				// Invoke the delegate function then signal the waiting thread
				DelegateMemberAsyncWaitBase<void(TClass(Param1, Param2))>::operator()(param1, param2);
				this->m_sema.Signal();
			}

			// If waiting thread is no longer waiting then delete heap data
			if (--this->m_refCnt == 0)
				deleteData = true;
		}
		if (deleteData) {
			delete *msg;
			*msg = 0;
			delete this;
		}
	}

	DelegateMemberAsyncWait& operator=(const DelegateMemberAsyncWait& rhs) {
		DelegateMemberAsyncWaitBase<void(TClass(Param1, Param2))>::operator=(rhs);
		return *this;
	}
};

// N=3 abstract base
template <class TClass, class RetType, class Param1, class Param2, class Param3>
class DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2, Param3))> : public DelegateMember<RetType(TClass(Param1, Param2, Param3))>, public IDelegateInvoker {
public:
	typedef TClass* ObjectPtr;
	typedef RetType (TClass::*MemberFunc)(Param1, Param2, Param3);
	typedef RetType (TClass::*ConstMemberFunc)(Param1, Param2, Param3) const;
	
	// Contructors take a class instance, member function, and delegate thread
	DelegateMemberAsyncWaitBase(ObjectPtr object, MemberFunc func, DelegateThread* thread, int timeout) : m_success(false), m_timeout(timeout), m_refCnt(0) {
		Bind(object, func, thread);
		LockGuard::Create(&m_lock);
	}
	DelegateMemberAsyncWaitBase(ObjectPtr object, ConstMemberFunc func, DelegateThread* thread, int timeout) : m_success(false), m_timeout(timeout), m_refCnt(0) {
		Bind(object, func, thread);
		LockGuard::Create(&m_lock);
	}
	DelegateMemberAsyncWaitBase(const DelegateMemberAsyncWaitBase& rhs) : DelegateMember<RetType(TClass(Param1, Param2, Param3))>(rhs), m_refCnt(0) {
		LockGuard::Create(&m_lock);
		Swap(rhs);
	}
	DelegateMemberAsyncWaitBase() : m_thread(0), m_success(false), m_timeout(0), m_refCnt(0) { LockGuard::Create(&m_lock); }
	virtual ~DelegateMemberAsyncWaitBase() { LockGuard::Destroy(&m_lock); }

	/// Bind a member function to a delegate. 
	void Bind(ObjectPtr object, MemberFunc func, DelegateThread* thread) {
		m_thread = thread;
		DelegateMember<RetType(TClass(Param1, Param2, Param3))>::Bind(object, func);
	}

	/// Bind a const member function to a delegate. 
	void Bind(ObjectPtr object, ConstMemberFunc func, DelegateThread* thread) {
		m_thread = thread;
		DelegateMember<RetType(TClass(Param1, Param2, Param3))>::Bind(object, func);
	}

	virtual bool operator==(const DelegateBase& rhs) const {
		const DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2, Param3))>* derivedRhs = dynamic_cast<const DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2, Param3))>*>(&rhs);
		return derivedRhs &&
			m_thread == derivedRhs->m_thread &&
			DelegateMember<RetType(TClass(Param1, Param2, Param3))>::operator==(rhs);
	}

	DelegateMemberAsyncWaitBase& operator=(const DelegateMemberAsyncWaitBase& rhs) {
		if (&rhs != this) {
			DelegateMember<RetType(TClass(Param1, Param2, Param3))>::operator=(rhs);
			Swap(rhs);
		}
		return *this;
	}

	/// Returns true if asynchronous function successfully invoked on target thread
	bool IsSuccess() { return m_success; }

protected:
	DelegateThread* m_thread;		// Target thread to invoke the delegate function
	bool m_success;					// Set to true if async function succeeds
	int m_timeout;					// Time in mS to wait for async function to invoke
	Semaphore m_sema;				// Semaphore to signal waiting thread
	LOCK m_lock;					// Lock to synchronize threads
	int m_refCnt;					// Ref count to determine when to delete objects

private:
	void Swap(const DelegateMemberAsyncWaitBase& s) {
		m_thread = s.m_thread;
		m_timeout = s.m_timeout;
		m_success = s.m_success;
	}
};

// N=3 with return type 
template <class TClass, class RetType, class Param1, class Param2, class Param3>
class DelegateMemberAsyncWait<RetType(TClass(Param1, Param2, Param3))> : public DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2, Param3))> {
public:
	typedef TClass* ObjectPtr;
	typedef RetType (TClass::*MemberFunc)(Param1, Param2, Param3);
	typedef RetType (TClass::*ConstMemberFunc)(Param1, Param2, Param3) const;
	
	DelegateMemberAsyncWait(ObjectPtr object, MemberFunc func, DelegateThread* thread, int timeout) :
		DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2, Param3))>(object, func, thread, timeout) { }
	DelegateMemberAsyncWait(ObjectPtr object, ConstMemberFunc func, DelegateThread* thread, int timeout) :
		DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2, Param3))>(object, func, thread, timeout) { }
	DelegateMemberAsyncWait() { }
	virtual DelegateMemberAsyncWait<RetType(TClass(Param1, Param2, Param3))>* Clone() const {
		return new DelegateMemberAsyncWait<RetType(TClass(Param1, Param2, Param3))>(*this);
	}

	/// Invoke delegate function asynchronously
	virtual RetType operator()(Param1 p1, Param2 p2, Param3 p3) {
		if (this->m_thread == 0)
			return DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2, Param3))>::operator()(p1, p2, p3);
		else {
			// Create a clone instance of this delegate 
			auto delegate = Clone();
			delegate->m_refCnt = 2;
			delegate->m_sema.Create();
			delegate->m_sema.Reset();

			// Create a new message instance 
			DelegateMsg3<Param1, Param2, Param3>* msg = new DelegateMsg3<Param1, Param2, Param3>(delegate, p1, p2, p3);

			// Dispatch message onto the callback destination thread. DelegateInvoke()
			// will be called by the target thread. 
			this->m_thread->DispatchDelegate(msg);

			// Wait for target thread to execute the delegate function
			if ((this->m_success = delegate->m_sema.Wait(this->m_timeout)))
				m_retVal = delegate->m_retVal;

			bool deleteData = false;
			{
				LockGuard lockGuard(&delegate->m_lock);
				if (--delegate->m_refCnt == 0)
					deleteData = true;
			}
			if (deleteData) {
				delete msg;
				delete delegate;
			}
			return m_retVal;
		}
	}

	/// Called by the target thread to invoke the delegate function 
	virtual void DelegateInvoke(DelegateMsgBase** msg) {
		bool deleteData = false;
		{
			// Typecast the base pointer to back to the templatized instance
			DelegateMsg3<Param1, Param2, Param3>* delegateMsg = static_cast<DelegateMsg3<Param1, Param2, Param3>*>(*msg);

			// Get the function parameter data
			Param1 param1 = delegateMsg->GetParam1();
			Param2 param2 = delegateMsg->GetParam2();
			Param3 param3 = delegateMsg->GetParam3();

			LockGuard lockGuard(&this->m_lock);
			if (this->m_refCnt == 2) {
				// Invoke the delegate function then signal the waiting thread
				m_retVal = DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2, Param3))>::operator()(param1, param2, param3);
				this->m_sema.Signal();
			}

			// If waiting thread is no longer waiting then delete heap data
			if (--this->m_refCnt == 0)
				deleteData = true;
		}
		if (deleteData) {
			delete *msg;
			*msg = 0;
			delete this;
		}
	}

	DelegateMemberAsyncWait& operator=(const DelegateMemberAsyncWait& rhs) {
		DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2, Param3))>::operator=(rhs);
		return *this;
	}

	RetType GetRetVal() { return m_retVal; }
private:
	RetType m_retVal;				// The delegate return value
};

// N=3 void return type
template <class TClass, class Param1, class Param2, class Param3>
class DelegateMemberAsyncWait<void(TClass(Param1, Param2, Param3))> : public DelegateMemberAsyncWaitBase<void(TClass(Param1, Param2, Param3))> {
public:
	typedef TClass* ObjectPtr;
	typedef void (TClass::*MemberFunc)(Param1, Param2, Param3);
	typedef void (TClass::*ConstMemberFunc)(Param1, Param2, Param3) const;
	
	DelegateMemberAsyncWait(ObjectPtr object, MemberFunc func, DelegateThread* thread, int timeout) :
		DelegateMemberAsyncWaitBase<void(TClass(Param1, Param2, Param3))>(object, func, thread, timeout) { }
	DelegateMemberAsyncWait(ObjectPtr object, ConstMemberFunc func, DelegateThread* thread, int timeout) :
		DelegateMemberAsyncWaitBase<void(TClass(Param1, Param2, Param3))>(object, func, thread, timeout) { }
	DelegateMemberAsyncWait() { }
	virtual DelegateMemberAsyncWait<void(TClass(Param1, Param2, Param3))>* Clone() const {
		return new DelegateMemberAsyncWait<void(TClass(Param1, Param2, Param3))>(*this);
	}

	/// Invoke delegate function asynchronously
	virtual void operator()(Param1 p1, Param2 p2, Param3 p3) {
		if (this->m_thread == 0)
			DelegateMemberAsyncWaitBase<void(TClass(Param1, Param2, Param3))>::operator()(p1, p2, p3);
		else {
			// Create a clone instance of this delegate 
			auto delegate = Clone();
			delegate->m_refCnt = 2;
			delegate->m_sema.Create();
			delegate->m_sema.Reset();

			// Create a new message instance 
			DelegateMsg3<Param1, Param2, Param3>* msg = new DelegateMsg3<Param1, Param2, Param3>(delegate, p1, p2, p3);

			// Dispatch message onto the callback destination thread. DelegateInvoke()
			// will be called by the target thread. 
			this->m_thread->DispatchDelegate(msg);

			// Wait for target thread to execute the delegate function
			this->m_success = delegate->m_sema.Wait(this->m_timeout);

			bool deleteData = false;
			{
				LockGuard lockGuard(&delegate->m_lock);
				if (--delegate->m_refCnt == 0)
					deleteData = true;
			}
			if (deleteData) {
				delete msg;
				delete delegate;
			}
		}
	}

	/// Called by the target thread to invoke the delegate function 
	virtual void DelegateInvoke(DelegateMsgBase** msg) {
		bool deleteData = false;
		{
			// Typecast the base pointer to back to the templatized instance
			DelegateMsg3<Param1, Param2, Param3>* delegateMsg = static_cast<DelegateMsg3<Param1, Param2, Param3>*>(*msg);

			// Get the function parameter data
			Param1 param1 = delegateMsg->GetParam1();
			Param2 param2 = delegateMsg->GetParam2();
			Param3 param3 = delegateMsg->GetParam3();

			LockGuard lockGuard(&this->m_lock);
			if (this->m_refCnt == 2) {
				// Invoke the delegate function then signal the waiting thread
				DelegateMemberAsyncWaitBase<void(TClass(Param1, Param2, Param3))>::operator()(param1, param2, param3);
				this->m_sema.Signal();
			}

			// If waiting thread is no longer waiting then delete heap data
			if (--this->m_refCnt == 0)
				deleteData = true;
		}
		if (deleteData) {
			delete *msg;
			*msg = 0;
			delete this;
		}
	}

	DelegateMemberAsyncWait& operator=(const DelegateMemberAsyncWait& rhs) {
		DelegateMemberAsyncWaitBase<void(TClass(Param1, Param2, Param3))>::operator=(rhs);
		return *this;
	}
};

// N=4 abstract base
template <class TClass, class RetType, class Param1, class Param2, class Param3, class Param4>
class DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2, Param3, Param4))> : public DelegateMember<RetType(TClass(Param1, Param2, Param3, Param4))>, public IDelegateInvoker {
public:
	typedef TClass* ObjectPtr;
	typedef RetType (TClass::*MemberFunc)(Param1, Param2, Param3, Param4);
	typedef RetType (TClass::*ConstMemberFunc)(Param1, Param2, Param3, Param4) const;

	// Contructors take a class instance, member function, and delegate thread
	DelegateMemberAsyncWaitBase(ObjectPtr object, MemberFunc func, DelegateThread* thread, int timeout) : m_success(false), m_timeout(timeout), m_refCnt(0) {
		Bind(object, func, thread);
		LockGuard::Create(&m_lock);
	}
	DelegateMemberAsyncWaitBase(ObjectPtr object, ConstMemberFunc func, DelegateThread* thread, int timeout) : m_success(false), m_timeout(timeout), m_refCnt(0) {
		Bind(object, func, thread);
		LockGuard::Create(&m_lock);
	}
	DelegateMemberAsyncWaitBase(const DelegateMemberAsyncWaitBase& rhs) : DelegateMember<RetType(TClass(Param1, Param2, Param3, Param4))>(rhs), m_refCnt(0) {
		LockGuard::Create(&m_lock);
		Swap(rhs);
	}
	DelegateMemberAsyncWaitBase() : m_thread(0), m_success(false), m_timeout(0), m_refCnt(0) { LockGuard::Create(&m_lock); }
	virtual ~DelegateMemberAsyncWaitBase() { LockGuard::Destroy(&m_lock); }

	/// Bind a member function to a delegate. 
	void Bind(ObjectPtr object, MemberFunc func, DelegateThread* thread) {
		m_thread = thread;
		DelegateMember<RetType(TClass(Param1, Param2, Param3, Param4))>::Bind(object, func);
	}

	/// Bind a const member function to a delegate. 
	void Bind(ObjectPtr object, ConstMemberFunc func, DelegateThread* thread) {
		m_thread = thread;
		DelegateMember<RetType(TClass(Param1, Param2, Param3, Param4))>::Bind(object, func);
	}

	virtual bool operator==(const DelegateBase& rhs) const {
		const DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2, Param3, Param4))>* derivedRhs = dynamic_cast<const DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2, Param3, Param4))>*>(&rhs);
		return derivedRhs &&
			m_thread == derivedRhs->m_thread &&
			DelegateMember<RetType(TClass(Param1, Param2, Param3, Param4))>::operator==(rhs);
	}

	DelegateMemberAsyncWaitBase& operator=(const DelegateMemberAsyncWaitBase& rhs) {
		if (&rhs != this) {
			DelegateMember<RetType(TClass(Param1, Param2, Param3, Param4))>::operator=(rhs);
			Swap(rhs);
		}
		return *this;
	}

	/// Returns true if asynchronous function successfully invoked on target thread
	bool IsSuccess() { return m_success; }

protected:
	DelegateThread* m_thread;		// Target thread to invoke the delegate function
	bool m_success;					// Set to true if async function succeeds
	int m_timeout;					// Time in mS to wait for async function to invoke
	Semaphore m_sema;				// Semaphore to signal waiting thread
	LOCK m_lock;					// Lock to synchronize threads
	int m_refCnt;					// Ref count to determine when to delete objects

private:
	void Swap(const DelegateMemberAsyncWaitBase& s) {
		m_thread = s.m_thread;
		m_timeout = s.m_timeout;
		m_success = s.m_success;
	}
};

// N=4 with return type 
template <class TClass, class RetType, class Param1, class Param2, class Param3, class Param4>
class DelegateMemberAsyncWait<RetType(TClass(Param1, Param2, Param3, Param4))> : public DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2, Param3, Param4))> {
public:
	typedef TClass* ObjectPtr;
	typedef RetType (TClass::*MemberFunc)(Param1, Param2, Param3, Param4);
	typedef RetType (TClass::*ConstMemberFunc)(Param1, Param2, Param3, Param4) const;
	
	DelegateMemberAsyncWait(ObjectPtr object, MemberFunc func, DelegateThread* thread, int timeout) :
		DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2, Param3, Param4))>(object, func, thread, timeout) { }
	DelegateMemberAsyncWait(ObjectPtr object, ConstMemberFunc func, DelegateThread* thread, int timeout) :
		DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2, Param3, Param4))>(object, func, thread, timeout) { }
	DelegateMemberAsyncWait() { }
	virtual DelegateMemberAsyncWait<RetType(TClass(Param1, Param2, Param3, Param4))>* Clone() const {
		return new DelegateMemberAsyncWait<RetType(TClass(Param1, Param2, Param3, Param4))>(*this);
	}

	/// Invoke delegate function asynchronously
	virtual RetType operator()(Param1 p1, Param2 p2, Param3 p3, Param4 p4) {
		if (this->m_thread == 0)
			return DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2, Param3, Param4))>::operator()(p1, p2, p3, p4);
		else {
			// Create a clone instance of this delegate 
			auto delegate = Clone();
			delegate->m_refCnt = 2;
			delegate->m_sema.Create();
			delegate->m_sema.Reset();

			// Create a new message instance 
			DelegateMsg4<Param1, Param2, Param3, Param4>* msg = new DelegateMsg4<Param1, Param2, Param3, Param4>(delegate, p1, p2, p3, p4);

			// Dispatch message onto the callback destination thread. DelegateInvoke()
			// will be called by the target thread. 
			this->m_thread->DispatchDelegate(msg);

			// Wait for target thread to execute the delegate function
			if ((this->m_success = delegate->m_sema.Wait(this->m_timeout)))
				m_retVal = delegate->m_retVal;

			bool deleteData = false;
			{
				LockGuard lockGuard(&delegate->m_lock);
				if (--delegate->m_refCnt == 0)
					deleteData = true;
			}
			if (deleteData) {
				delete msg;
				delete delegate;
			}
			return m_retVal;
		}
	}

	/// Called by the target thread to invoke the delegate function 
	virtual void DelegateInvoke(DelegateMsgBase** msg) {
		bool deleteData = false;
		{
			// Typecast the base pointer to back to the templatized instance
			DelegateMsg4<Param1, Param2, Param3, Param4>* delegateMsg = static_cast<DelegateMsg4<Param1, Param2, Param3, Param4>*>(*msg);

			// Get the function parameter data
			Param1 param1 = delegateMsg->GetParam1();
			Param2 param2 = delegateMsg->GetParam2();
			Param3 param3 = delegateMsg->GetParam3();
			Param4 param4 = delegateMsg->GetParam4();

			LockGuard lockGuard(&this->m_lock);
			if (this->m_refCnt == 2) {
				// Invoke the delegate function then signal the waiting thread
				m_retVal = DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2, Param3, Param4))>::operator()(param1, param2, param3, param4);
				this->m_sema.Signal();
			}

			// If waiting thread is no longer waiting then delete heap data
			if (--this->m_refCnt == 0)
				deleteData = true;
		}
		if (deleteData) {
			delete *msg;
			*msg = 0;
			delete this;
		}
	}

	DelegateMemberAsyncWait& operator=(const DelegateMemberAsyncWait& rhs) {
		DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2, Param3, Param4))>::operator=(rhs);
		return *this;
	}

	RetType GetRetVal() { return m_retVal; }
private:
	RetType m_retVal;				// The delegate return value
};

// N=4 void return type
template <class TClass, class Param1, class Param2, class Param3, class Param4>
class DelegateMemberAsyncWait<void(TClass(Param1, Param2, Param3, Param4))> : public DelegateMemberAsyncWaitBase<void(TClass(Param1, Param2, Param3, Param4))> {
public:
	typedef TClass* ObjectPtr;
	typedef void (TClass::*MemberFunc)(Param1, Param2, Param3, Param4);
	typedef void (TClass::*ConstMemberFunc)(Param1, Param2, Param3, Param4) const;
	
	DelegateMemberAsyncWait(ObjectPtr object, MemberFunc func, DelegateThread* thread, int timeout) :
		DelegateMemberAsyncWaitBase<void(TClass(Param1, Param2, Param3, Param4))>(object, func, thread, timeout) { }
	DelegateMemberAsyncWait(ObjectPtr object, ConstMemberFunc func, DelegateThread* thread, int timeout) :
		DelegateMemberAsyncWaitBase<void(TClass(Param1, Param2, Param3, Param4))>(object, func, thread, timeout) { }
	DelegateMemberAsyncWait() { }
	virtual DelegateMemberAsyncWait<void(TClass(Param1, Param2, Param3, Param4))>* Clone() const {
		return new DelegateMemberAsyncWait<void(TClass(Param1, Param2, Param3, Param4))>(*this);
	}

	/// Invoke delegate function asynchronously
	virtual void operator()(Param1 p1, Param2 p2, Param3 p3, Param4 p4) {
		if (this->m_thread == 0)
			DelegateMemberAsyncWaitBase<void(TClass(Param1, Param2, Param3, Param4))>::operator()(p1, p2, p3, p4);
		else {
			// Create a clone instance of this delegate 
			auto delegate = Clone();
			delegate->m_refCnt = 2;
			delegate->m_sema.Create();
			delegate->m_sema.Reset();

			// Create a new message instance 
			DelegateMsg4<Param1, Param2, Param3, Param4>* msg = new DelegateMsg4<Param1, Param2, Param3, Param4>(delegate, p1, p2, p3, p4);

			// Dispatch message onto the callback destination thread. DelegateInvoke()
			// will be called by the target thread. 
			this->m_thread->DispatchDelegate(msg);

			// Wait for target thread to execute the delegate function
			this->m_success = delegate->m_sema.Wait(this->m_timeout);

			bool deleteData = false;
			{
				LockGuard lockGuard(&delegate->m_lock);
				if (--delegate->m_refCnt == 0)
					deleteData = true;
			}
			if (deleteData) {
				delete msg;
				delete delegate;
			}
		}
	}

	/// Called by the target thread to invoke the delegate function 
	virtual void DelegateInvoke(DelegateMsgBase** msg) {
		bool deleteData = false;
		{
			// Typecast the base pointer to back to the templatized instance
			DelegateMsg4<Param1, Param2, Param3, Param4>* delegateMsg = static_cast<DelegateMsg4<Param1, Param2, Param3, Param4>*>(*msg);

			// Get the function parameter data
			Param1 param1 = delegateMsg->GetParam1();
			Param2 param2 = delegateMsg->GetParam2();
			Param3 param3 = delegateMsg->GetParam3();
			Param4 param4 = delegateMsg->GetParam4();

			LockGuard lockGuard(&this->m_lock);
			if (this->m_refCnt == 2) {
				// Invoke the delegate function then signal the waiting thread
				DelegateMemberAsyncWaitBase<void(TClass(Param1, Param2, Param3, Param4))>::operator()(param1, param2, param3, param4);
				this->m_sema.Signal();
			}

			// If waiting thread is no longer waiting then delete heap data
			if (--this->m_refCnt == 0)
				deleteData = true;
		}
		if (deleteData) {
			delete *msg;
			*msg = 0;
			delete this;
		}
	}

	DelegateMemberAsyncWait& operator=(const DelegateMemberAsyncWait& rhs) {
		DelegateMemberAsyncWaitBase<void(TClass(Param1, Param2, Param3, Param4))>::operator=(rhs);
		return *this;
	}
};

// N=5 abstract base
template <class TClass, class RetType, class Param1, class Param2, class Param3, class Param4, class Param5>
class DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2, Param3, Param4, Param5))> : public DelegateMember<RetType(TClass(Param1, Param2, Param3, Param4, Param5))>, public IDelegateInvoker {
public:
	typedef TClass* ObjectPtr;
	typedef RetType (TClass::*MemberFunc)(Param1, Param2, Param3, Param4, Param5);
	typedef RetType (TClass::*ConstMemberFunc)(Param1, Param2, Param3, Param4, Param5) const;
	
	// Contructors take a class instance, member function, and delegate thread
	DelegateMemberAsyncWaitBase(ObjectPtr object, MemberFunc func, DelegateThread* thread, int timeout) : m_success(false), m_timeout(timeout), m_refCnt(0) {
		Bind(object, func, thread);
		LockGuard::Create(&m_lock);
	}
	DelegateMemberAsyncWaitBase(ObjectPtr object, ConstMemberFunc func, DelegateThread* thread, int timeout) : m_success(false), m_timeout(timeout), m_refCnt(0) {
		Bind(object, func, thread);
		LockGuard::Create(&m_lock);
	}
	DelegateMemberAsyncWaitBase(const DelegateMemberAsyncWaitBase& rhs) : DelegateMember<RetType(TClass(Param1, Param2, Param3, Param4, Param5))>(rhs), m_refCnt(0) {
		LockGuard::Create(&m_lock);
		Swap(rhs);
	}
	DelegateMemberAsyncWaitBase() : m_thread(0), m_success(false), m_timeout(0), m_refCnt(0) { LockGuard::Create(&m_lock); }
	virtual ~DelegateMemberAsyncWaitBase() { LockGuard::Destroy(&m_lock); }

	/// Bind a member function to a delegate. 
	void Bind(ObjectPtr object, MemberFunc func, DelegateThread* thread) {
		m_thread = thread;
		DelegateMember<RetType(TClass(Param1, Param2, Param3, Param4, Param5))>::Bind(object, func);
	}

	/// Bind a const member function to a delegate. 
	void Bind(ObjectPtr object, ConstMemberFunc func, DelegateThread* thread) {
		m_thread = thread;
		DelegateMember<RetType(TClass(Param1, Param2, Param3, Param4, Param5))>::Bind(object, func);
	}

	virtual bool operator==(const DelegateBase& rhs) const {
		const DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2, Param3, Param4, Param5))>* derivedRhs = dynamic_cast<const DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2, Param3, Param4, Param5))>*>(&rhs);
		return derivedRhs &&
			m_thread == derivedRhs->m_thread &&
			DelegateMember<RetType(TClass(Param1, Param2, Param3, Param4, Param5))>::operator==(rhs);
	}

	DelegateMemberAsyncWaitBase& operator=(const DelegateMemberAsyncWaitBase& rhs) {
		if (&rhs != this) {
			DelegateMember<RetType(TClass(Param1, Param2, Param3, Param4, Param5))>::operator=(rhs);
			Swap(rhs);
		}
		return *this;
	}

	/// Returns true if asynchronous function successfully invoked on target thread
	bool IsSuccess() { return m_success; }

protected:
	DelegateThread* m_thread;		// Target thread to invoke the delegate function
	bool m_success;					// Set to true if async function succeeds
	int m_timeout;					// Time in mS to wait for async function to invoke
	Semaphore m_sema;				// Semaphore to signal waiting thread
	LOCK m_lock;					// Lock to synchronize threads
	int m_refCnt;					// Ref count to determine when to delete objects

private:
	void Swap(const DelegateMemberAsyncWaitBase& s) {
		m_thread = s.m_thread;
		m_timeout = s.m_timeout;
		m_success = s.m_success;
	}
};

// N=5 with return type 
template <class TClass, class RetType, class Param1, class Param2, class Param3, class Param4, class Param5>
class DelegateMemberAsyncWait<RetType(TClass(Param1, Param2, Param3, Param4, Param5))> : public DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2, Param3, Param4, Param5))> {
public:
	typedef TClass* ObjectPtr;
	typedef RetType (TClass::*MemberFunc)(Param1, Param2, Param3, Param4, Param5);
	typedef RetType (TClass::*ConstMemberFunc)(Param1, Param2, Param3, Param4, Param5) const;
	
	DelegateMemberAsyncWait(ObjectPtr object, MemberFunc func, DelegateThread* thread, int timeout) :
		DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2, Param3, Param4, Param5))>(object, func, thread, timeout) { }
	DelegateMemberAsyncWait(ObjectPtr object, ConstMemberFunc func, DelegateThread* thread, int timeout) :
		DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2, Param3, Param4, Param5))>(object, func, thread, timeout) { }
	DelegateMemberAsyncWait() { }
	virtual DelegateMemberAsyncWait<RetType(TClass(Param1, Param2, Param3, Param4, Param5))>* Clone() const {
		return new DelegateMemberAsyncWait<RetType(TClass(Param1, Param2, Param3, Param4, Param5))>(*this);
	}

	/// Invoke delegate function asynchronously
	virtual RetType operator()(Param1 p1, Param2 p2, Param3 p3, Param4 p4, Param5 p5) {
		if (this->m_thread == 0)
			return DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2, Param3, Param4, Param5))>::operator()(p1, p2, p3, p4, p5);
		else {
			// Create a clone instance of this delegate 
			auto delegate = Clone();
			delegate->m_refCnt = 2;
			delegate->m_sema.Create();
			delegate->m_sema.Reset();

			// Create a new message instance 
			DelegateMsg5<Param1, Param2, Param3, Param4, Param5>* msg = new DelegateMsg5<Param1, Param2, Param3, Param4, Param5>(delegate, p1, p2, p3, p4, p5);

			// Dispatch message onto the callback destination thread. DelegateInvoke()
			// will be called by the target thread. 
			this->m_thread->DispatchDelegate(msg);

			// Wait for target thread to execute the delegate function
			if ((this->m_success = delegate->m_sema.Wait(this->m_timeout)))
				m_retVal = delegate->m_retVal;

			bool deleteData = false;
			{
				LockGuard lockGuard(&delegate->m_lock);
				if (--delegate->m_refCnt == 0)
					deleteData = true;
			}
			if (deleteData) {
				delete msg;
				delete delegate;
			}
			return m_retVal;
		}
	}

	/// Called by the target thread to invoke the delegate function 
	virtual void DelegateInvoke(DelegateMsgBase** msg) {
		bool deleteData = false;
		{
			// Typecast the base pointer to back to the templatized instance
			DelegateMsg5<Param1, Param2, Param3, Param4, Param5>* delegateMsg = static_cast<DelegateMsg5<Param1, Param2, Param3, Param4, Param5>*>(*msg);

			// Get the function parameter data
			Param1 param1 = delegateMsg->GetParam1();
			Param2 param2 = delegateMsg->GetParam2();
			Param3 param3 = delegateMsg->GetParam3();
			Param4 param4 = delegateMsg->GetParam4();
			Param4 param5 = delegateMsg->GetParam5();

			LockGuard lockGuard(&this->m_lock);
			if (this->m_refCnt == 2) {
				// Invoke the delegate function then signal the waiting thread
				m_retVal = DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2, Param3, Param4, Param5))>::operator()(param1, param2, param3, param4, param5);
				this->m_sema.Signal();
			}

			// If waiting thread is no longer waiting then delete heap data
			if (--this->m_refCnt == 0)
				deleteData = true;
		}
		if (deleteData) {
			delete *msg;
			*msg = 0;
			delete this;
		}
	}

	DelegateMemberAsyncWait& operator=(const DelegateMemberAsyncWait& rhs) {
		DelegateMemberAsyncWaitBase<RetType(TClass(Param1, Param2, Param3, Param4, Param5))>::operator=(rhs);
		return *this;
	}

	RetType GetRetVal() { return m_retVal; }
private:
	RetType m_retVal;				// The delegate return value
};

// N=5 void return type
template <class TClass, class Param1, class Param2, class Param3, class Param4, class Param5>
class DelegateMemberAsyncWait<void(TClass(Param1, Param2, Param3, Param4, Param5))> : public DelegateMemberAsyncWaitBase<void(TClass(Param1, Param2, Param3, Param4, Param5))> {
public:
	typedef TClass* ObjectPtr;
	typedef void (TClass::*MemberFunc)(Param1, Param2, Param3, Param4, Param5);
	typedef void (TClass::*ConstMemberFunc)(Param1, Param2, Param3, Param4, Param5) const;
	
	DelegateMemberAsyncWait(ObjectPtr object, MemberFunc func, DelegateThread* thread, int timeout) :
		DelegateMemberAsyncWaitBase<void(TClass(Param1, Param2, Param3, Param4, Param5))>(object, func, thread, timeout) { }
	DelegateMemberAsyncWait(ObjectPtr object, ConstMemberFunc func, DelegateThread* thread, int timeout) :
		DelegateMemberAsyncWaitBase<void(TClass(Param1, Param2, Param3, Param4, Param5))>(object, func, thread, timeout) { }
	DelegateMemberAsyncWait() { }
	virtual DelegateMemberAsyncWait<void(TClass(Param1, Param2, Param3, Param4, Param5))>* Clone() const {
		return new DelegateMemberAsyncWait<void(TClass(Param1, Param2, Param3, Param4, Param5))>(*this);
	}

	/// Invoke delegate function asynchronously
	virtual void operator()(Param1 p1, Param2 p2, Param3 p3, Param4 p4, Param5 p5) {
		if (this->m_thread == 0)
			DelegateMemberAsyncWaitBase<void(TClass(Param1, Param2, Param3, Param4, Param5))>::operator()(p1, p2, p3, p4, p5);
		else {
			// Create a clone instance of this delegate 
			auto delegate = Clone();
			delegate->m_refCnt = 2;
			delegate->m_sema.Create();
			delegate->m_sema.Reset();

			// Create a new message instance 
			DelegateMsg5<Param1, Param2, Param3, Param4, Param5>* msg = new DelegateMsg5<Param1, Param2, Param3, Param4, Param5>(delegate, p1, p2, p3, p4, p5);

			// Dispatch message onto the callback destination thread. DelegateInvoke()
			// will be called by the target thread. 
			this->m_thread->DispatchDelegate(msg);

			// Wait for target thread to execute the delegate function
			this->m_success = delegate->m_sema.Wait(this->m_timeout);

			bool deleteData = false;
			{
				LockGuard lockGuard(&delegate->m_lock);
				if (--delegate->m_refCnt == 0)
					deleteData = true;
			}
			if (deleteData) {
				delete msg;
				delete delegate;
			}
		}
	}

	/// Called by the target thread to invoke the delegate function 
	virtual void DelegateInvoke(DelegateMsgBase** msg) {
		bool deleteData = false;
		{
			// Typecast the base pointer to back to the templatized instance
			DelegateMsg5<Param1, Param2, Param3, Param4, Param5>* delegateMsg = static_cast<DelegateMsg5<Param1, Param2, Param3, Param4, Param5>*>(*msg);

			// Get the function parameter data
			Param1 param1 = delegateMsg->GetParam1();
			Param2 param2 = delegateMsg->GetParam2();
			Param3 param3 = delegateMsg->GetParam3();
			Param4 param4 = delegateMsg->GetParam4();
			Param5 param5 = delegateMsg->GetParam5();

			LockGuard lockGuard(&this->m_lock);
			if (this->m_refCnt == 2) {
				// Invoke the delegate function then signal the waiting thread
				DelegateMemberAsyncWaitBase<void(TClass(Param1, Param2, Param3, Param4, Param5))>::operator()(param1, param2, param3, param4, param5);
				this->m_sema.Signal();
			}

			// If waiting thread is no longer waiting then delete heap data
			if (--this->m_refCnt == 0)
				deleteData = true;
		}
		if (deleteData) {
			delete *msg;
			*msg = 0;
			delete this;
		}
	}

	DelegateMemberAsyncWait& operator=(const DelegateMemberAsyncWait& rhs) {
		DelegateMemberAsyncWaitBase<void(TClass(Param1, Param2, Param3, Param4, Param5))>::operator=(rhs);
		return *this;
	}
};

// *** Free Classes Start ***

// Declare DelegateFreeAsyncWaitBase as a class template. It will be specialized for all number of arguments.
template <typename Signature>
class DelegateFreeAsyncWaitBase;

// Declare DelegateFreeAsyncWait as a class template. It will be specialized for all number of arguments.
template <typename Signature>
class DelegateFreeAsyncWait;

// N=0 abstract base
template <class RetType>
class DelegateFreeAsyncWaitBase<RetType(void)> : public DelegateFree<RetType(void)>, public IDelegateInvoker {
public:
	typedef RetType (*FreeFunc)();

	// Contructors take a class instance, member function, and delegate thread
	DelegateFreeAsyncWaitBase(FreeFunc func, DelegateThread* thread, int timeout) : m_success(false), m_timeout(timeout), m_refCnt(0) {
		Bind(func, thread); 
		LockGuard::Create(&m_lock);
	}
	DelegateFreeAsyncWaitBase(const DelegateFreeAsyncWaitBase& rhs) : DelegateFree<RetType(void)>(rhs), m_refCnt(0) {
		LockGuard::Create(&m_lock);
		Swap(rhs);
	}
	DelegateFreeAsyncWaitBase() : m_thread(0), m_success(false), m_timeout(0), m_refCnt(0) { LockGuard::Create(&m_lock); }
	virtual ~DelegateFreeAsyncWaitBase() { LockGuard::Destroy(&m_lock); }

	/// Bind a member function to a delegate. 
	void Bind(FreeFunc func, DelegateThread* thread) {
		m_thread = thread;
		DelegateFree<RetType(void)>::Bind(func);
	}

	virtual bool operator==(const DelegateBase& rhs) const {
		const DelegateFreeAsyncWaitBase<RetType(void)>* derivedRhs = dynamic_cast<const DelegateFreeAsyncWaitBase<RetType(void)>*>(&rhs);
		return derivedRhs &&
			m_thread == derivedRhs->m_thread &&
			DelegateFree<RetType(void)>::operator==(rhs);
	}

	DelegateFreeAsyncWaitBase& operator=(const DelegateFreeAsyncWaitBase& rhs) {
		if (&rhs != this) {
			DelegateFree<RetType(void)>::operator=(rhs);
			Swap(rhs);
		}
		return *this;
	}

	/// Returns true if asynchronous function successfully invoked on target thread
	bool IsSuccess() { return m_success; }

protected:
	DelegateThread* m_thread;		// Target thread to invoke the delegate function
	bool m_success;					// Set to true if async function succeeds
	int m_timeout;					// Time in mS to wait for async function to invoke
	Semaphore m_sema;				// Semaphore to signal waiting thread
	LOCK m_lock;					// Lock to synchronize threads
	int m_refCnt;					// Ref count to determine when to delete objects

private:
	void Swap(const DelegateFreeAsyncWaitBase& s) {
		m_thread = s.m_thread;
		m_timeout = s.m_timeout;
		m_success = s.m_success;
	}
};

// N=0 with return type
template <class RetType>
class DelegateFreeAsyncWait<RetType(void)> : public DelegateFreeAsyncWaitBase<RetType(void)> {
public:
	typedef RetType (*FreeFunc)();

	DelegateFreeAsyncWait(FreeFunc func, DelegateThread* thread, int timeout) :
		DelegateFreeAsyncWaitBase<RetType(void)>(func, thread, timeout) { }
	DelegateFreeAsyncWait() { }
	virtual DelegateFreeAsyncWait<RetType(void)>* Clone() {
		return new DelegateFreeAsyncWait<RetType(void)>(*this); }

	/// Invoke delegate function asynchronously
	virtual RetType operator()() {
		if (this->m_thread == 0)
			return DelegateFreeAsyncWaitBase<RetType(void)>::operator()();
		else {
			// Create a clone instance of this delegate 
			auto delegate = Clone();
			delegate->m_refCnt = 2;
			delegate->m_sema.Create();
			delegate->m_sema.Reset();

			// Create a new message instance 
			DelegateMsgBase* msg = new DelegateMsgBase(delegate);

			// Dispatch message onto the callback destination thread. DelegateInvoke()
			// will be called by the target thread. 
			this->m_thread->DispatchDelegate(msg);

			// Wait for target thread to execute the delegate function
			if ((this->m_success = delegate->m_sema.Wait(this->m_timeout)))
				m_retVal = delegate->m_retVal;

			bool deleteData = false;
			{
				LockGuard lockGuard(&delegate->m_lock);
				if (--delegate->m_refCnt == 0)
					deleteData = true;
			}
			if (deleteData) {
				delete msg;
				delete delegate;
			}
			return m_retVal;
		}
	}

	/// Called by the target thread to invoke the delegate function 
	virtual void DelegateInvoke(DelegateMsgBase** msg) {
		bool deleteData = false;
		{
			LockGuard lockGuard(&this->m_lock);
			if (this->m_refCnt == 2) {
				// Invoke the delegate function then signal the waiting thread
				m_retVal = DelegateFreeAsyncWaitBase<RetType(void)>::operator()();
				this->m_sema.Signal();
			}

			// If waiting thread is no longer waiting then delete heap data
			if (--this->m_refCnt == 0)
				deleteData = true;
		}
		if (deleteData) {
			delete *msg;
			*msg = 0;
			delete this;
		}
	}

	DelegateFreeAsyncWait& operator=(const DelegateFreeAsyncWait& rhs) {
		DelegateFreeAsyncWaitBase<RetType(void)>::operator=(rhs);
		return *this;
	}

	RetType GetRetVal() { return m_retVal; }
private:
	RetType m_retVal;				// The delegate return value
};

// N=0 void return type
template <>
class DelegateFreeAsyncWait<void(void)> : public DelegateFreeAsyncWaitBase<void(void)> {
public:
	typedef void (*FreeFunc)();

	DelegateFreeAsyncWait(FreeFunc func, DelegateThread* thread, int timeout) :
		DelegateFreeAsyncWaitBase(func, thread, timeout) { }
	DelegateFreeAsyncWait() { }
	virtual DelegateFreeAsyncWait<void(void)>* Clone() const {
		return new DelegateFreeAsyncWait<void(void)>(*this);	}

	/// Invoke delegate function asynchronously
	virtual void operator()() {
		if (this->m_thread == 0)
			return DelegateFreeAsyncWaitBase::operator()();
		else {
			// Create a clone instance of this delegate 
			auto delegate = Clone();
			delegate->m_refCnt = 2;
			delegate->m_sema.Create();
			delegate->m_sema.Reset();

			// Create a new message instance 
			DelegateMsgBase* msg = new DelegateMsgBase(delegate);

			// Dispatch message onto the callback destination thread. DelegateInvoke()
			// will be called by the target thread. 
			this->m_thread->DispatchDelegate(msg);

			// Wait for target thread to execute the delegate function
			if ((this->m_success = delegate->m_sema.Wait(this->m_timeout))) {
				// No return or param arguments
			}

			bool deleteData = false;
			{
				LockGuard lockGuard(&delegate->m_lock);
				if (--delegate->m_refCnt == 0)
					deleteData = true;
			}
			if (deleteData) {
				delete msg;
				delete delegate;
			}
		}
	}

	/// Called by the target thread to invoke the delegate function 
	virtual void DelegateInvoke(DelegateMsgBase** msg) {
		bool deleteData = false;
		{
			LockGuard lockGuard(&this->m_lock);
			if (this->m_refCnt == 2) {
				// Invoke the delegate function then signal the waiting thread
				DelegateFreeAsyncWaitBase<void(void)>::operator()();
				this->m_sema.Signal();
			}

			// If waiting thread is no longer waiting then delete heap data
			if (--this->m_refCnt == 0)
				deleteData = true;
		}
		if (deleteData) {
			delete *msg;
			*msg = 0;
			delete this;
		}
	}

	DelegateFreeAsyncWait& operator=(const DelegateFreeAsyncWait& rhs) {
		DelegateFreeAsyncWaitBase<void(void)>::operator=(rhs);
		return *this;
	}
};

// N=1 abstract base
template <class RetType, class Param1>
class DelegateFreeAsyncWaitBase<RetType(Param1)> : public DelegateFree<RetType(Param1)>, public IDelegateInvoker {
public:
	typedef RetType (*FreeFunc)(Param1);

	// Contructors take a class instance, member function, and delegate thread
	DelegateFreeAsyncWaitBase(FreeFunc func, DelegateThread* thread, int timeout) : m_success(false), m_timeout(timeout), m_refCnt(0) {
		Bind(func, thread);
		LockGuard::Create(&m_lock);
	}
	DelegateFreeAsyncWaitBase(const DelegateFreeAsyncWaitBase& rhs) : DelegateFree<RetType(Param1)>(rhs), m_refCnt(0) {
		LockGuard::Create(&m_lock);
		Swap(rhs);
	}
	DelegateFreeAsyncWaitBase() : m_thread(0), m_success(false), m_timeout(0), m_refCnt(0) { LockGuard::Create(&m_lock); }
	virtual ~DelegateFreeAsyncWaitBase() { LockGuard::Destroy(&m_lock); }

	/// Bind a member function to a delegate. 
	void Bind(FreeFunc func, DelegateThread* thread) {
		m_thread = thread;
		DelegateFree<RetType(Param1)>::Bind(func);
	}

	virtual bool operator==(const DelegateBase& rhs) const {
		const DelegateFreeAsyncWaitBase<RetType(Param1)>* derivedRhs = dynamic_cast<const DelegateFreeAsyncWaitBase<RetType(Param1)>*>(&rhs);
		return derivedRhs &&
			m_thread == derivedRhs->m_thread &&
			DelegateFree<RetType(Param1)>::operator==(rhs);
	}

	DelegateFreeAsyncWaitBase& operator=(const DelegateFreeAsyncWaitBase& rhs) {
		if (&rhs != this) {
			DelegateFree<RetType(Param1)>::operator=(rhs);
			Swap(rhs);
		}
		return *this;
	}

	/// Returns true if asynchronous function successfully invoked on target thread
	bool IsSuccess() { return m_success; }

protected:
	DelegateThread* m_thread;		// Target thread to invoke the delegate function
	bool m_success;					// Set to true if async function succeeds
	int m_timeout;					// Time in mS to wait for async function to invoke
	Semaphore m_sema;				// Semaphore to signal waiting thread
	LOCK m_lock;					// Lock to synchronize threads
	int m_refCnt;					// Ref count to determine when to delete objects

private:
	void Swap(const DelegateFreeAsyncWaitBase& s) {
		m_thread = s.m_thread;
		m_timeout = s.m_timeout;
		m_success = s.m_success;
	}
};

// N=1 with return type
template <class RetType, class Param1>
class DelegateFreeAsyncWait<RetType(Param1)> : public DelegateFreeAsyncWaitBase<RetType(Param1)> {
public:
	typedef RetType (*FreeFunc)(Param1);

	DelegateFreeAsyncWait(FreeFunc func, DelegateThread* thread, int timeout) :
		DelegateFreeAsyncWaitBase<RetType(Param1)>(func, thread, timeout) { }
	DelegateFreeAsyncWait() { }
	virtual DelegateFreeAsyncWait<RetType(Param1)>* Clone() const {
		return new DelegateFreeAsyncWait<RetType(Param1)>(*this);
	}

	/// Invoke delegate function asynchronously
	virtual RetType operator()(Param1 p1) {
		if (this->m_thread == 0)
			return DelegateFreeAsyncWaitBase<RetType(Param1)>::operator()(p1);
		else {
			// Create a clone instance of this delegate 
			auto delegate = Clone();
			delegate->m_refCnt = 2;
			delegate->m_sema.Create();
			delegate->m_sema.Reset();

			// Create a new message instance 
			DelegateMsg1<Param1>* msg = new DelegateMsg1<Param1>(delegate, p1);

			// Dispatch message onto the callback destination thread. DelegateInvoke()
			// will be called by the target thread. 
			this->m_thread->DispatchDelegate(msg);

			// Wait for target thread to execute the delegate function
			if ((this->m_success = delegate->m_sema.Wait(this->m_timeout)))
				m_retVal = delegate->m_retVal;

			bool deleteData = false;
			{
				LockGuard lockGuard(&delegate->m_lock);
				if (--delegate->m_refCnt == 0)
					deleteData = true;
			}
			if (deleteData) {
				delete msg;
				delete delegate;
			}
			return m_retVal;
		}
	}

	/// Called by the target thread to invoke the delegate function 
	virtual void DelegateInvoke(DelegateMsgBase** msg) {
		bool deleteData = false;
		{
			LockGuard lockGuard(&this->m_lock);
			// Typecast the base pointer to back to the templatized instance
			DelegateMsg1<Param1>* delegateMsg = static_cast<DelegateMsg1<Param1>*>(*msg);

			// Get the function parameter data
			Param1 param1 = delegateMsg->GetParam1();

			if (this->m_refCnt == 2) {
				// Invoke the delegate function then signal the waiting thread
				m_retVal = DelegateFreeAsyncWaitBase<RetType(Param1)>::operator()(param1);
				this->m_sema.Signal();
			}

			// If waiting thread is no longer waiting then delete heap data
			if (--this->m_refCnt == 0)
				deleteData = true;
		}
		if (deleteData) {
			delete *msg;
			*msg = 0;
			delete this;
		}
	}

	DelegateFreeAsyncWait& operator=(const DelegateFreeAsyncWait& rhs) {
		DelegateFreeAsyncWaitBase<RetType(Param1)>::operator=(rhs);
		return *this;
	}

	RetType GetRetVal() { return m_retVal; }
private:
	RetType m_retVal;				// The delegate return value
};

// N=1 void return type
template <class Param1>
class DelegateFreeAsyncWait<void(Param1)> : public DelegateFreeAsyncWaitBase<void(Param1)> {
public:
	typedef void (*FreeFunc)(Param1);

	DelegateFreeAsyncWait(FreeFunc func, DelegateThread* thread, int timeout) :
		DelegateFreeAsyncWaitBase<void(Param1)>(func, thread, timeout) { }
	DelegateFreeAsyncWait() { }
	virtual DelegateFreeAsyncWait<void(Param1)>* Clone() const {
		return new DelegateFreeAsyncWait<void(Param1)>(*this);
	}

	/// Invoke delegate function asynchronously
	virtual void operator()(Param1 p1) {
		if (this->m_thread == 0)
			DelegateFreeAsyncWaitBase<void(Param1)>::operator()(p1);
		else {
			// Create a clone instance of this delegate 
			auto delegate = Clone();
			delegate->m_refCnt = 2;
			delegate->m_sema.Create();
			delegate->m_sema.Reset();

			// Create a new message instance 
			DelegateMsg1<Param1>* msg = new DelegateMsg1<Param1>(delegate, p1);

			// Dispatch message onto the callback destination thread. DelegateInvoke()
			// will be called by the target thread. 
			this->m_thread->DispatchDelegate(msg);

			// Wait for target thread to execute the delegate function
			this->m_success = delegate->m_sema.Wait(this->m_timeout);

			bool deleteData = false;
			{
				LockGuard lockGuard(&delegate->m_lock);
				if (--delegate->m_refCnt == 0)
					deleteData = true;
			}
			if (deleteData) {
				delete msg;
				delete delegate;
			}
		}
	}

	/// Called by the target thread to invoke the delegate function 
	virtual void DelegateInvoke(DelegateMsgBase** msg) {
		bool deleteData = false;
		{
			LockGuard lockGuard(&this->m_lock);
			// Typecast the base pointer to back to the templatized instance
			DelegateMsg1<Param1>* delegateMsg = static_cast<DelegateMsg1<Param1>*>(*msg);

			// Get the function parameter data
			Param1 param1 = delegateMsg->GetParam1();

			if (this->m_refCnt == 2) {
				// Invoke the delegate function then signal the waiting thread
				DelegateFreeAsyncWaitBase<void(Param1)>::operator()(param1);
				this->m_sema.Signal();
			}

			// If waiting thread is no longer waiting then delete heap data
			if (--this->m_refCnt == 0)
				deleteData = true;
		}
		if (deleteData) {
			delete *msg;
			*msg = 0;
			delete this;
		}
	}

	DelegateFreeAsyncWait& operator=(const DelegateFreeAsyncWait& rhs) {
		DelegateFreeAsyncWaitBase<Param1>::operator=(rhs);
		return *this;
	}
};

// N=2 abstract base
template <class RetType, class Param1, class Param2>
class DelegateFreeAsyncWaitBase<RetType(Param1, Param2)> : public DelegateFree<RetType(Param1, Param2)>, public IDelegateInvoker {
public:
	typedef RetType (*FreeFunc)(Param1, Param2);

	// Contructors take a class instance, member function, and delegate thread
	DelegateFreeAsyncWaitBase(FreeFunc func, DelegateThread* thread, int timeout) : m_success(false), m_timeout(timeout), m_refCnt(0) {
		Bind(func, thread);
		LockGuard::Create(&m_lock);
	}
	DelegateFreeAsyncWaitBase(const DelegateFreeAsyncWaitBase& rhs) : DelegateFree<RetType(Param1, Param2)>(rhs), m_refCnt(0) {
		LockGuard::Create(&m_lock);
		Swap(rhs);
	}
	DelegateFreeAsyncWaitBase() : m_thread(0), m_success(false), m_timeout(0), m_refCnt(0) { LockGuard::Create(&m_lock); }
	virtual ~DelegateFreeAsyncWaitBase() { LockGuard::Destroy(&m_lock); }

	/// Bind a member function to a delegate. 
	void Bind(FreeFunc func, DelegateThread* thread) {
		m_thread = thread;
		DelegateFree<RetType(Param1, Param2)>::Bind(func);
	}

	virtual bool operator==(const DelegateBase& rhs) const {
		const DelegateFreeAsyncWaitBase<RetType(Param1, Param2)>* derivedRhs = dynamic_cast<const DelegateFreeAsyncWaitBase<RetType(Param1, Param2)>*>(&rhs);
		return derivedRhs &&
			m_thread == derivedRhs->m_thread &&
			DelegateFree<RetType(Param1, Param2)>::operator==(rhs);
	}

	DelegateFreeAsyncWaitBase& operator=(const DelegateFreeAsyncWaitBase& rhs) {
		if (&rhs != this) {
			DelegateFree<RetType(Param1, Param2)>::operator=(rhs);
			Swap(rhs);
		}
		return *this;
	}

	/// Returns true if asynchronous function successfully invoked on target thread
	bool IsSuccess() { return m_success; }

protected:
	DelegateThread* m_thread;		// Target thread to invoke the delegate function
	bool m_success;					// Set to true if async function succeeds
	int m_timeout;					// Time in mS to wait for async function to invoke
	Semaphore m_sema;				// Semaphore to signal waiting thread
	LOCK m_lock;					// Lock to synchronize threads
	int m_refCnt;					// Ref count to determine when to delete objects

private:
	void Swap(const DelegateFreeAsyncWaitBase& s) {
		m_thread = s.m_thread;
		m_timeout = s.m_timeout;
		m_success = s.m_success;
	}
};

// N=2 with return type
template <class RetType, class Param1, class Param2>
class DelegateFreeAsyncWait<RetType(Param1, Param2)> : public DelegateFreeAsyncWaitBase<RetType(Param1, Param2)> {
public:
	typedef RetType (*FreeFunc)(Param1, Param2);

	DelegateFreeAsyncWait(FreeFunc func, DelegateThread* thread, int timeout) :
		DelegateFreeAsyncWaitBase<RetType(Param1, Param2)>(func, thread, timeout) { }
	DelegateFreeAsyncWait() { }
	virtual DelegateFreeAsyncWait<RetType(Param1, Param2)>* Clone() const {
		return new DelegateFreeAsyncWait<RetType(Param1, Param2)>(*this);
	}

	/// Invoke delegate function asynchronously
	virtual RetType operator()(Param1 p1, Param2 p2) {
		if (this->m_thread == 0)
			return DelegateFreeAsyncWaitBase<RetType(Param1, Param2)>::operator()(p1, p2);
		else {
			// Create a clone instance of this delegate 
			auto delegate = Clone();
			delegate->m_refCnt = 2;
			delegate->m_sema.Create();
			delegate->m_sema.Reset();

			// Create a new message instance 
			DelegateMsg2<Param1, Param2>* msg = new DelegateMsg2<Param1, Param2>(delegate, p1, p2);

			// Dispatch message onto the callback destination thread. DelegateInvoke()
			// will be called by the target thread. 
			this->m_thread->DispatchDelegate(msg);

			// Wait for target thread to execute the delegate function
			if ((this->m_success = delegate->m_sema.Wait(this->m_timeout)))
				m_retVal = delegate->m_retVal;

			bool deleteData = false;
			{
				LockGuard lockGuard(&delegate->m_lock);
				if (--delegate->m_refCnt == 0)
					deleteData = true;
			}
			if (deleteData) {
				delete msg;
				delete delegate;
			}
			return m_retVal;
		}
	}

	/// Called by the target thread to invoke the delegate function 
	virtual void DelegateInvoke(DelegateMsgBase** msg) {
		bool deleteData = false;
		{
			LockGuard lockGuard(&this->m_lock);
			// Typecast the base pointer to back to the templatized instance
			DelegateMsg2<Param1, Param2>* delegateMsg = static_cast<DelegateMsg2<Param1, Param2>*>(*msg);

			// Get the function parameter data
			Param1 param1 = delegateMsg->GetParam1();
			Param2 param2 = delegateMsg->GetParam2();

			if (this->m_refCnt == 2) {
				// Invoke the delegate function then signal the waiting thread
				m_retVal = DelegateFreeAsyncWaitBase<RetType(Param1, Param2)>::operator()(param1, param2);
				this->m_sema.Signal();
			}

			// If waiting thread is no longer waiting then delete heap data
			if (--this->m_refCnt == 0)
				deleteData = true;
		}
		if (deleteData) {
			delete *msg;
			*msg = 0;
			delete this;
		}
	}

	DelegateFreeAsyncWait& operator=(const DelegateFreeAsyncWait& rhs) {
		DelegateFreeAsyncWaitBase<RetType(Param1, Param2)>::operator=(rhs);
		return *this;
	}

	RetType GetRetVal() { return m_retVal; }
private:
	RetType m_retVal;				// The delegate return value
};

// N=2 void return type
template <class Param1, class Param2>
class DelegateFreeAsyncWait<void(Param1, Param2)> : public DelegateFreeAsyncWaitBase<void(Param1, Param2)> {
public:
	typedef void (*FreeFunc)(Param1, Param2);

	DelegateFreeAsyncWait(FreeFunc func, DelegateThread* thread, int timeout) :
		DelegateFreeAsyncWaitBase<void(Param1, Param2)>(func, thread, timeout) { }
	DelegateFreeAsyncWait() { }
	virtual DelegateFreeAsyncWait<void(Param1, Param2)>* Clone() const {
		return new DelegateFreeAsyncWait<void(Param1, Param2)>(*this);
	}

	/// Invoke delegate function asynchronously
	virtual void operator()(Param1 p1, Param2 p2) {
		if (this->m_thread == 0)
			DelegateFreeAsyncWaitBase<void(Param1, Param2)>::operator()(p1, p2);
		else {
			// Create a clone instance of this delegate 
			auto delegate = Clone();
			delegate->m_refCnt = 2;
			delegate->m_sema.Create();
			delegate->m_sema.Reset();

			// Create a new message instance 
			DelegateMsg2<Param1, Param2>* msg = new DelegateMsg2<Param1, Param2>(delegate, p1, p2);

			// Dispatch message onto the callback destination thread. DelegateInvoke()
			// will be called by the target thread. 
			this->m_thread->DispatchDelegate(msg);

			// Wait for target thread to execute the delegate function
			this->m_success = delegate->m_sema.Wait(this->m_timeout);

			bool deleteData = false;
			{
				LockGuard lockGuard(&delegate->m_lock);
				if (--delegate->m_refCnt == 0)
					deleteData = true;
			}
			if (deleteData) {
				delete msg;
				delete delegate;
			}
		}
	}

	/// Called by the target thread to invoke the delegate function 
	virtual void DelegateInvoke(DelegateMsgBase** msg) {
		bool deleteData = false;
		{
			// Typecast the base pointer to back to the templatized instance
			DelegateMsg2<Param1, Param2>* delegateMsg = static_cast<DelegateMsg2<Param1, Param2>*>(*msg);

			// Get the function parameter data
			Param1 param1 = delegateMsg->GetParam1();
			Param2 param2 = delegateMsg->GetParam2();

			LockGuard lockGuard(&this->m_lock);
			if (this->m_refCnt == 2) {
				// Invoke the delegate function then signal the waiting thread
				DelegateFreeAsyncWaitBase<void(Param1, Param2)>::operator()(param1, param2);
				this->m_sema.Signal();
			}

			// If waiting thread is no longer waiting then delete heap data
			if (--this->m_refCnt == 0)
				deleteData = true;
		}
		if (deleteData) {
			delete *msg;
			*msg = 0;
			delete this;
		}
	}

	DelegateFreeAsyncWait& operator=(const DelegateFreeAsyncWait& rhs) {
		DelegateFreeAsyncWaitBase<void(Param1, Param2)>::operator=(rhs);
		return *this;
	}
};

// N=3 abstract base
template <class RetType, class Param1, class Param2, class Param3>
class DelegateFreeAsyncWaitBase<RetType(Param1, Param2, Param3)> : public DelegateFree<RetType(Param1, Param2, Param3)>, public IDelegateInvoker {
public:
	typedef RetType (*FreeFunc)(Param1, Param2, Param3);

	// Contructors take a class instance, member function, and delegate thread
	DelegateFreeAsyncWaitBase(FreeFunc func, DelegateThread* thread, int timeout) : m_success(false), m_timeout(timeout), m_refCnt(0) {
		Bind(func, thread);
		LockGuard::Create(&m_lock);
	}
	DelegateFreeAsyncWaitBase(const DelegateFreeAsyncWaitBase& rhs) : DelegateFree<RetType(Param1, Param2, Param3)>(rhs), m_refCnt(0) {
		LockGuard::Create(&m_lock);
		Swap(rhs);
	}
	DelegateFreeAsyncWaitBase() : m_thread(0), m_success(false), m_timeout(0), m_refCnt(0) { LockGuard::Create(&m_lock); }
	virtual ~DelegateFreeAsyncWaitBase() { LockGuard::Destroy(&m_lock); }

	/// Bind a member function to a delegate. 
	void Bind(FreeFunc func, DelegateThread* thread) {
		m_thread = thread;
		DelegateFree<RetType(Param1, Param2, Param3)>::Bind(func);
	}

	virtual bool operator==(const DelegateBase& rhs) const {
		const DelegateFreeAsyncWaitBase<RetType(Param1, Param2, Param3)>* derivedRhs = dynamic_cast<const DelegateFreeAsyncWaitBase<RetType(Param1, Param2, Param3)>*>(&rhs);
		return derivedRhs &&
			m_thread == derivedRhs->m_thread &&
			DelegateFree<RetType(Param1, Param2, Param3)>::operator==(rhs);
	}

	DelegateFreeAsyncWaitBase& operator=(const DelegateFreeAsyncWaitBase& rhs) {
		if (&rhs != this) {
			DelegateFree<RetType(Param1, Param2, Param3)>::operator=(rhs);
			Swap(rhs);
		}
		return *this;
	}

	/// Returns true if asynchronous function successfully invoked on target thread
	bool IsSuccess() { return m_success; }

protected:
	DelegateThread* m_thread;		// Target thread to invoke the delegate function
	bool m_success;					// Set to true if async function succeeds
	int m_timeout;					// Time in mS to wait for async function to invoke
	Semaphore m_sema;				// Semaphore to signal waiting thread
	LOCK m_lock;					// Lock to synchronize threads
	int m_refCnt;					// Ref count to determine when to delete objects

private:
	void Swap(const DelegateFreeAsyncWaitBase& s) {
		m_thread = s.m_thread;
		m_timeout = s.m_timeout;
		m_success = s.m_success;
	}
};

// N=3 with return type
template <class RetType, class Param1, class Param2, class Param3>
class DelegateFreeAsyncWait<RetType(Param1, Param2, Param3)> : public DelegateFreeAsyncWaitBase<RetType(Param1, Param2, Param3)> {
public:
	typedef RetType (*FreeFunc)(Param1, Param2, Param3);

	DelegateFreeAsyncWait(FreeFunc func, DelegateThread* thread, int timeout) :
		DelegateFreeAsyncWaitBase<RetType(Param1, Param2, Param3)>(func, thread, timeout) { }
	DelegateFreeAsyncWait() { }
	virtual DelegateFreeAsyncWait<RetType(Param1, Param2, Param3)>* Clone() const {
		return new DelegateFreeAsyncWait<RetType(Param1, Param2, Param3)>(*this);
	}

	/// Invoke delegate function asynchronously
	virtual RetType operator()(Param1 p1, Param2 p2, Param3 p3) {
		if (this->m_thread == 0)
			return DelegateFreeAsyncWaitBase<RetType(Param1, Param2, Param3)>::operator()(p1, p2, p3);
		else {
			// Create a clone instance of this delegate 
			auto delegate = Clone();
			delegate->m_refCnt = 2;
			delegate->m_sema.Create();
			delegate->m_sema.Reset();

			// Create a new message instance 
			DelegateMsg3<Param1, Param2, Param3>* msg = new DelegateMsg3<Param1, Param2, Param3>(delegate, p1, p2, p3);

			// Dispatch message onto the callback destination thread. DelegateInvoke()
			// will be called by the target thread. 
			this->m_thread->DispatchDelegate(msg);

			// Wait for target thread to execute the delegate function
			if ((this->m_success = delegate->m_sema.Wait(this->m_timeout)))
				m_retVal = delegate->m_retVal;

			bool deleteData = false;
			{
				LockGuard lockGuard(&delegate->m_lock);
				if (--delegate->m_refCnt == 0)
					deleteData = true;
			}
			if (deleteData) {
				delete msg;
				delete delegate;
			}
			return m_retVal;
		}
	}

	/// Called by the target thread to invoke the delegate function 
	virtual void DelegateInvoke(DelegateMsgBase** msg) {
		bool deleteData = false;
		{
			// Typecast the base pointer to back to the templatized instance
			DelegateMsg3<Param1, Param2, Param3>* delegateMsg = static_cast<DelegateMsg3<Param1, Param2, Param3>*>(*msg);

			// Get the function parameter data
			Param1 param1 = delegateMsg->GetParam1();
			Param2 param2 = delegateMsg->GetParam2();
			Param3 param3 = delegateMsg->GetParam3();

			LockGuard lockGuard(&this->m_lock);
			if (this->m_refCnt == 2) {
				// Invoke the delegate function then signal the waiting thread
				m_retVal = DelegateFreeAsyncWaitBase<RetType(Param1, Param2, Param3)>::operator()(param1, param2, param3);
				this->m_sema.Signal();
			}

			// If waiting thread is no longer waiting then delete heap data
			if (--this->m_refCnt == 0)
				deleteData = true;
		}
		if (deleteData) {
			delete *msg;
			*msg = 0;
			delete this;
		}
	}

	DelegateFreeAsyncWait& operator=(const DelegateFreeAsyncWait& rhs) {
		DelegateFreeAsyncWaitBase<RetType(Param1, Param2, Param3)>::operator=(rhs);
		return *this;
	}

	RetType GetRetVal() { return m_retVal; }
private:
	RetType m_retVal;				// The delegate return value
};

// N=3 void return type
template <class Param1, class Param2, class Param3>
class DelegateFreeAsyncWait<void(Param1, Param2, Param3)> : public DelegateFreeAsyncWaitBase<void(Param1, Param2, Param3)> {
public:
	typedef void (*FreeFunc)(Param1, Param2, Param3);

	DelegateFreeAsyncWait(FreeFunc func, DelegateThread* thread, int timeout) :
		DelegateFreeAsyncWaitBase<void(Param1, Param2, Param3)>(func, thread, timeout) { }
	DelegateFreeAsyncWait() { }
	virtual DelegateFreeAsyncWait<void(Param1, Param2, Param3)>* Clone() const {
		return new DelegateFreeAsyncWait<void(Param1, Param2, Param3)>(*this);
	}

	/// Invoke delegate function asynchronously
	virtual void operator()(Param1 p1, Param2 p2, Param3 p3) {
		if (this->m_thread == 0)
			DelegateFreeAsyncWaitBase<void(Param1, Param2, Param3)>::operator()(p1, p2, p3);
		else {
			// Create a clone instance of this delegate 
			auto delegate = Clone();
			delegate->m_refCnt = 2;
			delegate->m_sema.Create();
			delegate->m_sema.Reset();

			// Create a new message instance 
			DelegateMsg3<Param1, Param2, Param3>* msg = new DelegateMsg3<Param1, Param2, Param3>(delegate, p1, p2, p3);

			// Dispatch message onto the callback destination thread. DelegateInvoke()
			// will be called by the target thread. 
			this->m_thread->DispatchDelegate(msg);

			// Wait for target thread to execute the delegate function
			this->m_success = delegate->m_sema.Wait(this->m_timeout);

			bool deleteData = false;
			{
				LockGuard lockGuard(&delegate->m_lock);
				if (--delegate->m_refCnt == 0)
					deleteData = true;
			}
			if (deleteData) {
				delete msg;
				delete delegate;
			}
		}
	}

	/// Called by the target thread to invoke the delegate function 
	virtual void DelegateInvoke(DelegateMsgBase** msg) {
		bool deleteData = false;
		{
			// Typecast the base pointer to back to the templatized instance
			DelegateMsg3<Param1, Param2, Param3>* delegateMsg = static_cast<DelegateMsg3<Param1, Param2, Param3>*>(*msg);

			// Get the function parameter data
			Param1 param1 = delegateMsg->GetParam1();
			Param2 param2 = delegateMsg->GetParam2();
			Param3 param3 = delegateMsg->GetParam3();

			LockGuard lockGuard(&this->m_lock);
			if (this->m_refCnt == 2) {
				// Invoke the delegate function then signal the waiting thread
				DelegateFreeAsyncWaitBase<void(Param1, Param2, Param3)>::operator()(param1, param2, param3);
				this->m_sema.Signal();
			}

			// If waiting thread is no longer waiting then delete heap data
			if (--this->m_refCnt == 0)
				deleteData = true;
		}
		if (deleteData) {
			delete *msg;
			*msg = 0;
			delete this;
		}
	}

	DelegateFreeAsyncWait& operator=(const DelegateFreeAsyncWait& rhs) {
		DelegateFreeAsyncWaitBase<void(Param1, Param2, Param3)>::operator=(rhs);
		return *this;
	}
};

// N=4 abstract base
template <class RetType, class Param1, class Param2, class Param3, class Param4>
class DelegateFreeAsyncWaitBase<RetType(Param1, Param2, Param3, Param4)> : public DelegateFree<RetType(Param1, Param2, Param3, Param4)>, public IDelegateInvoker {
public:
	typedef RetType (*FreeFunc)(Param1, Param2, Param3, Param4);

	// Contructors take a class instance, member function, and delegate thread
	DelegateFreeAsyncWaitBase(FreeFunc func, DelegateThread* thread, int timeout) : m_success(false), m_timeout(timeout), m_refCnt(0) {
		Bind(func, thread);
		LockGuard::Create(&m_lock);
	}
	DelegateFreeAsyncWaitBase(const DelegateFreeAsyncWaitBase& rhs) : DelegateFree<RetType(Param1, Param2, Param3, Param4)>(rhs), m_refCnt(0) {
		LockGuard::Create(&m_lock);
		Swap(rhs);
	}
	DelegateFreeAsyncWaitBase() : m_thread(0), m_success(false), m_timeout(0), m_refCnt(0) { LockGuard::Create(&m_lock); }
	virtual ~DelegateFreeAsyncWaitBase() { LockGuard::Destroy(&m_lock); }

	/// Bind a member function to a delegate. 
	void Bind(FreeFunc func, DelegateThread* thread) {
		m_thread = thread;
		DelegateFree<RetType(Param1, Param2, Param3, Param4)>::Bind(func);
	}

	virtual bool operator==(const DelegateBase& rhs) const {
		const DelegateFreeAsyncWaitBase<RetType(Param1, Param2, Param3, Param4)>* derivedRhs = dynamic_cast<const DelegateFreeAsyncWaitBase<RetType(Param1, Param2, Param3, Param4)>*>(&rhs);
		return derivedRhs &&
			m_thread == derivedRhs->m_thread &&
			DelegateFree<RetType(Param1, Param2, Param3, Param4)>::operator==(rhs);
	}

	DelegateFreeAsyncWaitBase& operator=(const DelegateFreeAsyncWaitBase& rhs) {
		if (&rhs != this) {
			DelegateFree<RetType(Param1, Param2, Param3, Param4)>::operator=(rhs);
			Swap(rhs);
		}
		return *this;
	}

	/// Returns true if asynchronous function successfully invoked on target thread
	bool IsSuccess() { return m_success; }

protected:
	DelegateThread* m_thread;		// Target thread to invoke the delegate function
	bool m_success;					// Set to true if async function succeeds
	int m_timeout;					// Time in mS to wait for async function to invoke
	Semaphore m_sema;				// Semaphore to signal waiting thread
	LOCK m_lock;					// Lock to synchronize threads
	int m_refCnt;					// Ref count to determine when to delete objects

private:
	void Swap(const DelegateFreeAsyncWaitBase& s) {
		m_thread = s.m_thread;
		m_timeout = s.m_timeout;
		m_success = s.m_success;
	}
};

// N=4 with return type
template <class RetType, class Param1, class Param2, class Param3, class Param4>
class DelegateFreeAsyncWait<RetType(Param1, Param2, Param3, Param4)> : public DelegateFreeAsyncWaitBase<RetType(Param1, Param2, Param3, Param4)> {
public:
	typedef RetType (*FreeFunc)(Param1, Param2, Param3, Param4);

	DelegateFreeAsyncWait(FreeFunc func, DelegateThread* thread, int timeout) :
		DelegateFreeAsyncWaitBase<RetType(Param1, Param2, Param3, Param4)>(func, thread, timeout) { }
	DelegateFreeAsyncWait() { }
	virtual DelegateFreeAsyncWait<RetType(Param1, Param2, Param3, Param4)>* Clone() const {
		return new DelegateFreeAsyncWait<RetType(Param1, Param2, Param3, Param4)>(*this);
	}

	/// Invoke delegate function asynchronously
	virtual RetType operator()(Param1 p1, Param2 p2, Param3 p3, Param4 p4) {
		if (this->m_thread == 0)
			return DelegateFreeAsyncWaitBase<RetType(Param1, Param2, Param3, Param4)>::operator()(p1, p2, p3, p4);
		else {
			// Create a clone instance of this delegate 
			auto delegate = Clone();
			delegate->m_refCnt = 2;
			delegate->m_sema.Create();
			delegate->m_sema.Reset();

			// Create a new message instance 
			DelegateMsg4<Param1, Param2, Param3, Param4>* msg = new DelegateMsg4<Param1, Param2, Param3, Param4>(delegate, p1, p2, p3, p4);

			// Dispatch message onto the callback destination thread. DelegateInvoke()
			// will be called by the target thread. 
			this->m_thread->DispatchDelegate(msg);

			// Wait for target thread to execute the delegate function
			if ((this->m_success = delegate->m_sema.Wait(this->m_timeout)))
				m_retVal = delegate->m_retVal;

			bool deleteData = false;
			{
				LockGuard lockGuard(&delegate->m_lock);
				if (--delegate->m_refCnt == 0)
					deleteData = true;
			}
			if (deleteData) {
				delete msg;
				delete delegate;
			}
			return m_retVal;
		}
	}

	/// Called by the target thread to invoke the delegate function 
	virtual void DelegateInvoke(DelegateMsgBase** msg) {
		bool deleteData = false;
		{
			// Typecast the base pointer to back to the templatized instance
			DelegateMsg4<Param1, Param2, Param3, Param4>* delegateMsg = static_cast<DelegateMsg4<Param1, Param2, Param3, Param4>*>(*msg);

			// Get the function parameter data
			Param1 param1 = delegateMsg->GetParam1();
			Param2 param2 = delegateMsg->GetParam2();
			Param3 param3 = delegateMsg->GetParam3();
			Param4 param4 = delegateMsg->GetParam4();

			LockGuard lockGuard(&this->m_lock);
			if (this->m_refCnt == 2) {
				// Invoke the delegate function then signal the waiting thread
				m_retVal = DelegateFreeAsyncWaitBase<RetType(Param1, Param2, Param3, Param4)>::operator()(param1, param2, param3, param4);
				this->m_sema.Signal();
			}

			// If waiting thread is no longer waiting then delete heap data
			if (--this->m_refCnt == 0)
				deleteData = true;
		}
		if (deleteData) {
			delete *msg;
			*msg = 0;
			delete this;
		}
	}

	DelegateFreeAsyncWait& operator=(const DelegateFreeAsyncWait& rhs) {
		DelegateFreeAsyncWaitBase<RetType(Param1, Param2, Param3, Param4)>::operator=(rhs);
		return *this;
	}

	RetType GetRetVal() { return m_retVal; }
private:
	RetType m_retVal;				// The delegate return value
};

// N=4 void return type
template <class Param1, class Param2, class Param3, class Param4>
class DelegateFreeAsyncWait<void(Param1, Param2, Param3, Param4)> : public DelegateFreeAsyncWaitBase<void(Param1, Param2, Param3, Param4)> {
public:
	typedef void (*FreeFunc)(Param1, Param2, Param3, Param4);

	DelegateFreeAsyncWait(FreeFunc func, DelegateThread* thread, int timeout) :
		DelegateFreeAsyncWaitBase<void(Param1, Param2, Param3, Param4)>(func, thread, timeout) { }
	DelegateFreeAsyncWait() { }
	virtual DelegateFreeAsyncWait<void(Param1, Param2, Param3, Param4)>* Clone() const {
		return new DelegateFreeAsyncWait<void(Param1, Param2, Param3, Param4)>(*this);
	}

	/// Invoke delegate function asynchronously
	virtual void operator()(Param1 p1, Param2 p2, Param3 p3, Param4 p4) {
		if (this->m_thread == 0)
			DelegateFreeAsyncWaitBase<void(Param1, Param2, Param3, Param4)>::operator()(p1, p2, p3, p4);
		else {
			// Create a clone instance of this delegate 
			auto delegate = Clone();
			delegate->m_refCnt = 2;
			delegate->m_sema.Create();
			delegate->m_sema.Reset();

			// Create a new message instance 
			DelegateMsg4<Param1, Param2, Param3, Param4>* msg = new DelegateMsg4<Param1, Param2, Param3, Param4>(delegate, p1, p2, p3, p4);

			// Dispatch message onto the callback destination thread. DelegateInvoke()
			// will be called by the target thread. 
			this->m_thread->DispatchDelegate(msg);

			// Wait for target thread to execute the delegate function
			this->m_success = delegate->m_sema.Wait(this->m_timeout);

			bool deleteData = false;
			{
				LockGuard lockGuard(&delegate->m_lock);
				if (--delegate->m_refCnt == 0)
					deleteData = true;
			}
			if (deleteData) {
				delete msg;
				delete delegate;
			}
		}
	}

	/// Called by the target thread to invoke the delegate function 
	virtual void DelegateInvoke(DelegateMsgBase** msg) {
		bool deleteData = false;
		{
			// Typecast the base pointer to back to the templatized instance
			DelegateMsg4<Param1, Param2, Param3, Param4>* delegateMsg = static_cast<DelegateMsg4<Param1, Param2, Param3, Param4>*>(*msg);

			// Get the function parameter data
			Param1 param1 = delegateMsg->GetParam1();
			Param2 param2 = delegateMsg->GetParam2();
			Param3 param3 = delegateMsg->GetParam3();
			Param4 param4 = delegateMsg->GetParam4();

			LockGuard lockGuard(&this->m_lock);
			if (this->m_refCnt == 2) {
				// Invoke the delegate function then signal the waiting thread
				DelegateFreeAsyncWaitBase<void(Param1, Param2, Param3, Param4)>::operator()(param1, param2, param3, param4);
				this->m_sema.Signal();
			}

			// If waiting thread is no longer waiting then delete heap data
			if (--this->m_refCnt == 0)
				deleteData = true;
		}
		if (deleteData) {
			delete *msg;
			*msg = 0;
			delete this;
		}
	}

	DelegateFreeAsyncWait& operator=(const DelegateFreeAsyncWait& rhs) {
		DelegateFreeAsyncWaitBase<void(Param1, Param2, Param3, Param4)>::operator=(rhs);
		return *this;
	}
};

// N=5 abstract base
template <class RetType, class Param1, class Param2, class Param3, class Param4, class Param5>
class DelegateFreeAsyncWaitBase<RetType(Param1, Param2, Param3, Param4, Param5)> : public DelegateFree<RetType(Param1, Param2, Param3, Param4, Param5)>, public IDelegateInvoker {
public:
	typedef RetType (*FreeFunc)(Param1, Param2, Param3, Param4, Param5);

	// Contructors take a class instance, member function, and delegate thread
	DelegateFreeAsyncWaitBase(FreeFunc func, DelegateThread* thread, int timeout) : m_success(false), m_timeout(timeout), m_refCnt(0) {
		Bind(func, thread);
		LockGuard::Create(&m_lock);
	}
	DelegateFreeAsyncWaitBase(const DelegateFreeAsyncWaitBase& rhs) : DelegateFree<RetType(Param1, Param2, Param3, Param4, Param5)>(rhs), m_refCnt(0) {
		LockGuard::Create(&m_lock);
		Swap(rhs);
	}
	DelegateFreeAsyncWaitBase() : m_thread(0), m_success(false), m_timeout(0), m_refCnt(0) { LockGuard::Create(&m_lock); }
	virtual ~DelegateFreeAsyncWaitBase() { LockGuard::Destroy(&m_lock); }

	/// Bind a member function to a delegate. 
	void Bind(FreeFunc func, DelegateThread* thread) {
		m_thread = thread;
		DelegateFree<RetType(Param1, Param2, Param3, Param4, Param5)>::Bind(func);
	}

	virtual bool operator==(const DelegateBase& rhs) const {
		const DelegateFreeAsyncWaitBase<RetType(Param1, Param2, Param3, Param4, Param5)>* derivedRhs = dynamic_cast<const DelegateFreeAsyncWaitBase<RetType(Param1, Param2, Param3, Param4, Param5)>*>(&rhs);
		return derivedRhs &&
			m_thread == derivedRhs->m_thread &&
			DelegateFree<RetType(Param1, Param2, Param3, Param4, Param5)>::operator==(rhs);
	}

	DelegateFreeAsyncWaitBase& operator=(const DelegateFreeAsyncWaitBase& rhs) {
		if (&rhs != this) {
			DelegateFree<RetType(Param1, Param2, Param3, Param4, Param5)>::operator=(rhs);
			Swap(rhs);
		}
		return *this;
	}

	/// Returns true if asynchronous function successfully invoked on target thread
	bool IsSuccess() { return m_success; }

protected:
	DelegateThread* m_thread;		// Target thread to invoke the delegate function
	bool m_success;					// Set to true if async function succeeds
	int m_timeout;					// Time in mS to wait for async function to invoke
	Semaphore m_sema;				// Semaphore to signal waiting thread
	LOCK m_lock;					// Lock to synchronize threads
	int m_refCnt;					// Ref count to determine when to delete objects

private:
	void Swap(const DelegateFreeAsyncWaitBase& s) {
		m_thread = s.m_thread;
		m_timeout = s.m_timeout;
		m_success = s.m_success;
	}
};

// N=5 with return type
template <class RetType, class Param1, class Param2, class Param3, class Param4, class Param5>
class DelegateFreeAsyncWait<RetType(Param1, Param2, Param3, Param4, Param5)> : public DelegateFreeAsyncWaitBase<RetType(Param1, Param2, Param3, Param4, Param5)> {
public:
	typedef RetType (*FreeFunc)(Param1, Param2, Param3, Param4, Param5);

	DelegateFreeAsyncWait(FreeFunc func, DelegateThread* thread, int timeout) :
		DelegateFreeAsyncWaitBase<RetType(Param1, Param2, Param3, Param4, Param5)>(func, thread, timeout) { }
	DelegateFreeAsyncWait() { }
	virtual DelegateFreeAsyncWait<RetType(Param1, Param2, Param3, Param4, Param5)>* Clone() const {
		return new DelegateFreeAsyncWait<RetType(Param1, Param2, Param3, Param4, Param5)>(*this);
	}

	/// Invoke delegate function asynchronously
	virtual RetType operator()(Param1 p1, Param2 p2, Param3 p3, Param4 p4, Param5 p5) {
		if (this->m_thread == 0)
			return DelegateFreeAsyncWaitBase<RetType(Param1, Param2, Param3, Param4, Param5)>::operator()(p1, p2, p3, p4, p5);
		else {
			// Create a clone instance of this delegate 
			auto delegate = Clone();
			delegate->m_refCnt = 2;
			delegate->m_sema.Create();
			delegate->m_sema.Reset();

			// Create a new message instance 
			DelegateMsg5<Param1, Param2, Param3, Param4, Param5>* msg = new DelegateMsg5<Param1, Param2, Param3, Param4, Param5>(delegate, p1, p2, p3, p4, p5);

			// Dispatch message onto the callback destination thread. DelegateInvoke()
			// will be called by the target thread. 
			this->m_thread->DispatchDelegate(msg);

			// Wait for target thread to execute the delegate function
			if ((this->m_success = delegate->m_sema.Wait(this->m_timeout)))
				m_retVal = delegate->m_retVal;

			bool deleteData = false;
			{
				LockGuard lockGuard(&delegate->m_lock);
				if (--delegate->m_refCnt == 0)
					deleteData = true;
			}
			if (deleteData) {
				delete msg;
				delete delegate;
			}
			return m_retVal;
		}
	}

	/// Called by the target thread to invoke the delegate function 
	virtual void DelegateInvoke(DelegateMsgBase** msg) {
		bool deleteData = false;
		{
			// Typecast the base pointer to back to the templatized instance
			DelegateMsg5<Param1, Param2, Param3, Param4, Param5>* delegateMsg = static_cast<DelegateMsg5<Param1, Param2, Param3, Param4, Param5>*>(*msg);

			// Get the function parameter data
			Param1 param1 = delegateMsg->GetParam1();
			Param2 param2 = delegateMsg->GetParam2();
			Param3 param3 = delegateMsg->GetParam3();
			Param4 param4 = delegateMsg->GetParam4();
			Param5 param5 = delegateMsg->GetParam5();

			LockGuard lockGuard(&this->m_lock);
			if (this->m_refCnt == 2) {
				// Invoke the delegate function then signal the waiting thread
				m_retVal = DelegateFreeAsyncWaitBase<RetType(Param1, Param2, Param3, Param4, Param5)>::operator()(param1, param2, param3, param4, param5);
				this->m_sema.Signal();
			}

			// If waiting thread is no longer waiting then delete heap data
			if (--this->m_refCnt == 0)
				deleteData = true;
		}
		if (deleteData) {
			delete *msg;
			*msg = 0;
			delete this;
		}
	}

	DelegateFreeAsyncWait& operator=(const DelegateFreeAsyncWait& rhs) {
		DelegateFreeAsyncWaitBase<RetType(Param1, Param2, Param3, Param4, Param5)>::operator=(rhs);
		return *this;
	}

	RetType GetRetVal() { return m_retVal; }
private:
	RetType m_retVal;				// The delegate return value
};

// N=5 void return type
template <class Param1, class Param2, class Param3, class Param4, class Param5>
class DelegateFreeAsyncWait<void(Param1, Param2, Param3, Param4, Param5)> : public DelegateFreeAsyncWaitBase<void(Param1, Param2, Param3, Param4, Param5)> {
public:
	typedef void (*FreeFunc)(Param1, Param2, Param3, Param4, Param5);

	DelegateFreeAsyncWait(FreeFunc func, DelegateThread* thread, int timeout) :
		DelegateFreeAsyncWaitBase<void(Param1, Param2, Param3, Param4, Param5)>(func, thread, timeout) { }
	DelegateFreeAsyncWait() { }
	virtual DelegateFreeAsyncWait<void(Param1, Param2, Param3, Param4, Param5)>* Clone() const {
		return new DelegateFreeAsyncWait<void(Param1, Param2, Param3, Param4, Param5)>(*this);
	}

	/// Invoke delegate function asynchronously
	virtual void operator()(Param1 p1, Param2 p2, Param3 p3, Param4 p4, Param5 p5) {
		if (this->m_thread == 0)
			DelegateFreeAsyncWaitBase<void(Param1, Param2, Param3, Param4, Param5)>::operator()(p1, p2, p3, p4, p5);
		else {
			// Create a clone instance of this delegate 
			auto delegate = Clone();
			delegate->m_refCnt = 2;
			delegate->m_sema.Create();
			delegate->m_sema.Reset();

			// Create a new message instance 
			DelegateMsg5<Param1, Param2, Param3, Param4, Param5>* msg = new DelegateMsg5<Param1, Param2, Param3, Param4, Param5>(delegate, p1, p2, p3, p4, p5);

			// Dispatch message onto the callback destination thread. DelegateInvoke()
			// will be called by the target thread. 
			this->m_thread->DispatchDelegate(msg);

			// Wait for target thread to execute the delegate function
			this->m_success = delegate->m_sema.Wait(this->m_timeout);

			bool deleteData = false;
			{
				LockGuard lockGuard(&delegate->m_lock);
				if (--delegate->m_refCnt == 0)
					deleteData = true;
			}
			if (deleteData) {
				delete msg;
				delete delegate;
			}
		}
	}

	/// Called by the target thread to invoke the delegate function 
	virtual void DelegateInvoke(DelegateMsgBase** msg) {
		bool deleteData = false;
		{
			// Typecast the base pointer to back to the templatized instance
			DelegateMsg5<Param1, Param2, Param3, Param4, Param5>* delegateMsg = static_cast<DelegateMsg5<Param1, Param2, Param3, Param4, Param5>*>(*msg);

			// Get the function parameter data
			Param1 param1 = delegateMsg->GetParam1();
			Param2 param2 = delegateMsg->GetParam2();
			Param3 param3 = delegateMsg->GetParam3();
			Param4 param4 = delegateMsg->GetParam4();
			Param5 param5 = delegateMsg->GetParam5();

			LockGuard lockGuard(&this->m_lock);
			if (this->m_refCnt == 2) {
				// Invoke the delegate function then signal the waiting thread
				DelegateFreeAsyncWaitBase<void(Param1, Param2, Param3, Param4, Param5)>::operator()(param1, param2, param3, param4, param5);
				this->m_sema.Signal();
			}

			// If waiting thread is no longer waiting then delete heap data
			if (--this->m_refCnt == 0)
				deleteData = true;
		}
		if (deleteData) {
			delete *msg;
			*msg = 0;
			delete this;
		}
	}

	DelegateFreeAsyncWait& operator=(const DelegateFreeAsyncWait& rhs) {
		DelegateFreeAsyncWaitBase<void(Param1, Param2, Param3, Param4, Param5)>::operator=(rhs);
		return *this;
	}
};

//N=0
template <class TClass, class RetType>
DelegateMemberAsyncWait<RetType(TClass(void))> MakeDelegate(TClass* object, RetType (TClass::*func)(), DelegateThread* thread, int timeout) { 
	return DelegateMemberAsyncWait<RetType(TClass(void))>(object, func, thread, timeout);
}

template <class TClass, class RetType>
DelegateMemberAsyncWait<RetType(TClass(void))> MakeDelegate(TClass* object, RetType (TClass::*func)() const, DelegateThread* thread, int timeout) {
	return DelegateMemberAsyncWait<RetType(TClass(void))>(object, func, thread, timeout);
}

template <class RetType>
DelegateFreeAsyncWait<RetType(void)> MakeDelegate(RetType (*func)(), DelegateThread* thread, int timeout) { 
	return DelegateFreeAsyncWait<RetType(void)>(func, thread, timeout);
}

//N=1
template <class TClass, class Param1, class RetType>
DelegateMemberAsyncWait<RetType(TClass(Param1))> MakeDelegate(TClass* object, RetType (TClass::*func)(Param1 p1), DelegateThread* thread, int timeout) {
	return DelegateMemberAsyncWait<RetType(TClass(Param1))>(object, func, thread, timeout);
}

template <class TClass, class Param1, class RetType>
DelegateMemberAsyncWait<RetType(TClass(Param1))> MakeDelegate(TClass* object, RetType (TClass::*func)(Param1 p1) const, DelegateThread* thread, int timeout) {
	return DelegateMemberAsyncWait<RetType(TClass(Param1))>(object, func, thread, timeout);
}

template <class Param1, class RetType>
DelegateFreeAsyncWait<RetType(Param1)> MakeDelegate(RetType (*func)(Param1 p1), DelegateThread* thread, int timeout) {
	return DelegateFreeAsyncWait<RetType(Param1)>(func, thread, timeout);
}

//N=2
template <class TClass, class Param1, class Param2, class RetType>
DelegateMemberAsyncWait<RetType(TClass(Param1, Param2))> MakeDelegate(TClass* object, RetType (TClass::*func)(Param1 p1, Param2 p2), DelegateThread* thread, int timeout) {
	return DelegateMemberAsyncWait<RetType(TClass(Param1, Param2))>(object, func, thread, timeout);
}

template <class TClass, class Param1, class Param2, class RetType>
DelegateMemberAsyncWait<RetType(TClass(Param1, Param2))> MakeDelegate(TClass* object, RetType (TClass::*func)(Param1 p1, Param2 p2) const, DelegateThread* thread, int timeout) {
	return DelegateMemberAsyncWait<RetType(TClass(Param1, Param2))>(object, func, thread, timeout);
}

template <class Param1, class Param2, class RetType>
DelegateFreeAsyncWait<RetType(Param1, Param2)> MakeDelegate(RetType (*func)(Param1 p1, Param2 p2), DelegateThread* thread, int timeout) {
	return DelegateFreeAsyncWait<RetType(Param1, Param2)>(func, thread, timeout);
}

//N=3
template <class TClass, class Param1, class Param2, class Param3, class RetType>
DelegateMemberAsyncWait<RetType(TClass(Param1, Param2, Param3))> MakeDelegate(TClass* object, RetType (TClass::*func)(Param1 p1, Param2 p2, Param3 p3), DelegateThread* thread, int timeout) {
	return DelegateMemberAsyncWait<RetType(TClass(Param1, Param2, Param3))>(object, func, thread, timeout);
}

template <class TClass, class Param1, class Param2, class Param3, class RetType>
DelegateMemberAsyncWait<RetType(TClass(Param1, Param2, Param3))> MakeDelegate(TClass* object, RetType (TClass::*func)(Param1 p1, Param2 p2, Param3 p3) const, DelegateThread* thread, int timeout) {
	return DelegateMemberAsyncWait<RetType(TClass(Param1, Param2, Param3))>(object, func, thread, timeout);
}

template <class Param1, class Param2, class Param3, class RetType>
DelegateFreeAsyncWait<RetType(Param1, Param2, Param3)> MakeDelegate(RetType (*func)(Param1 p1, Param2 p2, Param3 p3), DelegateThread* thread, int timeout) {
	return DelegateFreeAsyncWait<RetType(Param1, Param2, Param3)>(func, thread, timeout);
}

//N=4
template <class TClass, class Param1, class Param2, class Param3, class Param4, class RetType>
DelegateMemberAsyncWait<RetType(TClass(Param1, Param2, Param3, Param4))> MakeDelegate(TClass* object, RetType (TClass::*func)(Param1 p1, Param2 p2, Param3 p3, Param4 p4), DelegateThread* thread, int timeout) {
	return DelegateMemberAsyncWait<RetType(TClass(Param1, Param2, Param3, Param4))>(object, func, thread, timeout);
}

template <class TClass, class Param1, class Param2, class Param3, class Param4, class RetType>
DelegateMemberAsyncWait<RetType(TClass(Param1, Param2, Param3, Param4))> MakeDelegate(TClass* object, RetType (TClass::*func)(Param1 p1, Param2 p2, Param3 p3, Param4 p4) const, DelegateThread* thread, int timeout) {
	return DelegateMemberAsyncWait<RetType(TClass(Param1, Param2, Param3, Param4))>(object, func, thread, timeout);
}

template <class Param1, class Param2, class Param3, class Param4, class RetType>
DelegateFreeAsyncWait<RetType(Param1, Param2, Param3, Param4)> MakeDelegate(RetType (*func)(Param1 p1, Param2 p2, Param3 p3, Param4 p4), DelegateThread* thread, int timeout) {
	return DelegateFreeAsyncWait<RetType(Param1, Param2, Param3, Param4)>(func, thread, timeout);
}

//N=5
template <class TClass, class Param1, class Param2, class Param3, class Param4, class Param5, class RetType>
DelegateMemberAsyncWait<RetType(TClass(Param1, Param2, Param3, Param4, Param5))> MakeDelegate(TClass* object, RetType (TClass::*func)(Param1 p1, Param2 p2, Param3 p3, Param4 p4, Param5 p5), DelegateThread* thread, int timeout) {
	return DelegateMemberAsyncWait<RetType(TClass(Param1, Param2, Param3, Param4, Param5))>(object, func, thread, timeout);
}

template <class TClass, class Param1, class Param2, class Param3, class Param4, class Param5, class RetType>
DelegateMemberAsyncWait<RetType(TClass(Param1, Param2, Param3, Param4, Param5))> MakeDelegate(TClass* object, RetType (TClass::*func)(Param1 p1, Param2 p2, Param3 p3, Param4 p4, Param5 p5) const, DelegateThread* thread, int timeout) {
	return DelegateMemberAsyncWait<RetType(TClass(Param1, Param2, Param3, Param4, Param5))>(object, func, thread, timeout);
}

template <class Param1, class Param2, class Param3, class Param4, class Param5, class RetType>
DelegateFreeAsyncWait<RetType(Param1, Param2, Param3, Param4, Param5)> MakeDelegate(RetType (*func)(Param1 p1, Param2 p2, Param3 p3, Param4 p4, Param5 p5), DelegateThread* thread, int timeout) {
	return DelegateFreeAsyncWait<RetType(Param1, Param2, Param3, Param4, Param5)>(func, thread, timeout);
}

} 

#endif
