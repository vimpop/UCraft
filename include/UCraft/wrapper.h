#ifndef WRAPPER_H
#define WRAPPER_H

#ifdef _MSC_VER
#include "wrapper_win.h"
#elif CUSTOM_WRAPPER
#include "wrapper_custom.h"
#else
#include "wrapper_unix.h"
#endif
#ifdef UCRAFT_TEST_HOOKS
#include "test_hooks.h"
#endif
#endif
