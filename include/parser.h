// parser.h - tokens -> command structures.
//
// Grammar (a small subset of POSIX sh):
//
//   line     := list ( (';' | '&' | newline) list )*
//   list     := pipeline ( ('&&' | '||') pipeline )*      <- an "and-or list"
//   pipeline := command ( '|' command )*
//   command  := ( WORD | redirection )+
//   redirection := [n]'<' WORD | [n]'>' WORD | [n]'>>' WORD | [n]'>&' m
//
// A trailing '&' makes the whole and-or list a background job.
#pragma once

#include <string>
#include <vector>

#include "lexer.h"

struct Redirection {
    enum class Type { In, Out, Append, Dup };
    Type type = Type::Out;
    int fd = 1;       // the fd being redirected
    Word target;      // file name (unused for Dup)
    int dup_fd = -1;  // Dup: copy this fd onto fd
};

struct Command {
    std::vector<Word> words;           // assignments first, then the program and its args
    size_t n_assign = 0;               // how many leading words are NAME=value
    std::vector<Redirection> redirs;   // applied left to right
};

struct Pipeline {
    std::vector<Command> cmds;
    std::string text;  // original source text, used for the job table
};

enum class Connector { Seq, And, Or };  // how an item relates to the previous one

struct AndOrItem {
    Connector conn;
    Pipeline pipeline;
};

struct AndOrList {
    std::vector<AndOrItem> items;
    bool background = false;
    std::string text;
};

struct CommandLine {
    std::vector<AndOrList> lists;
};

enum class ParseStatus { Ok, Incomplete, Error };

struct ParseResult {
    ParseStatus status = ParseStatus::Ok;
    CommandLine line;
    std::string error;
};

ParseResult parse(const std::string& src);
