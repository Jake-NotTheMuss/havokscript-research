template<typename T, unsigned int BLOCK_SIZE, unsigned int SPARE_BLOCKS>
class HksDynamicVector {
protected:
  lua_State *const m_state;

  struct Node {
    hksUint64 m_bufferStorage[BLOCK_SIZE * sizeof(T) / sizeof(hksUint64)];
    T *m_buffer;
    Node *m_nextNode;
    Node *m_prevNode;

    Node() {
      m_buffer = (T *)m_bufferStorage;
      m_nextNode = NULL;
      m_prevNode = NULL;
    }

    Node(Node *prevNode) {
      m_buffer = (T *)m_bufferStorage;
      m_nextNode = NULL;
      m_prevNode = prevNode;
      prevNode->m_nextNode = this;
    }
  };

  Node m_stackNode;

  Node *m_firstNode;
  Node *m_currentNode;
  T *m_bufferCurrentPlace;
  T *m_bufferNextPlace;
  hksUint m_size;
  hksUint m_storageSize;

public:

  HksDynamicVector(lua_State *s) : m_state(s) {
    m_firstNode = NULL;
    m_currentNode = NULL;
    m_bufferCurrentPlace = NULL;
    m_bufferNextPlace = NULL;
    m_size = 0;
    m_storageSize = 0;
    m_firstNode = &m_stackNode;
    m_currentNode = &m_stackNode;
    m_bufferNextPlace = m_stackNode.m_buffer;
    m_storageSize += BLOCK_SIZE;
  }

  ~HksDynamicVector() {
    Node *cur = m_firstNode->m_nextNode;
    while (cur != NULL) {
      Node *next = cur->m_nextNode;
      freeMemoryNoHeader(m_state, cur, sizeof(Node), Alloc_Dynamic_Buffer);
      cur = next;
    }
  }

  T &getTop () const {
    return *m_bufferCurrentPlace;
  }

  T &increment () {
    T &result = m_bufferNextPlace;
    m_bufferCurrentPlace = m_bufferNextPlace;
    m_bufferNextPlace += 1;
    if (m_currentNode->m_buffer + BLOCK_SIZE <= m_bufferNextPlace) {
      if (m_currentNode->m_nextNode == NULL) {
        m_currentNode = getMemoryNoHeader(m_state, sizeof(Node), Alloc_Dynamic_Buffer);
        m_storageSize += BLOCK_SIZE;
      }
      else
        m_currentNode = m_currentNode->m_nextNode;
      m_bufferNextPlace = m_currentNode->m_buffer;
    }
    m_size += 1;
    return result;
  }

  void destructDecrement () {
    m_bufferCurrentPlace->~T();
    decrement();
  }

  void decrement () {
    if (m_bufferCurrentPlace <= m_currentNode->m_buffer ||
        m_bufferCurrentPlace >= m_currentNode->m_buffer + BLOCK_SIZE) {
      if (m_bufferCurrentPlace == m_currentNode->m_buffer) {
        if (m_currentNode->m_prevNode == NULL)
          m_bufferCurrentPlace = NULL;
        else
          m_bufferCurrentPlace = m_currentNode->m_prevNode->m_buffer + BLOCK_SIZE-1;
        m_bufferNextPlace -= 1;
      }
      else {
        if (m_currentNode->m_prevNode != NULL &&
            m_currentNode->m_prevNode->m_buffer + BLOCK_SIZE-1 == m_bufferCurrentPlace) {
          Node *prev = m_currentNode->m_prevNode;
          m_bufferCurrentPlace -= 1;
          m_bufferNextPlace = prev->m_buffer + BLOCK_SIZE - 1;
          Node *cur = m_currentNode;
          if (cur)
            cur = cur->m_nextNode;
          if (cur) {
            cur->m_prevNode->m_nextNode = NULL;
            freeMemoryNoHeader(m_state, cur, sizeof(Node), Alloc_Dynamic_Buffer);
            m_storageSize -= BLOCK_SIZE;
          }
          m_currentNode = m_currentNode->m_prevNode;
        }
      }
    }
    else {
      m_bufferCurrentPlace -= 1;
      m_bufferNextPlace -= 1;
    }
    m_size -= 1;
  }

  T &unsafeIndex (const hksUint index) const {
    const hksUint blockNumber = index / BLOCK_SIZE;
    const hksUint blockOffset = index % BLOCK_SIZE;
    if (getSize() / BLOCK_SIZE == blockNumber) {
      return m_currentNode->m_buffer[blockOffset];
    }
    else {
      Node *cur = m_firstNode;
      for (hksUint i = 0; i < blockNumber; i++)
        cur = cur->m_nextNode;
      return cur->m_buffer[blockOffset];
    }
  }

  T &operator[] (const hksUint index) const {
    return unsafeIndex(index);
  }

  hksUint getSize () const {
    return m_size;
  }

  hksUint push (const T &obj) {
    memcpy(m_bufferNextPlace, obj, sizeof(T));
    increment();
    return getSize() - 1; /* getSize() - SPARE_BLOCKS ???? */
  }

  T &pop () {
    T &result = getTop();
    decrement();
    return result;
  }

  class iterator {
  protected:
    HksDynamicVector *m_container;
    Node *m_iterNode;
    T *m_iterPos;
    hksUint m_index;
  public:
    hksUint getIndex () const {
      return m_index;
    }

    T *getValue () const {
      return m_iterPos;
    }

    T *next () {
      if (m_iterPos == NULL)
        return NULL;
      if (m_iterPos == m_iterNode->m_buffer + BLOCK_SIZE - 1) {
        if (m_iterNode->m_nextNode == NULL) {
          m_iterPos = NULL;
          m_iterNode = NULL;
        }
        else {
          m_iterNode = m_iterNode->m_nextNode;
          m_iterPos = m_iterNode->m_buffer;
          m_index += 1;
        }
      }
      else {
        m_iterPos += 1;
        m_index += 1;
      }
      if (m_iterPos == m_container->m_bufferNextPlace) {
        m_iterPos = NULL;
        m_iterNode = NULL;
      }
      return m_iterPos;
    }

    iterator(HksDynamicVector *container, Node *node, T *pos, hksUint index) {
      m_container = container;
      m_iterNode = node;
      m_iterPos = pos;
      m_index = index;
    }
  };

  iterator getStartIterator() {
    if (m_bufferCurrentPlace == NULL)
      return iterator(this, NULL, NULL, 0);
    else
      return iterator(this, m_firstNode, m_firstNode->m_buffer, 0);
  }
}
