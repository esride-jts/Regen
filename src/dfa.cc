#include "dfa.h"

namespace regen{

DFA::Transition& DFA::get_new_transition() {
  transition_.resize(transition_.size()+1);
  return transition_.back();
}

void DFA::set_state_info(bool accept, state_t default_next, std::set<state_t> &dst_state)
{
  accepts_.push_back(accept);
  defaults_.push_back(default_next);
  dst_states_.push_back(dst_state);
  std::set<state_t>::iterator iter = dst_state.begin();
  while (iter != dst_state.end()) {
    if (*iter == DFA::REJECT) {
      ++iter;
      continue;
    }
    if (src_states_.size() <= static_cast<std::size_t>(*iter)) {
      src_states_.resize(*iter+1);
    }
    src_states_[*iter].insert(dst_states_.size()-1);
    ++iter;
  }
}

void DFA::state2label(state_t state, char* labelbuf) const
{
  if (state == DFA::REJECT) {
    strcpy(labelbuf, "reject");
  } else {
    assert(0 <= state && state <= (state_t)size());
    sprintf(labelbuf, "s%d", state);
  }
}

void
DFA::Minimize()
{
  if (minimum_) return;
  
  std::vector<std::vector<bool> > distinction_table;
  distinction_table.resize(size()-1);
  for (state_t i = 0; i < size()-1; i++) {
    distinction_table[i].resize(size()-i-1);
    for (state_t j = i+1; j < size(); j++) {
      distinction_table[i][size()-j-1] = accepts_[i] != accepts_[j];
    }
  }

  bool distinction_flag = true;
  while (distinction_flag) {
    distinction_flag = false;
    for (state_t i = 0; i < size()-1; i++) {
      for (state_t j = i+1; j < size(); j++) {
        if (!distinction_table[i][size()-j-1]) {
          for (std::size_t input = 0; input < 256; input++) {
            state_t n1, n2;
            n1 = transition_[i][input];
            n2 = transition_[j][input];
            if (n1 != n2) {
              if (n1 > n2) std::swap(n1, n2);
              if ((n1 == REJECT || n2 == REJECT) ||
                  distinction_table[n1][size()-n2-1]) {
                distinction_flag = true;
                distinction_table[i][size()-j-1] = true;
                break;
              }
            }
          }
        }
      }
    }
  }
  
  std::map<state_t, state_t> swap_map;
  for (state_t i = 0; i < size()-1; i++) {
    for (state_t j = i+1; j < size(); j++) {
      if (swap_map.find(j) == swap_map.end()) {
        if (!distinction_table[i][size()-j-1]) {
          swap_map[j] = i;
        }
      }
    }
  }

  if (swap_map.empty()) {
    minimum_ = true;
    return;
  }
  
  size_t minimum_size = size() - swap_map.size();
  std::vector<state_t> replace_map(size());
  for (state_t s = 0, d = 0; s < size(); s++) {
    if (swap_map.find(s) == swap_map.end()) {
      replace_map[s] = d++;
      if (s != replace_map[s]) {
        transition_[replace_map[s]] = transition_[s];
        accepts_[replace_map[s]] = accepts_[s];
        defaults_[replace_map[s]] = defaults_[s];
        dst_states_[replace_map[s]] = dst_states_[s];
        src_states_[replace_map[s]] = src_states_[s];
      }
    } else {
      replace_map[s] = replace_map[swap_map[s]];
    }
  }

  std::set<state_t>::iterator iter;
  std::set<state_t> tmp_set;
  for (state_t s = 0; s < minimum_size; s++) {
    for (std::size_t input = 0; input < 256; input++) {
      state_t n = transition_[s][input];
      if (n != REJECT) {
        transition_[s][input] = replace_map[n];
      }
    }
    if (defaults_[s] != REJECT) defaults_[s] = replace_map[defaults_[s]];
    tmp_set.clear();
    for (iter = dst_states_[s].begin(); iter != dst_states_[s].end(); ++iter) {
      if (*iter != REJECT) tmp_set.insert(replace_map[*iter]);
      else tmp_set.insert(REJECT);
    }
    dst_states_[s] = tmp_set;
    tmp_set.clear();
    for (iter = src_states_[s].begin(); iter != src_states_[s].end(); ++iter) {
      tmp_set.insert(replace_map[*iter]);
    }
    src_states_[s] = tmp_set;
  }

  transition_.resize(minimum_size);
  accepts_.resize(minimum_size);
  defaults_.resize(minimum_size);
  dst_states_.resize(minimum_size);
  src_states_.resize(minimum_size);

  minimum_ = true;
  
  return;
}

void
DFA::Complement()
{
  state_t reject = DFA::REJECT;
  std::size_t final = size();
  for (std::size_t state = 0; state < final; state++) {
    accepts_[state] = !accepts_[state];
    bool to_reject = false;
    for (std::size_t i = 0; i < 256; i++) {
      if (transition_[state][i] == DFA::REJECT) {
        if (reject == DFA::REJECT) {
          reject = transition_.size();
          transition_.push_back(Transition(reject));
          std::set<state_t> dst_states;
          dst_states.insert(reject);
          set_state_info(true, reject, dst_states);
        }
        to_reject = true;
        transition_[state][i] = reject;
      }
    }
    if (defaults_[state] == DFA::REJECT) {
      defaults_[state] = reject;
    }
    if (to_reject) {
      dst_states_[state].insert(reject);
      src_states_[reject].insert(state);
    }
  }
}

#if REGEN_ENABLE_XBYAK
class XbyakCompiler: public Xbyak::CodeGenerator {
 public:
  XbyakCompiler(const DFA &dfa, std::size_t state_code_size);
 private:
  std::size_t code_segment_size_;
  static std::size_t code_segment_size(std::size_t state_num) {
    const std::size_t setup_code_size_ = 16;
    const std::size_t state_code_size_ = 64;
    const std::size_t segment_align = 4096;    
    return (state_num*state_code_size_ + setup_code_size_)
        +  ((state_num*state_code_size_ + setup_code_size_) % segment_align);
  }
  static std::size_t data_segment_size(std::size_t state_num) {
    return state_num * 256 * sizeof(void *);
  }
};

XbyakCompiler::XbyakCompiler(const DFA &dfa, std::size_t state_code_size = 64):
    /* code segment for state transition.
     *   each states code was 16byte alligned.
     *                        ~~
     *   padding for 4kb allign between code and data
     *                        ~~
     * data segment for transition table
     *                                                */
    CodeGenerator(code_segment_size(dfa.size()) + data_segment_size(dfa.size())),
    code_segment_size_(code_segment_size(dfa.size()))
{
  std::vector<const uint8_t*> states_addr(dfa.size());

  const uint8_t* code_addr_top = getCurr();
  const uint8_t** transition_table_ptr = (const uint8_t **)(code_addr_top + code_segment_size_);

#ifdef XBYAK32
#error "64 only"
#elif defined(XBYAK64_WIN)
  const Xbyak::Reg64 arg1(rcx);
  const Xbyak::Reg64 arg2(rdx);
  const Xbyak::Reg64 tbl (r8);
  const Xbyak::Reg64 tmp1(r10);
  const Xbyak::Reg64 tmp2(r11);  
#else
  const Xbyak::Reg64 arg1(rdi);
  const Xbyak::Reg64 arg2(rsi);
  const Xbyak::Reg64 tbl (rdx);
  const Xbyak::Reg64 tmp1(r10);
  const Xbyak::Reg64 tmp2(r11);
#endif

  // setup enviroment on register
  mov(tbl,  (uint64_t)transition_table_ptr);
  jmp("s0");

  L("reject");
  const uint8_t *reject_state_addr = getCurr();
  mov(rax, -1); // return false
  ret();

  align(16);

  // state code generation, and indexing every states address.
  char labelbuf[100];
  for (std::size_t i = 0; i < dfa.size(); i++) {
    dfa.state2label(i, labelbuf);
    L(labelbuf);
    states_addr[i] = getCurr();
    // can transition without table lookup ?
    
    const DFA::AlterTrans &at = dfa.GetAlterTrans(i);
    if (dfa.olevel() >= O2 && at.next1 != DFA::NONE) {
      std::size_t state = i;
      std::size_t inline_level = dfa.olevel() == O3 ? dfa.inline_level(i) : 0;
      bool inlining = inline_level != 0;
      std::size_t transition_depth = -1;
      inLocalLabel();
      if (inlining) {
        lea(tmp1, ptr[arg1+inline_level]);
        cmp(tmp1, arg2);
        jge(".ret", T_NEAR);
      } else {
        cmp(arg1, arg2);
        je(".ret", T_NEAR);        
      }
   emit_transition:
      const DFA::AlterTrans &at = dfa.GetAlterTrans(state);
      bool jn_flag = false;
      transition_depth++;
      assert(at.next1 != DFA::NONE);
      dfa.state2label(at.next1, labelbuf);
      if (at.next1 != DFA::REJECT) {
        jn_flag = true;  
      }
      if (at.next2 == DFA::NONE) {
        if (!inlining) {
          inc(arg1);
          jmp(labelbuf, T_NEAR);
        } else if (transition_depth == inline_level) {
          add(arg1, transition_depth);
          jmp(labelbuf, T_NEAR);
        } else {
          state = at.next1;
          goto emit_transition;
        }
      } else {
        if (!inlining) {
          movzx(tmp1, byte[arg1]);
          inc(arg1);
        } else if (transition_depth == inline_level) {
          movzx(tmp1, byte[arg1+transition_depth]);
          add(arg1, transition_depth+1);
        } else {
          movzx(tmp1, byte[arg1+transition_depth]);
        }
        if (at.key.first == at.key.second) {
          cmp(tmp1, at.key.first);
          if (transition_depth == inline_level || !jn_flag) {
            je(labelbuf, T_NEAR);
          } else {
            jne("reject", T_NEAR);
          }
        } else {
          sub(tmp1, at.key.first);
          cmp(tmp1, at.key.second-at.key.first+1);
          if (transition_depth == inline_level || !jn_flag) {
            jc(labelbuf, T_NEAR);
          } else {
            jnc("reject", T_NEAR);
          }
        }
        dfa.state2label(at.next2, labelbuf);
        if (transition_depth == inline_level) {
          jmp(labelbuf, T_NEAR);
        } else {
          if (jn_flag) {
            state = at.next1;
            goto emit_transition;
          } else {
            state = at.next2;
            goto emit_transition;
          }
        }
      }
      if (inlining) {
        L(".ret");
        cmp(arg1, arg2);
        je("@f", T_NEAR);
        movzx(tmp1, byte[arg1]);
        inc(arg1);
        jmp(ptr[tbl+i*256*8+tmp1*8]);
        L("@@");
        mov(rax, i);
        ret();
      } else {
        L(".ret");
        mov(rax, i);
        ret();
      }
      outLocalLabel();
    } else {
      cmp(arg1, arg2);
      je("@f");
      movzx(tmp1, byte[arg1]);
      inc(arg1);
      jmp(ptr[tbl+i*256*8+tmp1*8]);
      L("@@");
      mov(rax, i);
      ret();
    }
    align(16);
  }

  // backpatching (each states address)
  for (std::size_t i = 0; i < dfa.size(); i++) {
    const DFA::Transition &trans = dfa.GetTransition(i);
    for (int c = 0; c < 256; c++) {
      DFA::state_t next = trans[c];
      transition_table_ptr[i*256+c] =
          (next  == DFA::REJECT ? reject_state_addr : states_addr[next]);
    }
  }
}

bool DFA::EliminateBranch()
{
  alter_trans_.resize(size());
  for (std::size_t state = 0; state < size(); state++) {
    const Transition &trans = transition_[state];
    state_t next1 = trans[0], next2 = DFA::NONE;
    unsigned int begin = 0, end = 256;
    int c;
    for (c = 1; c < 256 && next1 == trans[c]; c++);
    if (c < 256) {
      next2 = next1;
      next1 = trans[c];
      begin = c;
      for (++c; c < 256 && next1 == trans[c]; c++);
    }
    if (c < 256) {
      end = --c;
      for (++c; c < 256 && next2 == trans[c]; c++);
    }
    if (c < 256) {
      next1 = next2 = DFA::NONE;
    }
    AlterTrans &at = alter_trans_[state];
    at.next1 = next1;
    at.next2 = next2;
    at.key.first = begin;
    at.key.second = end;
  }

  return true;
}

bool DFA::Reduce()
{
  inline_level_.resize(size());
  src_states_[0].insert(DFA::NONE);
  std::vector<bool> inlined(size());
  for (std::size_t state = 0; state < size(); state++) {
    // Pick inlining region (make degenerate graph).
    if (inlined[state]) continue;
    state_t current = state, next;
    for(;;) {
      if (dst_states_[current].size() > 2 ||
          dst_states_[current].size() == 0) break;
      if (dst_states_[current].size() == 2 &&
          dst_states_[current].count(DFA::REJECT) == 0) break;
      if (dst_states_[current].size() == 1 &&
          dst_states_[current].count(DFA::REJECT) == 1) break;
      next = *(dst_states_[current].lower_bound(0));
      if (alter_trans_[next].next1 == DFA::NONE) break;
      if (src_states_[next].size() != 1 ||
          accepts_[next]) break;
      if (inlined[next]) break;
      inlined[next] = true;
      current = next;

      inline_level_[state]++;
    }
  }
  src_states_[0].erase(DFA::NONE);

  return true;
}

bool DFA::Compile(Optimize olevel)
{
  if (olevel <= olevel_) return true;
  if (olevel >= O2) {
    if (EliminateBranch()) {
      olevel_ = O2;
    }
    if (olevel == O3 && Reduce()) {
      olevel_ = O3;
    }
  }
  xgen_ = new XbyakCompiler(*this);
  CompiledFullMatch = (state_t (*)(const unsigned char *, const unsigned char *))xgen_->getCode();
  if (olevel_ < O1) olevel_ = O1;
  return olevel == olevel_;
}
#else
bool DFA::EliminateBranch() { return false; }
bool DFA::Reduce() { return false; }
bool DFA::Compile(Optimize) { return false; }
#endif

bool DFA::FullMatch(const unsigned char *str, const unsigned char *end) const
{
  state_t state = 0;

  if (olevel_ >= O1) {
    state = CompiledFullMatch(str, end);
    return state != DFA::REJECT ? accepts_[state] : false;
  }

  while (str < end && (state = transition_[state][*str++]) != DFA::REJECT);

  return state != DFA::REJECT ? accepts_[state] : false;
}

} // namespace regen
