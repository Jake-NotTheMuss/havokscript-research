/* memoryUtils.cpp */
namespace hks {
  template<typename Allocator, bool error>
  void *getMemoryGeneric(lua_State *s, hksSize size, AllocTypes type) {
    if (size != 0) {
      HksGlobal *manager = s->m_global;
      MemoryManager &memory = manager->m_memory;
      manager->m_collector.checkStep(s, 1);
      for (int attempts = 3; attempts >= 0; attempts--) {
        void *p;
        if (manager->m_collector.resetEmergencyMemoryReserve())
          p = Allocator::allocate(memory, size, type);
        else
          p = NULL;
        if (p != NULL)
          return p;
        if (attempts == 0) {
          if (error) {
            OutofMemoryError(s, size);
            attempts = 3;
          }
          else
            return NULL;
        }
        manager->m_collector.stepNonIncremental(s, GC_CALL_EMERGENCY);
      }
    }
    return NULL;
  }

  void *getMemoryNoHeader(lua_State *s, hksSize size, AllocTypes allocType) {
    return getMemoryGeneric<getMemoryNoHeaderAllocator, true>(s, size, allocType);
  }

  void *tryGetMemoryNoHeader(lua_State *s, hksSize, size, AllocTypes allocType) {
    return getMemoryGeneric<getMemoryNoHeaderAllocator, false>(s, size, allocType);
  }

  void *getMemoryNoSweep(lua_State *s, hksSize size, AllocTypes allocType) {
    return getMemoryGeneric<getMemoryNoSweepAllocator, true>(s, size, allocType);
  }

  void *getMemory(lua_State *s, hksSize size, AllocTypes allocType) {
    return getMemoryGeneric<getMemoryAllocator, true>(s, size, allocType);
  }
}
