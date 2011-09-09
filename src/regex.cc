#include "regex.h"

namespace regen {

Regex::Regex(const std::string &regex, std::size_t recursive_limit):
    regex_(regex),
    macro_expand_(false),
    recursive_limit_(recursive_limit),
    capture_num_(0),
    involved_char_(std::bitset<256>()),
    parse_ptr_(regex.c_str()),
    olevel_(Onone),
    dfa_failure_(false)
{
  Parse();
  NumberingStateExprVisitor::Numbering(expr_root_, &state_exprs_);
}

Expr::Type Regex::lex()
{
  if (*parse_ptr_ == '\0') {
    if (parse_stack_.empty()) {
      token_type_ = Expr::kEOP;
    } else {
      parse_ptr_ = parse_stack_.top();
      parse_stack_.pop();
      if (macro_expand_) {
        macro_expand_ = false;
      } else {
        recursive_depth_--;
      }
      token_type_ = lex();
    }
  } else switch (parse_lit_ = *parse_ptr_++) {
      case '.': token_type_ = Expr::kDot;       break;
      case '[': token_type_ = Expr::kCharClass; break;
      case '|': token_type_ = Expr::kUnion;     break;
      case '?': token_type_ = Expr::kQmark;     break;
      case '+': token_type_ = Expr::kPlus;      break;
      case '*': token_type_ = Expr::kStar;      break;
      case '!': token_type_ = Expr::kComplement;  break;
      case '&': token_type_ = Expr::kIntersection;  break;
      case ')': token_type_ = Expr::kRpar;      break;
      case '^': token_type_ = Expr::kBegLine;   break;
      case '$': token_type_ = Expr::kEndLine;   break;
      case '(': {
        if (*parse_ptr_ == ')') {
          parse_ptr_++;
          token_type_ = Expr::kNone;
        } else if (*parse_ptr_     == '?' &&
            *(parse_ptr_+1) == 'R' &&
            *(parse_ptr_+2) == ')') {
          // recursive expression
          parse_ptr_ += 2;
          if (recursive_depth_ >= recursive_limit_) {
            parse_ptr_++;
            token_type_ = Expr::kNone;
          } else {
            parse_stack_.push(parse_ptr_);
            recursive_depth_++;
            parse_ptr_ = regex_.c_str();
            token_type_ = Expr::kLpar;
          }
        } else {
          token_type_ = Expr::kLpar;
        }
        break;
      }
      case '{':
        lex_repetition();
        break;
      case '\\':
        lex_metachar();
        break;
      default:
        token_type_ = Expr::kLiteral;
  }
  return token_type_;
}

void Regex::lex_metachar()
{
  parse_lit_ = *parse_ptr_++;
  switch (parse_lit_) {
    case '\0': exitmsg("bad '\\'");
    case 'a': /* bell */
      parse_lit_ = '\a';
      token_type_ = Expr::kLiteral;
      break;      
    case 'd': /*digits*/
      parse_stack_.push(parse_ptr_);
      parse_ptr_ = "[0-9]";
      macro_expand_ = true;
      token_type_ = lex();
      break;
    case 'D': /*not digits*/
      parse_stack_.push(parse_ptr_);
      parse_ptr_ = "[^0-9]";
      macro_expand_ = true;
      token_type_ = lex();
      break;
    case 'f': /* form feed */
      parse_lit_ = '\f';
      token_type_ = Expr::kLiteral;
      break;
    case 'n': /* new line */
      parse_lit_ = '\n';
      token_type_ = Expr::kLiteral;
      break;
    case 'r': /* carriage retur */
      parse_lit_ = '\r';
      token_type_ = Expr::kLiteral;
      break;
    case 't': /* horizontal tab */
      parse_lit_ = '\t';
      token_type_ = Expr::kLiteral;
      break;
    case 'v': /* vertical tab */
      parse_lit_ = '\v';
      token_type_ = Expr::kLiteral;
      break;
    case 'w':
      parse_stack_.push(parse_ptr_);
      parse_ptr_ = "[0-9A-Za-z_]";
      macro_expand_ = true;
      token_type_ = lex();
      break;
    case 'W':
      parse_stack_.push(parse_ptr_);
      parse_ptr_ = "[^0-9A-Za-z_]";
      macro_expand_ = true;
      token_type_ = lex();
      break;
    case 'x': {
      unsigned char hex = 0;
      for (int i = 0; i < 2; i++) {
        parse_lit_ = *parse_ptr_++;
        hex <<= 4;
        if ('A' <= parse_lit_ && parse_lit_ <= 'F') {
          hex += parse_lit_ - 'A' + 10;
        } else if ('a' <= parse_lit_ && parse_lit_ <= 'f') {
          hex += parse_lit_ - 'a' + 10;
        } else if ('0' <= parse_lit_ && parse_lit_ <= '9') {
          hex += parse_lit_ - '0';
        } else {
          if (i == 0) {
            hex = 0;
          } else {
            hex >>= 4;
          }
          parse_ptr_--;
          break;
        }
      }
      token_type_ = Expr::kLiteral;
      parse_lit_ = hex;
      break;
    }
    default:
      token_type_ = Expr::kLiteral;
      break;
  }
}

void Regex::lex_repetition()
{
  const char *ptr = parse_ptr_;
  lower_repetition_ = 0;
  if ('0' <= *ptr && *ptr <= '9') {
    do {
      lower_repetition_ *= 10;
      lower_repetition_ += *ptr++ - '0';
    } while ('0' <= *ptr && *ptr <= '9');
  } else if (*ptr == ',') {
    lower_repetition_ = 0;
  } else {
    goto invalid;
  }
  if (*ptr == ',') {
    upper_repetition_ = 0;
    ptr++;
    if ('0' <= *ptr && *ptr <= '9') {
      do {
        upper_repetition_ *= 10;
        upper_repetition_ += *ptr++ - '0';
      } while ('0' <= *ptr && *ptr <= '9');
      if (*ptr != '}') {
        goto invalid;
      }
    } else if (*ptr == '}') {
      upper_repetition_ = -1;
    } else {
      goto invalid;
    }
  } else if (*ptr == '}') {
    upper_repetition_ = lower_repetition_;
  } else {
    goto invalid;
  }
  parse_ptr_ = ++ptr;
  if (lower_repetition_ == 0 && upper_repetition_ == -1) {
    token_type_ = Expr::kStar;
  } else if (lower_repetition_ == 1 && upper_repetition_ == -1) {
    token_type_ = Expr::kPlus;
  } else if (lower_repetition_ == 0 && upper_repetition_ == 1) {
    token_type_ = Expr::kQmark;
  } else if (lower_repetition_ == 1 && upper_repetition_ == 1) {
    token_type_ = lex();
  } else if (upper_repetition_ != -1 && upper_repetition_ < lower_repetition_) {
    exitmsg("Invalid repetition quantifier {%d,%d}",
            lower_repetition_, upper_repetition_);
  } else {
    token_type_ = Expr::kRepetition;
  }
  return;
invalid:
  token_type_ = Expr::kLiteral;
  return;
}

StateExpr*
Regex::CombineStateExpr(StateExpr *e1, StateExpr *e2)
{
  StateExpr *s;
  CharClass *cc = new CharClass(e1, e2);
  if (cc->count() == 256) {
    delete cc;
    s = new Dot();
  } else if (cc->count() == 1) {
    delete cc;
    char c;
    switch (e1->type()) {
      case Expr::kLiteral:
        c = ((Literal*)e1)->literal();
        break;
      case Expr::kBegLine: case Expr::kEndLine:
        c = '\n';
        break;
      default: exitmsg("Invalid Expr Type: %d", e1->type());
    }
    s = new Literal(c);
  } else {
    s = cc;
  }
  return s;
}

CharClass*
Regex::BuildCharClass() {
  std::size_t i;
  CharClass *cc = new CharClass();
  std::bitset<256>& table = cc->table();
  bool range;
  char lastc = '\0';

  if (*parse_ptr_ == '^') {
    parse_ptr_++;
    cc->set_negative(true);
  }

  if (*parse_ptr_ == ']' || *parse_ptr_ == '-') {
    table.set((unsigned char)*parse_ptr_);
    lastc = *parse_ptr_++;
  }

  for (range = false; (*parse_ptr_ != '\0') && (*parse_ptr_ != ']'); parse_ptr_++) {
    if (!range && *parse_ptr_ == '-') {
      range = true;
      continue;
    }

    if (*parse_ptr_ == '\\') {
      if (*(parse_ptr_++) == '\0') {
        exitmsg(" [ ] imbalance");
      }
    }

    table.set(*parse_ptr_);

    if (range) {
      for (i = (unsigned char)(*parse_ptr_) - 1; i > (unsigned char)lastc; i--) {
        table.set(i);
      }
      range = false;
    }
    lastc = *parse_ptr_;
  }
  if (*parse_ptr_ == '\0') exitmsg(" [ ] imbalance");

  if (range) {
    table.set('-');
    range = false;
  }

  parse_ptr_++;

  if (cc->count() == 1) {
    parse_lit_ = lastc;
  } else if (cc->count() >= 128 && !cc->negative()) {
    cc->set_negative(true);
    cc->flip();
  }

  return cc;
}

void Regex::Parse()
{
  Expr* e;
  StateExpr *eop;

  lex();

  do {
    e = e0();
  } while (e->type() == Expr::kNone);

  if (token_type_ != Expr::kEOP) exitmsg("expected end of pattern.");

  // add '.*' to top of regular expression.
  // Expr *dotstar;
  // StateExpr *dot;
  // dot = new Dot();
  // dot->set_expr_id(++expr_id_);
  // dot->set_state_id(++state_id_);
  // dotstar = new Star(dot);
  // dotstar->set_expr_id(++expr_id_);
  // e = new Concat(dotstar, e);
  // e->set_expr_id(++expr_id_);

  eop = new EOP();
  e = new Concat(e, eop);
  e->set_expr_id(++expr_id_);

  expr_root_ = e;
  expr_root_->FillTransition();
}

/* Regen parsing rules
 * RE ::= e0 EOP
 * e0 ::= e1 ('|' e1)*                    # union
 * e1 ::= e2 ('&' e2)*                    # intersection
 * e2 ::= e3+                             # concatenation
 * e3 ::= e4 ([?+*]|{N,N}|{,}|{,N}|{N,})* # repetition
 * e4 ::= ATOM | '(' e0 ')' | '!' e0      # ATOM, grouped expresion, complement expresion
*/

Expr *Regex::e0()
{
  Expr *e, *f;
  e = e1();
  
  while (e->type() == Expr::kNone &&
         token_type_ == Expr::kUnion) {
    lex();
    e = e1();
  }
  
  while (token_type_ == Expr::kUnion) {
    lex();
    f = e1();
    if (f->type() != Expr::kNone) {
      if (e->stype() == Expr::kStateExpr &&
          f->stype() == Expr::kStateExpr) {
        e = CombineStateExpr((StateExpr*)e, (StateExpr*)f);
      } else {
        e = new Union(e, f);
      }
    }
  }
  return e;
}

Expr *
Regex::e1()
{
  Expr *e;
  std::vector<Expr*> exprs;

  e = e2();

  while (e->type() == Expr::kNone &&
         token_type_ == Expr::kIntersection) {
    lex();
    e = e2();
  }

  exprs.push_back(e);

  while (token_type_ == Expr::kIntersection) {
    lex();
    e = e2();
    if (e->type() != Expr::kNone) {
      exprs.push_back(e);
    }
  }

  if (exprs.size() == 1) {
    e = exprs[0];
  } else {
    std::vector<Expr*>::iterator iter = exprs.begin();
    e = new Concat(*iter, new EOP());
    *iter = e;
    ++iter;
    while (iter != exprs.end()) {
      *iter = new Concat(*iter, new EOP());
      e = new Union(e, *iter);
      ++iter;
    }
    
    e->FillTransition();
    DFA dfa(e, std::numeric_limits<size_t>::max(), exprs.size());
    e = CreateRegexFromDFA(dfa);

    iter = exprs.begin();
    while (iter != exprs.end()) {
      delete *iter;
      ++iter;
    }
  }
  
  return e;
}

Expr *
Regex::e2()
{
  Expr *e, *f;
  e = e3();
  
  while (e->type() == Expr::kNone &&
         (token_type_ == Expr::kLiteral ||
          token_type_ == Expr::kCharClass ||
          token_type_ == Expr::kDot ||
          token_type_ == Expr::kEndLine ||
          token_type_ == Expr::kBegLine ||
          token_type_ == Expr::kNone ||
          token_type_ == Expr::kLpar ||
          token_type_ == Expr::kComplement)) {
    e = e3();
  }

  while (token_type_ == Expr::kLiteral ||
         token_type_ == Expr::kCharClass ||
         token_type_ == Expr::kDot ||
         token_type_ == Expr::kEndLine ||
         token_type_ == Expr::kBegLine ||
         token_type_ == Expr::kNone ||
         token_type_ == Expr::kLpar ||
         token_type_ == Expr::kComplement) {
    f = e3();
    if (f->type() != Expr::kNone) {
      e = new Concat(e, f);
    }
  }

  return e;
}

Expr *
Regex::e3()
{
  Expr *e;
  e = e4();
  bool infinity = false, nullable = false;

looptop:
  switch (token_type_) {
    case Expr::kStar:
      infinity = true;
      nullable = true;
      goto loop;
    case Expr::kPlus:
      infinity = true;
      goto loop;
    case Expr::kQmark:
      nullable = true;
      goto loop;
    default: goto loopend;
  }
loop:
  lex();
  goto looptop;
loopend:

  if (e->type() != Expr::kNone &&
      (infinity || nullable)) {
    if (infinity && nullable) {
      e = new Star(e);
    } else if (infinity) {
      e = new Plus(e);
    } else { //nullable
      e = new Qmark(e);
    }
  }
  
  if (token_type_ == Expr::kRepetition) {
    if (e->type() == Expr::kNone) goto loop;
    if (lower_repetition_ == 0 && upper_repetition_ == 0) {
      delete e;
      e = new None();
    } else if (upper_repetition_ == -1) {
      Expr* f = e;
      for (int i = 0; i < lower_repetition_ - 2; i++) {
        e = new Concat(e, f->Clone());
      }
      e = new Concat(e, new Plus(f->Clone()));
    } else if (upper_repetition_ == lower_repetition_) {
      Expr* f = e;
      for (int i = 0; i < lower_repetition_ - 1; i++) {
        e = new Concat(e, f->Clone());
      }
    } else {
      Expr *f = e;
      for (int i = 0; i < lower_repetition_ - 1; i++) {
        e = new Concat(e, f->Clone());
      }
      if (lower_repetition_ == 0) {
        e = new Qmark(e);
        lower_repetition_ = 1;
      }
      for (int i = 0; i < (upper_repetition_ - lower_repetition_); i++) {
        e = new Concat(e, new Qmark(f->Clone()));
      }
    }
    infinity = false, nullable = false;
    goto loop;
  }

  return e;
}

Expr *
Regex::e4()
{ 
  Expr *e;

  switch(token_type_) {
    case Expr::kLiteral:
      e = new Literal(parse_lit_);
      break;
    case Expr::kBegLine:
      e = new BegLine();
      break;
    case Expr::kEndLine:
      e = new EndLine();
      break;
    case Expr::kDot:
      e = new Dot();
      break;
    case Expr::kCharClass: {
      CharClass *cc = BuildCharClass();
      if (cc->count() == 1) {
        e = new Literal(parse_lit_);
        delete cc;
      } else if (cc->count() == 256) {
        e = new Dot();
        delete cc;
      } else {
        e = cc;
      }
      break;
    }
    case Expr::kNone:
      e = new None();
      break;
    case Expr::kLpar:
      lex();
      e = e0();
      if (token_type_ != Expr::kRpar) exitmsg("expected a ')'");
      Capture(e);
      break;
    case Expr::kComplement: {
      bool complement = false;
      do {
        complement = !complement;
        lex();
      } while (token_type_ == Expr::kComplement);
      e = e4();
      if (complement) {
        e = new Concat(e, new EOP());
        e->FillTransition();
        Expr *e_ = e;
        DFA dfa(e);
        dfa.Complement();
        e = CreateRegexFromDFA(dfa);
        delete e_;
      }
      return e;
    }
    case Expr::kRpar:
      exitmsg("expected a '('!");
    case Expr::kEOP:
      exitmsg("expected none-nullable expression.");
    default:
      exitmsg("can't handle Expr: %s", Expr::TypeString(token_type_));
  }

  lex();

  return e;
}

void Regex::Capture(Expr* e)
{
  std::set<StateExpr*>& first = e->transition().first;
  std::set<StateExpr*>& last = e->transition().last;

  std::set<StateExpr*>::iterator iter;
  for (iter = first.begin(); iter != first.end(); ++iter) {
    (*iter)->tag().enter.insert(capture_num_);
  }
  for (iter = last.begin(); iter != last.end(); ++iter) {
    (*iter)->tag().leave.insert(capture_num_+1);
  }
  
  capture_num_++;
  return;
}

// Converte DFA to Regular Expression using GNFA.
Expr* Regex::CreateRegexFromDFA(DFA &dfa)
{
  int GSTART  = dfa.size();
  int GACCEPT = GSTART+1;

  typedef std::map<int, Expr*> GNFATrans;
  std::vector<GNFATrans> gnfa_transition(GACCEPT);

  for (std::size_t i = 0; i < dfa.size(); i++) {
    const DFA::Transition &transition = dfa.GetTransition(i);
    GNFATrans &gtransition = gnfa_transition[i];
    for (int c = 0; c < 256; c++) {
      DFA::state_t next = transition[c];
      if (next != DFA::REJECT) {
        Expr *e;
        if (c < 255 && next == transition[c+1]) {
          int begin = c;
          while (++c < 255) {
            if (transition[c] != transition[c+1]) break;
          }
          int end = c;
          if (begin == 0 && end == 255) {
            e = new Dot();
          } else {
            std::bitset<256> table;
            bool negative = false;
            for (int j = begin; j <= end; j++) {
              table.set(j);
            }
            if (table.count() >= 128) {
              negative = true;
              table.flip();
            }
            e = new CharClass(table, negative);
          }
        } else {
          e = new Literal(c);
        }
        if (gtransition.find(next) != gtransition.end()) {
          Expr* f = gtransition[next];
          if (e->stype() == Expr::kStateExpr &&
              f->stype() == Expr::kStateExpr) {
            e = CombineStateExpr((StateExpr*)e, (StateExpr*)f);
          } else {
            e = new Union(e, f);
          }
        }
        gtransition[next] = e;
      }
    }
  }

  for (std::size_t i = 0; i < dfa.size(); i++) {
    if (dfa.IsAcceptState(i)) {
      gnfa_transition[i][GACCEPT] = NULL;
    }
  }

  gnfa_transition[GSTART][0] = NULL;
  
  for (int i = 0; i < GSTART; i++) {
    Expr* loop = NULL;
    GNFATrans &gtransition = gnfa_transition[i];
    if (gtransition.find(i) != gtransition.end()) {
      loop = new Star(gtransition[i]);
      gtransition.erase(i);
    }
    for (int j = i+1; j <= GSTART; j++) {
      if (gnfa_transition[j].find(i) != gnfa_transition[j].end()) {
        Expr* regex1 = gnfa_transition[j][i];
        gnfa_transition[j].erase(i);
        GNFATrans::iterator iter = gtransition.begin();
        while (iter != gtransition.end()) {
          Expr* regex2 = (*iter).second;
          if (loop != NULL) {
            if (regex2 != NULL) {
              regex2 = new Concat(loop, regex2);
            } else {
              regex2 = loop;
            }
          }
          if (regex1 != NULL) {
            if (regex2 != NULL) {
              regex2 = new Concat(regex1, regex2);
            } else {
              regex2 = regex1;
            }
          }
          if (regex2 != NULL) {
            regex2 = regex2->Clone();
          }
          if (gnfa_transition[j].find((*iter).first) != gnfa_transition[j].end()) {
            if (gnfa_transition[j][(*iter).first] != NULL) {
              if (regex2 != NULL) {
                Expr* e = gnfa_transition[j][(*iter).first];
                Expr* f = regex2;
                if (e->stype() == Expr::kStateExpr &&
                    f->stype() == Expr::kStateExpr) {
                  e = CombineStateExpr((StateExpr*)e, (StateExpr*)f);
                } else {
                  e = new Union(e, f);
                }
                gnfa_transition[j][(*iter).first] = e;
              } else {
                gnfa_transition[j][(*iter).first] =
                    new Qmark(gnfa_transition[j][(*iter).first]);
              }
            } else {
              if (regex2 != NULL) {
                gnfa_transition[j][(*iter).first] = new Qmark(regex2);
              } else {
                gnfa_transition[j][(*iter).first] = regex2;
              }
            }
          } else {
            gnfa_transition[j][(*iter).first] = regex2;
          }
          ++iter;
        }
      }
    }
    GNFATrans::iterator iter = gtransition.begin();
    iter = gtransition.begin();
    while (iter != gtransition.end()) {
      if ((*iter).second != NULL) {
        delete (*iter).second;
      }
      ++iter;
    }
    if (loop != NULL) {
      delete loop;
    }
  }

  if(gnfa_transition[GSTART][GACCEPT] == NULL) {
    return new None();
  } else {
    return gnfa_transition[GSTART][GACCEPT];
  }
}

/*
 *         - slower -
 * Onone: NFA based matching (Thompson NFA, Cached)
 *    O0: DFA based matching
 *      ~ Xbyak(JIT library) required ~
 *    O1: JIT-ed DFA based matching
 *    O2: transition rule optimized-JIT-ed DFA based mathing
 *    O3: transition rule & dfa reduction optimized-JIT-ed DFA based mathing
 *         - faster -
 */

bool Regex::Compile(CompileFlag olevel) {
  if (olevel == Onone || olevel_ >= olevel) return true;
  if (!dfa_failure_ && !dfa_.Complete()) {
    /* try create DFA.  */
    int limit = state_exprs_.size();
    limit = limit * limit * limit;
    dfa_.Construct(expr_root_, limit);
  }
  if (dfa_failure_) {
    /* can not create DFA. (too many states) */
    return false;
  }

  if (!dfa_.Compile(olevel)) {
    olevel_ = dfa_.olevel();
  } else {
    olevel_ = olevel;
  }
  return olevel_ == olevel;
}

bool Regex::FullMatch(const std::string &string)  const {
  const unsigned char* begin = (const unsigned char *)string.c_str();
  return FullMatch(begin, begin+string.length());
}

bool Regex::FullMatch(const unsigned char *begin, const unsigned char * end) const {
  if (dfa_.Complete()) {
    return dfa_.FullMatch(begin, end);
  } else {
    return FullMatchNFA(begin, end);
  }
}

/* Thompson-NFA based matching */
bool Regex::FullMatchNFA(const unsigned char *begin, const unsigned char *end) const
{
  typedef std::vector<StateExpr*> NFA;
  std::size_t nfa_size = state_exprs_.size();
  std::vector<uint32_t> next_states_flag(nfa_size);
  NFA states, next_states;
  NFA::iterator iter;
  std::set<StateExpr*>::iterator next_iter;
  std::vector<DFA::Transition> transition_cache;
  std::map<NFA, std::size_t> dfa_cache;
  std::map<std::size_t, NFA> nfa_cache;
  std::map<NFA, std::size_t>::iterator dfa_iter;
  DFA::state_t dfa_id = 0, step = 1, dfa_state = 0, dfa_next = DFA::NONE;

  states.insert(states.begin(), expr_root_->transition().first.begin(), expr_root_->transition().first.end());
  nfa_cache[dfa_id] = states;
  dfa_cache[states] = dfa_id++;
  transition_cache.resize(dfa_id);

  for (const unsigned char *p = begin; p < end; p++, step++) {
    /* on cache transition. */
    while (p < end && (dfa_next = transition_cache[dfa_state][*p++]) != DFA::REJECT) {
      dfa_state = dfa_next;
    }
    if (dfa_next == DFA::REJECT) p--;
    states = nfa_cache[dfa_state];
    if (p >= end) break;

    /* trying to construct 1-DFA(cache missed) state. */
    for (iter = states.begin(); iter != states.end(); ++iter) {
      StateExpr *s = *iter;
      if (s->Match(*p)) {
        for (next_iter = s->transition().follow.begin();
             next_iter != s->transition().follow.end();
             ++next_iter) {
          if (next_states_flag[(*next_iter)->state_id()] != step) {
            next_states_flag[(*next_iter)->state_id()] = step;
            next_states.push_back(*next_iter);
          }
        }
      }
    }
    dfa_iter = dfa_cache.find(next_states);
    if (dfa_iter == dfa_cache.end()) {
      nfa_cache[dfa_id] = next_states;
      dfa_cache[next_states] = dfa_id++;
      transition_cache.resize(dfa_id);
    }

    /* cache state, and retry transition on cache. */
    dfa_next = dfa_cache[next_states];
    transition_cache[dfa_state][*p] = dfa_next;
    dfa_state = dfa_next;
    states.swap(next_states);
    if (states.empty()) break;
    next_states.clear();
  }

  bool match = false;
  for (iter = states.begin(); iter != states.end(); ++iter) {
    if ((*iter)->type() == Expr::kEOP) {
      match = true;
      break;
    }
  }

  return match;
}

void Regex::PrintRegex() {
  PrintRegexVisitor::Print(expr_root_);
}

void Regex::PrintParseTree() const {
  PrintParseTreeVisitor::Print(expr_root_);
}

void Regex::DumpExprTree() const {
  DumpExprVisitor::Dump(expr_root_);
}

} // namespace regen
