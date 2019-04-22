class MemberPointer {
private:
  char MemberPointer::*value;
public:
  template<typename T>
  MemberPointer(const T& other) {
    value = reinterpret_cast<decltype(value)>(other);
  }
  template<typename T>
  operator T() {
    return reinterpret_cast<T>(value);
  }
};
