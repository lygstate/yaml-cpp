#pragma once

#include "yaml-cpp/noncopyable.h"
#include "yaml-cpp/mark.h"
#include <cstddef>
#include <deque>
#include <ios>
#include <iostream>
#include <set>
#include <string>

namespace YAML {
class Stream : private noncopyable {
 public:
  friend class StreamCharSource;

  Stream(std::istream& input);
  ~Stream();

  //operator bool() const;
  operator bool() const {
    return (!m_readahead.empty() && m_readahead[0] != Stream::eof()) || m_input.good();
  }

  bool operator!() const { return !static_cast<bool>(*this); }

  //inline char peek() const;
  char peek() const {
    if (m_readahead.empty()) {
        return Stream::eof();
    }
    return m_readahead[0];
  }

  char get();
  std::string get(int n);
  void eat(int n);
  void eat();

  static constexpr char eof() { return 0x04; }

  const Mark mark() const { return m_mark; }
  int pos() const { return m_mark.pos; }
  int line() const { return m_mark.line; }
  int column() const { return m_mark.column; }
  void ResetColumn() { m_mark.column = 0; }
  void SkipWhiteSpace();
 private:
  enum CharacterSet { utf8, utf16le, utf16be, utf32le, utf32be };

  std::istream& m_input;
  Mark m_mark;

  CharacterSet m_charSet;
  mutable std::deque<char> m_readahead;
  unsigned char* const m_pPrefetched;
  mutable size_t m_nPrefetchedAvailable;
  mutable size_t m_nPrefetchedUsed;

  inline void AdvanceCurrent();
  char CharAt(size_t i) const;
  bool ReadAheadTo(size_t i) const;
  bool _ReadAheadTo(size_t i) const;
  void StreamInUtf8() const;
  void StreamInUtf16() const;
  void StreamInUtf32() const;
  unsigned char GetNextByte() const;
};

// CharAt
// . Unchecked access
inline char Stream::CharAt(size_t i) const { return m_readahead[i]; }

inline bool Stream::ReadAheadTo(size_t i) const {
  if (m_readahead.size() > i)
    return true;
  return _ReadAheadTo(i);
}
}
