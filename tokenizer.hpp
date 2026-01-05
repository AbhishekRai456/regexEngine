#ifndef TOKENIZER_HPP
#define TOKENIZER_HPP
#include "std.hpp"
#include<vector>

enum class TokenType{
    // Literals
    LITERAL,
    DOT,

    // Operators
    STAR,
    PLUS,
    QUESTION,
    ALTERNATION,

    // Grouping
    LPAREN,
    RPAREN,

    // Anchors
    CARET,
    DOLLAR,
    
    // Character Class
    CHAR_CLASS,

    // Quantifier
    QUANTIFIER_RANGE,

    // End
    END,

    // Concat
    CONCAT
};

struct CharRange{
    char lo;
    char hi;
};

struct Token{
    TokenType type;
    size_t pos; // index in pattern for error handling
    int group_id = -1; // Group ID
    // Info:

    // For literals
    char literal = '\0';

    // For character class
    bool negated = false;
    std::vector<CharRange> ranges{};

    // For {m, n}
    int min = 0;
    int max = 0;    // max = -1 -> unbounded
};

class Tokenizer{
    public:
    explicit Tokenizer(std::string_view pattern);
    std::vector<Token> tokenize();
    private:
    std::string_view pattern;
    size_t i = 0;
    int group_counter = 0;
    std::stack<int> group_stack;
    char peek() const;
    char get();
    bool eof() const;
    Token next_token();
    Token read_literal(char);
    Token read_escape();
    Token read_char_class();
    Token read_quantifier();
    void add_shorthand_ranges(char, Token&);
    void add_concat_tokens(std::vector<Token>&);
};

void print(const std::vector<Token>);

#endif // TOKENIZER_HPP

// =======================
// TODO — Tokenizer
// =======================

// [ ] Fix escape handling inside character classes:
//     - \n, \t, \r, \f, \v should be converted to actual characters inside [].
//     - Currently \n inside [] becomes 'n'.

// [ ] Fix std::isdigit undefined behavior:
//     - Use std::isdigit(static_cast<unsigned char>(c)).

// [ ] Improve quantifier error handling:
//     - Detect and error on unterminated {m,n}.
//     - Better messages for malformed quantifiers.

// [ ] Improve whitespace / non-whitespace shorthand ranges:
//     - Double-check correctness of \s and \S range definitions.
//     - Fix misleading comments in \S implementation.

// [ ] Validate empty or invalid character classes more strictly:
//     - e.g. [] , [^] , [-] edge cases.

// [ ] Consider tracking token length for better error spans.
//     (optional)

// [ ] Add more unit tests for tokenizer edge cases.
//     - Escapes
//     - Nested ranges
//     - Invalid syntax