#include "nfa_builder.hpp"

State* NfaBuilder::create_state(StateType type) {
    return new State(type);
}

Frag NfaBuilder::copy_fragment(Frag original) {
    std::map<State*, State*> old_to_new;
    State* new_start = copy_state(original.start, old_to_new);
    
    std::vector<State**> new_exits;
    // We traverse the NEWly copied states to find where the paths end (nullptr)
    std::set<State*> visited;
    std::stack<State*> s;
    s.push(new_start);
    
    while(!s.empty()){
        State* curr = s.top(); s.pop();
        if(!curr || visited.count(curr)) continue;
        visited.insert(curr);
        
        // If out is null, it's a dangling exit we need to patch later
        if (!curr->out && curr->type != StateType::MATCH) {
            new_exits.push_back(&curr->out);
        }
        // If out1 is null (and it's a SPLIT state), it's also an exit
        if (!curr->out1 && curr->type == StateType::SPLIT) {
            new_exits.push_back(&curr->out1);
        }
        
        if(curr->out) s.push(curr->out);
        if(curr->out1) s.push(curr->out1);
    }
    
    return Frag(new_start, new_exits);
}

State* NfaBuilder::copy_state(State* s, std::map<State*, State*>& lookup) {
    if (!s || s->type == StateType::MATCH) return s;
    if (lookup.count(s)) return lookup[s];

    State* res = create_state(s->type);
    res->c = s->c;
    res->ranges = s->ranges;
    res->negated = s->negated;
    res->save_id = s->save_id;
    lookup[s] = res;

    res->out = copy_state(s->out, lookup);
    res->out1 = copy_state(s->out1, lookup);
    return res;
}

State* NfaBuilder::build(const std::vector<Token>& postfix) {
    if (postfix.empty()) return nullptr;

    std::stack<Frag> stack;

    for (const auto& t : postfix) {
        switch (t.type) {
            case TokenType::LITERAL: {
                State* s = create_state(StateType::CHAR);
                s->c = t.literal;
                stack.push(Frag(s));
                break;
            }
            case TokenType::DOT: {
                stack.push(Frag(create_state(StateType::DOT)));
                break;
            }
            case TokenType::CHAR_CLASS: {
                State* s = create_state(StateType::CHAR_CLASS);
                s->ranges = t.ranges;
                s->negated = t.negated;
                stack.push(Frag(s));
                break;
            }
            case TokenType::CARET: {
                stack.push(Frag(create_state(StateType::ANCHOR_START)));
                break;
            }
            case TokenType::DOLLAR: {
                stack.push(Frag(create_state(StateType::ANCHOR_END)));
                break;
            }
            case TokenType::LPAREN: {
                State* s = create_state(StateType::SAVE);
                s->save_id = t.group_id * 2; // Start register (even)
                stack.push(Frag(s));
                break;
            }
            case TokenType::RPAREN: {
                State* s = create_state(StateType::SAVE);
                s->save_id = t.group_id * 2 + 1; // End register (odd)
                stack.push(Frag(s));
                break;
            }
            case TokenType::CONCAT: {
                // Pop in reverse order: right operand, then left
                Frag e2 = stack.top(); stack.pop();
                Frag e1 = stack.top(); stack.pop();
                e1.patch(e2.start);
                stack.push(Frag(e1.start, e2.out_ptrs));
                break;
            }
            case TokenType::ALTERNATION: {
                Frag e2 = stack.top(); stack.pop();
                Frag e1 = stack.top(); stack.pop();
                State* s = create_state(StateType::SPLIT);
                s->out = e1.start;
                s->out1 = e2.start;
                // Combine dangling exits from both branches
                std::vector<State**> combined = e1.out_ptrs;
                combined.insert(combined.end(), e2.out_ptrs.begin(), e2.out_ptrs.end());
                stack.push(Frag(s, combined));
                break;
            }
            case TokenType::STAR: {
                Frag e = stack.top(); stack.pop();
                State* s = create_state(StateType::SPLIT);
                s->out = e.start; // Loop back into the expression
                e.patch(s);       // The expression's end loops back to the split
                stack.push(Frag(s, {&s->out1})); // out1 is the escape route
                break;
            }
            case TokenType::PLUS: {
                Frag e = stack.top(); stack.pop();
                State* s = create_state(StateType::SPLIT);
                s->out = e.start; // Loop back
                e.patch(s);       // Connect expression end to split
                stack.push(Frag(e.start, {&s->out1}));
                break;
            }
            case TokenType::QUESTION: {
                Frag e = stack.top(); stack.pop();
                State* s = create_state(StateType::SPLIT);
                s->out = e.start; // Option 1: match the expression
                // Option 2: skip the expression (out1)
                std::vector<State**> exits = e.out_ptrs;
                exits.push_back(&s->out1);
                stack.push(Frag(s, exits));
                break;
            }
            case TokenType::QUANTIFIER_RANGE: {
                Frag e = stack.top(); stack.pop();
                
                // --- Part 1: Mandatory Repetitions (m) ---
                // We start with a fragment representing the first 'm' matches
                Frag mandatory = (t.min == 0) ? Frag(create_state(StateType::SPLIT)) : e;
                
                // If min > 1, we need to append copies of 'e'
                // Note: In a real engine, you'd call a deep_copy(e) here
                for (int i = 1; i < t.min; ++i) {
                    Frag next_copy = copy_fragment(e); 
                    mandatory.patch(next_copy.start);
                    mandatory = Frag(mandatory.start, next_copy.out_ptrs);
                }

                // --- Part 2: Optional Repetitions (n - m) ---
                if (t.max == -1) { 
                    // Case: {m,} -> Mandatory followed by a STAR
                    State* s = create_state(StateType::SPLIT);
                    Frag loop_part = (t.min == 0) ? e : copy_fragment(e);
                    
                    s->out = loop_part.start;
                    loop_part.patch(s);
                    
                    mandatory.patch(s);
                    stack.push(Frag(mandatory.start, {&s->out1}));
                } 
                else if (t.max > t.min) {
                    Frag optional_chain = mandatory;
                    // We need to keep track of ALL ways to exit the optional section
                    std::vector<State**> all_optional_exits = mandatory.out_ptrs;

                    for (int i = 0; i < (t.max - t.min); ++i) {
                        Frag next_opt = copy_fragment(e);
                        State* s = create_state(StateType::SPLIT);
                        
                        s->out = next_opt.start;
                        // s->out1 is the "skip" path for this repetition
                        
                        optional_chain.patch(s);
                        
                        // The new exits are the end of this copy AND the skip path we just made
                        all_optional_exits.insert(all_optional_exits.end(), next_opt.out_ptrs.begin(), next_opt.out_ptrs.end());
                        all_optional_exits.push_back(&s->out1);
                        
                        optional_chain = Frag(next_opt.start, next_opt.out_ptrs); 
                    }
                    stack.push(Frag(mandatory.start, all_optional_exits));
                }
                else {
                    // Case: {m,m} -> Exactly m times, already handled by mandatory
                    stack.push(mandatory);
                }
                break;
            }
            default:
                break;
        }
    }

    if (stack.empty()) return nullptr;
    while (stack.size() > 1) {
    Frag e2 = stack.top(); stack.pop();
    Frag e1 = stack.top(); stack.pop();
    e1.patch(e2.start); // Manually link the orphaned fragments
    stack.push(Frag(e1.start, e2.out_ptrs));
    }

    // Finalize the NFA by patching all dangling exits to a MATCH state
    Frag final_frag = stack.top(); stack.pop();
    State* match_state = create_state(StateType::MATCH);
    final_frag.patch(match_state);

    return final_frag.start;
}