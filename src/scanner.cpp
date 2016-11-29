#include <cassert>
#include <memory>

#include "exp.h"
#include "scanner.h"
#include "token.h"
#include "yaml-cpp/exceptions.h"  // IWYU pragma: keep

namespace YAML {
Scanner::Scanner(std::istream& in)
    : INPUT(in),
      m_startedStream(false),
      m_endedStream(false),
      m_simpleKeyAllowed(false),
      m_canBeJSONFlow(false) {}

Scanner::Scanner(const std::string& in)
    : INPUT(in),
      m_startedStream(false),
      m_endedStream(false),
      m_simpleKeyAllowed(false),
      m_canBeJSONFlow(false) {}

Scanner::~Scanner() {}

bool Scanner::empty() {
  EnsureTokensInQueue();
  return m_tokens.empty();
}

void Scanner::pop() {
  EnsureTokensInQueue();
  if (!m_tokens.empty())
    m_tokens.pop_front();
}

Token& Scanner::peek() {
  EnsureTokensInQueue();
  assert(!m_tokens.empty());  // should we be asserting here? I mean, we really
                              // just be checking
                              // if it's empty before peeking.

#if 0
                static Token *pLast = 0;
                if(pLast != &m_tokens.front())
                        std::cerr << "peek: " << m_tokens.front() << "\n";
                pLast = &m_tokens.front();
#endif

  return m_tokens.front();
}

Mark Scanner::mark() const { return INPUT.mark(); }

void Scanner::EnsureTokensInQueue() {
  while (1) {
    if (!m_tokens.empty()) {
      Token& token = m_tokens.front();

      // if this guy's valid, then we're done
      if (token.status == Token::VALID) {
        return;
      }

      // here's where we clean up the impossible tokens
      if (token.status == Token::INVALID) {
        m_tokens.pop_front();
        continue;
      }

      // note: what's left are the unverified tokens
    }

    // no token? maybe we've actually finished
    if (m_endedStream) {
      return;
    }

    // no? then scan...
    ScanNextToken();
  }
}

void Scanner::ScanNextToken() {
  if (m_endedStream) {
    return;
  }

  if (!m_startedStream) {
    return StartStream();
  }

  // get rid of whitespace, etc. (in between tokens it should be irrelevent)
  ScanToNextToken();

  // maybe need to end some blocks
  PopIndentToHere();

  // *****
  // And now branch based on the next few characters!
  // *****

  // end of stream
  if (!INPUT) {
    return EndStream();
  }
  char c = INPUT.peek();

  // flow start/end/entry
  if (c == Keys::FlowSeqStart ||
      c == Keys::FlowMapStart) {
    return ScanFlowStart();
  }

  if (c == Keys::FlowSeqEnd ||
      c == Keys::FlowMapEnd) {
    return ScanFlowEnd();
  }

  if (c == Keys::FlowEntry) {
    return ScanFlowEntry();
  }

  // Get large enough lookahead buffer for all Matchers
  auto input = INPUT.LookaheadBuffer(5);

  if (INPUT.column() == 0) {
    if (c == Keys::Directive) {
      return ScanDirective();
    }

    // document token
    if (Exp::DocStart::Matches(input)) {
        return ScanDocStart();
    }

    if (Exp::DocEnd::Matches(input)) {
        return ScanDocEnd();
    }
  }

  // block/map stuff
  if (Exp::BlockEntry::Matches(input)) {
    return ScanBlockEntry();
  }

  if (InBlockContext() ?
      // TODO these are the same?
      Exp::Key::Matches(input) :
      Exp::KeyInFlow::Matches(input)) {
    return ScanKey();
  }

  if ((InBlockContext() && Exp::Value::Matches(input)) ||
      (m_canBeJSONFlow ?
       Exp::ValueInJSONFlow::Matches(input) :
       Exp::ValueInFlow::Matches(input))) {
    return ScanValue();
  }

  // alias/anchor
  if (c == Keys::Alias ||
      c == Keys::Anchor) {
    return ScanAnchorOrAlias();
  }

  // tag
  if (c == Keys::Tag) {
    return ScanTag();
  }

  // special scalars
  if (InBlockContext() && (c == Keys::LiteralScalar ||
                           c == Keys::FoldedScalar)) {
    return ScanBlockScalar();
  }

  if (c == '\'' || c == '\"') {
    return ScanQuotedScalar();
  }

  if (Exp::PlainScalarCommon::Matches(input)) {
    // plain scalars
    if (InBlockContext() ?
        Exp::PlainScalar::Matches(input) :
        Exp::PlainScalarInFlow::Matches(input)) {
      return ScanPlainScalar();
    }
  }

  // don't know what it is!
  throw ParserException(INPUT.mark(), ErrorMsg::UNKNOWN_TOKEN);
}

void Scanner::ScanToNextToken() {
  while (1) {
    INPUT.SkipWhiteSpace();

    // first eat whitespace
    while (INPUT && IsWhitespaceToBeEaten(INPUT.peek())) {
      if (InBlockContext() && Exp::Tab::Matches(INPUT)) {
        m_simpleKeyAllowed = false;
      }
      INPUT.eat();
    }

    // then eat a comment
    if (Exp::Comment::Matches(INPUT)) {
      // eat until line break
      while (INPUT && !Exp::Break::Matches(INPUT)) {
        INPUT.eat();
      }
    }

    // if it's NOT a line break, then we're done!
    int n = Exp::Break::Match(INPUT);
    if (n < 0) { break; }

    // otherwise, let's eat the line break and keep going
    INPUT.eat(n);

    // oh yeah, and let's get rid of that simple key
    InvalidateSimpleKey();

    // new line - we may be able to accept a simple key now
    if (InBlockContext()) {
      m_simpleKeyAllowed = true;
    }
  }
}

///////////////////////////////////////////////////////////////////////
// Misc. helpers

// IsWhitespaceToBeEaten
// . We can eat whitespace if it's a space or tab
// . Note: originally tabs in block context couldn't be eaten
//         "where a simple key could be allowed
//         (i.e., not at the beginning of a line, or following '-', '?', or
// ':')"
//   I think this is wrong, since tabs can be non-content whitespace; it's just
//   that they can't contribute to indentation, so once you've seen a tab in a
//   line, you can't start a simple key
bool Scanner::IsWhitespaceToBeEaten(char ch) {
  if (ch == ' ') {
    return true;
  }

  if (ch == '\t') {
    return true;
  }

  return false;
}


void Scanner::StartStream() {
  m_startedStream = true;
  m_simpleKeyAllowed = true;
  std::unique_ptr<IndentMarker> pIndent(
      new IndentMarker(-1, IndentMarker::NONE));
  m_indentRefs.push_back(std::move(pIndent));
  m_indents.push(m_indentRefs.back().get());
}

void Scanner::EndStream() {
  // force newline
  if (INPUT.column() > 0) {
    INPUT.ResetColumn();
  }

  PopAllIndents();
  PopAllSimpleKeys();

  m_simpleKeyAllowed = false;
  m_endedStream = true;
}

Token* Scanner::PushToken(Token::TYPE type) {
  m_tokens.emplace_back(type, INPUT.mark());
  return &m_tokens.back();
}

Token::TYPE Scanner::GetStartTokenFor(IndentMarker::INDENT_TYPE type) const {
  switch (type) {
    case IndentMarker::SEQ:
      return Token::BLOCK_SEQ_START;
    case IndentMarker::MAP:
      return Token::BLOCK_MAP_START;
    case IndentMarker::NONE:
      assert(false);
      break;
  }
  assert(false);
  throw std::runtime_error("yaml-cpp: internal error, invalid indent type");
}

Scanner::IndentMarker* Scanner::PushIndentTo(int column,
                                             IndentMarker::INDENT_TYPE type) {
  // are we in flow?
  if (InFlowContext()) {
    return 0;
  }

  std::unique_ptr<IndentMarker> pIndent(new IndentMarker(column, type));
  IndentMarker& indent = *pIndent;
  const IndentMarker& lastIndent = *m_indents.top();

  // is this actually an indentation?
  if (indent.column < lastIndent.column) {
    return 0;
  }
  if (indent.column == lastIndent.column &&
      !(indent.type == IndentMarker::SEQ &&
        lastIndent.type == IndentMarker::MAP)) {
    return 0;
  }

  // push a start token
  indent.pStartToken = PushToken(GetStartTokenFor(type));

  // and then the indent
  m_indents.push(&indent);
  m_indentRefs.push_back(std::move(pIndent));
  return m_indentRefs.back().get();
}

void Scanner::PopIndentToHere() {
  // are we in flow?
  if (InFlowContext()) {
    return;
  }

  // now pop away
  while (!m_indents.empty()) {
    const IndentMarker& indent = *m_indents.top();
    if (indent.column < INPUT.column()) {
      break;
    }
    if (indent.column == INPUT.column() &&
        !(indent.type == IndentMarker::SEQ &&
          !Exp::BlockEntry::Matches(INPUT))) {
      break;
    }

    PopIndent();
  }

  while (!m_indents.empty() &&
         m_indents.top()->status == IndentMarker::INVALID) {
    PopIndent();
  }
}

void Scanner::PopAllIndents() {
  // are we in flow?
  if (InFlowContext()) {
    return;
  }

  // now pop away
  while (!m_indents.empty()) {
    const IndentMarker& indent = *m_indents.top();
    if (indent.type == IndentMarker::NONE) {
      break;
    }

    PopIndent();
  }
}

void Scanner::PopIndent() {
  const IndentMarker& indent = *m_indents.top();
  m_indents.pop();

  if (indent.status != IndentMarker::VALID) {
    InvalidateSimpleKey();
    return;
  }

  if (indent.type == IndentMarker::SEQ) {
    m_tokens.emplace_back(Token::BLOCK_SEQ_END, INPUT.mark());
  } else if (indent.type == IndentMarker::MAP) {
    m_tokens.emplace_back(Token::BLOCK_MAP_END, INPUT.mark());
  }
}

int Scanner::GetTopIndent() const {
  if (m_indents.empty()) {
    return 0;
  }
  return m_indents.top()->column;
}

void Scanner::ThrowParserException(const std::string& msg) const {
  Mark mark = Mark::null_mark();
  if (!m_tokens.empty()) {
    const Token& token = m_tokens.front();
    mark = token.mark;
  }
  throw ParserException(mark, msg);
}
}  // namespace YAML
