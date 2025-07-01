namespace hks {

  StructInst *getStructMemory(lua_State *s, StructProto *prototype) {
    const hksSize size = (prototype->m_slots[prototype->m_numSlots-1].m_position * sizeof(HksValue)) + 24;
    StructInst *tStruct = getMemory(s, size, Alloc_Struct_Instance);
    if (tStruct != NULL) {
      tStruct->m_blocks[0].values[0].ptr = prototype;
      memset(tStruct->m_blocks[0].values + 1, 0, size - sizeof(HksValue));
    }
    return tStruct;
  }

  struct StructInst : ChunkHeader {
    hksSize unk;

    struct Block {
      unsigned char types[sizeof(HksValue)-1];
      HksValue values[sizeof(HksValue)-1];
    };

    Block m_blocks[256];

    static StructInst *Create(lua_State *s, StructProto *prototype, hksUint32 arraySize, hksUint32 hashSize) {
      if (arraySize == 0 && hashSize == 0)
        return getStructMemory(s, prototype);
      if (!prototype->m_hasProxy) {
        hksi_luaL_error(s, "Attempt to create a struct instance with a backing "
                        "table, which isn't enabled in the prototype.");
      }
      HashTable *backingTable = HashTable::Create(s, arraySize, hashSize);
      s->m_global->m_collector.saveValue(backingTable);
      StructInst *tStruct = getStructMemory(s, prototype);
      tStruct->m_blocks[0].values[2].table = backingTable;
      s->m_global->m_collector.clearValue();
      return tStruct;
    }

  };
}
