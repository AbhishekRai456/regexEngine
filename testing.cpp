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
        "()+", // -> runtime error (empty parentheses) (correct by design)
        "a**", // -> runtime error (quantifier follow invalid token) (correct)

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
        "a*?", // -> runtime error (does not support lazy quantifiers as of now)
        "(a?)*","(a*)?","((a?)*)*",

        // Large
        "abcdefghij","(abc){10}","((ab)c){5}",

        // Escapes
        "a\\.b","\\\\\\*","a\\{2\\}","a b",

        // Special / Edge
        "",
        "(a(b)", // -> runtime error (missing parentheses) (correct)
        "[a-z" , // -> runtime error (unterminated char class) (correct)
        "a{2,1}" // -> runtime error (invalid range) (correct)

        // CHAR CLASS TESTS: (These tests mostly test the tokenizer because the NFA is lite)
        // Basic valid classes
        "[a]", "[z]", "[0]", "[_]", "[9]",
        "[abc]", "[xyz]", "[aZ9_]",

        // Simple ranges
        "[a-z]", "[A-Z]", "[0-9]",

        // Multiple ranges
        "[a-zA-Z]", "[a-z0-9]", "[A-Fa-f0-9]",
        "[a-zA-Z0-9_]", "[A-Za-z_]", "[0-9A-Fa-f]",

        // Mixed range + literal
        "[a-z_]", "[_a-z]", "[a-z9]", "[0-9a-f]",

        // Negated classes
        "[^a]", "[^abc]", "[^a-z]", "[^a-zA-Z0-9_]",
        "[^\\w\\s\\d]", "[^-]", "[^\\d]", "[^\\d-]", "[^]]",

        // Escaped characters
        "[\\]]", "[\\[]", "[\\-]", "[\\\\]", "[\\^]",
        "[\\.]", "[\\{]", "[\\}]", "[\\(]", "[\\)]",

        // Escaped + normal mix
        "[a\\-z]", "[a\\]z]", "[\\-a-z]", "[a\\[b\\]c]",
        "[a\\]b]", "[a\\]]", "[a\\-\\]]",

        // Hyphen handling
        "[-]", "[--]", "[a-]", "[-a]", "[a-b]", "[--a]",
        "[a-b-c]", "[a--c]", "[a\\--c]", "[\\--\\-]",

        // ASCII / table spans
        "[ -/]", "[A-z]", "[!-~]",

        // Empty / malformed
        "[]", "[^]", "[", "[a", "[^a", "[a-z", "[\\]", "[]]", "[]-a]",

        // Invalid ranges
        "[z-a]", "[9-0]", "[Z-A]", "[a--b]",

        // Nested / weird
        "[[a]]", "[a[b]c]",

        // Shorthands
        "[\\d]", "[\\D]", "[\\w]", "[\\W]", "[\\s]", "[\\S]",
        "[\\d\\d]", "[\\d\\w]", "[\\w\\d]", "[\\s\\d]",
        "[\\d-]", "[\\w-]", "[a\\dZ]",

        // Illegal shorthand ranges
        "[\\d-a]", "[a-\\d]", "[\\d-\\w]", "[\\w-\\s]",
        "[\\s-\\d]", "[^\\d-a]", "[^\\s-\\w]",

        // Boundary / weird negations
        "[^^]", "[^\\^]", "[^\\[]",

        // Adjacent ranges
        "[a-bc]", "[ab-c]", "[a-b-c-d]",

        // Escaped range boundaries
        "[\\[-\\]]",

        // Weird escapes
        "[\\n]", "[\\t]", "[\\r]", "[\\v]", "[\\f]",

        // Literal metacharacters
        "[.]", "[(]", "[)]", "[{]", "[}]", "[|]", "[*]", "[+]", "[?]",

        // Overlapping syntax
        "[a|b]", "[a||b]",

        // Stress
        "[abcdefghijklmnopqrstuvwxyz]",

        // Weird Quantifiers
        "{2,3}", "{  4   , 7   }", "{  6   ,  }", "{  ,   10  }", "{    ,  }", "{}", "{   }", "{ 22 , a }", "{  8 ,  2}"
    };

    vector<std::string> weirdQuantifiers = {"{2,3}", "{  4   , 7   }", "{  6   ,  }", "{  ,   10  }", "{    ,  }", "{}", "{   }", "{ 22 , a }", "{  8 ,  2}"};

    // Test:
    auto start = std::chrono::high_resolution_clock::now();
    
    NfaBuilder nb;
    for(size_t i = 0; i < weirdQuantifiers.size(); i++){
        try{
            std::cout << "running tc: " << i << " : " << weirdQuantifiers[i] <<"\n";
            Tokenizer t(weirdQuantifiers[i]);
            vector<Token> tokens = t.tokenize();
            // 1. print tokens:
            std::cout << "tokens:" << std::endl;
            print(tokens);
    
            vector<Token> postfix = PostfixConverter::convert(tokens);
            // 2. print postfix:
            // std::cout << "postfix:" << std::endl;
            // print(tokens);
            
            // 3. print nfa: (check results in nfas/)
            // NfaPrinter::print_nfa(nb.build(postfix), i);
            // State* s = nb.build(postfix);
        }catch (const std::exception& e) {
            std::cout << "ERR: "  << " -> " << e.what() << "\n";
        }
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "Elapsed time: " << elapsed.count() << " ms\n";
    return 0;
}

// Result of tests:
// Successfully built correct NFAs for these 200+ tcs
// NOTE:
// -> does not support lazy quantifier
// -> empty parentheses -> gives error (design choice, pcre does not give error)
// -> the time to execute this file might be large but that's only because we are printing the nfas using 'graphviz' for testing purposes
// -> without the nfa printing, total time recorded by me to build these 200+ nfas was 100ms

// compile and run the file:
// g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wshadow -Wconversion testing.cpp tokenizer.cpp postfix.cpp nfa_builder.cpp -o testing.exe
// .\testing .exe