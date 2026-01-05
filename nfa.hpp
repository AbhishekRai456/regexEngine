#ifndef NFA_HPP
#define NFA_HPP
#include "std.hpp"
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
    int c = 0;                  // Literal char value
    int save_id = -1;           // For SAVE states (group_id * 2 or 2+1)
    std::vector<CharRange> ranges;
    bool negated = false;

    State* out = nullptr;       // Primary transition
    State* out1 = nullptr;      // Secondary transition (only for SPLIT)

    // Help the matcher avoid infinite loops/duplicate processing
    int last_list = -1;

    State(StateType t) : type(t) {}
};

struct Frag {
    State* start;
    std::vector<State**> out_ptrs; 

    // Constructor for single-exit fragments (like a literal 'a')
    Frag(State* s) : start(s) {
        out_ptrs.push_back(&s->out);
    }

    // Constructor for multi-exit fragments (like alternation or star)
    Frag(State* s, std::vector<State**> out) : start(s), out_ptrs(out) {}

    /**
     * Patches (connects) all dangling arrows in this fragment 
     * to the next state 's'.
     */
    void patch(State* s) {
    for (auto& ptr : out_ptrs) {
        if (ptr && !*ptr) { // Only patch if the pointer exists and is currently null
            *ptr = s;
        }
    }
}
};

#endif  // NFA_HPP