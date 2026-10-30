#include "lexer.h"

#include <glob.h>

#include <cctype>
#include <cstring>

#include "utils.h"

namespace {

bool is_blank(char c) { return c == ' ' || c == '\t' || c == '\r'; }
bool is_op_char(char c) {
    return c == '|' || c == '&' || c == ';' || c == '<' || c == '>';
}
bool is_name_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}
bool is_name_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

void add_literal(Word& w, const std::string& text, bool quoted) {
    if (!w.pieces.empty() && w.pieces.back().kind == WordPiece::Kind::Literal &&
        w.pieces.back().quoted == quoted) {
        w.pieces.back().text += text;
    } else {
        // Note: an empty quoted literal ("" or '') is kept on purpose, it makes
        // the word exist even though it has no characters.
        w.pieces.push_back({WordPiece::Kind::Literal, text, quoted});
    }
}

void add_var(Word& w, const std::string& name, bool quoted) {
    w.pieces.push_back({WordPiece::Kind::Var, name, quoted});
}

// s[i] == '$'. Appends a Var piece (or a literal '$') and returns the next index.
// On unsupported syntax sets err.
size_t lex_dollar(const std::string& s, size_t i, Word& w, bool quoted, std::string& err) {
    const size_t n = s.size();
    size_t j = i + 1;
    if (j < n) {
        char c = s[j];
        if (c == '?' || c == '$' || c == '!' || std::isdigit(static_cast<unsigned char>(c))) {
            add_var(w, std::string(1, c), quoted);
            return j + 1;
        }
        if (c == '{') {
            size_t e = s.find('}', j);
            if (e == std::string::npos) {
                err = "bad substitution: missing `}'";
                return n;
            }
            add_var(w, s.substr(j + 1, e - j - 1), quoted);
            return e + 1;
        }
        if (is_name_start(c)) {
            size_t e = j;
            while (e < n && is_name_char(s[e])) ++e;
            add_var(w, s.substr(j, e - j), quoted);
            return e;
        }
        if (c == '(') {
            err = "command substitution $(...) is not supported";
            return n;
        }
    }
    add_literal(w, "$", quoted);
    return i + 1;
}

}  // namespace

LexResult lex(const std::string& s) {
    LexResult r;
    const size_t n = s.size();
    size_t i = 0;

    auto push = [&](TokType t, size_t b, size_t e) {
        Token tok;
        tok.type = t;
        tok.begin = b;
        tok.end = e;
        tok.text = s.substr(b, e - b);
        r.tokens.push_back(std::move(tok));
    };
    auto fail = [&](LexStatus st, const std::string& msg) {
        r.status = st;
        r.error = msg;
        return r;
    };

    while (i < n) {
        char c = s[i];

        if (is_blank(c)) { ++i; continue; }
        if (c == '\n') { push(TokType::Semi, i, i + 1); ++i; continue; }
        if (c == '#') {  // comment: only at the start of a word
            while (i < n && s[i] != '\n') ++i;
            continue;
        }
        if (c == '\\' && i + 1 < n && s[i + 1] == '\n') { i += 2; continue; }

        // ---- operators -------------------------------------------------
        if (c == '|') {
            if (i + 1 < n && s[i + 1] == '|') { push(TokType::OrIf, i, i + 2); i += 2; }
            else { push(TokType::Pipe, i, i + 1); ++i; }
            continue;
        }
        if (c == '&') {
            if (i + 1 < n && s[i + 1] == '&') { push(TokType::AndIf, i, i + 2); i += 2; }
            else { push(TokType::Amp, i, i + 1); ++i; }
            continue;
        }
        if (c == ';') { push(TokType::Semi, i, i + 1); ++i; continue; }

        // ---- redirections: [n]<  [n]>  [n]>>  [n]>&m -------------------
        {
            size_t j = i;
            int fd = -1;
            if (std::isdigit(static_cast<unsigned char>(c)) && j + 1 < n &&
                (s[j + 1] == '<' || s[j + 1] == '>')) {
                fd = c - '0';
                ++j;
            }
            if (s[j] == '<' || s[j] == '>') {
                Token t;
                t.begin = i;
                if (s[j] == '<') {
                    if (j + 1 < n && s[j + 1] == '<') {
                        return fail(LexStatus::Error, "here-documents (<<) are not supported");
                    }
                    t.type = TokType::RedirIn;
                    t.fd = fd < 0 ? 0 : fd;
                    ++j;
                } else {
                    t.fd = fd < 0 ? 1 : fd;
                    ++j;
                    if (j < n && s[j] == '>') {
                        t.type = TokType::RedirAppend;
                        ++j;
                    } else if (j + 1 < n && s[j] == '&' &&
                               std::isdigit(static_cast<unsigned char>(s[j + 1]))) {
                        t.type = TokType::RedirDup;
                        t.dup_fd = s[j + 1] - '0';
                        j += 2;
                    } else {
                        t.type = TokType::RedirOut;
                    }
                }
                t.end = j;
                t.text = s.substr(i, j - i);
                r.tokens.push_back(std::move(t));
                i = j;
                continue;
            }
        }

        // ---- a word ------------------------------------------------------
        Token t;
        t.type = TokType::Word;
        t.begin = i;
        Word& w = t.word;
        std::string err;

        // ~ and ~/path at the start of a word
        if (c == '~' && (i + 1 >= n || s[i + 1] == '/' || is_blank(s[i + 1]) ||
                         s[i + 1] == '\n' || is_op_char(s[i + 1]))) {
            add_var(w, "HOME", true);
            ++i;
        }

        while (i < n) {
            char ch = s[i];
            if (is_blank(ch) || ch == '\n' || is_op_char(ch)) break;

            if (ch == '(' || ch == ')' || ch == '`') {
                return fail(LexStatus::Error,
                            std::string("syntax error near unexpected token `") + ch +
                                "' (subshells and command substitution are not supported)");
            }
            if (ch == '\\') {
                if (i + 1 >= n) return fail(LexStatus::Incomplete, "trailing backslash");
                if (s[i + 1] == '\n') { i += 2; continue; }  // line continuation
                add_literal(w, std::string(1, s[i + 1]), true);
                i += 2;
                continue;
            }
            if (ch == '\'') {
                size_t e = s.find('\'', i + 1);
                if (e == std::string::npos) return fail(LexStatus::Incomplete, "unterminated '");
                add_literal(w, s.substr(i + 1, e - i - 1), true);
                i = e + 1;
                continue;
            }
            if (ch == '"') {
                ++i;
                add_literal(w, "", true);  // "" alone is an (empty) word
                while (true) {
                    if (i >= n) return fail(LexStatus::Incomplete, "unterminated \"");
                    char d = s[i];
                    if (d == '"') { ++i; break; }
                    if (d == '\\' && i + 1 < n && std::strchr("$\"\\`\n", s[i + 1])) {
                        if (s[i + 1] != '\n') add_literal(w, std::string(1, s[i + 1]), true);
                        i += 2;
                        continue;
                    }
                    if (d == '$') {
                        i = lex_dollar(s, i, w, true, err);
                        if (!err.empty()) return fail(LexStatus::Error, err);
                        continue;
                    }
                    add_literal(w, std::string(1, d), true);
                    ++i;
                }
                continue;
            }
            if (ch == '$') {
                i = lex_dollar(s, i, w, false, err);
                if (!err.empty()) return fail(LexStatus::Error, err);
                continue;
            }
            add_literal(w, std::string(1, ch), false);
            ++i;
        }

        t.end = i;
        t.text = s.substr(t.begin, i - t.begin);
        r.tokens.push_back(std::move(t));
    }
    return r;
}

// ---------------------------------------------------------------------------
// Expansion
// ---------------------------------------------------------------------------

namespace {

struct Field {
    std::string plain;     // the text as the user would see it
    std::string pattern;   // same text, with glob-special chars escaped unless active
    bool has_glob = false;
    bool exists = false;   // a field exists even if empty when it came from quotes
};

void escape_append(std::string& pat, const std::string& text) {
    for (char c : text) {
        if (c == '\\' || c == '*' || c == '?' || c == '[') pat += '\\';
        pat += c;
    }
}

}  // namespace

std::vector<std::string> expand_word(const Word& w, const LookupFn& lookup) {
    std::vector<Field> fields;
    Field cur;
    auto flush = [&]() {
        if (cur.exists) fields.push_back(cur);
        cur = Field();
    };

    for (const WordPiece& p : w.pieces) {
        if (p.kind == WordPiece::Kind::Literal) {
            cur.exists = true;
            cur.plain += p.text;
            if (p.quoted) {
                escape_append(cur.pattern, p.text);
            } else {
                for (char c : p.text) {
                    if (c == '*' || c == '?' || c == '[') cur.has_glob = true;
                    cur.pattern += c;
                }
            }
        } else {
            std::string v = lookup(p.text);
            if (p.quoted) {
                cur.exists = true;
                cur.plain += v;
                escape_append(cur.pattern, v);
            } else {
                // Unquoted $VAR: split on whitespace, then glob-expand like any
                // unquoted text (POSIX behaviour: G='*.txt'; echo $G lists files).
                for (char c : v) {
                    if (c == ' ' || c == '\t' || c == '\n') {
                        flush();
                    } else {
                        cur.exists = true;
                        cur.plain += c;
                        if (c == '*' || c == '?' || c == '[') cur.has_glob = true;
                        cur.pattern += c;
                    }
                }
            }
        }
    }
    flush();

    std::vector<std::string> out;
    for (const Field& f : fields) {
        if (f.has_glob) {
            glob_t g;
            std::memset(&g, 0, sizeof g);
            int rc = glob(f.pattern.c_str(), 0, nullptr, &g);
            if (rc == 0 && g.gl_pathc > 0) {
                for (size_t k = 0; k < g.gl_pathc; ++k) out.emplace_back(g.gl_pathv[k]);
            } else {
                out.push_back(f.plain);  // no match: keep the pattern literally
            }
            globfree(&g);
        } else {
            out.push_back(f.plain);
        }
    }
    return out;
}

std::string expand_single(const Word& w, const LookupFn& lookup) {
    std::string out;
    for (const WordPiece& p : w.pieces) {
        out += (p.kind == WordPiece::Kind::Literal) ? p.text : lookup(p.text);
    }
    return out;
}

bool is_assignment_word(const Word& w) {
    if (w.pieces.empty()) return false;
    const WordPiece& first = w.pieces[0];
    if (first.kind != WordPiece::Kind::Literal || first.quoted) return false;
    size_t eq = first.text.find('=');
    if (eq == std::string::npos || eq == 0) return false;
    return util::is_valid_name(first.text.substr(0, eq));
}
