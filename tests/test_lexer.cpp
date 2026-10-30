// Unit tests for the lexer and word expansion. Run with `make unit`.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "lexer.h"

static int g_fail = 0, g_total = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_total;                                                           \
        if (!(cond)) {                                                       \
            ++g_fail;                                                        \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                    \
    } while (0)

static std::string lookup(const std::string& n) {
    if (n == "A") return "alpha";
    if (n == "SP") return "x  y";
    if (n == "STAR") return "*.no_such_ext_zz";
    if (n == "HOME") return "/home/u";
    return "";
}

// Lex `s` and expand every word token; operator tokens are shown as their text.
static std::string flat(const std::string& s) {
    LexResult r = lex(s);
    if (r.status != LexStatus::Ok) return "<err>";
    std::string out;
    for (const Token& t : r.tokens) {
        if (!out.empty()) out += " ";
        if (t.type == TokType::Word) {
            out += "[";
            bool first = true;
            for (const std::string& w : expand_word(t.word, lookup)) {
                if (!first) out += "|";
                out += w;
                first = false;
            }
            out += "]";
        } else {
            out += t.text == "\n" ? "NL" : t.text;
        }
    }
    return out;
}

int main() {
    CHECK(flat("echo hi") == "[echo] [hi]");
    CHECK(flat("  a   b  ") == "[a] [b]");

    // operators glue to neighbours without spaces
    CHECK(flat("a|b") == "[a] | [b]");
    CHECK(flat("a&&b||c") == "[a] && [b] || [c]");
    CHECK(flat("a;b&") == "[a] ; [b] &");
    CHECK(flat("cat<in>out") == "[cat] < [in] > [out]");
    CHECK(flat("a >> f") == "[a] >> [f]");
    CHECK(flat("cmd 2>&1") == "[cmd] 2>&1");
    CHECK(flat("cmd 2>err") == "[cmd] 2> [err]");

    // quoting
    CHECK(flat("echo 'a  b'") == "[echo] [a  b]");
    CHECK(flat("echo \"a  b\"") == "[echo] [a  b]");
    CHECK(flat("echo 'a|b;c'") == "[echo] [a|b;c]");
    CHECK(flat("echo a\\ b") == "[echo] [a b]");
    CHECK(flat("echo \"a\"'b'c") == "[echo] [abc]");
    CHECK(flat("echo \"\"") == "[echo] []");      // empty quoted word still exists
    CHECK(flat("echo ''") == "[echo] []");

    // variables
    CHECK(flat("echo $A") == "[echo] [alpha]");
    CHECK(flat("echo ${A}x") == "[echo] [alphax]");
    CHECK(flat("echo \"$A\"") == "[echo] [alpha]");
    CHECK(flat("echo '$A'") == "[echo] [$A]");
    CHECK(flat("echo $SP") == "[echo] [x|y]");        // unquoted: field splitting
    CHECK(flat("echo \"$SP\"") == "[echo] [x  y]");   // quoted: kept whole
    CHECK(flat("echo $NOPE") == "[echo] []");         // unset -> no word at all
    CHECK(flat("echo \"$NOPE\"") == "[echo] []");     // quoted -> one empty word
    CHECK(flat("echo $?") == "[echo] []");            // lookup stub returns ""
    CHECK(flat("echo $") == "[echo] [$]");
    CHECK(flat("echo a$") == "[echo] [a$]");
    CHECK(flat("echo \\$A") == "[echo] [$A]");
    CHECK(flat("echo ~") == "[echo] [/home/u]");
    CHECK(flat("echo ~/x") == "[echo] [/home/u/x]");
    CHECK(flat("echo a~") == "[echo] [a~]");
    CHECK(flat("echo '~'") == "[echo] [~]");
    CHECK(flat("echo $STAR") == "[echo] [*.no_such_ext_zz]");  // no match: pattern kept

    // comments and newlines
    CHECK(flat("echo a # comment") == "[echo] [a]");
    CHECK(flat("echo a#b") == "[echo] [a#b]");
    CHECK(flat("a\nb") == "[a] NL [b]");

    // incomplete / error
    CHECK(lex("echo 'abc").status == LexStatus::Incomplete);
    CHECK(lex("echo \"abc").status == LexStatus::Incomplete);
    CHECK(lex("echo abc\\").status == LexStatus::Incomplete);
    CHECK(lex("echo $(date)").status == LexStatus::Error);
    CHECK(lex("echo `date`").status == LexStatus::Error);
    CHECK(lex("(ls)").status == LexStatus::Error);
    CHECK(lex("cat <<EOF").status == LexStatus::Error);

    // assignment detection
    CHECK(is_assignment_word(lex("A=1").tokens[0].word));
    CHECK(is_assignment_word(lex("_x9=hello").tokens[0].word));
    CHECK(!is_assignment_word(lex("=1").tokens[0].word));
    CHECK(!is_assignment_word(lex("1A=1").tokens[0].word));
    CHECK(!is_assignment_word(lex("'A'=1").tokens[0].word));
    CHECK(!is_assignment_word(lex("echo").tokens[0].word));

    // expand_single: no splitting
    {
        LexResult r = lex("v=$SP");
        CHECK(expand_single(r.tokens[0].word, lookup) == "v=x  y");
    }

    std::printf("test_lexer: %d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail ? 1 : 0;
}
