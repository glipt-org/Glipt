#ifndef glipt_compiler_h
#define glipt_compiler_h

#include "object.h"
#include "vm.h"

ObjFunction* compile(VM* vm, const char* source);

#endif
