// Minimal CryEngine stubs for the deterministic test executables. These
// symbols live inside WHGame.dll; tests must not link the game or the full
// libKCD2 runtime subset just to run pure logic checks. Definitions use C++
// linkage to match the declarations in the CryThread headers.
#include <Windows.h>

void CryCreateCriticalSectionInplace(void* p)
{
    InitializeCriticalSection(static_cast<CRITICAL_SECTION*>(p));
}

void CryDeleteCriticalSectionInplace(void* p)
{
    DeleteCriticalSection(static_cast<CRITICAL_SECTION*>(p));
}
