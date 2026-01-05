#ifndef NFA_BUILDER_HPP
#define NFA_BUILDER_HPP
#include "nfa.hpp"
#include "postfix.hpp"

class NfaBuilder {
public:
    // Takes the postfix vector and returns the starting State of the NFA graph.
    // The graph ends at a State with type StateType::MATCH.
    State* build(const std::vector<Token>& postfix);
    Frag copy_fragment(Frag);
    State* copy_state(State*, std::map<State*, State*>&);
private:
    // Helper to create and manage state allocation
    State* create_state(StateType type);
};

class NfaDebugger {
public:
    static void print_graph(State* start) {
        std::set<State*> visited;
        std::cout << "\n--- NFA Graph Visualization ---\n";
        print_state(start, visited);
        std::cout << "-------------------------------\n";
    }

private:
    static void print_state(State* s, std::set<State*>& visited) {
        if (!s || visited.count(s)) return;
        visited.insert(s);

        // Print current state info
        std::cout << "State [" << s << "] ";
        std::cout << std::left << std::setw(15) << type_to_string(s->type);

        if (s->type == StateType::CHAR) {
            std::cout << "char: '" << (char)s->c << "' ";
        } else if (s->type == StateType::SAVE) {
            std::cout << "reg: " << s->save_id << " (" 
                      << (s->save_id % 2 == 0 ? "Start" : "End") << ") ";
        } else if (s->type == StateType::CHAR_CLASS) {
            std::cout << "ranges: ";
            for(auto& r : s->ranges) std::cout << r.lo << "-" << r.hi << " ";
        }

        // Print transitions
        if (s->out) std::cout << " -> [" << s->out << "]";
        if (s->out1) std::cout << " | -> [" << s->out1 << "]";
        
        std::cout << "\n";

        // Recursively visit neighbors
        print_state(s->out, visited);
        print_state(s->out1, visited);
    }

    static std::string type_to_string(StateType t) {
        switch (t) {
            case StateType::CHAR:         return "CHAR";
            case StateType::DOT:          return "DOT";
            case StateType::CHAR_CLASS:   return "CHAR_CLASS";
            case StateType::MATCH:        return "MATCH (ACCEPT)";
            case StateType::SPLIT:        return "SPLIT (EPS)";
            case StateType::SAVE:         return "SAVE (REG)";
            case StateType::ANCHOR_START: return "ANCHOR ^";
            case StateType::ANCHOR_END:   return "ANCHOR $";
            default:                      return "UNKNOWN";
        }
    }
};
#endif  // NFA_BUILDER_HPP