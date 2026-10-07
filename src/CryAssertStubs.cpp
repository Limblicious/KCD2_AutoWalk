// Minimal stubs for the CryEngine assert machinery that inline headers
// (CryStringT::check_cast etc.) reference. These symbols exist only inside
// WHGame.dll; a plugin cannot link them. The stubs are inert: an assert
// failure simply returns without breaking.
#include <cstdarg>

bool CryAssert(const char* condition, const char* file, unsigned int line, bool* ignoreAlways)
{
    (void)condition;
    (void)file;
    (void)line;
    if (ignoreAlways) {
        *ignoreAlways = true; // fail once, then never again
    }
    return false;
}

void CryAssertTrace(const char* format, ...)
{
    (void)format;
}

void CryDebugBreak()
{
}
