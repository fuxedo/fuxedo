// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <regex.h>
#include <cstring>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <sstream>

#include <fml32.h>
#include "basic_parser.h"
#include "misc.h"

#include <iostream>

// unary plus is recognized and ignored
enum tree_op {
  first_invalid = 0,
  const_long,
  const_double,
  const_string,
  field,
  field_any,
  unary_minus,
  logical_negation,
  bitwise_negation,
  multiplication,
  division,
  modulus,
  addition,
  substraction,
  exclusive_or,
  logical_and,
  logical_or,
  less_than,
  greater_than,
  less_or_equal,
  greater_or_equal,
  equal,
  not_equal,
  matches,
  not_matches,
  last_invalid,
};

const char *tree_ops[] = {
    "inv", "long", "double", "string", "field", "field_any", "-",  "!",  "~",
    "*",   "/",    "%",      "+",      "-",     "^",         "&&", "||", "<",
    ">",   "<=",   ">=",     "==",     "!=",    "%%",        "!%", "inv"};

// AST tree as a flat data structure
// Unlike Fbfr32 where users exprect some alignment of data this is optimized
// for size to hold as big AST tree as possible (16bit length)
// Evaluator takes care of alignment
class ast_tree {
 public:
  // first and second byte are reserved for length (Tuxedo API)
  // reserve next two bytes as well to support longer expressions
  ast_tree() : size_(1024), len_(4) { tree_ = (char *)malloc(size_); }
  ~ast_tree() { free(tree_); }

  char *take_tree() {
    // The first and second characters of the tree array form the least
    // significant byte and the most significant byte, respectively, of an
    // unsigned 16-bit quantity that gives the length, in bytes, of the entire
    // array.
    reinterpret_cast<unsigned char *>(tree_)[0] = len_ & 0xff;
    reinterpret_cast<unsigned char *>(tree_)[1] = len_ >> 8;
    auto ret = tree_;
    tree_ = nullptr;
    return ret;
  }

  void insert(uint32_t where, const char *data, uint32_t len) {
    if (size_ - len_ < len) {
      size_ *= 2;
      tree_ = (char *)realloc(tree_, size_);
    }
    memmove(tree_ + where + len, tree_ + where, len_ - where);
    std::copy_n(data, len, tree_ + where);
    len_ += len;
  }

  char peek(uint32_t where) { return tree_[where]; }

  uint32_t append(char op) {
    auto ret = len_;
    insert(len_, &op, sizeof(op));
    return ret;
  }
  uint32_t append(char op, const char *data, uint32_t len) {
    auto ret = len_;
    insert(len_, &op, sizeof(op));
    insert(len_, data, len);
    return ret;
  }

  uint32_t append(FLDID32 fieldid, FLDOCC32 oc) {
    auto ret = len_;
    char op = field;
    insert(len_, &op, sizeof(op));
    insert(len_, reinterpret_cast<char *>(&fieldid), sizeof(fieldid));
    insert(len_, reinterpret_cast<char *>(&oc), sizeof(oc));
    return ret;
  }

  auto len() const { return len_; }

 private:
  uint32_t size_;
  uint32_t len_;
  char *tree_;
};

class unknown_field_name : public basic_parser_error {
 public:
  unknown_field_name(const char *what, int row, int col, std::string extra)
      : basic_parser_error(what, row, col, extra) {}
};
class invalid_field_type : public basic_parser_error {
 public:
  invalid_field_type(const char *what, int row, int col, std::string extra)
      : basic_parser_error(what, row, col, extra) {}
};

class expression_parser : public basic_parser {
 public:
  expression_parser(std::istream &f) : basic_parser(f) {}

  char *parse() {
    if (parse_expr()) {
      return tree_.take_tree();
    }
    return nullptr;
  }

 private:
  struct binop {
    int prec;
    char opcode;
  };

  const binop &which(const std::string &op) {
#define MAKE_OP(prec, opcode)          \
  {                                    \
    tree_ops[opcode], { prec, opcode } \
  }
    static const std::map<std::string, binop> binops = {
        MAKE_OP(70, multiplication),   MAKE_OP(70, division),
        MAKE_OP(70, modulus),          MAKE_OP(60, addition),
        MAKE_OP(60, substraction),     MAKE_OP(50, less_than),
        MAKE_OP(50, greater_than),     MAKE_OP(50, less_or_equal),
        MAKE_OP(50, greater_or_equal), MAKE_OP(50, equal),
        MAKE_OP(50, not_equal),        MAKE_OP(40, matches),
        MAKE_OP(40, not_matches),      MAKE_OP(30, exclusive_or),
        MAKE_OP(20, logical_and),      MAKE_OP(10, logical_or)};

    auto it = binops.find(op);
    if (it == binops.end()) {
      static const binop not_found{-1, first_invalid};
      return not_found;
    }
    return it->second;
  }

  uint32_t parse_expr() {
    auto e = parse_primary();
    if (!e) {
      return 0;
    }
    return parse_binop(0, e);
  }

  uint32_t parse_binop(int expr_prec, uint32_t lhs) {
    while (true) {
      space();

      std::string ops;
      if (!op(&ops)) {
        return lhs;
      }

      auto tok = which(ops);
      if (tok.opcode == first_invalid) {
        throw basic_parser_error("Unknown binary operator", row_, col_,
                                 "operator '" + ops + "' not supported");
      } else if (tok.prec < expr_prec) {
        unget(ops);
        return lhs;
      }

      auto rhs = parse_primary();
      if (!rhs) {
        return 0;
      }

      std::string next_ops;
      if (op(&next_ops)) {
        unget(next_ops);
        auto next = which(next_ops);
        if (tok.prec < next.prec) {
          rhs = parse_binop(tok.prec + 1, (rhs));
          if (!rhs) {
            return 0;
          }
        }
      }

      char op = tok.opcode;
      if (op == matches || op == not_matches) {
        // LHS: field or string constant; RHS: string constant;
        if (tree_.peek(lhs) != const_string && tree_.peek(lhs) != field &&
            tree_.peek(lhs) != field_any) {
          throw basic_parser_error(
              "Right hand side of %% and !% must be a field or a string", row_,
              col_, "");
        }
        if (tree_.peek(rhs) != const_string) {
          throw basic_parser_error(
              "Left hand side of %% and !% must be a string", row_, col_, "");
        }
      }
      tree_.insert(lhs, &op, sizeof(op));
    }
  }

  uint32_t parse_primary() {
    space();

    std::string tok;

    uint32_t e;
    if (unary(&tok)) {
      if (tok == "-") {
        e = tree_.append(unary_minus);
      } else if (tok == "!") {
        e = tree_.append(logical_negation);
      } else if (tok == "~") {
        e = tree_.append(bitwise_negation);
      } else {
        // unary + is ignored
        e = tree_.len();
      }
      parse_primary();
      return e;
    }

    if (field_name(&tok)) {
      std::string oc;
      if (accept('[')) {
        if (unsigned_number(&oc)) {
        } else if (accept('?')) {
          oc = "?";
        }
        if (!accept(']')) {
          throw basic_parser_error("Invalid field subscript", row_, col_,
                                   "found '" + std::string(1, sym_) + "'");
        }
      } else {
        oc = "0";
      }
      auto fieldid = Fldid32(const_cast<char *>(tok.c_str()));
      if (fieldid == BADFLDID) {
        throw unknown_field_name("Unknown field name", row_, col_,
                                 "field name '" + tok + "'");
      }
      switch (Fldtype32(fieldid)) {
        case FLD_SHORT:
        case FLD_LONG:
        case FLD_FLOAT:
        case FLD_DOUBLE:
        case FLD_CHAR:
        case FLD_STRING:
        case FLD_CARRAY:
          break;
        default:
          throw invalid_field_type("Invalid field type", row_, col_,
                                   "field name '" + tok + "'");
      }
      if (oc == "?") {
        e = tree_.append(field_any, reinterpret_cast<char *>(&fieldid),
                         sizeof(fieldid));
      } else {
        e = tree_.append(fieldid, std::stol(oc));
      }
    } else if (unsigned_number(&tok)) {
      if (tok.find('.') != std::string::npos) {
        double value = std::stof(tok);
        e = tree_.append(const_double, reinterpret_cast<char *>(&value),
                         sizeof(value));
      } else {
        long value = std::stol(tok);
        e = tree_.append(const_long, reinterpret_cast<char *>(&value),
                         sizeof(value));
      }
    } else if (string(&tok)) {
      e = tree_.append(const_string, tok.c_str(), tok.size() + 1);
    } else if (accept('(')) {
      e = parse_expr();
      if (!accept(')')) {
        throw basic_parser_error("Missing closing )", row_, col_, "");
      }
    } else {
      throw basic_parser_error(
          "Unknown token", row_, col_,
          " invalid character '" + std::string(1, sym_) + "'");
    }
    space();
    return e;
  }

  bool string(std::string *s = nullptr) {
    if (accept('\'')) {
      while (true) {
        if (accept('\\')) {
          hex(s);
        } else if (!accept([](int c) { return c != '\''; }, s)) {
          break;
        }
      }
      if (!accept('\'')) {
        throw basic_parser_error("Invalid string literal", row_, col_,
                                 " invalid character '" + std::string(1, sym_) +
                                     "', expected '\\''");
      }
      return true;
    }
    return false;
  }

  void unget(std::string op) { op_ = op; }
  bool op(std::string *s = nullptr) {
    if (!op_.empty()) {
      if (s != nullptr) {
        *s = op_;
        op_.clear();
      }
      return true;
    }
    if (accept([](int c) { return strchr("+-!~*/%<>=^&|", c) != nullptr; },
               s)) {
      accept([](int c) { return strchr("=%&|", c) != nullptr; }, s);
      return true;
    }
    return false;
  }

  bool unary(std::string *s = nullptr) {
    if (accept([](int c) { return strchr("+-!~", c) != nullptr; }, s)) {
      return true;
    }
    return false;
  }

  // FIXME: to emulate lookahead/advance of binops because we don't have a lexer
  std::string op_;
  ast_tree tree_;
};

char *Fboolco32(char *expression) {
  if (expression == nullptr) {
    Ferror32 = FEINVAL;
    return nullptr;
  }
  try {
    std::istringstream s(expression);
    expression_parser p(s);
    return p.parse();
  } catch (const unknown_field_name &e) {
    Ferror32 = FBADNAME;
  } catch (const invalid_field_type &e) {
    Ferror32 = FEBADOP;
  } catch (const basic_parser_error &e) {
    Ferror32 = FSYNTAX;
  } catch (...) {
    Ferror32 = FSYNTAX;
  }
  return nullptr;
}

static char *boolpr(char *tree, FILE *iop) {
  uint8_t op = static_cast<uint8_t>(*tree++);
  if (op == const_long) {
    long value;
    std::copy_n(tree, sizeof(value), reinterpret_cast<char *>(&value));
    tree += sizeof(value);
    fprintf(iop, "( %ld ) ", value);
  } else if (op == const_double) {
    double value;
    std::copy_n(tree, sizeof(value), reinterpret_cast<char *>(&value));
    tree += sizeof(value);
    fprintf(iop, "( %f ) ", value);
  } else if (op == const_string) {
    auto len = strlen(tree);
    fprintf(iop, "( '%s' ) ", tree);
    tree += len + 1;
  } else if (op == field) {
    FLDID32 fieldid;
    std::copy_n(tree, sizeof(fieldid), reinterpret_cast<char *>(&fieldid));
    tree += sizeof(fieldid);
    FLDOCC32 oc;
    std::copy_n(tree, sizeof(oc), reinterpret_cast<char *>(&oc));
    tree += sizeof(oc);
    fprintf(iop, "( %s[%d] ) ", Fname32(fieldid), oc);
  } else if (op == field_any) {
    FLDID32 fieldid;
    std::copy_n(tree, sizeof(fieldid), reinterpret_cast<char *>(&fieldid));
    tree += sizeof(fieldid);
    fprintf(iop, "( %s[?] ) ", Fname32(fieldid));
  } else if (op == unary_minus || op == logical_negation ||
             op == bitwise_negation) {
    fprintf(iop, "( %s", tree_ops[op]);
    tree = boolpr(tree, iop);
    fprintf(iop, ") ");
  } else if (op > first_invalid && op < last_invalid) {
    fprintf(iop, "( ");
    tree = boolpr(tree, iop);
    fprintf(iop, "%s ", tree_ops[op]);
    tree = boolpr(tree, iop);
    fprintf(iop, ") ");
  }
  return tree;
}

void Fboolpr32(char *tree, FILE *iop) {
  if (tree != nullptr) {
    boolpr(tree + 4, iop);
    fprintf(iop, "\n");
  }
}

// C++17 std::variant ?
class eval_value {
 public:
  char *tree;
  long l;
  double d;
  const char *s;

  eval_value(char *tree, int value)
      : tree(tree), l(value), type_(eval_type::is_long) {}
  eval_value(char *tree, long value)
      : tree(tree), l(value), type_(eval_type::is_long) {}
  eval_value(char *tree, double value)
      : tree(tree), d(value), type_(eval_type::is_double) {}
  eval_value(char *tree, const char *value, bool is_const = false)
      : tree(tree),
        s(value),
        type_(eval_type::is_string),
        is_const_(is_const) {}

  bool is_long() const { return type_ == eval_type::is_long; }
  bool is_double() const { return type_ == eval_type::is_double; }
  bool is_string() const { return type_ == eval_type::is_string; }
  bool is_const_string() const {
    return type_ == eval_type::is_string && is_const_;
  }

  double to_double() const {
    if (type_ == eval_type::is_long) {
      return l;
    } else if (type_ == eval_type::is_double) {
      return d;
    } else if (type_ == eval_type::is_string) {
      return atof(s);
    }
    __builtin_unreachable(); // LCOV_EXCL_LINE
  }
  long to_long() const {
    if (type_ == eval_type::is_long) {
      return l;
    } else if (type_ == eval_type::is_double) {
      return d;
    } else if (type_ == eval_type::is_string) {
      return atol(s);
    }
    __builtin_unreachable(); // LCOV_EXCL_LINE
  }

  const char *to_string(char *buf) const {
    if (type_ == eval_type::is_long) {
      sprintf(buf, "%ld", l);
      return buf;
    } else if (type_ == eval_type::is_double) {
      sprintf(buf, "%f", d);
      return buf;
    }
    return s;
  }

 private:
  enum class eval_type { is_long, is_double, is_string } type_;
  bool is_const_;
};

template <template <class> class Op>
eval_value apply(char *tree, eval_value &lhs, eval_value &rhs) {
  auto use_double = lhs.is_double() || rhs.is_double();
  if (use_double) {
    return eval_value(tree, Op<double>()(lhs.to_double(), rhs.to_double()));
  } else {
    return eval_value(tree, Op<long>()(lhs.to_long(), rhs.to_long()));
  }
}

static eval_value boolev_field(FBFR32 *fbfr, char *tree, FLDID32 fieldid,
                               FLDOCC32 oc) {
  switch (Fldtype32(fieldid)) {
    case FLD_SHORT:
    case FLD_LONG: {
      auto value = reinterpret_cast<long *>(
          CFfind32(fbfr, fieldid, oc, nullptr, FLD_LONG));
      if (value != nullptr) {
        return eval_value(tree, *value);
      } else {
        return eval_value(tree, 0);
      }
    }
    case FLD_FLOAT:
    case FLD_DOUBLE: {
      auto value = reinterpret_cast<double *>(
          CFfind32(fbfr, fieldid, oc, nullptr, FLD_DOUBLE));
      if (value != nullptr) {
        return eval_value(tree, *value);
      } else {
        return eval_value(tree, 0.0);
      }
    }
    case FLD_CHAR:
    case FLD_STRING:
    case FLD_CARRAY: {
      auto value = reinterpret_cast<char *>(
          CFfind32(fbfr, fieldid, oc, nullptr, FLD_STRING));
      if (value != nullptr) {
        return eval_value(tree, value);
      } else {
        return eval_value(tree, "");
      }
    }
    default:
      throw std::runtime_error("unsupported field type");
  }
}

static eval_value boolev_cmp(char *tree, char op, eval_value lhs,
                             eval_value rhs) {
  auto lexical_compare = lhs.is_const_string() || rhs.is_const_string() ||
                         (lhs.is_string() && rhs.is_string());
  if (lexical_compare && op != matches && op != not_matches) {
    char lbuf[64], rbuf[64];
    lhs = eval_value(nullptr, strcmp(lhs.to_string(lbuf), rhs.to_string(rbuf)));
    rhs = eval_value(nullptr, 0);
  }

  if (op == less_than) {
    return apply<std::less>(tree, lhs, rhs);
  } else if (op == greater_than) {
    return apply<std::greater>(tree, lhs, rhs);
  } else if (op == less_or_equal) {
    return apply<std::less_equal>(tree, lhs, rhs);
  } else if (op == greater_or_equal) {
    return apply<std::greater_equal>(tree, lhs, rhs);
  } else if (op == equal) {
    return apply<std::equal_to>(tree, lhs, rhs);
  } else if (op == not_equal) {
    return apply<std::not_equal_to>(tree, lhs, rhs);
  } else if (op == matches) {
    regex_t regex;
    if (regcomp(&regex, rhs.s, 0) == 0) {
      regmatch_t m[1];
      auto r = regexec(&regex, lhs.s, 1, m, 0);
      regfree(&regex);
      return eval_value(tree, r == 0 && m[0].rm_so == 0);
    }
  } else if (op == not_matches) {
    regex_t regex;
    if (regcomp(&regex, rhs.s, 0) == 0) {
      regmatch_t m[1];
      auto r = regexec(&regex, lhs.s, 1, m, 0);
      regfree(&regex);
      return eval_value(tree, r == REG_NOMATCH || m[0].rm_so != 0);
    }
  }
  throw std::runtime_error("unsupported comparison operator");
}

eval_value boolev(FBFR32 *fbfr, char *tree) {
  char op = *tree++;
  if (op == const_long) {
    long value;
    std::copy_n(tree, sizeof(value), reinterpret_cast<char *>(&value));
    return eval_value(tree + sizeof(value), value);
  } else if (op == const_double) {
    double value;
    std::copy_n(tree, sizeof(value), reinterpret_cast<char *>(&value));
    return eval_value(tree + sizeof(value), value);
  } else if (op == const_string) {
    auto len = strlen(tree);
    return eval_value(tree + len + 1, tree, /*is_const=*/true);
  } else if (op == field) {
    FLDID32 fieldid;
    std::copy_n(tree, sizeof(fieldid), reinterpret_cast<char *>(&fieldid));
    tree += sizeof(fieldid);
    FLDOCC32 oc;
    std::copy_n(tree, sizeof(oc), reinterpret_cast<char *>(&oc));
    tree += sizeof(oc);

    return boolev_field(fbfr, tree, fieldid, oc);
  } else if (op == field_any) {
    throw std::runtime_error("unsupported use of '?' field subscript");
  } else if (op >= unary_minus && op <= bitwise_negation) {
    auto val = boolev(fbfr, tree);
    tree = val.tree;
    if (op == unary_minus) {
      return eval_value(tree, -val.to_long());
    } else if (op == logical_negation) {
      return eval_value(tree, !val.to_long());
    } else if (op == bitwise_negation) {
      return eval_value(tree, ~val.to_long());
    }
  } else if (op >= multiplication && op < less_than) {
    auto lhs = boolev(fbfr, tree);
    tree = lhs.tree;
    auto rhs = boolev(fbfr, tree);
    tree = rhs.tree;

    if (op == multiplication) {
      return apply<std::multiplies>(tree, lhs, rhs);
    } else if (op == division) {
      return apply<std::divides>(tree, lhs, rhs);
    } else if (op == modulus) {
      // Fail on double?
      return eval_value(tree, lhs.to_long() % rhs.to_long());
    } else if (op == addition) {
      return apply<std::plus>(tree, lhs, rhs);
    } else if (op == substraction) {
      return apply<std::minus>(tree, lhs, rhs);

    } else if (op == exclusive_or) {
      // Fail on double?
      return eval_value(tree, lhs.to_long() ^ rhs.to_long());
    } else if (op == logical_and) {
      return apply<std::logical_and>(tree, lhs, rhs);
    } else if (op == logical_or) {
      return apply<std::logical_or>(tree, lhs, rhs);
    }
  } else if (op >= less_than && op < last_invalid) {
    // Try many LHS values until TRUE
    if (*tree == field_any) {
      tree++;
      FLDID32 fieldid;
      std::copy_n(tree, sizeof(fieldid), reinterpret_cast<char *>(&fieldid));
      tree += sizeof(fieldid);
      auto rhs = boolev(fbfr, tree);
      tree = rhs.tree;

      FLDOCC32 count = Foccur32(fbfr, fieldid);
      for (FLDOCC32 oc = 0; oc < count; oc++) {
        auto lhs = boolev_field(fbfr, nullptr, fieldid, oc);
        auto ret = boolev_cmp(tree, op, lhs, rhs);
        if (ret.to_long() != 0) {
          return ret;
        }
      }
      return eval_value(tree, 0);
    } else {
      auto lhs = boolev(fbfr, tree);
      tree = lhs.tree;
      auto rhs = boolev(fbfr, tree);
      tree = rhs.tree;

      return boolev_cmp(tree, op, lhs, rhs);
    }
  }
  throw std::runtime_error("Unsupported opcode");
}

int Fboolev32(FBFR32 *fbfr, char *tree) {
  if (fbfr == nullptr) {
    FERROR(FNOTFLD, "fbfr is NULL");
    return -1;
  }
  if (tree == nullptr) {
    FERROR(FNOTFLD, "tree is NULL");
    return -1;
  }
  auto v = boolev(fbfr, tree + 4);
  return v.to_long() != 0;
}

double Ffloatev32(FBFR32 *fbfr, char *tree) {
  if (fbfr == nullptr) {
    FERROR(FNOTFLD, "fbfr is NULL");
    return -1;
  }
  if (tree == nullptr) {
    FERROR(FNOTFLD, "tree is NULL");
    return -1;
  }
  auto v = boolev(fbfr, tree + 4);
  return v.to_double();
}
