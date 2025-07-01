
/* GarbageCollectorWeights.cpp */

namespace hks {
  HksGcCost HksGcWeights::*const hksGcWeightMembers[] = {

  }

  void RemoveLuaPlusReferencesToCoroutine(lua_State *s) {
#if HKS_LUAPLUS
    lua_State *rootState = s->m_global->m_root;
    LuaPlus::LuaObject *obj;
    for (obj = s->m_global->m_luaplusObjectList; obj != NULL; obj = obj->m_next) {
      if (obj->L == s)
        obj->L = rootState;
    }
    // todo
#endif /* HKS_LUAPLUS */
    (void)s;
  }

  // analogous to luaF_close
  // close any pending upvalues above stack position LFP (level frame pointer?)
  void closePendingUpvalues(lua_State *s, HksObject *lfp) {
    UpValue **pending = &s->pending;
    UpValue *p = *pending;
    UpValue *q;
    for (q = p; q != NULL; q = q->m_next) {
      if (q->loc < lfp)
        p = q;
      else {
        q->close(s);
        // remove from pending list
        if (q == *pending)
          *pending = p = q->m_next;
        else
          p->m_next = q->m_next;
      }
    }
  }
}


lua_State *hksi_hks_newstate(const HksStateSettings &settings) {
  api_assert(settings.m_allocator != NULL, "Allocator cannot be a NULL pointer");
  api_assert(settings.m_logFunction != NULL, "Log cannot be a NULL pointer");
  api_assert(settings.m_name != NULL, "Name cannot be a NULL pointer");
  HksGlobal *globals = (*settings.m_allocator)(settings.m_allocatorData, NULL, 0, sizeof(HksGlobal));
  api_assert(globals != NULL, "Error allocating enough space for the globals.");
  memset(globals, 0, sizeof(HksGlobal));
  globals->m_heapAssertionFrequency = settings.m_heapAssertionFrequency;
  globals->m_logFunction = settings.m_logFunction;
  globals->m_panicFunction = settings.m_panicFunction;
  globals->m_emergencyGCFailFunction = settings.m_emergencyGCFailFunction;
  globals->m_compilerSettings = settings.m_compilerSettings;
  globals->m_bytecodeSharingMode = settings.m_bytecodeSharingMode;
  globals->m_bytecodeDumpEndianness = settings.m_bytecodeDumpEndianness;
  globals->m_memory.init(settings.m_allocator, settings.m_allocatorData);
  lua_State *s = globals->m_memory.allocateMainState();
  api_assert(s != NULL, "Error allocating the lua_State");
  memset(s, 0, sizeof(lua_State));
  globals->m_root = s;
  s->m_global = globals;
  s->m_status = RUNNING;
  globals->m_collector.initialize(settings.m_gcPause, settings.m_gcStepMul,
                                  settings.m_gcEmergencyMemorySize, &gcDisabledPolicy,
                                  settings.m_gcWeakStackSize, settings.m_gcGreyStackSize);
  init_state_common(s, s);
  globals->m_stringTable.init(s, settings.m_initialStringTableSize);
  s->m_global->m_registry.v.table = HashTable::Create(s, settings.m_initialRegistryArraySize, settings.m_initialRegistrySize);
  s->m_global->m_registry.t = TTABLE;
  globals->m_staticStringCache.initialize(s);
  runtimeProfilerInit(s);
  RuntimeProfileScoper _hksRuntimeScoper(s);
  hksi_hks_setname(s, settings.m_name);
  s->m_global->m_collector.setGCPolicy(settings.m_gcPolicy);
  hksi_lua_gc(s, LUA_GCRESTART, 1);
  if (settings.m_debugObject != NULL)
    settings.m_debugObject->Register(s);
}


int hksi_lua_gc(lua_State *s, int what, int data) {
  int result = 0;
  api_assert(s != NULL, "Parameter 's' must be a valid state pointer");
  RuntimeProfileScoper _hksRuntimeScoper(s);
  switch (what) {
    case LUA_GCSTOP:
      s->m_global->m_collector.stop();
      break;
    case LUA_GCRESTART:
      s->m_global->m_collector.restart();
      break;
    case LUA_GCCOLLECT:
      s->m_global->m_collector.stepNonIncremental(s, GC_CALL_USER);
      break;
    case LUA_GCCOUNT:
      result = (int)(s->m_global->m_memory.getOccupiedMemory() / 1024);
      break;
    case LUA_GCCOUNTB:
      result = (int)(s->m_global->m_memory.getOccupiedMemory() % 1024);
      break;
    case LUA_GCSTEP:
      result = s->m_global->m_collector.stepIncremental(s, GC_CALL_USER, data);
      break;
    case LUA_GCSETPAUSE:
      s->m_global->m_collector.setPause(data);
      break;
    case LUA_GCSETSTEPMUL:
      s->m_global->m_collector.setStepMul(data);
      break;
    default: result = -1;
  }
  return result;
}


namespace hks {
  class MemoryManager {
  private:
    void *externalAllocate(void *ptr, hksSize oldSize, hksSize newSize) {
      void *allocation = (*m_allocator)(m_allocatorUd, ptr, oldSize, newSize);
      api_assert(((hksSize)allocation & 0x7) == 0);
      return allocation;
    }
  }

  struct CallStack {
    bool isLua(lua_State *s, int level) const {
      return HKS_GET_TYPE(*getFunction(s, level)) == TIFUNCTION;
    }

    bool isCurrentLua(lua_State *s) const {
      return isLua(s, -1);
    }
  }
}


/* GarbageCollector.h */

namespace hks {

  int remarkStackCompare(const void *a, const void *b) {
    const char *const ht_a_ptr = a;
    const char *const ht_b_ptr = b;
    return ht_a_ptr - ht_b_ptr;
  }

  HksGcCost HksGcWeights::*const hksGcWeightMembers[];

  class HksGcWeights {
    HksGcCost m_removeString;
    HksGcCost m_finalizeUserdataNoMM;
    HksGcCost m_finalizeUserdataGcMM;
    HksGcCost m_cleanCoroutine;
    HksGcCost m_removeWeak;
    HksGcCost m_markObject;
    HksGcCost m_traverseString;
    HksGcCost m_traverseUserdata;
    HksGcCost m_traverseCoroutine;
    HksGcCost m_traverseWeakTable;
    HksGcCost m_freeChunk;
    HksGcCost m_sweepTraverse;

    void initialize() {
      m_removeString = 60;
      m_finalizeUserdataNoMM = 84;
      m_finalizeUserdataGcMM = 120;
      m_cleanCoroutine = 24;
      m_removeWeak = 60;
      m_markObject = 12;
      m_traverseString = 3;
      m_traverseUserdata = 4;
      m_traverseCoroutine = 4;
      m_traverseWeakTable = 4;
      m_freeChunk = 192;
      m_sweepTraverse = 12;
    }

    HksGcCost calculateScale() {
      int i;
      for (i = 0; i <= hksNumWeightNames; i++)
        api_assert(this->*hksGcWeightMembers[i] >= 0, "Negative weight provided.");
      const HksGcCost total =
      m_removeString +
      m_finalizeUserdataNoMM +
      m_finalizeUserdataGcMM +
      m_cleanCoroutine +
      m_removeWeak +
      m_markObject +
      m_traverseString +
      m_traverseUserdata +
      m_traverseCoroutine +
      m_traverseWeakTable +
      m_freeChunk +
      m_sweepTraverse;
      api_assert(total >= HKS_GC_UNIT * 10,
                 "Total weight is not sufficient. Please rescale to higher values.");
      return total / HKS_GC_UNIT;
    }
  }
  
  HksGcCost HksGcWeights::*const hksGcWeightMembers[] = {
    &HksGcWeights::m_removeString,
    &HksGcWeights::m_finalizeUserdataNoMM,
    &HksGcWeights::m_finalizeUserdataGcMM,
    &HksGcWeights::m_cleanCoroutine,
    &HksGcWeights::m_removeWeak,
    &HksGcWeights::m_markObject,
    &HksGcWeights::m_traverseString,
    &HksGcWeights::m_traverseUserdata,
    &HksGcWeights::m_traverseCoroutine,
    &HksGcWeights::m_traverseWeakTable,
    &HksGcWeights::m_freeChunk,
    &HksGcWeights::m_sweepTraverse
  };

  const char *const hksGcWeightNames[] = {
    "removeString",
    "finalizeUserdataNoMM",
    "finalizeUserdataGcMM",
    "cleanCoroutine",
    "removeWeak",
    "markObject",
    "tarverseString",
    "traverseUserdata",
    "traverseCoroutine",
    "traverseWeakTable",
    "freeChunk",
    "sweepTraverse"
  };

  const int hksNumWeightNames = sizeof(hksGcWeightNames) / sizeof(hksGcWeightNames[0]);

  struct GarbageCollector {

    void initialize(lua_State *s, hksInt32 pause, HksGcCost stepMultiplier, hksSize emergencyMemorySize, lua_CFunction gcPolicy, int weakStackSize, int /**/) {
      memset(this, 0, sizeof(*this));
      m_costs.initialize();
      m_unit = m_costs.calculateScale();
      m_target = stepMultiplier > 0 ? 1 : HKS_GC_NONINCREMENTAL_TARGET;
      m_mainState = s;
      m_memory = &s->m_global->m_memory;
      m_stopped = true;
      m_phase = 6;
      m_pauseMultiplier = pause;
      m_stepMultiplier = stepMultiplier;
      m_stepsLeft = m_stepLimit = stepMultiplier * m_unit;
      m_stepTriggerCountdown = 317;
      m_emergencyGCMemory = NULL;
      m_emergencyMemorySize = emergencyMemorySize;
      m_gcPolicy = gcPolicy;
      m_lastBlackUD = s->m_global->m_userDataList.getStartPrev();
      resetEmergencyMemoryReserve();
      initVisitStack();
      initWeakStack(weakStackSize);
    }

    void checkStep(lua_State *callingState, hksSize numAllocs) {
      m_stepTriggerCountdown -= (int)numAllocs;
      const hksSize occupied = m_memory->getOccupiedMemory();
      if (m_pauseTriggerMemoryUsage < occupied && m_stepTriggerCountdown <= 0 && !m_stopped) {
        m_stepTriggerCountdown = 317;
        if (m_gcPolicy != NULL && (*m_gcPolicy)(m_mainState))
          return;
        m_stepsLeft = m_stepMultiplier * m_stepTriggerCountdown * m_unit;
        increment(callingState, GC_CALL_AUTOMATED, 0);
      }
    }

    int stepNonIncremental(lua_State *callingState, GC_Call_Type callType) {
      if (m_gcPolicy != NULL && (*m_gcPolicy)(m_mainState))
        return 0;
      if (callType == GC_CALL_EMERGENCY)
        freeEmergencyMemoryReserve();
      HksGcCost oldTarget = m_target;
      m_target = HKS_GC_NONINCREMENTAL_TARGET;
      const int result = increment(callingState, callType, -1);
      m_target = oldTarget;
      return result;
    }

    int stepIncremental(lua_State *callingState, GC_Call_Type callType, HksGcCost size) {
      if (m_gcPolicy != NULL && (*m_gcPolicy)(m_mainState))
        return 0;
      if (size)
        m_stepLimit = size * m_unit;
      m_stepsLeft = m_stepLimit;
      HksGcCost oldTarget = m_target;
      m_target = 1;
      const int result = increment(callingState, callType, size);
      m_target = oldTarget;
      return result;
    }

    hksInt32 increment(lua_State *callingState, GC_Call_Type callType, HksGcCost size) {
      jmp_buf jumpBuffer;
      int result = 0;
      if (m_jumpPoint != NULL)
        return 0;
      if (callingState->m_global->m_profiler != NULL)
        Profiler::ProfilerInstance::GCCall(callingState->m_global->m_profiler, callType, size);
      runtimeProfilerEvent(m_mainState, 0);
      m_jumpPoint = &jumpBuffer;
      do {
        const int dummy = setjmp(jumpBuffer);
        if (dummy == 0) {
          switch (m_phase) {
            case 0: {
              if (callingState->m_global->m_profiler != NULL)
                Profiler::ProfilerInstance::GCPhaseChange(callingState->m_global->m_profiler, GC_Starting);
              m_resumeStack.m_numEntries = 0;
              m_startOfStateStackList = NULL;
              m_endOfStateStackList = NULL;
              m_currentState = NULL;
              m_phase = 1;
              if (callingState->m_global->m_profiler != NULL)
                Profiler::ProfilerInstance::GCPhaseChange(callingState->m_global->m_profiler, GC_MainStateMarking);
              markState_extend(m_mainState, NULL, 200);
            }
            case 1: {
              if (callingState->m_global->m_profiler != NULL)
                Profiler::ProfilerInstance::GCPhaseChange(callingState->m_global->m_profiler, GC_VisitStack);
              while (m_resumeStack.m_numEntries > 0) {
                ResumeData_Entry *resumeEntry = &m_resumeStack.m_storage[--m_resumeStack.m_numEntries];
                switch (resumeEntry->State.h.m_type) {
                  case TTABLE:
                    markTable_extend(resumeEntry->HashTable.m_table, NULL, 200);
                    break;
                  case TUSERDATA:
                    markUserdata_extend(resumeEntry->Userdata.m_data, NULL, 200);
                    break;
                  case TTHREAD:
                    markState_extend(resumeEntry->State.m_state, NULL, 200);
                    break;
                  case TIFUNCTION:
                    markClosure_extend(resumeEntry->Closure.m_closure, NULL, 200);
                    break;
                  case TCFUNCTION:
                    markCClosure_extend(resumeEntry->CClosure.m_cclosure, NULL, 200);
                    break;
                  case TSTRUCT:
                    markStruct_extend(resumeEntry->Struct.m_struct, NULL, 200);
                    break;
                  default: break;
                }
              }
              if (callingState->m_global->m_profiler != NULL)
                Profiler::ProfilerInstance::GCPhaseChange(callingState->m_global->m_profiler, GC_GreyStack);
              markGreyStack(200);
              if (callingState->m_global->m_profiler != NULL)
                Profiler::ProfilerInstance::GCPhaseChange(callingState->m_global->m_profiler, GC_SafeValue);
              if (HKS_GET_TYPE(m_safeTableValue) != TNIL)
                markTObject(&m_safeTableValue, NULL, 200);
              if (HKS_GET_TYPE(m_safeValue) != TNIL)
                markTObject(&m_safeValue, NULL, 200);
              if (callingState->m_global->m_profiler != NULL)
                Profiler::ProfilerInstance::GCPhaseChange(callingState->m_global->m_profiler, GC_SafeStacks);
              markStateStacks(200);
              if (callingState->m_global->m_profiler != NULL)
                Profiler::ProfilerInstance::GCPhaseChange(callingState->m_global->m_profiler, GC_CompilerDataStructures);
              for (CodeGenerator *compiler = m_compiler; compiler != NULL; compiler = compiler->m_next)
                compiler->markMethods(&methodAndChildrenMarker);
              for (BytecodeReader *reader = m_bytecodeReader; reader != NULL; reader = reader->m_next) {
                Method *method = reader->getReadMethod();
                if (method != NULL)
                  methodAndChildrenMarker(NULL, method);
              }
              m_memory->setNewAllocations(BLACK);
              m_phase = 2;
            }
            case 2: {
              if (callingState->m_global->m_profiler != NULL)
                Profiler::ProfilerInstance::GCPhaseChange(callingState->m_global->m_profiler, GC_ProcessCoroutines);
              processAllCoroutines();
              if (callingState->m_global->m_profiler != NULL)
                Profiler::ProfilerInstance::GCPhaseChange(callingState->m_global->m_profiler, GC_CleanWeakTables);
              cleanWeakTables();
              m_phase = 3;
              if (callingState->m_global->m_profiler != NULL)
                Profiler::ProfilerInstance::GCPhaseChange(callingState->m_global->m_profiler, GC_UserdataFinalization);
              m_lastBlackUD = m_mainState->m_global->m_userDataList.getStartPrev();
              m_activeUD = m_mainState->m_global->m_userDataList.getStart();
            }
            case 3: {
              finalizeUserdata(callingState);
              m_phase = 4;
              if (callingState->m_global->m_profiler != NULL)
                Profiler::ProfilerInstance::GCPhaseChange(callingState->m_global->m_profiler, GC_ProcessInternList);
            }
            case 4: {
              processInternList();
              m_memory->setNewAllocations(WHITE);
              m_memory->initializeSweep();
              m_phase = 5;
              if (callingState->m_global->m_profiler != NULL)
                Profiler::ProfilerInstance::GCPhaseChange(callingState->m_global->m_profiler, GC_Sweeping);
            }
            case 5: {
              m_stepsLeft = m_memory->sweep(m_stepsLeft, m_costs.m_freeChunk, m_costs.m_sweepTraverse, m_target);
              if (m_stepsLeft < m_target)
                yield(NULL);
              m_phase = 6;
            }
            case 6: {
              if (callingState->m_global->m_profiler != NULL)
                Profiler::ProfilerInstance::GCPhaseChange(callingState->m_global->m_profiler, GC_Finished);
              const double pausePercentage = m_pauseMultiplier * m_memory->getOccupiedMemory();
              const hksSize newPause = ?;
              m_pauseTriggerMemoryUsage = newPause;
              m_phase = 0;
            }
            default: {
              result = 1;
              break;
            }
          }
        }
        else
          m_stepsLeft = m_stepLimit;
      } while (result == 0 && m_target == HKS_GC_NONINCREMENTAL_TARGET);
      m_jumpPoint = NULL;
      runtimeProfilerEvent(m_mainState, 1);
      if (callingState->m_global->m_profiler != NULL)
        Profiler::ProfilerInstance::GCExit(callingState->m_global->m_profiler);
      return result;
    }

    bool resetEmergencyMemoryReserve() {
      if (m_emergencyGCMemory == NULL) {
        m_emergencyGCMemory = m_memory->allocateNoHeader(m_emergencyMemorySize, Alloc_GC_VisitStack);
        return m_emergencyGCMemory != NULL;
      }
      return true;
    }

    bool freeEmergencyMemoryReserve() {
      if (m_emergencyGCMemory != NULL) {
        m_memory->release(m_emergencyGCMemory, m_emergencyMemorySize, Alloc_GC_VisitStack);
        m_emergencyGCMemory = NULL;
        return true;
      }
      return false;
    }

    void yield(VisitData_Header *endOfVisitDataList) {
      if (m_currentState != NULL) {
        if (m_currentState != m_endOfStateStackList) {
          lua_State *oldlist = m_startOfStateStackList;
          m_startOfStateStackList = m_currentState->m_nextStateStack;
          m_currentState->m_nextStateStack = NULL;
          m_endOfStateStackList->m_nextStateStack = oldlist;
          m_endOfStateStackList = m_currentState;
        }
        m_currentState = NULL;
      }
      if (endOfVisitDataList != NULL) {
        int count = 0;
        VisitData_Header *lastLink, *currentLink, *nextLink;
        ResumeData_Entry *resumeState;
        currentLink = endOfVisitDataList;
        lastLink = NULL;
        do {
          nextLink = currentLink->m_prev;
          currentLink->m_prev = lastLink;
          lastLink = currentLink;
          count++;
          currentLink = nextLink;
        } while (nextLink != NULL);
        m_resumeStack.m_numEntries += count;
        if (m_resumeStack.m_numEntries > m_resumeStack.m_numAllocated) {
          api_assert(m_target != HKS_GC_NONINCREMENTAL_TARGET,
                     "Allocating during non-incremental garbage collection.");
          for (;;) {
            resumeState = m_memory->allocateNoHeader((m_resumeStack.m_numEntries + 8) * sizeof(ResumeData_Entry), Alloc_GC_VisitStack);
            if (resumeState != NULL)
              break;
            OutOfMemoryError(m_mainState, (m_resumeStack.m_numEntries + 8) * sizeof(ResumeData_Entry));
          }
          ::memcpy(resumeState, m_resumeStack.m_storage, (m_resumeStack.m_numEntries - count) * sizeof(ResumeData_Entry));
          freeMemoryNoHeader(m_mainState, m_resumeStack.m_storage, m_resumeStack.m_numAllocated * sizeof(ResumeData_Entry), Alloc_GC_VisitStack);
          m_resumeStack.m_storage = resumeState;
          m_resumeStack.m_numAllocated = (m_resumeStack.m_numEntries + 8) * sizeof(ResumeData_Entry);
        }
        do {
          ::memcpy(m_resumeStack.m_storage + m_resumeStack.m_numEntries, &lastLink->m_data, sizeof(ResumeData_Entry));
          m_resumeStack.m_numEntries++;
          lastLink = lastLink->m_prev;
        } while (lastLink != NULL);
      }
      ::longjmp(m_jumpPoint, 1);
      initVisitStack();
    }

    hksBool isFinalizing() const {
      return m_finalizing;
    }

    hksBool isInGC() const {
      return m_jumpPoint != NULL;
    }

    HksGcWeights &getWeights() {
      return m_costs;
    }

    hksSize getAllocatedSize() const {
      const hksSize resume_stack = m_resumeStack.m_numAllocated * sizeof(ResumeData_Entry);
      const hksSize grey_stack = m_greyStack.m_numAllocated * sizeof(HksObject);
      const hksSize remark_stack = m_remarkStack.m_numAllocated * sizeof(HashTable *);
      const hksSize weak_stack = m_weakStack.m_numAllocated * sizeof(WeakStack_Entry);
      return resume_stack + grey_stack + remark_stack + weak_stack;
    }

    void restart() {
      m_stopped = false;
      if (m_finalizerState == NULL)
        createFinalizerState();
    }

    void initVisitStack() {
      const hksSize size = 32 * sizeof(ResumeData_Entry);
      while (true) {
        m_resumeStack.m_storage = m_memory->allocateNoHeader(size, Alloc_GC_VisitStack);
        if (m_resumeStack.m_storage != NULL) {
          memset(m_resumeStack.m_storage, 0, size);
          m_resumeStack.m_numEntries = 0;
          m_resumeStack.m_numAllocated = 32;
          return;
        }
        api_assert(false, "Insufficient memory allocated for GC initialization");
        OutOfMemoryError(m_mainState, size);
      }
    }

    void initWeakStack(int initNumAllocated) {
      const int initNumEntries = 0;
      const hksSize size = initNumAllocated * sizeof(WeakStack_Entry);
      while (true) {
        m_weakStack.m_storage = m_memory->allocateNoHeader(size, Alloc_GC_WeakStack);
        if (m_weakStack.m_storage != NULL) {
          memset(m_weakStack.m_storage, 0, size);
          m_weakStack.m_numEntries = initNumEntries;
          m_weakStack.m_numAllocated = initNumAllocated;
          return;
        }
        api_assert(false, "Insufficient memory allocated for GC initialization");
        OutOfMemoryError(m_mainState, size);
      }
    }

    void checkDepth(hksSize depthCount, VisitData_Header *endOfVisitDataList) {
      if (depthCount > 200)
        yield(endOfVisitDataList);
    }

    void writeBarrier(const GenericChunkHeader *to, const HksObject *from) {
      const HksObjectType fromType = HKS_GET_TYPE(*from);
      if (fromType > TNUMBER && fromType < TUI64) {
        hksBool toIsWhite = to->isWhite();
        GenericChunkHeader *fromChunk = from;
        if (toIsWhite)
          return;
        if (fromType == TSTRING && from->v.str->isStatic())
          return;
        if (!fromChunk->isBlack()) {
          fromChunk->setBlack();
          if (fromType != TSTRING)
            pushGreyStack(from);
        }
      }
    }

    void processAllCoroutines() {
      lua_State **patch = &m_mainState->m_next;
      lua_State *coroutine = m_mainState->m_next;
      for (; coroutine != NULL; coroutine = coroutine->m_next) {
        if (!coroutine->isWhite()) {
          // marked black; keep it, update patch to here
          patch = &coroutine->m_next;
          m_stepsLeft -= m_costs.m_traverseCoroutine;
        }
        else {
          closePendingUpvalues(coroutine, S_API_STACK_BOTTOM(coroutine));
          RemoveLuaPlusReferencesToCoroutine(coroutine);
          // remove this coroutine from the list
          *patch = coroutine->m_next;
          m_stepsLeft -= m_costs.m_cleanCoroutine;
        }
      }
    }

    void cleanWeakTables() {
      const HksObject nilValue = NilValue;
      for (int i = 0; i < m_weakStack.m_numEntries; i++) {
        WeakStack_Entry &wtable = m_weakStack.m_storage[i];
        HashTable *t = wtable.m_table;
        const hksInt32 weakness = wtable.m_weakness;

        HksObject key = nilValue;


        HksRegister iterator;
        for (iterator = t->getNext(&key); HKS_GET_TYPE(iterator) != TNIL; iterator = t->getNext(&key)) {
          HksObject value = iterator;
          // if key is collectible and table has weak keys
          if ((HKS_GET_TYPE(key) > TSTRING && HKS_GET_TYPE(key) < TUI64) && (weakness & 2) != 0) {
            HksObject value = iterator;
            if (key.v.str.isWhite()) {
              t->tableInsert(m_mainState, &key, &nilValue);
              m_stepsLeft -= m_costs.m_removeWeak;
            }
          }
          if ((HKS_GET_TYPE(value) > TSTRING && HKS_GET_TYPE(value) < TUI64) && (weakness & 1) != 0) {
            if (value.v.str.isWhite()) {
              t->tableInsert(m_mainState, &key, &nilValue);
              m_stepsLeft -= m_costs.m_removeWeak;
            }
          }
        }
        m_stepsLeft -= m_costs.m_traverseWeakTable;
      }
      m_weakStack.m_numEntries = 0;
    }

    void pushGreyStack(const HksObject *obj) {
      hksSize goSize = m_greyStack.m_numAllocated;
      if (m_greyStack.m_numEntries == goSize) {
        const hksSize numBytes = (goSize == 0 ? m_initialGreyStackSize : goSize * 2);
        HksObject *oldGreyObjects = m_greyStack.m_storage;
        while (true) {
          m_greyStack.m_storage = m_memory->allocateNoHeader(numBytes * sizeof(HksObject), Alloc_GC_GreyStack);
          if (m_greyStack.m_storage != NULL)
            break;
          OutOfMemoryError(m_mainState, numBytes * sizeof(HksObject));
        }
        if (oldGreyObjects != NULL) {
          memcpy(m_greyStack.m_storage, oldGreyObjects, m_greyStack.m_numEntries * sizeof(HksObject));
          freeMemoryNoHeader(m_mainState, oldGreyObjects, m_greyStack.m_numAllocated * sizeof(HksObject), Alloc_GC_GreyStack);
        }
        m_greyStack.m_numAllocated = numBytes;
      }
      m_greyStack.m_storage[m_greyStack.m_numEntries++] = *obj;
    }

    void finalizeUserdataWorker(lua_State *callingState) {
      UserData *ud = m_activeUD;
      UserData *nextud;
      while (ud != NULL) {
        nextud = ud->getNext();
        if (!ud->isWhite()) {
          // update last black userdata
          m_lastBlackUD = ud;
          ud->setWhite();
          m_activeUD = nextud;
          if (m_stepsLeft - m_costs.m_traverseUserdata < m_target) {
            hksi_lua_pushinteger(callingState, 0);
            return;
          }
          m_stepsLeft -= m_costs.m_traverseUserdata;
        }
        else {
          callingState->m_global->m_userDataList.removeAfter(m_lastBlackUD, ud);
          m_activeUD = ud;
          const HksGcCost cost = finalize(callingState, ud);
          freeNoSweeper(m_mainState, ud, ud->getAllocatedSize(), Alloc_UserData);
          m_activeUD = nextud;
          if (m_stepsLeft - cost < m_target) {
            hksi_lua_pushinteger(callingState, 0);
            return;
          }
          m_stepsLeft -= cost;
        }
        ud = nextud;
      }
      m_lastBlackUD = callingState->m_global->m_userDataList.getStartPrev();
      hksi_lua_pushinteger(callingState, 1);
    }

    HksGcCost finalize(lua_State *s, UserData *ud) {
      if (ud->m_meta != NULL) {
        const HksRegister finalizer = HKS_METATABLE_GET(m_mainState, ud->m_meta, M_GC);
        if (HKS_GET_TYPE(finalizer) != TNIL) {
          HksObject *stacktop = s->m_apistack.top;
          *stacktop = finalizer;
          s->m_apistack.top++;
          hksi_lua_call(s, 1, 0);
          return m_costs.m_finalizeUserdataGcMM;
        }
      }
      return m_costs.m_finalizeUserdataNoMM;
    }

    void markTObject(HksObject *obj, VisitData_Header *prev, hksSize depthCount) {
      const int objType = HKS_GET_TYPE(*obj);
      switch (objType) {
        case TSTRING:
          if (markInternString(obj->v.str)) {
            if (m_stepsLeft - m_costs.m_markObject < m_target)
              yield(prev);
            else
              m_stepsLeft -= m_costs.m_markObject;
          }
          break;
        case TTABLE:
          markTable_extend(obj->v.table, prev, depthCount);
          break;
        case TFUNCTION:
          break;
        case TUSERDATA:
          markUserdata_extend(obj->v.userData, prev, depthCount);
          break;
        case TTHREAD:
          markState_extend(obj->v.thread, prev, depthCount);
          break;
        case TIFUNCTION:
          markClosure_extend(obj->v.closure, prev, depthCount);
          break;
        case TCFUNCTION:
          markCClosure_extend(obj->v.cClosure, prev, depthCount);
          break;
        case TSTRUCT:
          markStruct_extend(obj->v.tstruct, prev, depthCount);
          break;
      }
    }

    void markTRegister(HksRegister obj, VisitData_Header *prev, hksSize depthCount) {
      const int objType = HKS_GET_TYPE(obj);
      switch (objType) {
        case TSTRING:
          if (markInternString(obj.v.str)) {
            if (m_stepsLeft - m_costs.m_markObject < m_target)
              yield(prev);
            else
              m_stepsLeft -= m_costs.m_markObject;
          }
          break;
        case TTABLE:
          markTable_extend(obj.v.table, prev, depthCount);
          break;
        case TFUNCTION:
          break;
        case TUSERDATA:
          markUserdata_extend(obj.v.userData, prev, depthCount);
          break;
        case TTHREAD:
          markState_extend(obj.v.thread, prev, depthCount);
          break;
        case TIFUNCTION;
          markClosure_extend(obj.v.closure, prev, depthCount);
          break;
        case TCFUNCTION:
          markCClosure_extend(obj.v.cClosure, prev, depthCount);
          break;
        case TSTRUCT:
          markStruct_extend(obj.v.tstruct, prev, depthCount);
          break;
      }
    }

    void markGreyStack(hksSize depthCount) {
      if (m_greyStack.m_storage != NULL) {
        for (HksObject *mostRecentGreyObject = m_greyStack.m_storage + m_greyStack.m_numEntries - 1;
             mostRecentGreyObject >= m_greyStack.m_storage;
             mostRecentGreyObject--) {
          HksObject localVar = *mostRecentGreyObject;
          localVar.v.str->setWhite();
          m_greyStack.m_numEntries--;
          markTObject(&localVar, NULL, depthCount - 1);
        }
      }
      if (m_remarkStack.m_storage != NULL && m_remarkStack.m_numAllocated != 0) {
        qsort(m_remarkStack.m_storage, m_remarkStack.m_numEntries, sizeof(HashTable *), remarkStackCompare);
        for (HashTable **mostRecentTable = m_remarkStack.m_storage + m_remarkStack.m_numEntries - 1;
             mostRecentTable >= m_remarkStack.m_storage;) {
          HashTable *tbl = *mostRecentTable;
          do {
            m_remarkStack.m_numEntries--;
            mostRecentTable--;
          } while (mostRecentTable >= m_remarkStack.m_storage && *mostRecentTable == tbl);
          tbl->setWhite();
          markTable_extend(tbl, NULL, depthCount - 1);
        }
      }
    }

    void markStateStacks(hksSize depthCount) {
      for (lua_State *coro = m_startOfStateStackList; coro != NULL; coro = coro->m_nextStateStack) {
        m_currentState = coro;
        if (coro->isBlack()) {
          const ApiStack &stack = coro->m_apistack;
          if (stack.top)
            memset(stack.top, 0, (stack.alloc_top - stack.top + 4) * sizeof(HksObject));
          for (HksObject *nextElement = stack.bottom; nextElement < stack.top; nextElement++)
            markTObject(nextElement, NULL, depthCount - 1);
        }
      }
      m_currentState = NULL;
    }

    void markState_extend(lua_State *s, VisitData_Header *prev, hksSize depthCount) {
      VisitData_State visitData;
      if (!s->isBlack()) {
        visitData.m_data.h.m_type = TTHREAD;
        visitData.m_data.m_phase = GC_STATE_MARKING_UPVALUES;
        visitData.m_data.m_pending = s->pending;
        visitData.h2.m_prev = prev;
        visitData.m_data.m_state = s;
        s->setBlack();
        m_stepsLeft -= m_costs.m_markObject;
        if (s->m_name != NULL) {
          markInternString(s->m_name);
          m_stepsLeft -= m_costs.m_markObject;
        }
        s->m_nextStateStack = NULL;
        if (m_startOfStateStackList == NULL)
          m_startOfStateStackList = s;
        else
          m_startOfStateStackList->m_nextStateStack = s;
        m_endOfStateStackList = s;
        if (m_stepsLeft < m_target)
          yield(&visitData);
        checkDepth(depthCount, &visitData);
        markState_common(s, &visitData, depthCount);
      }
    }

    void markState_common(lua_State *s, VisitData_State *visitData, hksSize depthCount) {
      switch (visitData->m_data.m_phase) {
        case GC_STATE_MARKING_UPVALUES: {
          UpValue *pendingUpvalue = visitData->m_data.m_pending;
          while (pendingUpvalue != NULL) {
            pendingUpvalue->setBlack();
            m_stepsLeft -= m_costs.m_markObject;
            markTObject(pendingUpvalue->loc, visitData, depthCount - 2);
            pendingUpvalue = pendingUpvalue->m_next;
            visitData->m_data.m_pending = pendingUpvalue;
          }
          visitData->m_data.m_phase = GC_STATE_MARKING_GLOBAL_TABLE;
        }
        case GC_STATE_MARKING_GLOBAL_TABLE:
          visitData->m_data.m_phase = GC_STATE_MARKING_REGISTRY;
          markTable_extend(s->globals.v.table, visitData, depthCount - 1);
          if (s->m_global->m_root != s)
            break;
        case GC_STATE_MARKING_REGISTRY:
          if (s->m_global->m_root == s) {
            visitData->m_data.m_phase = GC_STATE_MARKING_PROTOTYPES;
            markTable_extend(s->m_global->m_registry.v.table, visitData, depthCount - 1);
          }
        case GC_STATE_MARKING_PROTOTYPES:
          if (s->m_global->m_root == s) {
            visitData->m_data.m_phase = GC_STATE_MARKING_SCRIPT_PROFILER;
            /* mark prototypes */
          }
        case GC_STATE_MARKING_SCRIPT_PROFILER:
          visitData->m_data.m_phase = GC_STATE_MARKING_FINALIZER_STATE;
          if (s->m_global->m_root == s) {
          }
        case GC_STATE_MARKING_FINALIZER_STATE:
          if (m_finalizerState != NULL)
            markState_extend(m_finalizerState, visitData, depthCount);
          break;
        default: break;
      }
    }

    hksBool markInternString(InternString *str) {
      if (str->isStatic()) {
        return false;
      }
      else {
        const hksBool was_white = str->isWhite();
        str->setBlack();
        return was_white;
      }
    }

    void markTable_extend(HashTable *t, VisitData_Header *prev, hksSize depthCount) {
      const hksInt32 weakness;
      VisitData_Table visitData;
      if (!t->isBlack()) {
        if (t->m_meta != NULL) {
          visitData. HKS_METATABLE_GET(m_mainState, t->m_meta, M_MODE)

          visitData.m_data.h.m_type = TTABLE;
          visitData.m_data.m_arrayIndex = 0;
          visitData.m_data.m_hashIndex = 0;
          visitData.m_data.m_weakness = weakness;
          visitData.m_data.m_table = t;
          if (weakness != 0)
            pushWeakTable(t, weakness);
          t->setBlack();
          if (t->m_meta != NULL)
            markTable_extend(t->m_meta->toTable(), prev, depthCount - 1);
          if (m_stepsLeft - m_costs.m_markObject < m_target)
            yield(prev);
          else
            m_stepsLeft -= m_costs.m_markObject;
          checkDepth(depthCount, prev);
          if (weakness == 0)
            markTable_common(t, prev, depthCount);
          else
            markWeakTable(t, prev, depthCount - 1);
        }
      }
    }

    void markWeakTable(HashTable *h, VisitData_Table *visitData, hksSize depthCount) {
      const hksInt32 strongvalues = (visitData->m_data.m_weakness & 1) == 0;
      const hksInt32 strongkeys = (visitData->m_data.m_weakness & 2) == 0;
      const hksUint32 tableSize = h->getHashSize();
      const hksUint32 arraySize = h->getArraySize();
      HashTable::Node *hashPart;
      HksRegister reg;
      hksUint32 i = visitData->m_data.m_arrayIndex;
      while (i < arraySize) {
        reg = h->getArray(i);
        if (strongvalues || HKS_GET_TYPE(reg) == TSTRING)
          markTRegister(reg, visitData, depthCount);
        i++;
        visitData->m_data.m_arrayIndex = i;
      }
      i = visitData->m_data.m_hashIndex;
      hashPart = h->getHashPart();
      while (i < tableSize) {
        HashTable::Node *n = &hashPart[i];
        if (n->isValidNode()) {
          if (strongkeys || n->getKeyTypeOfValidNode() == TSTRING) {
            reg = n->getKeyOfValidNode();
            markTRegister(reg, visitData, depthCount);
          }
          if (strongvalues || n->getValTypeofValidNode() == TSTRING) {
            reg = n->getValueOfValidNode();
            markTRegister(reg, visitData, depthCount);
          }
        }
        i++;
        visitData->m_data.m_hashIndex = i;
      }
    }

    void markTable_common(HashTable *t, VisitData_Table *visitData, hksSize depthCount) {
      HksRegister reg;
      HashTable::Node *hashPart;
      const hksUint32 arraySize = t->getArraySize();
      const hksUint32 tableSize = t->getHashSize();
      hksUint32 i = visitData->m_data.m_arrayIndex;
      while (i = arraySize) {
        reg = t->getArray(i);
        markTRegister(reg, visitData, depthCount);
        i++;
        visitData->m_data.m_arrayIndex = i;
      }
      hashPart = t->getHashPart();
      i = visitData->m_data.m_hashIndex;
      while (i < tableSize) {
        HashTable::Node *n = &hashPart[i];
        if (n->isValidNode()) {
          reg = n->getKeyOfValidNode();
          markTRegister(reg, visitData, depthCount);
          reg = n->getValueOfValidNode();
          markTRegister(reg, visitData, depthCount);
        }
        i++;
        visitData->m_data.m_hashIndex = i;
      }
    }

    void markUserdata_extend(UserData *data, VisitData_Header *prev, hksSize depthCount) {
      VisitData_Userdata visitData;
      if (!data->isBlack()) {
        visitData.m_data.h.m_type = TUSERDATA;
        visitData.m_data.m_data = data;
        visitData.h2.m_prev = prev;
        data->setBlack();
        m_stepsLeft -= m_costs.m_markObject;
        if (data->m_env != NULL)
          markTable_extend(data->m_env, &visitData, depthCount - 1);
        if (m_stepsLeft < m_target)
          yield(&visitData);
        checkDepth(depthCount, &visitData);
        markUserdata_common(data, &visitData, depthCount);
      }
    }

    void markUserdata_common(UserData *data, VisitData_Userdata *visitData, hksSize depthCount) {
      if (data->m_meta != NULL)
        markTable_extend(data->m_meta->toTable(), visitData, depthCount - 1);
    }

    void markCClosure_extend(cclosure *cc, VisitData_Header *prev, hksSize depthCount) {
      VisitData_CClosure visitData;
      if (!cc->isBlack()) {
        visitData.m_data.m_upvalueIndex = cc->m_numUpvalues;
        visitData.m_data.h.m_type = TCFUNCTION;
        visitData.h2.m_prev = prev;
        visitData.m_data.m_cclosure = cc;
        cc->setBlack();
        if (m_stepsLeft - m_costs.m_markObject < m_target)
          yield(&visitData);
        else
          m_stepsLeft -= m_costs.m_markObject;
        checkDepth(depthCount, &visitData);
        markCClosure_common(cc, &visitData, depthCount);
      }
    }

    void markCClosure_common(cclosure *cc, VisitData_CClosure *visitData, hksSize depthCount) {
      hksUint32 upValue;
      for (upValue = visitData->m_data.m_upvalueIndex; upValue != 0; upValue--) {
        visitData->m_data.m_upvalueIndex = upValue - 1;
        markTObject(&cc->m_upvalues[upValue - 1], visitData, depthCount - 1);
      }
      if (cc->m_name != NULL)
        markInternString(cc->m_name);
      if (cc->m_env != NULL)
        markTable_extend(cc->m_env, visitData, depthCount - 1);
    }

    void markClosure_extend(HksClosure *c, VisitData_Header *prev, hksSize depthCount) {
      VisitData_Closure visitData;
      if (!c->isBlack()) {
        visitData.m_data.h.m_type = TIFUNCTION;
        visitData.m_data.m_index = -2;
        visitData.h2.m_prev = prev;
        visitData.m_data.m_closure = c;
        c->setBlack();
        if (m_stepsLeft - m_costs.m_markObject < m_target)
          yield(&visitData);
        else
          m_stepsLeft -= m_costs.m_markObject;
        checkDepth(depthCount, &visitData);
        markClosure_common(c, &visitData, depthCount);
      }
    }

    void markClosure_common(HksClosure *c, VisitData_Closure *visitData, hksSize depthCount) {
      UpValue *uv;
      hksInt32 i = visitData->m_data.m_index;
      if (i == -2) {
        i = -1;
        visitData->m_data.m_index = -1;
        if (c->m_env != NULL)
          markTable_extend(c->m_env, visitData, depthCount - 1);
      }
      if (i == -1) {
        i = c->m_method->num_upvals;
        visitData->m_data.m_index = i;
      }
      if (i > 0) {
        unsigned int n = i;
        while (--n != 0) {
          visitData->m_data.m_index = n;
          uv = closureGetUpValue(c, n);
          if (uv != NULL) {
            uv->setBlack();
            m_stepsLeft -= m_costs.m_markObject;
            markTObject(uv->loc, visitData, depthCount - 2);
          }
        }
      }
      Method *m = c->m_method;
      if (m->isWhite()) {
        hksInt32 marked = markMethod(m);
        m_stepsLeft -= marked * m_costs.m_markObject;
      }
    }

    hksInt32 markMethod(Method *m) {
      int marked = 1;
      m->setBlack();
      if (m->m_debug != NULL) {
        if (m->m_debug->source != NULL) {
          markInternString(m->m_debug->source);
          marked = 2;
        }
        if (m->m_debug->name != NULL) {
          markInternString(m->m_debug->name);
          marked++;
        }
        for (unsigned int i = 0; i < m->m_debug->localInfo.size; i++)
          markInternString(m->m_debug->localInfo.data[i].name);
        marked += m->m_debug->localInfo.size;
        for (unsigned int i = 0; i < m->m_debug->upvalInfo.size; i++)
          markInternString(m->m_debug->upvalInfo.data[i]);
        marked += m->m_debug->upvalInfo.size;
      }
      for (unsigned int i = 0; i < m->constants.size; i++) {
        if (m->constants.data[i].t == TSTRING) {
          markInternString(m->constants.data[i].v.str);
          marked++;
        }
      }
      for (unsigned int i = 0; i < m->children.size; i++)
        marked += markMethod(m->children.data[i]);
      return marked;
    }
  };

  int GarbageCollector::luaWrapper_finalizeUserdata(lua_State *s) {
    if (s->m_global->m_collector.m_jumpPoint == (void *)-1)
      s->m_global->m_collector.finalizeAllUserdataWorker(s);
    else
      s->m_global->m_collector.finalizeUserdataWorker(s);
    return 1;
  }
}
