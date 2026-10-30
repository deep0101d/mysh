// lexer.h - turns a line of text into tokens, and expands words at run time.
//
// Important design point: the lexer does NOT expand $variables. A word is
// stored as a list of pieces (literal text / variable reference, each marked
// quoted or not). Expansion happens later, right before a command runs, so
// that `false; echo $?` and `cd /tmp; echo $PWD` see up-to-date values.
#pragma once

#include <functional>
#include <string>
#include <vector>

using LookupFn = std::function<std::string(const std::string&)>;

struct WordPiece {
    enum class Kind { Literal, Var };
    Kind kind;
    std::string text;  // literal text, or the variable name
    bool quoted;       // inside '' or "" or escaped -> no splitting / globbing
};

struct Word {
    std::vector<WordPiece> pieces;
};

enum class TokType {
    Word,
    Pipe,         // |
    OrIf,         // ||
    AndIf,        // &&
    Amp,          // &
    Semi,         // ;  or a newline (text == "\n")
    RedirIn,      // [n]<
    RedirOut,     // [n]>
    RedirAppend,  // [n]>>
    RedirDup      // [n]>&m
};

struct Token {
    TokType type = TokType::Word;
    Word word;          // only for TokType::Word
    std::string text;   // raw source text (used in error messages)
    int fd = -1;        // redirections: which fd is redirected
    int dup_fd = -1;    // RedirDup: the fd it is duplicated from (2>&1 -> fd=2, dup_fd=1)
    size_t begin = 0;   // [begin, end) range in the source line
    size_t end = 0;
};

enum class LexStatus {
    Ok,
    Incomplete,  // open quote or trailing backslash: ask for another line
    Error        // unsupported / invalid syntax
};

struct LexResult {
    LexStatus status = LexStatus::Ok;
    std::vector<Token> tokens;
    std::string error;
};

LexResult lex(const std::string& src);

// Full expansion of one word into zero or more argv entries:
// $VAR substitution, field splitting of unquoted values, and glob (* ? [).
std::vector<std::string> expand_word(const Word& w, const LookupFn& lookup);

// $VAR substitution only: no splitting, no globbing (redirect targets, assignments).
std::string expand_single(const Word& w, const LookupFn& lookup);

// True for words that look like NAME=value (NAME unquoted).
bool is_assignment_word(const Word& w);
