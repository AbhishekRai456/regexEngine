#ifndef NFA_BUILDER_HPP
#define NFA_BUILDER_HPP
#include "nfa.hpp"
#include "postfix.hpp"

class NfaBuilder
{
public:
    // Build an NFA from postfix regex and return its start state.
    // The NFA's accepting state will have type StateType::MATCH.
    State *build(const std::vector<Token> &postfix);

    Frag copy_fragment(Frag);
    State *copy_state(State *, std::unordered_map<State *, State *> &);

private:
    // Allocate a new state and keep ownership in the internal pool
    State *create_state(StateType type);

    // Owns all states created during NFA construction
    // Ensures that all State objects live as long as the NfaBuilder lives
    // When NfaBuilder is destroyed, state_pool is destroyed, and all State
    // objects are automatically deleted
    std::vector<std::unique_ptr<State>> state_pool; // Automatic cleanup (RAII)
};

// Debugging tools
class NfaPrinter {
public:
    static void print_nfa(State* start, size_t idx) {
        std::string dot_file = "nfas/nfa_" + std::to_string(idx) + ".dot";
        std::string png_file = "nfas/nfa_" + std::to_string(idx) + ".png";
        std::ofstream out(dot_file);
        if (!out) {
            std::cout << "Failed to open nfa.dot\n";
            return;
        }

        std::set<State*> visited;
        out << "digraph NFA {\n";
        out << "  rankdir=LR;\n";
        out << "  fontname=\"monospace\";\n";
        print_state(start, true, visited, out);
        out << "}\n";
        out.close();

        std::string cmd = "dot -Tpng " + dot_file + " -o " + png_file;
        int rc = std::system(cmd.c_str());
        if (rc != 0) {
            std::cout << "Graphviz rendering failed." << std::endl;
            std::cout << "If Graphviz is not installed, install it first: https://graphviz.org/download/" << std::endl;
        }
    }

private:
    static void print_state(State* s, bool isStart, std::set<State*>& visited, std::ostream& out) {
        if (!s || visited.count(s)) return;
        visited.insert(s);

        // Node
        out << "  \"" << s << "\" [label=\"" << (isStart?"(START)\\n":"") << state_label(s) << "\"";

        if(isStart) out << ", shape=doublecircle";
        else if (s->type == StateType::MATCH) out << ", shape=doublecircle color=green";
        else out << ", shape=circle";

        out << "];\n";

        // Edges
        if (s->out) {
            out << "  \"" << s << "\" -> \"" << s->out << "\"";
            std::string lbl = edge_label(s);
            if (!lbl.empty())
                out << " [label=\"" << lbl << "\"]";
            out << ";\n";
        }

        if (s->out1) {
            out << "  \"" << s << "\" -> \"" << s->out1 << "\" [label=\"ε\"];\n";
        }

        print_state(s->out, false, visited, out);
        print_state(s->out1, false, visited, out);
    }

    static std::string state_label(State* s) {
        switch (s->type) {
            case StateType::CHAR:
                return "CHAR";
            case StateType::DOT:
                return "DOT (.)";
            case StateType::CHAR_CLASS:
                return "CHAR_CLASS";
            case StateType::SPLIT:
                return "SPLIT";
            case StateType::MATCH:
                return "MATCH";
            case StateType::SAVE:
                return "SAVE " + std::to_string(s->save_id) +
                       (s->save_id % 2 == 0 ? " (start)" : " (end)");
            case StateType::ANCHOR_START:
                return "ANCHOR ^";
            case StateType::ANCHOR_END:
                return "ANCHOR $";
            default:
                return "UNKNOWN";
        }
    }

    static std::string edge_label(State* s) {
        std::string str = "";
        switch (s->type) {
            case StateType::CHAR:
                switch (s->c) {
                    case '\\': str += "\\\\"; break;
                    case '"':  str += "\\\""; break;
                    case '\n': str += "\\n";  break;
                    case '\t': str += "\\t";  break;
                    case '\r': str += "\\r";  break;
                    case '\f': str += "\\f";  break;
                    case '\v': str += "\\v";  break;
                    default:   str += s->c; break;
                }
                break;
            case StateType::CHAR_CLASS: {
                str = s->negated ? "[^" : "[";
                for (auto& r : s->ranges) {
                    str += r.lo;
                    if (r.lo != r.hi) {
                        str += "-";
                        str += r.hi;
                    }
                }
                str += "]";
                break;
            }
            case StateType::SPLIT:
            case StateType::SAVE:
            case StateType::ANCHOR_START:
            case StateType::ANCHOR_END:
                str += "ε";
                break;
            default:
                break;
        }
        return str;
    }
};

#endif // NFA_BUILDER_HPP