#include "lexer.h"

#include <algorithm>
#include <charconv>
#include <unordered_map>

using namespace std;

namespace parse {

    bool operator==(const Token& lhs, const Token& rhs) {
        using namespace token_type;

        if (lhs.index() != rhs.index()) {
            return false;
        }
        if (lhs.Is<Char>()) {
            return lhs.As<Char>().value == rhs.As<Char>().value;
        }
        if (lhs.Is<Number>()) {
            return lhs.As<Number>().value == rhs.As<Number>().value;
        }
        if (lhs.Is<String>()) {
            return lhs.As<String>().value == rhs.As<String>().value;
        }
        if (lhs.Is<Id>()) {
            return lhs.As<Id>().value == rhs.As<Id>().value;
        }
        return true;
    }

    bool operator!=(const Token& lhs, const Token& rhs) {
        return !(lhs == rhs);
    }

    std::ostream& operator<<(std::ostream& os, const Token& rhs) {
        using namespace token_type;

#define VALUED_OUTPUT(type) \
    if (auto p = rhs.TryAs<type>()) return os << #type << '{' << p->value << '}';

        VALUED_OUTPUT(Number);
        VALUED_OUTPUT(Id);
        VALUED_OUTPUT(String);
        VALUED_OUTPUT(Char);

#undef VALUED_OUTPUT

#define UNVALUED_OUTPUT(type) \
    if (rhs.Is<type>()) return os << #type;

        UNVALUED_OUTPUT(Class);
        UNVALUED_OUTPUT(Return);
        UNVALUED_OUTPUT(If);
        UNVALUED_OUTPUT(Else);
        UNVALUED_OUTPUT(Def);
        UNVALUED_OUTPUT(Newline);
        UNVALUED_OUTPUT(Print);
        UNVALUED_OUTPUT(Indent);
        UNVALUED_OUTPUT(Dedent);
        UNVALUED_OUTPUT(And);
        UNVALUED_OUTPUT(Or);
        UNVALUED_OUTPUT(Not);
        UNVALUED_OUTPUT(Eq);
        UNVALUED_OUTPUT(NotEq);
        UNVALUED_OUTPUT(LessOrEq);
        UNVALUED_OUTPUT(GreaterOrEq);
        UNVALUED_OUTPUT(None);
        UNVALUED_OUTPUT(True);
        UNVALUED_OUTPUT(False);
        UNVALUED_OUTPUT(Eof);

#undef UNVALUED_OUTPUT

        return os << "Unknown token :("sv;
    }

    Lexer::Lexer(std::istream& input) 
        : input_(input)
    {
        IgnoreInitialComments();
        current_token_ = token_type::Newline{};
        current_token_ = NextToken();
    }

    const Token& Lexer::CurrentToken() const {
        return current_token_;
    }

    Token Lexer::NextToken() {
        if (current_token_ == token_type::Eof{}) {
            return token_type::Eof{};
        }
        if (current_token_ == token_type::Newline{}) {
            IgnoreEmptyRawsAndComments();
        }
        if (diff_current_prev_indent_ != 0) {
            return current_token_ = ParseIndentOrDedent();
        }

        IgnoreSpaces();

        char c = input_.get();

        if (isdigit(c)) {
            input_.putback(c);
            return current_token_ = ParseNumber();
        }

        else if (c == '\'' || c == '\"') {
            input_.putback(c);
            return current_token_ = ParseStringConstant();;
        }

        else if (c == EOF) {
            if (current_token_ == token_type::Newline{} || current_token_ == token_type::Indent{} 
                || current_token_ == token_type::Dedent{}) {
                return current_token_ = token_type::Eof{};
            }
            return current_token_ = token_type::Newline{};
        }

        else if (c == '\n') {
            return current_token_ = token_type::Newline{};
        }

        else if (c == '#') {
            string tmp;
            getline(input_, tmp);
            IgnoreEmptyRawsAndComments();
            char c = input_.peek();
            if (c == '#') {
                return current_token_ = NextToken();
            }
            else if (c == EOF) {
                return current_token_ = token_type::Eof{};
            }
            return current_token_ = token_type::Newline{};
        }

        else if (c == '=' && input_.peek() != '=') {
            return current_token_ = token_type::Char{ '=' };
        }

        else if (comparison_symbols.count(c) && input_.peek() == '=') {
            input_.putback(c);
            return current_token_ = ParseComparisonOperand();
        }

        else if (special_symbols.count(c)) {
            return current_token_ = token_type::Char{ c };
        }

        else if (isalpha(c) || c == '_') {
            input_.putback(c);
            return current_token_ = ParseName();
        }

        else {
            throw LexerError("parsing error");
        }
    }

    Token Lexer::ParseIndentOrDedent() {
        if (diff_current_prev_indent_ > 0) {
            --diff_current_prev_indent_;
            return token_type::Indent{};
        }
        else if (diff_current_prev_indent_ < 0) {
            ++diff_current_prev_indent_;
            return token_type::Dedent{};
        }
        throw LexerError("no indent or dedent");
    }

    Token Lexer::ParseNumber() {
        char c = input_.get();
        if (!isdigit(c)) {
            throw LexerError("expected number"s);
        }
        string number;
        while (isdigit(c)) {
            number += c;
            c = input_.get();
        }
        if (c != ' ' && c != '\n' && c != EOF && special_symbols.count(c) == 0) {
            throw LexerError("expected space or new line or end of file or special symbol"s);
        }
        input_.putback(c);
        return token_type::Number{stoi(number)};
    }

    Token Lexer::ParseStringConstant() {
        const char initial_quote = input_.get();
        if (initial_quote != '\'' && initial_quote != '\"') {
            throw LexerError("expected opening qoute"s);
        }
        string str;
        while (true) {
            if (input_.peek() == EOF) {
                throw LexerError("expected closing quote");
            }
            char c = input_.get();
            if (c != initial_quote && c != '\\') {
                str += c;
            }
            else if (c == '\\') {
                c = input_.get();
                if (c == 't') {
                    str += '\t';
                }
                else if (c == 'n') {
                    str += '\n';
                }
                else {
                    str += c;
                }
            }
            else {
                break;
            }
        }
        return token_type::String{str};
    }

    Token Lexer::ParseName() {
        char c = input_.get();
        if (!isalpha(c) && c != '_') {
            throw LexerError("invalid keyword or id");
        }
        string name;
        name += c;
        c = input_.get();
        while (c == '_' || isdigit(c) || isalpha(c)) {
            name += c;
            c = input_.get();
        }

        input_.putback(c);
        if (token_type::keyword_to_token.count(name)) {
            return token_type::keyword_to_token.at(name);
        }
        return token_type::Id{name};
    }

    Token Lexer::ParseComparisonOperand() {
        string operand;
        operand += (char) input_.get();
        operand += (char) input_.get();
        if (operand == "==") {
            return token_type::Eq{};
        }
        else if (operand == "!=") {
            return token_type::NotEq{};
        }
        else if (operand == "<=") {
            return token_type::LessOrEq{};
        }
        else if (operand == ">=") {
            return token_type::GreaterOrEq{};
        }
        else {
            throw LexerError("expected comparison operand"s);
        }
    }

    void Lexer::IgnoreInitialComments() {
        IgnoreSpaces();
        if (input_.peek() != '#') {
            return;
        }
        string tmp;
        getline(input_, tmp);
        IgnoreInitialComments();
    }

    void Lexer::IgnoreSpaces() {
        while (input_.peek() == ' ') {
            input_.ignore(1);
        }
    }

    void Lexer::IgnoreEmptyRawsAndComments() {
     
        size_t count_spaces = 0;
        while (input_.peek() == ' ') {
            input_.ignore(1);
            ++count_spaces;
        }

        if (input_.peek() == '\n') {
            input_.ignore(1);
            IgnoreEmptyRawsAndComments();
            return;
        }

        if (input_.peek() == '#') {
            string tmp;
            getline(input_, tmp);
            IgnoreEmptyRawsAndComments();
            return;
        }

        if (count_spaces % 2 == 1) {
            throw LexerError("invalid indent");
        } 

        diff_current_prev_indent_ = count_spaces / 2 - current_indent_;
        current_indent_ = count_spaces / 2;
    }

}  // namespace parse
