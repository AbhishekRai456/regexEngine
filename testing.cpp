#include<iostream>
#include<vector>
#include"tokenizer.hpp"
#include"postfix.hpp"
#include"nfa_builder.hpp"
#include<chrono>
using namespace std;

int main(){
    // Test Set:
    std::vector<std::string> tcs = {
        // Basics
        "a","ab","abc","aaaa","b","."," ",

        // Alternation
        "a|b","ab|cd","a|b|c","(a|b)c","a(b|c)d","a|b|c|d|e",

        // Grouping
        "(a)","(ab)","(a|b)","((a))","(a(b(c)))","((((a))))","(a)|(b)","(a(b)c)",

        // Star / Plus / Optional
        "a*","(ab)*","(a|b)*","((ab)*)*","(a*)*",
        "a+","(ab)+","(a|b)+",
        "a?","(ab)?","(a|b)?",
            // "()+", -> runtime error (empty parentheses) (correct by design)
            // "a**", -> runtime error (quantifier follow invalid token) (correct)

        // Mixed Quantifiers
        "a*b+","a+b*","a?b+","(a|b)*c","(a|b)+c",

        // Ranges
        "a{0}","a{1}","a{2}","a{3}",
        "a{0,1}","a{1,2}","a{2,4}","a{3,5}","a{1,}","a{0,}",
        "(ab){2}","(a|b){2,4}","(abc){2,3}","[a-z]{2,5}",

        // Char classes
        "[a]","[abc]","[^abc]","[a-z]","[A-Z0-9]","[a-zA-Z]","[a-zA-Z0-9]",

        // Dot
        ".*",".+",".{2,4}","(.)*",

        // Anchors
        "^a","a$","^a$","^abc$","^(a|b)*$","^a|b$","^.*$",

        // Captures
        "(a)","(a)(b)","((a)b)","(a(b(c)))","(a|b)c(d|e)",

        // Pathological nesting
        "((((a)))*|b)+","((a|b)*)*","((a*)*)*","(((ab)*)*)*","((a|ab)*)*",

        // Precedence
        "a|bc","ab|c","a(b|c)d","(a|b)(c|d)","a|b*","(a|b)*","a(b*)","(ab)*c",

        // Overlapping
        "a|aa","(a|aa)*","(a|ab)*","(ab|a)*",

        // Epsilon-heavy
            // "a*?", -> runtime error (does not support lazy quantifiers as of now)
        "(a?)*","(a*)?","((a?)*)*",

        // Large
        "abcdefghij","(abc){10}","((ab)c){5}",

        // Escapes
        "a\\.b","\\\\\\*","a\\{2\\}","a b",

        // Special / Edge
        "",
            // "(a(b)", -> runtime error (missing parentheses) (correct)
            // "[a-z" , -> runtime error (unterminated char class) (correct)
            // "a{2,1}" -> runtime error (invalid range) (correct)
    };

    // Test:
    PostfixConverter pc;
    NfaBuilder nb;
    for(size_t i = 0; i < tcs.size(); i++){
        std::cout << "running tc: " << i << " : " << tcs[i] <<"\n";
        Tokenizer t(tcs[i]);
        vector<Token> tokens = t.tokenize();
        // 1. print tokens:
        // std::cout << "tokens:" << std::endl;
        // print(tokens);

        vector<Token> postfix = pc.convert(tokens);
        // 2. print postfix:
        // std::cout << "postfix:" << std::endl;
        // print(tokens);
        
        // 3. print nfa: (check results in nfas/)
        NfaPrinter::print_nfa(nb.build(postfix), i);
    }
    return 0;
}

// Result:
// Successfully built correct NFAs for these 100+ tcs
// NOTE:
// -> does not support lazy quantifier
// -> empty parentheses -> gives error (design choice, pcre does not give error)
// -> the time to execute this file might be large but that's only because we are printing the nfas using 'graphviz' for testing purposes
// -> without the nfa printing, total time recorded by me to build these 100+ nfas was 184ms

// compile and run the file:
// g++ -std=c++20 -Wall -Wextra -Wpedantic -Wshadow -Wconversion testing.cpp tokenizer.cpp postfix.cpp nfa_builder.cpp -o testing.exe
// .\testing .exe