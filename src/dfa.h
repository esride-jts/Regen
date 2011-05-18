#ifndef REGEN_DFA_H_
#define  REGEN_DFA_H_
#include "util.h"

namespace regen {

class DFA {
public:
  typedef enum State {
    REJECT = -1,
    ACCEPT = -2
  } State;
  
  DFA() {}
  ~DFA();

  std::size_t size() { return transition_.size(); }
  std::size_t start_state() { return 0; }
  std::size_t max_length() { return max_length_; }
  std::size_t min_length() { return min_length_; }
  std::size_t must_max_length() { return must_max_length_; }

  const std::string& must_max_word() { return must_max_word_; }
  void set_max_length(std::size_t len) { max_length_ = len; }
  void set_min_length(std::size_t len) { min_length_ = len; }
  void set_must_max_length(std::size_t len) { must_max_length_ = len; }
  void set_must_max_word(const std::string& string) { must_max_word_ = string; }
  void set_transition(std::vector<int>*tbl, bool accept, int default_state);
  bool IsMatched(const std::string &str) { IsMatched((unsigned char*)str.c_str(), (unsigned char *)str.c_str()+str.length()); }
  bool IsMatched(const unsigned char *str, const unsigned char *end);
  const std::vector<int> &GetTransition(std::size_t state) { return *transition_[state]; };
  int GetDefaultNext(std::size_t state) { return defaults_[state]; }
  bool IsAcceptState(std::size_t state) { return accepts_[state]; }
  
 private:
  std::deque<std::vector<int>*> transition_;
  std::deque<int> defaults_;
  std::deque<bool> accepts_;
  std::size_t max_length_;
  std::size_t min_length_;
  std::size_t must_max_length_;
  std::string must_max_word_;
};

} // namespace regen
#endif // REGEN_DFA_H_
