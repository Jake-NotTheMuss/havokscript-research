namespace hks {
  enum AllocTypes {
    Alloc_Table_Hdr = 0,
    Alloc_Table_Hash = 1,
    Alloc_Table_Array = 2,
    Alloc_CClosure = 3,
    Alloc_Closure = 4,
    Alloc_IFunction_Hdr = 5,
    Alloc_IFunction_Constants = 6,
    Alloc_IFunction_Debug_LineInfo = 7,
    Alloc_IFunction_Debug_LocalInfo = 8,
    Alloc_IFunction_Debug_UpvalInfo = 9,
    Alloc_IFunction_Instructions = 10,
    Alloc_IFunction_Children = 11,
    Alloc_ByteCodeReader_Data = 12,
    Alloc_CharacterBuffer = 13,
    Alloc_State = 14,
    Alloc_State_APIStack = 15,
    Alloc_State_CallStack = 16,
    Alloc_State_Globals = 17,
    Alloc_Upvalue = 18,
    Alloc_Struct_Proto_List = 19,
    Alloc_Struct_Instance = 20,
    Alloc_Serialize_MemBuffer = 21,
    Alloc_Serialize_StringReader = 22,
    Alloc_GC_VisitStack = 23,
    Alloc_GC_WeakStack = 24,
    Alloc_GC_GreyStack = 25,
    Alloc_GC_GreyStack_RemarkStack = 26,
    Alloc_UserData = 27,
    Alloc_String = 28,
    Alloc_String_Table = 29,
    Alloc_String_Pinner = 30,
    Alloc_String_Pinner_Node = 31,
    Alloc_Script_Profiler = 32,
    Alloc_Dynamic_Buffer = 33,
    Alloc_Bytecode_Optimizer = 34,
    Alloc_Compiler_Preprocessor = 35,
    Alloc_Compiler_Func_Object = 36,
    Alloc_IFunction_DebugInfo = 37,
    Alloc_Num_Types = 38
  };

  struct ChunkList {
    void append(ChunkHeader *last, ChunkList &other) {
      last->setNext(other.m_head.getNext());
    }

    void setList(ChunkList &other) {
      m_head.setNext(other.m_head.getNext());
      other.m_head.setNext(NULL);
    }

    void addToStart(ChunkHeader *toAdd) {
      ChunkHeader *next = m_head.getNext();
      toAdd->setNext(next);
      m_head.setNext(toAdd);
    }

    void clearList() {
      m_head.setNext(NULL);
    }
  };

  struct MemoryManager {
    void *externalAllocate(void *ptr, hksSize oldSize, hksSize newSize) {
      void *allocation = (*m_allocator)(m_allocatorUd, ptr, oldSize, newSize);
      api_assert(((hksSize)allocation & 0x7) == 0);
      return allocation;
    }

    void *allocateNoHeader(hksSize new_size, AllocTypes allocType) {
      void *memory = externalAllocate(NULL, 0, new_size + 8);
      if (memory != NULL) {
        memset(memory, 0x99, 4);
        memory = (char *)memory + 4;
        memset((char *)memory + new_size, 0xaa, 4);
        if (m_state->m_global->m_profiler != NULL)
          m_state->m_global->m_profiler->AllocateMem(memory, allocType, new_size);
#if COD
        LUI_TrackMemoryAllocation(allocType, new_size, memory);
#endif
        m_used += new_size;
        if (m_used > m_highwatermark)
          m_highwatermark = m_used;
      }
      return memory;
    }

    lua_Alloc getAllocator(void **ud) {
      if (ud != NULL)
        *ud = m_allocatorUd;
      return m_allocator;
    }

    void *allocate(hksSize new_size, AllocTypes allocType) {
      GenericChunkHeader *memory = allocateNoHeader(new_size, allocType);
      if (memory != NULL) {
        memory->m_flags = m_chunkColor | ((unsigned char)allocType << 2) | (new_size << 10);
        m_allocationList.addToStart(memory);
      }
    }

    void *allocateNonSweptMemory(hksSize new_size, AllocTypes allocType) {
      GenericChunkHeader *memory = allocateNoHeader(new_size, allocType);
      if (memory != NULL)
        memory->m_flags = m_chunkColor | ((unsigned char)allocType << 2) | (new_size << 10)
      return memory;
    }

    lua_State *allocateMainState() {
      // todo: I don't think this is just inlined allocate, as in this function,
      // m_highwatermark is incremented unconditionally
      ChunkHeader *memory = allocate(sizeof(lua_State), Alloc_State);
    }

    hksSize getHighWaterMark() const {
      return m_highwatermark;
    }

    hksSize getOccupiedMemory() const {
      return m_used;
    }

    void init(lua_Alloc allocator, void *userdata) {
      memset(this, 0, sizeof(*this));
      setAllocator(allocator, userdata);
      m_used = sizeof(HksGlobal);
      m_highwatermark = m_used;
    }

    void initializeSweep() {
      m_sweepList.setList(m_allocationList);
      m_lastKeptChunk = m_sweepList.getStartPrev();
    }

    HksGcCost sweep(HksGcCost workToDo, HksGcCost freeChunkCost, HksGcCost traverseCost, HksGcCost target) {
      ChunkHeader *currentChunk = m_lastKeptChunk->getNext();
      ChunkHeader *nextChunk;
      while (currentChunk != NULL) {
        nextChunk = currentChunk->getNext();
        if (!currentChunk->isWhite()) {
          workToDo -= traverseCost;
          m_lastKeptChunk = currentChunk;
          currentChunk->setWhite();
        }
        else {
          m_sweepList.removeAfter(m_lastKeptChunk, currentChunk);
          workToDo -= freeChunkCost;
          SweepChunk(currentChunk);
        }
        if (workToDo < target)
          return;
        currentChunk = nextChunk;
      }
      m_sweepList.append(m_lastKeptChunk, m_allocationList);
      m_allocationList.setList(m_sweepList);
      m_lastKeptChunk = NULL;
      return workToDo;
    }

    void SweepChunk(ChunkHeader *objPtr) {
      const AllocTypes type = objPtr->getType();
      const hksSize allocatedSize = objPtr->getSize();
      switch (type) {
        case Alloc_Table_Hdr: {
          HashTable *target = objPtr;
          if (target->getHashPart() != NULL) {
            const hksSize allocSize = HashTable::getHashPartBytes(target->getHashSize());
            void *hashAlloc = HashTable::hashNodesToAllocation(target->getHashPart(), target->getHashSize());
            release(hashAlloc, allocSize, Alloc_Table_Hash);
          }
          if (target->getArrayPart() != NULL) {
            const hksSize allocSize = target->getArraySize() * sizeof(HksObject);
            release(target->getArrayPart(), allocSize, Alloc_Table_Array);
          }
          break;
        }
        case Alloc_State: {
          lua_State *target = objPtr;
          if (S_API_STACK_BOTTOM(target) != NULL) {
            const hksSize stackSize = (S_API_STACK_ALLOC_TOP(target) - S_API_STACK_BOTTOM(target) + 5) * sizeof(HksObject);
            release(S_API_STACK_BOTTOM(target), stackSize, Alloc_State_APIStack);
          }
          if (target->m_callStack.m_records != NULL) {
            const hksSize callstackSize = (target->m_callStack.m_lastrecord - target->m_callStack.m_records) * sizeof(CallStack::ActivationRecord);
            release(target->m_callStack.m_records, callstackSize, Alloc_State_CallStack);
          }
          break;
        }
        case Alloc_IFunction_Hdr: {
          Method *m = objPtr;
          if (m_state != NULL && m_state->m_global->m_debugger != NULL)
            m_state->m_global->m_debugger->MethodReleased(m_state, m);
          if (m->instructions.data != NULL && (m->m_flags & 8) == 0)
            release(m->instructions.data, m->instructions.size * sizeof(hksInstruction), Alloc_IFunction_Instructions);
          if (m->constants.data != NULL)
            release(m->constants.data, m->constants.size * sizeof(HksObject), Alloc_IFunction_Constants);
          if (m->children.data != NULL)
            release(m->children.data, m->children.size * sizeof(Method *), Alloc_IFunction_Children);
          if (m->m_debug != NULL) {
            const hksSize allocSize = Method::sizeofDebugInfo(m->m_debug);
            release(m->m_debug, allocSize, Alloc_IFunction_DebugInfo);
          }
          break;
        }
        default: {
          const releaseBytes = allocatedSize;
        }
      }
      release(objPtr, allocatedSize, type);
    }

    void release(void *unused, const hksSize size, AllocTypes AllocType) {
      if (m_state != NULL && m_state->m_global->m_profiler != NULL)
        m_state->m_global->m_profiler->FreeMem(unused, AllocType, size);
#if COD
      LUI_TrackMemoryRelease(AllocType, size, unused);
#endif
      m_used -= size;
      externalAllocate((char *)unused - 4, size + 8, 0);
    }

    void releaseAllMethods() {
      ChunkHeader *ch = m_allocationList.getStart();
      while (ch != NULL) {
        ChunkHeader *last_chunk = ch->getNext();
        if (ch->getType() == Alloc_IFunction_Hdr && m_state != NULL &&
            m_state->m_global->m_debugger != NULL)
          m_state->m_global->m_debugger->MethodReleased(m_state, ch);
        ch = last_chunk;
      }
      ch = m_sweepList.getStart();
      while (ch != NULL) {
        ChunkHeader *last_chunk = ch->getNext();
        if (ch->getType() == Alloc_IFunction_Hdr && m_state != NULL &&
            m_state->m_global->m_debugger != NULL)
          m_state->m_global->m_debugger->MethodReleased(m_state, ch);
      }
    }

    void setAllocator(lua_Alloc allocator, void *userdata) {
      m_allocator = allocator;
      m_allocatorUd = userdata;
    }

    void setNewAllocations(ChunkColor color) {
      m_chunkColor = color;
    }

    void shutdown() {
      m_state = NULL;
      ChunkHeader *ch = m_allocationList.getStart();
      while (ch != NULL) {
        ChunkHeader *last_chunk = ch->getNext();
        SweepChunk(ch);
        ch = last_chunk;
      }
      ch = m_sweepList.getStart();
      while (ch != NULL) {
        ChunkHeader *last_chunk = ch->getNext();
        SweepChunk(ch);
        ch = last_chunk;
      }
      m_allocationList.clearList();
      m_sweepList.clearList();
      m_lastKeptChunk = NULL;
    }
  };
}
