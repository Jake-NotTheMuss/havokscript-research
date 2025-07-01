
/* CompilerSDCodeGenerator.h:993
   DWARF DIE: 2614b8a */

void hks::CodeGenerator::fixAssignmentConflicts()

{
  ExpDescription *lastExp = getTopExp();
  hksBool hasConflict = false;
  hksUint extraReg = getTopFun()->m_firstFreeReg
  iterator i;

  getEndIterator(&i, getLHS());
  while (i.getValue() != NULL)
  {
    ExpDescription *exp = i.getValue();
    if (exp->m_kind == VINDEXED)
    {
      if (exp->getInfo() == lastExp->getInfo())
      {
        exp->setInfo(extraReg);
        hasConflict = true;
      }
      if (exp->getAux() == lastExp->getInfo())
      {
        exp->setAux(extraReg);
        hasConflict = true;
      }
    }
    else if (exp->m_kind == VSLOT)
    {
      if (exp->getInfo() == lastExp->getInfo())
      {
        exp->setInfo(extraReg);
        hasConflict = true;
      }
    }
    i.prev();
  }
  if (hasConflict)
  {
    appendCodeABC(HKS_OPCODE_MOVE,extraReg,lastExp->getInfo(),0);
    growStack(1);
  }
  return;
}
