#ifndef POSTFIX_HPP
#define POSTFIX_HPP
#include "tokenizer.hpp"

class PostfixConverter{
public:
    static std::vector<Token> convert(const std::vector<Token>& infix);
private:
    static int get_precedence(TokenType type);
};

#endif  // POSTFIX_HPP