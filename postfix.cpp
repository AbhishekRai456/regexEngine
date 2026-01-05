#include "postfix.hpp"

int PostfixConverter::get_precedence(TokenType type) {
    switch (type) {
        case TokenType::STAR:
        case TokenType::PLUS:
        case TokenType::QUESTION:
        case TokenType::QUANTIFIER_RANGE:
            return 3;
        case TokenType::CONCAT:
            return 2;
        case TokenType::ALTERNATION:
            return 1;
        default:
            return 0;
    }
}

std::vector<Token> PostfixConverter::convert(const std::vector<Token>& infix) {
    std::vector<Token> postfix;
    std::stack<Token> operators;
    TokenType last_type = TokenType::END; // Initial sentinel

    for (const auto& t : infix) {
        switch (t.type) {
            case TokenType::LITERAL:
            case TokenType::DOT:
            case TokenType::CHAR_CLASS:
            case TokenType::CARET:
            case TokenType::DOLLAR:
                postfix.push_back(t);
                break;

            case TokenType::LPAREN:{
                postfix.push_back(t);
                operators.push(t);
                break;
            }

            case TokenType::RPAREN:{
                if (last_type == TokenType::LPAREN) 
                    throw std::runtime_error("Syntax Error: Empty parentheses ()");
                
                while (!operators.empty() && operators.top().type != TokenType::LPAREN) {
                    postfix.push_back(operators.top());
                    operators.pop();
                }
                if (operators.empty()) throw std::runtime_error("Syntax Error: Mismatched )");
                operators.pop();
                postfix.push_back(t);
                break;
            }
            case TokenType::STAR:
            case TokenType::PLUS:
            case TokenType::QUESTION:
            case TokenType::QUANTIFIER_RANGE:
                // Validation: Quantifiers must follow matchable atoms or groups
                if (last_type != TokenType::LITERAL && last_type != TokenType::DOT && 
                    last_type != TokenType::CHAR_CLASS && last_type != TokenType::RPAREN) {
                    throw std::runtime_error("Syntax Error: Quantifier follows invalid token");
                }
                goto push_operator;

            case TokenType::ALTERNATION:
                // Validation: Cannot start with | or have ||
                if (last_type == TokenType::END || last_type == TokenType::LPAREN || 
                    last_type == TokenType::ALTERNATION) {
                    throw std::runtime_error("Syntax Error: Empty side in alternation |");
                }
                goto push_operator;

            case TokenType::CONCAT:
            push_operator:
                while (!operators.empty() && operators.top().type != TokenType::LPAREN &&
                       get_precedence(operators.top().type) >= get_precedence(t.type)) {
                    postfix.push_back(operators.top());
                    operators.pop();
                }
                operators.push(t);
                break;

            default: break;
        }
        
        if (t.type != TokenType::END) last_type = t.type;
    }

    // Final trailing operator check
    if (last_type == TokenType::ALTERNATION || last_type == TokenType::CONCAT) {
        throw std::runtime_error("Syntax Error: Trailing binary operator");
    }

    while (!operators.empty()) {
        if (operators.top().type == TokenType::LPAREN) throw std::runtime_error("Syntax Error: Mismatched (");
        postfix.push_back(operators.top());
        operators.pop();
    }

    return postfix;
}