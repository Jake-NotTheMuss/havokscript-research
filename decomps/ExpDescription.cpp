class ExpDescription /* relative to line 347 in CompilerSDCodeGenerator.h */
{

public:
  ExpDescription(); // 351
  ExpDescription(ExpressionKind kind) // 352
  : m_kind(kind)
  {
    m_trueExitsJumpList = -1;
    m_falseExitsJumpList = -1;
    HksStaticVector(&m_structLookupChain);
    m_inferredType = TNONE;
    if (kind == VTRUE || kind == VFALSE)
    {
      m_inferredType = TBOOLEAN;
    }
    else if (kind == VNIL)
    {
      m_inferredType = TNIL;
    }
    setInfo(0);
  }










  ExpDescription(ExpressionKind kind, hksInt info) // 378
  {
    m_kind = kind;
    m_trueExitsJumpList = -1;
    m_falseExitsJumpList = -1;
    HksStaticVector(&m_structLookupChain);
    m_inferredType = TNONE;
    if ()
    setInfo(info);
  }

















  ExpDescription(ExpressionKind kind, hksUint) // 405

public:
  ExpressionKind m_kind;
  union {
    struct {
      hksInt m_info;
      hksInt m_aux;
      StructProto *m_proto;
      hksObject m_structIndex;


      IntrinsicType m_intrinsicType;
      hksUint32 m_a, m_b, m_c;
    } d;
    hksNumber m_numericValue;
  } m_data;


  // patch list of `exit when true'
  hksInt m_trueExitsJumpList;

  // patch list of `exit when false'
  hksInt m_falseExitsJumpList;

  HksStaticVector<unsigned char, 16> m_structLookupChain;

  HksObjectType m_inferredType;
  StructProto *m_inferredProto;
}
