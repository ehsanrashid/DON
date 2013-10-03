/************************************************************
*          This is a part of the VideoClick project.
*               Copyright (C) 1998 Vsoft Ltd.
*                   All rights reserved.
* NAME: StackTracer.h
*
* Description:
*
* The classes in this file are used by applications requesting a stack trace to be recorded
* in the logger.
* A trace of all function calls can be written to a text file for postmortem analysis.
*
* Each class in the application should contain a member of class StackTracer.
* This member access the COM object and passes information to it.
* 
* When a function is entered, the first line ( possibly after declarations) has to be a
* call to StackTracer::Add(), and the last call before leaving a function has to be a call 
* to StackTracer::Remove() . 
* The data registered when calling Add() is the string passed to it, and 32 bytes of the current
* stack. ( This stack dump can be used to see the arguments passed to the function).
*
* ACTIVATING THE TRACING:
* This code is activated by setting two registry entries. 
* To activate the tracer, set
*  "software\vsoft\videoClick\logger\StackTraceIsActive" to 0x1 .
*
* To activate dumping of function calls to a text file, set
*	"software\vsoft\videoClick\logger\StackTraceFuncDump" to 0x1.
* If a value is zero, or nonexistant, the default is NON ACTIVE.
*
* If StackTraceFuncDump is active, a file name has to be supplied in 
*	"software\vsoft\videoClick\logger\TraceFileName" (REGSZ).
* If this value is missing, no error is reported and the dumping is turned off.
*
*
* WHAT DOES IT COST? (cpu)
* Tests performed on Pentium II 266MHz. compiled: release, maximize speed + any suitable inline,
*	code generation: "pentium pro".
* Without the STACK_TRACE() : 4.55 uSec
* With STACK_TRACE(), StackTraceIsActive = false: 5.00 uSec
* With STACK_TRACE(), StackTraceIsActive = true : 6.00 uSec
* meaning the cost is 1.5uSec / 0.5 uSec  for using the code (enabled/disabled)
*
* // noam 16-Jul-01: Added dumping trace to file and macros for the handler.
* // noam 06-Jun-02: Added macro to format the dump file as XML (for XML tree viewers),
*					 Changed the COM object to be global.
*
* NOTE:
*	1) Failures are not reported. ( well, maybe to a debug window if it is present)
*	2) You must call CoInitialize***() before constructing an object that contains
*		a StackTracer.
*	3) Call to CoUnintialize() must be only after destructing all objects containing
*		a StackTracer.
*	4) TO TURN IT OFF COMPLETELY, AND HAVE ZERO IMPACT, DEFINE THE "NO_STACKTRACE" MACRO BELOW.
*
* -------
* To make life easier, there is a macro that should be used as following:
* 
*
*  in every function body:
*
*  int MyClass::f(int a)
*  {
*	STACK_TRACE("MyClass::f()");   <--- this is the ONLY thing you have to put in the function
*	// your code here
*	}
*
* 
* --------------------------------------
* Supporting exception handling:
* when an unahdled exception happens, the content of the stack trace can be dumped to the logger.
*
* sample:
*
*  DECLARE_TOP_HANDLER // <<<<< HERE
*
* main()
*{
*	SET_TOP_HANDLER // <<<< AND HERE
* ...
*}
*
* Author: Noam Cohen
* 
************************************************************/

#ifndef STACK_TRACER_H_
#define STACK_TRACER_H_


// if you do not want ANY stack trace, open the line below
//#define NO_STACKTRACE

#ifdef NO_STACKTRACE
// supply dummy macro definitions 
#define STACK_TRACE(msg)  
#define DECLARE_TOP_HANDLER
#define SET_TOP_HANDLER
#pragma message("INFO: STACK_TRACE macro is disabled.")

#else

#pragma message("INFO: STACK_TRACE macro is ENABLED.")

//#include "Logger/IStackTrace.h"
//#include "Logger.h" // for exception logging support
#include "tri_logger.h"
#include "stdio.h"  // for sprintf()


// if an application uses load time DLLs and static linking, a singleton object
// can be made by creating a global variable of StackTracer.
// Since the code we use is COM based, using run time loading, the loader creates
// clashes between references to globals from DLLs.
// Therefore we use a singleton COM server object to do the job.
//
// Keep a stack trace for each thread. 
// Each function wishing to be listed in the trace has to call Add() and Remove()
class StackTracer
{

public:

    StackTracer()
    {
        CoInitialize(0);
        // get a pointer to the tracer. This is a singleton object ( one object in
        // the system ). Currently, one object per process.
        HRESULT hr = CoCreateInstance(CLSID_StackTrace,NULL,CLSCTX_INPROC_SERVER,
            IID_StackTrace,(void**)&m_pTracer); 

        if (FAILED(hr))
        {
#ifdef _DEBUG
            char msg[100];
            sprintf(msg, "[StackTracer] Failed launching the COM object (0x%X)",hr);
            TRI_LOG_MSG(msg << "\n");
#endif
            m_pTracer = NULL;
            m_bIsActive = false;
        }
        else
            m_bIsActive = m_pTracer->IsActive();
    }

    ~StackTracer()
    {
        Cleanup();
    }

    void Add(IN const char* message,	// null terminated string
        const char* buffer				// arbitrary buffer content up to 32 bytes long
        )
    {
        if(m_bIsActive && (NULL != m_pTracer))
            m_pTracer->Add(message,buffer );
    }

    void Remove()// Remove the top entry for current thread.no need for name here - since its a LIFO
    {
        if(m_bIsActive && NULL != m_pTracer)
            m_pTracer->Remove();
    }
    // release the internal Trace object - and among other things it will close files.
    void Cleanup()
    {
        if(NULL != m_pTracer)
        {
            m_pTracer->Release();
            m_pTracer = NULL;
        }
        CoUninitialize();
    }

private:
    IStackTrace * m_pTracer;   // the COM object
    bool          m_bIsActive; // true if work should be done.
};

// this is needed for accessing the stackTracer from the exception handler
extern	StackTracer g_StackTracer; 

// helper class for making life easier for the users of the StackTracer
// When entering a function, simply construct an object of this class
class AutoStackTrackEntry
{
public:
    AutoStackTrackEntry(IN const char* message ,const char* _ebp)
    {
        g_StackTracer.Add(message,_ebp);
    }

    ~AutoStackTrackEntry()
    {
        g_StackTracer.Remove();
    }
};

// noam 09-Jun-02: This version uses one global object.
// Now it is possible to have stack tracing for any function, and classes are not modified.
//
// macro to make life even easier using AutoStackTrackEntry.
#define STACK_TRACE(msg)    \
    unsigned int _ebp;      \
{__asm mov _ebp,ebp }       \
    AutoStackTrackEntry  _stackEntry(msg, reinterpret_cast<char*>(_ebp + 4))


#define DECLARE_TOP_HANDLER \
    LPTOP_LEVEL_EXCEPTION_FILTER  TopExceptionHandler::m_pOldHandler = NULL; 


#define DECLARE_STACKTRACE  \
    StackTracer	g_StackTracer;
#define		SET_TOP_HANDLER \
    TopExceptionHandler::RegisterHandler(); 

// --- support for fatal exception handling ------------------------------------
class TopExceptionHandler
{
public:
    static void RegisterHandler()
    {
        // set the top handler
        m_pOldHandler =  SetUnhandledExceptionFilter( TopExceptionHandler::TopHandler);
    }
    static void UnRegisterHandler()
    {
        SetUnhandledExceptionFilter( m_pOldHandler);
    }

private:
    // the handler is called in the context of the offending thread.
    // more than one thread can get here concurrently.
    static long __stdcall TopHandler(LPEXCEPTION_POINTERS arg)
    {
        UnRegisterHandler(); // avoid recursion...

        CLogger logger;
        logger.Init("Unahdled exception");

        logger.LogEvent(LOG_SEVERITY_FATAL,"exception args will come here...");

        // close the tracer - at least we shall have the sequence to the crash.
        g_StackTracer.Cleanup();

        MessageBox(NULL,"Last chance data is written to the log file","Fatal error",MB_ICONINFORMATION);

        return EXCEPTION_CONTINUE_SEARCH; // let the normal working continue
    }

    static LPTOP_LEVEL_EXCEPTION_FILTER  m_pOldHandler;
};

#endif // #ifdef NO_STACKTRACE

#endif  // STACK_TRACER_H_
