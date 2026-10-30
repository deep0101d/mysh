#include "parser.h"

namespace {

struct ParseFail {
    ParseStatus status;
    std::string message;
};

class Parser {
public:
    Parser(const std::string& src, const std::vector<Token>& toks) : src_(src), toks_(toks) {}

    CommandLine parse_line() {
        CommandLine line;
        while (true) {
            skip_newlines();
            if (pos_ >= toks_.size()) break;
            line.lists.push_back(parse_list());
        }
        return line;
    }

private:
    const std::string& src_;
    const std::vector<Token>& toks_;
    size_t pos_ = 0;

    bool at_end() const { return pos_ >= toks_.size(); }
    const Token& cur() const { return toks_[pos_]; }

    static bool is_newline(const Token& t) { return t.type == TokType::Semi && t.text == "\n"; }

    void skip_newlines() {
        while (!at_end() && is_newline(cur())) ++pos_;
    }

    std::string slice(size_t first_tok, size_t end_tok) const {
        size_t b = toks_[first_tok].begin;
        size_t e = toks_[end_tok - 1].end;
        return src_.substr(b, e - b);
    }

    [[noreturn]] void unexpected(size_t at) const {
        std::string what = "newline";
        if (at < toks_.size() && !is_newline(toks_[at])) what = toks_[at].text;
        throw ParseFail{ParseStatus::Error, "syntax error near unexpected token `" + what + "'"};
    }

    AndOrList parse_list() {
        AndOrList l;
        size_t first = pos_;
        l.items.push_back({Connector::Seq, parse_pipeline()});
        while (!at_end() && (cur().type == TokType::AndIf || cur().type == TokType::OrIf)) {
            Connector c = cur().type == TokType::AndIf ? Connector::And : Connector::Or;
            ++pos_;
            skip_newlines();  // "a &&\n b" is allowed
            if (at_end()) throw ParseFail{ParseStatus::Incomplete, "unexpected end of input"};
            l.items.push_back({c, parse_pipeline()});
        }
        l.text = slice(first, pos_);

        if (!at_end()) {
            const Token& t = cur();
            if (t.type == TokType::Amp) {
                l.background = true;
                ++pos_;
            } else if (t.type == TokType::Semi) {
                ++pos_;
            } else {
                unexpected(pos_);
            }
        }
        return l;
    }

    Pipeline parse_pipeline() {
        Pipeline p;
        size_t first = pos_;
        p.cmds.push_back(parse_command());
        while (!at_end() && cur().type == TokType::Pipe) {
            ++pos_;
            skip_newlines();  // "a |\n b" is allowed
            if (at_end()) throw ParseFail{ParseStatus::Incomplete, "unexpected end of input"};
            p.cmds.push_back(parse_command());
        }
        p.text = slice(first, pos_);
        return p;
    }

    Command parse_command() {
        Command cmd;
        bool any = false;
        while (!at_end()) {
            const Token& t = cur();
            if (t.type == TokType::Word) {
                cmd.words.push_back(t.word);
                ++pos_;
                any = true;
            } else if (t.type == TokType::RedirDup) {
                Redirection r;
                r.type = Redirection::Type::Dup;
                r.fd = t.fd;
                r.dup_fd = t.dup_fd;
                cmd.redirs.push_back(std::move(r));
                ++pos_;
                any = true;
            } else if (t.type == TokType::RedirIn || t.type == TokType::RedirOut ||
                       t.type == TokType::RedirAppend) {
                Redirection r;
                r.type = t.type == TokType::RedirIn    ? Redirection::Type::In
                         : t.type == TokType::RedirOut ? Redirection::Type::Out
                                                       : Redirection::Type::Append;
                r.fd = t.fd;
                ++pos_;
                if (at_end() || cur().type != TokType::Word) unexpected(pos_);
                r.target = cur().word;
                ++pos_;
                cmd.redirs.push_back(std::move(r));
                any = true;
            } else {
                break;
            }
        }
        if (!any) unexpected(pos_);

        // Leading NAME=value words are assignments.
        while (cmd.n_assign < cmd.words.size() && is_assignment_word(cmd.words[cmd.n_assign])) {
            ++cmd.n_assign;
        }
        return cmd;
    }
};

}  // namespace

ParseResult parse(const std::string& src) {
    ParseResult res;
    LexResult lr = lex(src);
    if (lr.status == LexStatus::Incomplete) {
        res.status = ParseStatus::Incomplete;
        res.error = lr.error;
        return res;
    }
    if (lr.status == LexStatus::Error) {
        res.status = ParseStatus::Error;
        res.error = lr.error;
        return res;
    }
    try {
        Parser p(src, lr.tokens);
        res.line = p.parse_line();
    } catch (const ParseFail& f) {
        res.status = f.status;
        res.error = f.message;
        res.line = CommandLine();
    }
    return res;
}
