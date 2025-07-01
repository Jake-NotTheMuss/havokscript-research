namespace hks {
  void runtimeProfilerEvent(lua_State *s, hksInt32 event) {
    RuntimeProfileData *rd = &s->m_global->m_runProfilerData;
    hksUint64 currentCycles = __rdtsc();
    switch (event) {
      case HKS_RUNTIME_PROFILER_EVENT_GC_START: {
        rd->gcStartTime = currentCycles;
        rd->frameStats.gcTimeSamples++;
        rd->finalizerStartTime = rd->frameStats.callbackTime;
        break;
      }
      case HKS_RUNTIME_PROFILER_EVENT_GC_END: {
        const hksUint64 gcTime = currentCycles - rd->gcStartTime;
        rd->gcStartTime = 0;
        const hksUint64 finalizerTime = rd->frameStats.callbackTime - rd->finalizerStartTime;
        rd->finalizerStartTime = 0;
        if (s->m_callStack.m_current - s->m_callStack.m_records > 0 &&
            HKS_GET_TYPE(S_API_STACK_BASE(s)[-1]) == TIFUNCTION) {
          rd->frameStats.hksTime -= gcTime - finalizerTime;
        }
        rd->frameStats.callbackTime -= finalizerTime;
        rd->frameStats.gcTime += finalizerTime - gcTime;
        rd->frameStats.cFinalizerTime += finalizerTime;
        break;
      }
      case HKS_RUNTIME_PROFILER_EVENT_COMPILER_START: {
        if (rd->compilerDepth == 0) {
          rd->compilerStartTime = currentCycles;
          rd->compilerStartGCTime = rd->frameStats.gcTime;
          rd->compilerStartGCFinalizerTime = rd->frameStats.cFinalizerTime;
          rd->compilerCallbackStartTime = rd->frameStats.callbackTime;
          rd->frameStats.compilerTimeSamples += 1;
        }
        rd->compilerDepth++;
        break;
      }
      case HKS_RUNTIME_PROFILER_EVENT_COMPILER_END: {
        rd->compilerDepth--;
        if (rd->compilerDepth == 0) {
          hksUint64 compilerTime = currentCycles - rd->compilerStartTime;
          compilerTime -= rd->frameStats.gcTime - rd->compilerStartGCTime;
          compilerTime -= rd->frameStats.cFinalizerTime - rd->compilerStartGCFinalizerTime;
          compilerTime -= rd->frameStats.callbackTime - rd->compilerCallbackStartTime;
          rd->frameStats.compilerTime += compilerTime;
          rd->compilerStartTime = 0;
          rd->compilerStartGCTime = 0;
          rd->compilerStartGCFinalizerTime = 0;
          rd->compilerCallbackStartTime = 0;
          if (s->m_callStack.m_current - s->m_callStack.m_records > 0 &&
              HKS_GET_TYPE(S_API_STACK_BASE(s)[-1]) == TIFUNCTION)
            rd->frameStats.hksTime -= compilerTime;
        }
        break;
      }
    }
  }
}



namespace hks {
  struct RuntimeProfileScoper {
    RuntimeProfileData *const m_data;
    const hksInt64 m_savedDepth;
    const hksInt64 m_savedCallbackDepth;
    hksBool m_wasSetup;
    lua_State *const m_state;
    HksError m_error;

    inline RuntimeProfileScoper(lua_State *const s) :
    m_state(s),
    m_data(&s->m_global->m_runProfilerData),
    m_savedDepth(s->m_global->m_runProfilerData.stackDepth),
    m_savedCallbackDepth(s->m_global->m_runProfilerData.callbackTime),
    m_wasSetup(false),
    m_error(s->m_error) {
      const hksUint64 tick = __rdtsc();
      const hksUint64 mask = (m_data->stackDepth >> 63) & 1;
      const hksUint64 callbackMask = (m_data->callbackDepth >> 63) & 1;
      const hksUint64 lastTimer = m_data->lastTimer;
      m_data->frameStats.hksTime += (tick - lastTimer) & mask;
      m_data->frameStats.callbackTime += (tick - lastTimer) & callbackMask;
      m_data->frameStats.hkssTimeSamples += mask;
      m_data->frameStats.callbackTimeSamples += callbackMask;
      m_data->stackDepth -= 1;
      m_data->callbackDepth = 0;
      m_data->lastTimer = tick;
      s->m_error = HKS_NO_ERROR;
    }

    inline ~RuntimeProfileScoper() {
      if (m_state->m_error != HKS_THROWING_ERROR) {
        const hksUint64 tick = __rdtsc();
        m_data->frameStats.hksTime += tick - m_data->lastTimer;
        m_data->frameStats.hkssTimeSamples += 1;
        m_data->stackDepth += 1;
        m_data->callbackDepth = m_savedDepth;
        m_data->lastTimer = tick;
        m_state->m_error = m_error;
      }
    }

    void onDebugEntry(lua_State *s) const {
      RuntimeProfileData *data = &s->m_global->m_runProfilerData;
      data->stackDepth = 0;
      const hksUint64 tick = __rdtsc();
      data->frameStats.hksTime += tick - data->lastTimer;
      data->frameStats.hkssTimeSamples += 1;
      data->lastTimer = tick;
    }

    void onDebugExit(lua_State *s) const {
      RuntimeProfileData *data = &s->m_global->m_runProfilerData;
      const hksUint64 tick = __rdtsc();
      data->lastTimer = tick;
      data->stackDepth = m_savedDepth - 1;
    }

    void onExit(lua_State *s) const {
      RuntimeProfileData *data = &s->m_global->m_runProfilerData;
      data->stackDepth = 0;
      data->callbackDepth -= 1;
      const hksUint64 tick = __rdtsc();
      data->frameStats.hksTime += tick - data->lastTimer;
      data->frameStats.hkssTimeSamples += 1;
      data->lastTimer = tick;
    }

    void onReturn(lua_State *s) const {
      RuntimeProfileData *data = &s->m_global->m_runProfilerData;
      const hksUint64 tick = __rdtsc();
      data->frameStats.callbackTime += tick - data->lastTimer;
      data->frameStats.callbackTimeSamples += 1;
      data->lastTimer = tick;
      data->stackDepth = m_savedDepth - 1;
      data->callbackDepth += 1;
    }

    void jmpSavepoint(lua_State *s) {
    }

    void jmpRestore(lua_State *s) {
      RuntimeProfileData *data = &s->m_global->m_runProfilerData;
      data->stackDepth = m_savedDepth - 1;
      data->callbackDepth = m_savedCallbackDepth;
      const hksUint64 tick = __rdtsc();
      data->frameStats.hksTime += tick - data->lastTimer;
      data->frameStats.hkssTimeSamples += 1;
      data->lastTimer = tick;
    }
  }
}
