#include "std.hpp"
#include "tokenizer.hpp"

Tokenizer::Tokenizer(std::string_view pattern) : pattern(pattern) {}

char Tokenizer::peek() const{
    return eof() ? '\0' : pattern[i];
}

char Tokenizer::get(){
    return eof() ? '\0' : pattern[i++];
}

bool Tokenizer::eof() const{
    return i >= pattern.size();
}

std::vector<Token> Tokenizer::tokenize(){
    std::vector<Token> tokens;
    while(!eof()){
        tokens.push_back(next_token());
    }
    tokens.push_back(Token{TokenType::END, i});
    add_concat_tokens(tokens);
    return tokens;
}

void Tokenizer::add_concat_tokens(std::vector<Token>& tokens) {
    if (tokens.size() <= 2) return;

    std::vector<Token> normalized;
    normalized.reserve(tokens.size() * 2);

    for (size_t i = 0; i < tokens.size(); i++) {
        normalized.push_back(tokens[i]);

        if (i + 1 >= tokens.size()) break;

        const Token& current = tokens[i];
        const Token& next = tokens[i + 1];

        // Can the current token be the left side of a concatenation?
    bool is_ender = (
        current.type == TokenType::LITERAL ||
        current.type == TokenType::DOT ||
        current.type == TokenType::CHAR_CLASS ||
        current.type == TokenType::RPAREN || 
        current.type == TokenType::STAR ||
        current.type == TokenType::PLUS ||
        current.type == TokenType::QUESTION ||
        current.type == TokenType::QUANTIFIER_RANGE ||
        current.type == TokenType::CARET     // YES: Anchor start links to what follows
    );

    bool is_starter = (
        next.type == TokenType::LITERAL ||
        next.type == TokenType::DOT ||
        next.type == TokenType::LPAREN || 
        next.type == TokenType::CHAR_CLASS ||
        next.type == TokenType::DOLLAR       // YES: Link previous content to Anchor end
    );

        if (is_ender && is_starter) {
            // We use the position of the current token for debugging/error info
            Token concat;
            concat.type = TokenType::CONCAT;
            concat.pos = current.pos; 
            normalized.push_back(concat);
        }
    }

    tokens = std::move(normalized);
}

Token Tokenizer::next_token(){
    char c = get();
    size_t pos = i-1;

    switch(c){
        case '.': return {TokenType::DOT, pos};
        case '*': return {TokenType::STAR, pos};
        case '+': return {TokenType::PLUS, pos};
        case '?': return {TokenType::QUESTION, pos};
        case '|': return {TokenType::ALTERNATION, pos};
        case '(':{
            int id = ++group_counter;
            group_stack.push(id);
            return {TokenType::LPAREN, '(', id};
        }
        case ')':{
            if (group_stack.empty()) throw std::runtime_error("Mismatched )");
            int id = group_stack.top();
            group_stack.pop();
            return {TokenType::RPAREN, ')', id};
        }
        case '^': return {TokenType::CARET, pos};
        case '$': return {TokenType::DOLLAR, pos};
        case '\\': return read_escape();
        case '[': return read_char_class();
        case '{': return read_quantifier();
        default: return read_literal(c);
    }
}

Token Tokenizer::read_literal(char c){
    Token t{TokenType::LITERAL, i-1};
    t.literal = c;
    return t;
}

Token Tokenizer::read_escape(){
    if(eof()) throw std::runtime_error("Dangling Escape");

    Token t;
    t.pos = i-1;
    char c = get();

    if (c == 'd' || c == 'D' ||
        c == 'w' || c == 'W' ||
        c == 's' || c == 'S'){
        t.type = TokenType::CHAR_CLASS;
        add_shorthand_ranges(c, t);
        return t;
    }
    
    t.type = TokenType::LITERAL;
    switch(c){
        case 'n': t.literal = '\n'; break;
        case 't': t.literal = '\t'; break;
        case 'r': t.literal = '\r'; break;
        case 'f': t.literal = '\f'; break;
        case 'v': t.literal = '\v'; break;
        default: t.literal = c; break;
    }
    return t;
}

void Tokenizer::add_shorthand_ranges(char c, Token& t){
    const char MIN_CHAR = '\0'; // ascii index 0
    const char MAX_CHAR = '\x7F'; // ascii index 127
    switch(c){
        case 'd':
            t.ranges.push_back({'0', '9'});
            break;
        case 'D':
            t.ranges.insert(t.ranges.end(),{
                            {MIN_CHAR, '/'},    // Everything before '0'
                            {':', MAX_CHAR}     // Everything after '9'
            });
            break;
        case 'w':
            t.ranges.insert(
                t.ranges.end(),
                {
                    {'a', 'z'},
                    {'A', 'Z'},
                    {'0', '9'},
                    {'_', '_'}
                }
            );
            break;
        case 'W':t.ranges.insert(t.ranges.end(), {
                    {MIN_CHAR, '/'},   // Before '0'
                    {':', '@'},        // Between '9' and 'A'
                    {'[', '^'},        // Between 'Z' and '_'
                    {'`', '`'},        // Between '_' and 'a'
                    {'{', MAX_CHAR}    // After 'z'
                });
                break;
        case 's':
            t.ranges.insert(
                t.ranges.end(),
                {   {' ', ' '},
                    {'\t', '\t'},
                    {'\n', '\n'},
                    {'\r', '\r'},
                    {'\f', '\f'},
                    {'\v', '\v'}
                }
            );
            break;
        
        case 'S':
            t.ranges.insert(t.ranges.end(), {
                {MIN_CHAR, '\x08'}, // Before \t (0-8)
                {'\x0E', '\x1F'},   // Between \r and Space (14-31)
                {'!', MAX_CHAR}     // After Space (33-127)
            });
        break;
    }
}

Token Tokenizer::read_char_class(){
    Token t{TokenType::CHAR_CLASS, i-1};
    if(peek() == '^'){
        t.negated = true;
        get();
    }

    bool have_prev = false;
    char prev = '\0';
    while(!eof() && peek() != ']'){
        char c = get();
        if(c == '\\'){
            if(eof()) throw std::runtime_error("dangling escape in char class");
            c = get();
            if (c == 'd' || c == 'D' ||
                c == 'w' || c == 'W' ||
                c == 's' || c == 'S'){
                if(have_prev){
                    t.ranges.push_back({prev, prev});
                    have_prev = false;
                }
                add_shorthand_ranges(c, t);
                have_prev = false;
                continue;
            }

            // If the hyphen is escaped
            if(c == '-'){
                if(have_prev){
                    t.ranges.push_back({prev, prev});
                }
                prev = '-';
                have_prev = true;
                continue;
            }
        }
        if(have_prev && c == '-' && peek() != ']'){
            char end = get();
            if(end == '\\'){
                if(eof()) throw std::runtime_error("dangling escape in range");
                end = get();
            }
            if(prev > end) throw std::runtime_error("invalid character range");
            t.ranges.push_back({prev, end});
            have_prev = false;
            continue;
        }

        if(have_prev){
            t.ranges.push_back({prev, prev});
        }

        prev = c;
        have_prev = true;
    }

    if(eof()) throw std::runtime_error("unterminated character class");
    if(have_prev){
        t.ranges.push_back({prev, prev});
    }
    if(t.ranges.empty()) throw std::runtime_error("empty character class");
    get();
    return t;
}

Token Tokenizer::read_quantifier(){
    Token t{TokenType::QUANTIFIER_RANGE, i-1};

    auto skip_spaces = [&](){
        while(!eof() && std::isspace(static_cast<unsigned char>(peek()))){
            get();
        }
    };

    auto read_int = [&]() -> int{
        skip_spaces();
        int val = 0;
        bool found = false;
        while(!eof() && std::isdigit(peek())){
            found = true;
            val = val * 10 + (get() - '0');
        }
        if(!found) throw std::runtime_error("expected number in quantifier");
        skip_spaces();
        return val;
    };

    t.min = read_int();

    if(peek() == '}'){
        get();
        t.max = t.min;
        return t;
    }

    if(peek() != ',') throw std::runtime_error("invalid Quantifier");
    get();
    skip_spaces();

    if(peek() == '}'){
        get();
        t.max = -1;
        return t;
    }

    t.max = read_int();
    if(peek() != '}') throw std::runtime_error("invalid Quantifier");
    get();

    if(t.max != -1 && t.max < t.min) throw std::runtime_error("invalid range in quantifier");
    return t;
}

void print(const std::vector<Token> v){
    for(auto it : v){
        switch (it.type)
        {
        case TokenType::LITERAL:
            std::cout << "LITERAL(" << it.literal << ") ";
            break;
        case TokenType::DOT:
            std::cout << "DOT ";
            break;
        case TokenType::STAR:
            std::cout << "STAR ";
            break;
        case TokenType::PLUS:
            std::cout << "PLUS ";
            break;
        case TokenType::QUESTION:
            std::cout << "QUESTION ";
            break;
        case TokenType::ALTERNATION:
            std::cout << "ALTERNATION ";
            break;
        case TokenType::LPAREN:
            std::cout << "LPAREN(" << it.group_id << ") ";
            break;
        case TokenType::RPAREN:
            std::cout << "RPAREN(" << it.group_id << ") ";
            break;
        case TokenType::CARET:
            std::cout << "CARET ";
            break;
        case TokenType::DOLLAR:
            std::cout << "DOLLAR ";
            break;
        case TokenType::CHAR_CLASS:
            std::cout << "CHAR_CLASS ";
            if(it.negated){
                std::cout << "(negated) ";
            }
            std::cout << "ranges= ";
            for (size_t i = 0; i < it.ranges.size(); i++){
                std::cout << "{" << it.ranges[i].lo << "," << it.ranges[i].hi << "}";
                if(i == it.ranges.size()-1){
                    std::cout << " ";
                }else{
                    std::cout << ", ";
                }
            }
            break;
        case TokenType::QUANTIFIER_RANGE:
            std::cout << "QUANTIFIER_RANGE(m=" << it.min << ", n=" << it.max << ") ";
            break;
        case TokenType::END:
            std::cout << "END" << std::endl;
            break;
        case TokenType::CONCAT:
            std::cout << "CONCAT ";
            break;
        default:
            break;
        }
    }
    std::cout << std::endl;
}

/*TOKENIZER TESTING: */

// std::ostream& operator<<(std::ostream& os, TokenType type) {
//     switch (type) {
//         case TokenType::LITERAL:      return os << "LITERAL";
//         case TokenType::DOT:          return os << "DOT";
//         case TokenType::STAR:         return os << "STAR";
//         case TokenType::PLUS:         return os << "PLUS";
//         case TokenType::QUESTION:     return os << "QUESTION";
//         case TokenType::ALTERNATION:  return os << "ALTERNATION";
//         case TokenType::LPAREN:       return os << "LPAREN";
//         case TokenType::RPAREN:       return os << "RPAREN";
//         case TokenType::CARET:        return os << "CARET";
//         case TokenType::DOLLAR:       return os << "DOLLAR";
//         case TokenType::CHAR_CLASS:   return os << "CHAR_CLASS";
//         case TokenType::QUANTIFIER_RANGE: return os << "QUANTIFIER";
//         case TokenType::END:          return os << "END";
//         default:                      return os << "UNKNOWN";
//     }
// }

// int main(){

//     std::vector<std::string> char_class_tests = {

//         // =====================
//         // BASIC VALID CLASSES
//         // =====================
//         "[a]",
//         "[z]",
//         "[0]",
//         "[_]",
//         "[9]",

//         // =====================
//         // MULTIPLE LITERALS
//         // =====================
//         "[abc]",
//         "[xyz]",
//         "[aZ9_]",

//         // =====================
//         // SIMPLE RANGES
//         // =====================
//         "[a-z]",
//         "[A-Z]",
//         "[0-9]",

//         // =====================
//         // MULTIPLE RANGES
//         // =====================
//         "[a-zA-Z]",
//         "[a-z0-9]",
//         "[A-Fa-f0-9]",

//         // =====================
//         // MIXED RANGE + LITERAL
//         // =====================
//         "[a-z_]",
//         "[_a-z]",
//         "[a-z9]",
//         "[0-9a-f]",

//         // =====================
//         // NEGATED CLASSES
//         // =====================
//         "[^a]",
//         "[^abc]",
//         "[^a-z]",
//         "[^a-zA-Z0-9_]",

//         // =====================
//         // ESCAPED CHARACTERS
//         // =====================
//         "[\\]]",
//         "[\\-]",
//         "[\\\\]",
//         "[\\^]",

//         // =====================
//         // ESCAPED + NORMAL MIX
//         // =====================
//         "[a\\-z]",
//         "[a\\]z]",
//         "[\\-a-z]",

//         // =====================
//         // DASH EDGE CASES
//         // =====================
//         "[-a]",
//         "[a-]",
//         "[a\\-]",
//         "[-]",

//         // =====================
//         // EMPTY CLASSES (INVALID)
//         // =====================
//         "[]",
//         "[^]",

//         // =====================
//         // UNTERMINATED CLASSES
//         // =====================
//         "[a",
//         "[^a",
//         "[a-z",

//         // =====================
//         // INVALID RANGE ORDER
//         // =====================
//         "[z-a]",
//         "[9-0]",
//         "[Z-A]",

//         // =====================
//         // DANGLING / BAD DASH
//         // =====================
//         "[a-]",
//         "[-a-]",
//         "[a--z]",

//         // =====================
//         // ESCAPE AT END
//         // =====================
//         "[\\]",

//         // =====================
//         // NESTED / WEIRD BRACKETS
//         // =====================
//         "[[a]]",
//         "[a[b]c]",

//         // =====================
//         // SHORTHANDS (IF SUPPORTED)
//         // =====================
//         "[\\d]",
//         "[\\w]",
//         "[\\s]",
//         "[\\D]",
//         "[\\W]",
//         "[\\S]",
//         "[a\\dZ]",

//         // =====================
//         // EXTRA CLOSING BRACKET
//         // =====================
//         "[a]]",
//         "[a-z]]",

//         // =====================
//         // STRESS / LARGE CLASSES
//         // =====================
//         "[abcdefghijklmnopqrstuvwxyz]",
//         "[a-zA-Z0-9_]",
//         "[^\\w\\s\\d]"
//     };

//     for(int i = 0; i < char_class_tests.size(); i++){
//         std::cout << i << " ";
//         std::string test = char_class_tests[i];
//         try {
//             Tokenizer tz(test);
//             std::vector<Token> tokens = tz.tokenize();
//             std::cout << "OK: " << test << " ";
//             for(Token token : tokens){
//                 std::cout << token.type << " ";
//                 if(token.type == TokenType::CHAR_CLASS){
//                     if(token.negated){
//                         std::cout << "Negated ";
//                     }
//                     std::cout << "Ranges: ";
//                     for(CharRange range : token.ranges){
//                         std::cout << range.lo << " to " << range.hi << "  ";
//                     }
//                 }
//             }
//             std::cout << std::endl;
//         } catch (const std::exception& e) {
//             std::cout << "ERR: " << test << " -> " << e.what() << "\n";
//         }
//     }
// }