namespace hks {

  HksRegister HKS_METATABLE_GET(const lua_State *s, HashTable *mt, Metamethod m) {
    return mt->getByString(s->m_global->m_staticStringCache.m_objects[m]);
  }

  inline HksObjectType RegType(const HksRegister reg)
  {
    return HKS_GET_TYPE(reg);
  }

  int S_API_STACK_FRAMESIZE(const lua_State *s) {
    return S_API_STACK_TOP(s) - S_API_STACK_BASE(s);
  }

  inline HksObject *indexToHksObjectSafe(lua_State *s, int index, int &failure) {
    if (index > 0) {
      HksObject *sp = S_API_STACK_BASE(s) + index - 1;
      if (sp < S_API_STACK_TOP(s))
        failure = 0;
      else {
        failure = 1;
        return NULL;
      }
      return sp;
    }
    else if (index > LUA_REGISTRYINDEX) {
      HksObject *sp = S_API_STACK_TOP(s) + index;
      if (sp < S_API_STACK_BASE(s)) {
        failure = 1;
        return NULL;
      }
      failure = 0;
      return sp;
    }
    else switch (index) {
      case LUA_REGISTRYINDEX: return &s->m_global->m_registry;
      case LUA_ENVIRONINDEX:
        api_assert(S_API_STACK_BASE(s) > S_API_STACK_BOTTOM(s),
                   "No context for LUA_ENVIRONINDEX");
        s->m_cEnv.v = S_API_STACK_BASE(s)[-1].v.cClosure->m_env;
        s->m_cEnv.t = TTABLE;
        return &s->m_cEnv;
      case LUA_GLOBALSINDEX: return &s->globals;
      default:
        return S_API_STACK_BASE(s)[-1].v.cClosure->m_upvalues + (LUA_GLOBALSINDEX - index);
    }
  }

  inline HksObject *indexToHksObjectFast(lua_State *s, int index) {
    if (index <= LUA_REGISTRYINDEX) {
      switch (index) {
        case LUA_REGISTRYINDEX: return &s->m_global->m_registry;
        case LUA_ENVIRONINDEX:
          api_assert(S_API_STACK_BASE(s) > S_API_STACK_BOTTOM(s),
                     "No context for LUA_ENVIRONINDEX");
          s->m_cEnv.v = S_API_STACK_BASE()[-1].v.cClosure->m_env;
          s->m_cEnv.t = TTABLE;
          return &s->m_cEnv;
        case LUA_GLOBALSINDEX: return &s->globals;
      }
    }
    else {
      HksObject *sp;
      if (index > 0)
        sp = S_API_STACK_BASE(s) + index;
      else
        sp = S_API_STACK_TOP(s) + index;
      return sp;
    }
  }

  void hksi_lua_pushvalue(lua_State *s, int index) {
    api_assert(s != NULL, "Parameter 's' must be a valid state pointer");
    HksObject *sp = indexToHksObjectFast(s, index);
    HksObject *st = S_API_STACK_TOP(s);
    *st = *sp;
    S_API_STACK_TOP(s)++;
  }
}
