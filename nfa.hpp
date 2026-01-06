#ifndef NFA_HPP
#define NFA_HPP
#include "tokenizer.hpp"

enum class StateType{
    CHAR,
    DOT,
    CHAR_CLASS,
    MATCH,
    SPLIT,
    SAVE,
    ANCHOR_START,
    ANCHOR_END
};

struct State {
    StateType type;
    char c;
    // Valid only when type == StateType::CHAR; value is unspecified otherwise.

    int save_id = -1;
    // For capture groups: store input positions
    // even = group start, odd = group end

    // when type == StateType::CHAR_CLASS
    std::vector<CharRange> ranges;
    bool negated = false;

    State* out = nullptr;       // transition1
    State* out1 = nullptr;      // optional transition2 (only for SPLIT)

    int last_list = -1; 
    // Marks whether this state has already been added to the current active-states list,
    // preventing duplicate entries and infinite ε-transition loops

    State(StateType t) : type(t) {}
};

// Frag represents a start state and a list of "dangling exits" of an NFA fragment
struct Frag {
    State* start;
    std::vector<State**> out_ptrs;  // stores the address of the pointers which are dangling

    // Constructor for single-exit fragments (like a literal 'a')
    Frag(State* s) : start(s) {
        out_ptrs.push_back(&s->out);
    }

    // Constructor for multi-exit fragments (like alternation or star)
    Frag(State* s, std::vector<State**> out) : start(s), out_ptrs(out) {}

    // Patch (connect) dangling arrows in this fragment
    void patch(State* s) {
        for (auto& ptr : out_ptrs) {
            if (ptr && !*ptr) { // Only patch if the pointer exists and is currently null
                *ptr = s;
            }
        }
    }
};

#endif  // NFA_HPP