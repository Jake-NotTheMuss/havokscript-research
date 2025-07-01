

template<typename T, unsigned int SIZE>
class HksStaticVector {
  protected:
  hksUint64 m_dataStorage[SIZE * sizeof(T) / sizeof(hksUint64)];
  T *m_data;
  T *m_currentPosition;

  HksStaticVector() {
    m_data = (T *)m_dataStorage;
    m_currentPosition = m_data - 1;
  }

  HksStaticVector &operator=(const HksStaticVector &other) {
    if (this != other) {
      if (other.m_currentPosition < other.m_data)
        m_currentPosition = m_data - 1;
      else
        m_currentPosition = m_data + (other.m_currentPosition - other.m_data);
      memcpy(m_dataStorage, other.m_dataStorage, SIZE);
    }
    return *this;
  }

  T &operator[](const hksUint index) const {
    return m_data[index];
  }
};
