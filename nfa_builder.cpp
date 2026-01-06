#include "nfa_builder.hpp"

// Creates a new State object of the given type, stores it in the state pool,
// and returns a raw pointer to the newly created state
State *NfaBuilder::create_state(StateType type){
    state_pool.push_back(std::make_unique<State>(type));
    return state_pool.back().get();
}

// Deep copy a fargment's NFA
// Returns a new Frag with the copied start state and the copied exits
Frag NfaBuilder::copy_fragment(Frag original){
    std::unordered_map<State *, State *> old_to_new;
    State *new_start = copy_state(original.start, old_to_new);

    std::vector<State **> new_exits;
    // Traverse the newly copied states to store adresses of dangling pointers in the new fragment
    std::unordered_set<State *> visited;  // Remember which states have been already visited
    // Depth first traversal of the graph:
    std::stack<State *> s;
    s.push(new_start);
    while (!s.empty())  // Loop until there are no more states left to process
    {
        State *curr = s.top();
        s.pop();
        if (!curr || visited.count(curr)) continue;
        visited.insert(curr);
        // If out is null, it's a dangling exit we need to patch later
        if (!curr->out && curr->type != StateType::MATCH)
        {
            new_exits.push_back(&curr->out);
        }
        // If out1 is null (and it's a SPLIT state), it's also an exit
        if (!curr->out1 && curr->type == StateType::SPLIT)
        {
            new_exits.push_back(&curr->out1);
        }

        if (curr->out) s.push(curr->out);
        if (curr->out1) s.push(curr->out1);
    }

    return Frag(new_start, new_exits);
}

// Deep copy a NFA subgraph starting from state 's'
// Creates new State objects for all reachable states (except MATCH)
// Returns a pointer to the copied version of 's'
State *NfaBuilder::copy_state(State *s, std::unordered_map<State *, State *> &lookup){
    // 'lookup' stores the states which are already 
    // copied. map: key = old_state, value = new_state (copy of the old_state)

    if (!s || s->type == StateType::MATCH) return s;    // If state is null or is the final MATCH state, return it as is
    if (lookup.count(s)) return lookup[s];  // If this state was already copied, return the existing copy

    // Create a new state with the same type and copy fields
    State *result = create_state(s->type);
    result->c = s->c;
    result->ranges = s->ranges;
    result->negated = s->negated;
    result->save_id = s->save_id;
    lookup[s] = result; // Remember that this original state is now copied

    // Recursively copy outgoing transitions
    result->out = copy_state(s->out, lookup);
    result->out1 = copy_state(s->out1, lookup);
    return result;
}

// Builds an NFA from a tokenized postfix regex pattern.
// Iterates the postfix tokens, pushed and combines NFA fragments on a stack
// according to each operator, and finally connects all remaining dangling 
// exits to a single MATCH state.
// Returns a pointer to the start state of the constructed NFA, or nullptr if
// the input is empty.
State *NfaBuilder::build(const std::vector<Token> &postfix){
    if (postfix.empty()) return nullptr;

    std::stack<Frag> stack;

    for (const auto &t : postfix){
        switch (t.type){
        case TokenType::LITERAL:
        {
            State *s = create_state(StateType::CHAR);
            s->c = t.literal;
            stack.push(Frag(s));
            break;
        }
        case TokenType::DOT:
        {
            stack.push(Frag(create_state(StateType::DOT)));
            break;
        }
        case TokenType::CHAR_CLASS:
        {
            State *s = create_state(StateType::CHAR_CLASS);
            s->ranges = t.ranges;
            s->negated = t.negated;
            stack.push(Frag(s));
            break;
        }
        case TokenType::CARET:
        {
            stack.push(Frag(create_state(StateType::ANCHOR_START)));
            break;
        }
        case TokenType::DOLLAR:
        {
            stack.push(Frag(create_state(StateType::ANCHOR_END)));
            break;
        }
        case TokenType::LPAREN:
        {
            State *s = create_state(StateType::SAVE);
            s->save_id = t.group_id * 2; // Start register (even)
            stack.push(Frag(s));
            break;
        }
        case TokenType::RPAREN:
        {
            State *s = create_state(StateType::SAVE);
            s->save_id = t.group_id * 2 + 1; // End register (odd)
            stack.push(Frag(s));
            break;
        }
        case TokenType::CONCAT:
        {
            Frag e2 = stack.top();
            stack.pop();
            Frag e1 = stack.top();
            stack.pop();
            e1.patch(e2.start);
            stack.push(Frag(e1.start, e2.out_ptrs));
            break;
        }
        case TokenType::ALTERNATION:
        {
            Frag e2 = stack.top();
            stack.pop();
            Frag e1 = stack.top();
            stack.pop();
            State *s = create_state(StateType::SPLIT);
            s->out = e1.start;
            s->out1 = e2.start;
            // Combine dangling exits from both branches
            std::vector<State **> combined = e1.out_ptrs;
            combined.insert(combined.end(), e2.out_ptrs.begin(), e2.out_ptrs.end());
            stack.push(Frag(s, combined));
            break;
        }
        case TokenType::STAR:
        {
            Frag e = stack.top();
            stack.pop();
            State *s = create_state(StateType::SPLIT);
            s->out = e.start;                // Loop back into the expression
            e.patch(s);                      // The expression's end loops back to the split
            stack.push(Frag(s, {&s->out1})); // out1 is the escape route
            break;
        }
        case TokenType::PLUS:
        {
            Frag e = stack.top();
            stack.pop();
            State *s = create_state(StateType::SPLIT);
            s->out = e.start; // Loop back
            e.patch(s);       // Connect expression end to split
            stack.push(Frag(e.start, {&s->out1}));
            break;
        }
        case TokenType::QUESTION:
        {
            Frag e = stack.top();
            stack.pop();
            State *s = create_state(StateType::SPLIT);
            s->out = e.start; // Option 1: match the expression
            // Option 2: skip the expression (out1)
            std::vector<State **> exits = e.out_ptrs;
            exits.push_back(&s->out1);
            stack.push(Frag(s, exits));
            break;
        }
        case TokenType::QUANTIFIER_RANGE:
        {
            Frag e = stack.top();
            stack.pop();

            // 1. Handle the mandatory part (m)
            // Initialize 'mandatory' with an immediately-invoked lambda (no valid default state).
            Frag mandatory = [&](){
                if (t.min == 0){
                    State *eps = create_state(StateType::SPLIT);
                    return Frag(eps, {&eps->out});
                }else{
                    return e; // Use the first one as the base
                }
            }();
            // If min > 1, append the necessary copies
            for (int i = 1; i < t.min; i++){
                Frag next_copy = copy_fragment(e);
                mandatory.patch(next_copy.start);
                mandatory = Frag(mandatory.start, next_copy.out_ptrs);
            }

            // 2. Handle the optional part (n - m) or infinite (m, )
            if (t.max == -1){ // Case {m,}
                State *s = create_state(StateType::SPLIT);
                Frag loop_part = copy_fragment(e);

                s->out = loop_part.start;
                loop_part.patch(s);

                mandatory.patch(s);
                stack.push(Frag(mandatory.start, {&s->out1}));
            }else if (t.max > t.min){ // Case {m,n}
                // Build a chain of optional fragments, each one guarded by a SPLIT that can either
                // take the repetition or skip it and move on
                Frag optional_chain = mandatory;
                std::vector<State **> all_exits;

                for (int i = 0; i < (t.max - t.min); i++){
                    Frag next_opt = copy_fragment(e);
                    State *s = create_state(StateType::SPLIT);

                    s->out = next_opt.start;
                    optional_chain.patch(s);

                    // Collect exits from the skip path
                    all_exits.push_back(&s->out1);

                    optional_chain = Frag(next_opt.start, next_opt.out_ptrs);
                }
                // Add exits from the last repetition: if all optional parts are taken,
                // the match can continue after the final copied fragment.
                all_exits.insert(all_exits.end(), optional_chain.out_ptrs.begin(), optional_chain.out_ptrs.end());
                stack.push(Frag(mandatory.start, all_exits));
            }else{  // Case {m} (exactly m)
                stack.push(mandatory);
            }
            break;
        }
        default:
            break;
        }
    }

    // If no fragments were built (empty regex) return nullptr (no NFA)
    if (stack.empty()) return nullptr;

    // After processing all tokens, ideally there should be exactly one fragment
    // If more than one fragments remain, they are implicitly concatenated
    while (stack.size() > 1)
    {
        Frag e2 = stack.top();
        stack.pop();
        Frag e1 = stack.top();
        stack.pop();
        e1.patch(e2.start);
        stack.push(Frag(e1.start, e2.out_ptrs));
    }

    // Finalize the NFA by patching all dangling exits to a MATCH state
    Frag final_frag = stack.top();
    stack.pop();
    State *match_state = create_state(StateType::MATCH);
    final_frag.patch(match_state);

    return final_frag.start;
}

// Time Complexity Analysis:

// T = number of postfix tokens
// S = total number of NFA states created, including expansions caused by
// operators like '*', '+', '?', and '{m, n}'.

// create_state():
// O(1) per call → total O(S)

// copy_state(s):
// Copies each reachable state once → O(k) (k is the number of states reachable from s)

// copy_fragment(f):
// O(k) (k = number of states in the fragment)

// build() function;
// Total Average TC = O(T + S) (assuming good hash behavior of std::unordered_map and std::unordered_set)
// The builder therefore runs in time linear in the size of the constructed NFA