namespace hks {
  void getTableMemory(lua_State *s, hksSize sizeofHashTable, hksSize hashSize, hksSize arraySize, void **allocations) {
    MemoryManager &memory = s->m_global->m_memory;
    const hksSize totalSize = sizeofHashTable + hashSize + arraySize;
    s->m_global->m_collector.checkStep(s, 3);
    do {
      for (int attempts = 0; attempts < 3; attempts++) {
        if (s->m_global->m_collector.resetEmergencyMemoryReserve()) {
          hksBool succeeded;
          allocations[0] = memory.allocate(sizeofHashTable, Alloc_Table_Hdr);
          succeeded = allocations[0] != NULL;
          if (hashSize) {
            allocations[1] = memory.allocateNoHeader(hashSize, Alloc_Table_Hash);
            succeeded = succeeded && allocations[1] != NULL;
          }
          if (arraySize) {
            allocations[2] = memory.allocateNoHeader(arraySize, Alloc_Table_Array);
            succeeded = succeeded && allocations[2] != NULL;
          }
          if (succeeded)
            return;
        }
        if (allocations[0]) {
          memset((HashTable *)allocations[0] + sizeof(ChunkHeader), 0, sizeofHashTable - sizeof(ChunkHeader));
          ((GenericChunkHeader *)allocations[0])->setWhite();
          allocations[0] = NULL;
        }
        if (allocations[1]) {
          freeMemoryNoHeader(s, allocations[1], hashSize, Alloc_Table_Hash);
          allocations[1] = NULL;
        }
        if (allocations[2]) {
          freeMemoryNoHeader(s, allocations[2], arraySize, Alloc_Table_Array);
          allocations[2] = NULL;
        }
        s->m_global.m_collector.stepNonIncremental(s, GC_CALL_EMERGENCY);
      }
      OutOfMemoryError(s, totalSize);
    } while (true);
  }

  struct HashTable : ChunkHeader {
    Metatable *m_meta;

    struct Node {
      HksRegister m_key;
      HksRegister m_value;

      bool isValidNode() const;
      HksObjectType getKeyTypeOfValidNode() const;
      HksObjectType getValTypeOfValidNode() const;
      HksRegister getKeyOfValidNode() const;
      HksRegister getValueOfValidNode() const;
      bool compareKey(const HksRegister) const;
      HksObject getKeyAsObject() const;
      lua_Number getKeyNumber() const;
      InternString *getKeyString() const;
      HksObjectType getKeyType() const;
      HksObjectType getValType() const;
      void setValue(const HksRegister);
      void setValueObject(const HksObject);
      void setKey(const HksObject);
      HksRegister get_value() const;
      HksRegister get_key() const;
    };

    hksUint32 m_mask;
    Node *m_hashPart;
    HksRegister *m_arrayPart;
    hksUint32 m_arraySize;
    Node *m_freeNode;

    static hksUint computeHashSizeBits(hksUint numElements) {
      return numElements ? log2ceil(numElements) : 0;
    }

    void setHashSize(hksUint32 size) {
      m_mask = size - 1;
    }

    hksUint getHashSize() {
      m_mask + 1;
    }

    Node *hashPartAllocationToNodes(void *alloc) {
      return (char *)alloc + getHashSize() * sizeof(void *);
    }

    static HashTable *Create(lua_State *s, hksUint32 arraySize, hksUint32 hashSize) {
      s->m_global->m_runProfilerData.frameStats.num_newtables++;
      const hksUint hashMask = hashSize ? 1 << (computeHashSizeBits(hashSize) & 31) : 0;
      const hksSize hashSizeBytes = getHashPartBytes(hashMask);
      const hksUint arraySizeBytes = arraySize * sizeof(HksObject);
      void *allocations[3];
      memset(allocations, 0, sizeof(allocations));
      getTableMemory(s, sizeof(HashTable), hashSizeBytes, arraySizeBytes, allocations);
      HashTable *table = allocations[0];
      memset((char *)table + sizeof(ChunkHeader), 0, sizeof(HashTable) - sizeof(ChunkHeader));
      table->setHashSize(hashMask);
      if (allocations[1] == NULL) {
        table->m_hashPart = NULL;
        table->m_freeNode = NULL;
      }
      else {
        memset(allocations[1], 0, hashSizeBytes);
        table->m_hashPart = table->hashPartAllocationToNodes(allocations[1]);
        table->m_freeNode = table->m_hashPart + table->getHashSize();
      }
      if (allocations[2] != NULL && arraySizeBytes != 0)
        memset(allocations[2], 0, arraySizeBytes);
      table->m_arrayPart = allocations[2];
      table->m_arraySize = arraySize;
      return table;
    }

    typedef hksUint SlicedIntCount[25];

    hksUint countArrayKeys(hksUint *slicedIntCount) {

    }

    void doRehash(lua_State *s, const HksObject *key) {
      s->m_global->m_runProfilerData.frameStats.num_tablerehash++;
      if (s->m_global->m_profiler != NULL)
        s->m_global->m_profiler->Hazard_TableRehash(this);
      if (!isUsedForReffing(s)) {
        SlicedIntCount slicedIntCount;
        memset(slicedIntCount, 0, sizeof(slicedIntCount));
        hksUint arraySize = countArrayKeys(slicedIntCount);
        hksUint totalKeys = arraySize;
        totalKeys += countHashKeys();
        arraySize += updateIntegerCounts(key, slicedIntCount);
        const hksUint numArrayElements = computeNewArraySize(slicedIntCount, &arraySize);
        resize(s, arraySize, totalKeys - numArrayElements);
      }
      else {
        resize(s, m_arraySize, getHashSize() + 1);
      }
      if (s->m_global->m_collector.m_phase == 1)
        s->m_global->m_collector.tableRehashBarrier(this);
    }
  };
}
