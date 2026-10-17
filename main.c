//===INCLUDES==================================================================
#include <stdio.h>
#include <stdbool.h>
#include <setjmp.h>

#include "windows.h"
//=============================================================================

//===TYPEDEFS==================================================================
typedef void (*PFN_script_update)(void);
struct {
    PFN_script_update update;
} typedef ScriptCallbacks;

struct {
    bool tryRecoverFromFatalError;
    jmp_buf fatalErrorRecoveryAddress;
} typedef TLS_ThreadDesc;
//=============================================================================

//===STATICS===================================================================
static DWORD s_tlsThreadDescHandle;
//=============================================================================

//===FORWARD_DECL==============================================================
void tls_try_recover_fatal_error(void);
//=============================================================================

//===MACRO_DEFS================================================================
#define debug_assert(condition)                 \
do{                                             \
    if(!(condition)){                           \
        tls_try_recover_fatal_error();          \
        __debugbreak();                         \
    }                                           \
}while(0)

#define debug_assert_no_recovery(condition)     \
do{                                             \
    if(!(condition)){                           \
        __debugbreak();                         \
    }                                           \
}while(0)
//=============================================================================

//===INTERNAL==================================================================
static TLS_ThreadDesc* tls_thread_desc_get(void){
    TLS_ThreadDesc* out = TlsGetValue(s_tlsThreadDescHandle);
    debug_assert_no_recovery(out); // Note: all threads that use the app specific asserts should have thread desc setup.
    return out;
}

static void tls_thread_desc_alloc(void){
    TLS_ThreadDesc* alloc = (void*) LocalAlloc(LPTR, sizeof(TLS_ThreadDesc));
    TlsSetValue(s_tlsThreadDescHandle, alloc);
}

void tls_thread_desc_create(void){
    s_tlsThreadDescHandle = TlsAlloc();
    debug_assert(s_tlsThreadDescHandle != TLS_OUT_OF_INDEXES);
    tls_thread_desc_alloc();
}

static void scr_update_0(void){ printf("update_0\n"); };                                               // OK
static void scr_update_1(void){ debug_assert(false); printf("update_1\n"); };                          // RECOVER from assert   via tls_try_recover_fatal_error in assert.
static void scr_update_2(void){ void* addr = NULL; ((char*)addr)[0] = 'a'; printf("update_2\n"); };    // RECOVER from Segfault via tls_try_recover_fatal_error in exception_handler_callback
static void scr_update_3(void){ printf("update_3\n");};                                                // OK

static void script_thread_entry_point(void) {
    tls_thread_desc_alloc();
    TLS_ThreadDesc *threadDesc = tls_thread_desc_get();
    threadDesc->tryRecoverFromFatalError = true;

    ScriptCallbacks scripts[4];
    scripts[0] = (ScriptCallbacks) {.update = scr_update_0};
    scripts[1] = (ScriptCallbacks) {.update = scr_update_1};
    scripts[2] = (ScriptCallbacks) {.update = scr_update_2};
    scripts[3] = (ScriptCallbacks) {.update = scr_update_3};

    while (true){
        int scriptIndex = 0;
        if(setjmp(tls_thread_desc_get()->fatalErrorRecoveryAddress)){
            printf( "err: recovered from fatal error on scr index %i\n", scriptIndex );
            scriptIndex++;
        }

        for (; scriptIndex < 4; ++scriptIndex) {
            scripts[scriptIndex].update();
            void* addr = NULL; ((char*)addr)[0] = 'a';
        }

        Sleep(33);
    }
}

static void script_thread_create(void){
    DWORD scrThreadId = {};
    HANDLE scrThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) script_thread_entry_point, NULL, 0, &scrThreadId);
    debug_assert(scrThread);
}

static void crash_thread_entry_point(void){
    Sleep(5000);
    debug_assert(false);
}

static void crash_thread_create(void){
    DWORD scrThreadId = {};
    HANDLE scrThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) crash_thread_entry_point, NULL, 0, &scrThreadId);
    debug_assert(scrThread);
}

static LONG exception_handler_callback(struct _EXCEPTION_POINTERS *exceptionInfo){
    const EXCEPTION_RECORD* record = exceptionInfo->ExceptionRecord;
    printf("err: caught exception with NT status code = 0x%lx \n", record->ExceptionCode); // TODO: consider getting win32 str errcode name for the NTSTATUS.
    tls_try_recover_fatal_error();
    return EXCEPTION_CONTINUE_EXECUTION;
}
//=============================================================================

//===GLOBAL====================================================================
int main(void) {
    AddVectoredExceptionHandler(0, exception_handler_callback);
    tls_thread_desc_create();

    script_thread_create();
    crash_thread_create();

    volatile bool shouldExit = false;
    while(!shouldExit){
        //spin...
    }

    return 0; // Note: os frees for us here :^)
}

void tls_try_recover_fatal_error(void){
    TLS_ThreadDesc* threadDesc = tls_thread_desc_get();
    if(threadDesc && threadDesc->tryRecoverFromFatalError){
        longjmp(threadDesc->fatalErrorRecoveryAddress, true);
    }
}
//=============================================================================
