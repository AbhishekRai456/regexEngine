#include<iostream>
#include<vector>
#include "tokenizer.hpp"
#include "postfix.hpp"
#include "nfa_builder.hpp"
using namespace std;

int main(){
    NfaBuilder nfa;
    PostfixConverter pc;
    vector<string> vec1 = {"a", "a|b", "[a-z]", "a{2, 3}"};
    vector<string> vec2 = {"{5}", "{    5  }", "{2,4}" ,"{ 2 , 4  }", "{ 10  , }", "{\n 2 \t , \r 3 }"};
    std::vector<std::string> vec3 = {
        "abc",                  // 1. Basic Literals (a.b.c)
        "a*b+c?",               // 2. Postfix Quantifiers (a*.b+.c?)
        "(a)b",                 // 3. Groups ( (a).b )
        "a|b",                  // 4. Alternation (No concats: a|b)
        "(a|b)*cd",             // 5. Nested Groups ( (a|b)*.c.d )
        "^abc$",                // 6. Anchors ( ^.a.b.c.$ )
        "[a-z]2",               // 7. Character Classes ( [a-z].2 )
        "a{ 2 , 3 }b",          // 8. Range Quantifiers w/ spaces ( a{2,3}.b )
        "a\\.b",                // 9. Escaped characters ( a.\..b )
        "^([a-z])+. (b|c)?$"    // 10. The "Everything" Test
    };
    for(size_t i = 0; i < vec3.size(); i++){
        Tokenizer t(vec3[i]);
        vector<Token> result = pc.convert(t.tokenize());
        // cout << i << ": ";
        // print(result);
        NfaPrinter::print_nfa(nfa.build(result), i);
        // NfaDebugger::print_graph(nfa.build(result));
    }
    return 0;
}

// g++ -std=c++20 -Wall -Wextra -Wpedantic -g testing.cpp tokenizer.cpp postfix.cpp nfa_builder.cpp -o testing.exe
// .\testing .exe