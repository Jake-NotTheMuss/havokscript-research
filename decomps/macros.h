/* API */
#define HKS_VERSION  ""
#define HKS_BUILDNO  ""

#define HKS_SELF  0
#define HKS_UI64API  0
#define HKS_WITHDOUBLES  0
#define HKS_LUAPLUS  0  /* 0 in cod build, 1 in DLL */
#define LUA_COMPAT_VARARG
#define HKS_INSTRUCTION_PROFILE  0
#define HKS_INSTRUCTION_DISPATCH_PROFILE  0
#define HKS_GETGLOBAL_MEMOIZATION  0
#define HKS_WITHASSERTS  0
#define HKS_ASSERT_OPCODES  0
#define HKS_WITHAPIASSERTS  0
#define HKS_STRUCTURE_EXTENSION_ON  0
#define HKS_DEBUG_HOOKS  0
#define HKS_DEBUGGER  0
#define HKS_SCRUB_MEMORY  0
#define HKS_SCRIPT_PROFILER  0
#define HKS_RUNTIME_PROFILER  0
#define HKS_PROFILER  0
#define HKS_HEAP_ASSERTIONS  0

/* common */
#define HKS_GET_TYPE(obj)  ((obj).t & 0xf)

#define LUAL_BUFFERSIZE 512

/* BytecodeReader */
#define IS_ALIGNED(x) (((x) & 3) == 0)

/* NetworkDebugger */
#define MAX_NUM_USER_EVENTS 64


#define S_API_STACK_TOP(s)  ((s)->m_apistack.top)
#define S_API_STACK_BASE(s)  ((s)->m_apistack.base)
#define S_API_STACK_ALLOC_TOP(s)  ((s)->m_apistack.alloc_top)
#define S_API_STACK_BOTTOM(s)  ((s)->m_apistack.bottom)

#define S_STACK_NOT_FULL(s)  (S_API_STACK_TOP(s) < S_API_STACK_ALLOC_TOP(s))

#define LUA_REGISTRYINDEX (-10000)
#define LUA_ENVIRONINDEX  (-10001)
#define LUA_GLOBALSINDEX  (-10002)


/*
** Event codes
*/
#define LUA_HOOKCALL  0
#define LUA_HOOKRET 1
#define LUA_HOOKLINE  2
#define LUA_HOOKCOUNT 3
#define LUA_HOOKTAILRET 4


/*
** Event masks
*/
#define LUA_MASKCALL  (1 << LUA_HOOKCALL)
#define LUA_MASKRET (1 << LUA_HOOKRET)
#define LUA_MASKLINE  (1 << LUA_HOOKLINE)
#define LUA_MASKCOUNT (1 << LUA_HOOKCOUNT)


#define VM_PSEUDOINDEXBOUNDS (-10000)

/* garbage collector */
#define HKS_GC_NONINCREMENTAL_TARGET  (-1)
#define HKS_GC_UNIT  51


/* profiler */
#define HKS_RUNTIMEPROFILE_START  3
#define HKS_RUNTIMEPROFILE_STATS  4
#define HKS_RUNTIMEPROFILE_STOP  5



