enum ProfGCPhases {
  GC_Starting = 0,
  GC_MainStateMarking = 1,
  GC_VisitStack = 2,
  GC_GreyStack = 3,
  GC_SafeValue = 4,
  GC_LuaPlus = 5,
  GC_StateStacks = 6,
  GC_CompilerDataStructures = 7,
  GC_StructProtos = 8,
  GC_ProcessCoroutines = 9,
  GC_CleanWeakTables = 10,
  GC_UserdataFinalization = 11,
  GC_ProcessInternList = 12,
  GC_Sweeping = 13,
  GC_Finished = 14,
  NumProGCPhases
};
