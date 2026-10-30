// Unit tests for the parser. Run with `make unit`.
#include <cstdio>
#include <string>

#include "parser.h"

static int g_fail = 0, g_total = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_total;                                                           \
        if (!(cond)) {                                                       \
            ++g_fail;                                                        \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                    \
    } while (0)

static std::string lookup(const std::string&) { return ""; }

static std::string word0(const Command& c, size_t i) {
    return expand_single(c.words[i], lookup);
}

int main() {
    // single command
    {
        ParseResult r = parse("ls -l /tmp");
        CHECK(r.status == ParseStatus::Ok);
        CHECK(r.line.lists.size() == 1);
        const auto& pl = r.line.lists[0].items[0].pipeline;
        CHECK(pl.cmds.size() == 1);
        CHECK(pl.cmds[0].words.size() == 3);
        CHECK(word0(pl.cmds[0], 1) == "-l");
        CHECK(!r.line.lists[0].background);
    }

    // pipeline
    {
        ParseResult r = parse("cat f | grep x | wc -l");
        CHECK(r.status == ParseStatus::Ok);
        CHECK(r.line.lists[0].items.size() == 1);
        CHECK(r.line.lists[0].items[0].pipeline.cmds.size() == 3);
        CHECK(r.line.lists[0].items[0].pipeline.text == "cat f | grep x | wc -l");
    }

    // && / || / ;
    {
        ParseResult r = parse("a && b || c ; d");
        CHECK(r.status == ParseStatus::Ok);
        CHECK(r.line.lists.size() == 2);
        const auto& items = r.line.lists[0].items;
        CHECK(items.size() == 3);
        CHECK(items[1].conn == Connector::And);
        CHECK(items[2].conn == Connector::Or);
        CHECK(r.line.lists[1].items.size() == 1);
    }

    // background
    {
        ParseResult r = parse("sleep 5 &");
        CHECK(r.status == ParseStatus::Ok);
        CHECK(r.line.lists.size() == 1);
        CHECK(r.line.lists[0].background);
    }
    {
        ParseResult r = parse("a & b");
        CHECK(r.status == ParseStatus::Ok);
        CHECK(r.line.lists.size() == 2);
        CHECK(r.line.lists[0].background);
        CHECK(!r.line.lists[1].background);
    }
    {
        ParseResult r = parse("a && b &");
        CHECK(r.status == ParseStatus::Ok);
        CHECK(r.line.lists[0].background);
        CHECK(r.line.lists[0].items.size() == 2);
    }

    // redirections
    {
        ParseResult r = parse("sort < in > out 2>> err");
        CHECK(r.status == ParseStatus::Ok);
        const Command& c = r.line.lists[0].items[0].pipeline.cmds[0];
        CHECK(c.words.size() == 1);
        CHECK(c.redirs.size() == 3);
        CHECK(c.redirs[0].type == Redirection::Type::In && c.redirs[0].fd == 0);
        CHECK(c.redirs[1].type == Redirection::Type::Out && c.redirs[1].fd == 1);
        CHECK(c.redirs[2].type == Redirection::Type::Append && c.redirs[2].fd == 2);
        CHECK(expand_single(c.redirs[1].target, lookup) == "out");
    }
    {
        ParseResult r = parse("cmd > f 2>&1");
        const Command& c = r.line.lists[0].items[0].pipeline.cmds[0];
        CHECK(c.redirs.size() == 2);
        CHECK(c.redirs[1].type == Redirection::Type::Dup);
        CHECK(c.redirs[1].fd == 2 && c.redirs[1].dup_fd == 1);
    }
    {  // redirection before the command name
        ParseResult r = parse("> out echo hi");
        const Command& c = r.line.lists[0].items[0].pipeline.cmds[0];
        CHECK(c.words.size() == 2 && c.redirs.size() == 1);
    }

    // assignments
    {
        ParseResult r = parse("A=1 B=2 env");
        const Command& c = r.line.lists[0].items[0].pipeline.cmds[0];
        CHECK(c.n_assign == 2 && c.words.size() == 3);
    }
    {
        ParseResult r = parse("A=1");
        const Command& c = r.line.lists[0].items[0].pipeline.cmds[0];
        CHECK(c.n_assign == 1 && c.words.size() == 1);
    }
    {  // NAME=value after the command name is just an argument
        ParseResult r = parse("echo A=1");
        const Command& c = r.line.lists[0].items[0].pipeline.cmds[0];
        CHECK(c.n_assign == 0);
    }

    // line continuation: pending input
    CHECK(parse("ls |").status == ParseStatus::Incomplete);
    CHECK(parse("ls &&").status == ParseStatus::Incomplete);
    CHECK(parse("ls ||").status == ParseStatus::Incomplete);
    CHECK(parse("echo 'x").status == ParseStatus::Incomplete);
    CHECK(parse("ls |\n wc").status == ParseStatus::Ok);

    // syntax errors
    CHECK(parse("| ls").status == ParseStatus::Error);
    CHECK(parse("ls | | wc").status == ParseStatus::Error);
    CHECK(parse("&& ls").status == ParseStatus::Error);
    CHECK(parse("ls >").status == ParseStatus::Error);
    CHECK(parse("ls > > f").status == ParseStatus::Error);
    CHECK(parse(";").status == ParseStatus::Error);
    CHECK(parse("ls ; ; ls").status == ParseStatus::Error);
    CHECK(parse("echo $(ls)").status == ParseStatus::Error);

    // empty / blank / comments
    CHECK(parse("").line.lists.empty());
    CHECK(parse("   ").line.lists.empty());
    CHECK(parse("# just a comment").line.lists.empty());
    CHECK(parse("ls;").status == ParseStatus::Ok);
    CHECK(parse("\n\nls\n\n").line.lists.size() == 1);

    std::printf("test_parser: %d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail ? 1 : 0;
}
