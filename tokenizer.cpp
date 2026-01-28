#include "std.hpp"
#include "tokenizer.hpp"

Tokenizer::Tokenizer(std::string_view pat) : pattern(pat) {}

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

    for (size_t idx = 0; idx < tokens.size(); idx++) {
        normalized.push_back(tokens[idx]);

        if (idx + 1 >= tokens.size()) break;

        const Token& current = tokens[idx];
        const Token& next = tokens[idx + 1];

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
        current.type == TokenType::CARET
    );

    bool is_starter = (
        next.type == TokenType::LITERAL ||
        next.type == TokenType::DOT ||
        next.type == TokenType::LPAREN || 
        next.type == TokenType::CHAR_CLASS ||
        next.type == TokenType::DOLLAR
    );

        if (is_ender && is_starter) {
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

// Sorts the ranges and merges overlapping or adjacent intervals in place,
// producing a minimal, ordered set of non-overlapping character ranges.
void Tokenizer::normalize_ranges(std::vector<CharRange>& ranges) {
    if (ranges.empty()) return;

    std::sort(ranges.begin(), ranges.end(),
        [](const CharRange& a, const CharRange& b) {
            if (a.lo != b.lo) return a.lo < b.lo;
            return a.hi < b.hi;
        });

    size_t write = 0;

    for (size_t read = 1; read < ranges.size(); ++read) {
        CharRange& last = ranges[write];
        const CharRange& cur = ranges[read];

        if (cur.lo <= last.hi + 1) {
            // merge into last
            last.hi = std::max(last.hi, cur.hi);
        } else {
            // move cur to next write position
            ++write;
            ranges[write] = cur;
        }
    }

    ranges.resize(write + 1);
}

Token Tokenizer::read_char_class()
{
    Token t{TokenType::CHAR_CLASS, i - 1};
    if (peek() == '^')
    {
        t.negated = true;
        get();
    }

    bool have_prev = false; // whether we have a pending character for range or literal
    bool last_was_shorthand = false; // whether last token was \d, \w, etc.
    char prev;

    // Read until closing ']'
    while (!eof() && peek() != ']')
    {
        char c = get();
        if (c == '\\')  // Handle escape sequences
        {
            if (eof())
                throw std::runtime_error("dangling escape in char class");
            // Flush pending literal before escape
            if (have_prev)
            {
                t.ranges.push_back({prev, prev});
                have_prev = false;
            }
            c = get();
            switch (c)
            {
            // Common escaped control characters
            case 'n': prev = '\n'; have_prev = true; last_was_shorthand = false; break;
            case 't': prev = '\t'; have_prev = true; last_was_shorthand = false; break;
            case 'r': prev = '\r'; have_prev = true; last_was_shorthand = false; break;
            case 'f': prev = '\f'; have_prev = true; last_was_shorthand = false; break;
            case 'v': prev = '\v'; have_prev = true; last_was_shorthand = false; break;

            // Shorthand character classes
            case 'd':
            case 'w':
            case 's':
            case 'D':
            case 'W':
            case 'S':
            {
                add_shorthand_ranges(c, t);
                last_was_shorthand = true;
                break;
            }

            // Escaped literal characters
            default:
            {
                prev = c;
                have_prev = true;
                last_was_shorthand = false;
                break;
            }
            }
            continue;
        }

        // Handle range syntax:
        if (have_prev && c == '-' && peek() != ']')
        { // when '-' acts as a range specifier
            char ub = get();
            if (ub == '\\') // Handle escaped upper bound
            {
                if (eof())
                    throw std::runtime_error("dangling escape in range");
                ub = get();
                if (ub == 'd' || ub == 'D' ||
                    ub == 'w' || ub == 'W' ||
                    ub == 's' || ub == 'S')
                {
                    throw std::runtime_error("cannot create a range with shorthand escape sequences");
                }
            }
            if (prev > ub) throw std::runtime_error("invalid character range");
            t.ranges.push_back({prev, ub});
            have_prev = false;
            continue;
        }
        if (c == '-' && last_was_shorthand && peek() != ']')
        {
            throw std::runtime_error("cannot create a range with shorthand escape sequences");
        }

        // Flush pending literal if no range follows
        if (have_prev) t.ranges.push_back({prev, prev});

        prev = c;
        have_prev = true;
        last_was_shorthand = false;
    }

    // Missing closing ']'
    if (eof()) throw std::runtime_error("unterminated character class");
    if (have_prev) t.ranges.push_back({prev, prev}); // Flush last pending character
    if (t.ranges.empty()) throw std::runtime_error("empty character class"); // Disallow empty classes
    get(); // consume ']'
    normalize_ranges(t.ranges);
    return t;
}
// NOTE: []] will be treated as an empty char class followed by a ] literal
// In many engines it gets processed as a valid char class with literal ']' but
// we currently treat the earliest found ] as the end of the char class as a design
// choice. To use ] as a literal inside the char class, user needs to escape it.

Token Tokenizer::read_quantifier(){
    Token t{TokenType::QUANTIFIER_RANGE, i-1};

    auto skip_spaces = [&](){
        while(!eof() && std::isspace(peek())){
            get();
        }
    };

    auto read_int = [&]() -> unsigned int{
        skip_spaces();
        int val = 0;
        bool found = false;
        while(!eof() && std::isdigit(peek())){
            found = true;
            val = val * 10 + (get() - '0');
        }
        if(!found && peek() != ',') throw std::runtime_error("expected number in quantifier");
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