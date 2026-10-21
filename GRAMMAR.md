# Arete grammar (EBNF)

Phase 2. Each rule below is one function in `src/parser.c` (or one branch of
the Pratt loop for binary operators). Terminals in `UPPER_CASE` are tokens from
`src/lexer.c`. `{ }` means zero or more, `[ ]` means optional, `|` means or.

The grammar covers the minimum named in ROADMAP phase 2: version header,
numbers, strings, `+ - * / // % **`, parentheses, `let`/`var`, `print`.
The right-hand column says which phase implements the rule; rules marked 3 are
in the code now.

## Rules

```
program        = HEADER { statement } EOF ;                                   (3)
HEADER         = "arete" INT "." INT NEWLINE ;      checked before tokenizing  (3)

statement      = let_stmt | assign_stmt | expr_stmt ;                        (3: expr_stmt only)
let_stmt       = ( "let" | "var" ) IDENT [ ":" type ] "=" expression NEWLINE ; (5)
assign_stmt    = IDENT assign_op expression NEWLINE ;                        (5)
assign_op      = "=" | "+=" | "-=" | "*=" | "/=" | "//=" | "%=" ;             (5)
expr_stmt      = expression NEWLINE ;                                         (3)

expression     = additive ;                      Pratt loop over this table   (4)
additive       = multiplicative { ( "+" | "-" ) multiplicative } ;            (4)
multiplicative = unary { ( "*" | "/" | "//" | "%" ) unary } ;                 (4)
unary          = "-" unary | power ;                                          (4)
power          = postfix [ "**" unary ] ;                                     (4)
postfix        = primary { call } ;                                           (3)
call           = "(" [ arguments ] ")" ;                                      (3)
arguments      = expression { "," expression } ;                              (3)
primary        = STRING | IDENT                                               (3)
               | INT | FLOAT | "(" expression ")" ;                           (4)

type           = union_type ;                                                 (5)
union_type     = postfix_type { "|" postfix_type } ;                          (5)
postfix_type   = primary_type { "?" } ;                                       (5)
primary_type   = IDENT [ "<" type { "," type } ">" ]                          (5)
               | "fn" "(" [ type { "," type } ] ")" [ "->" type ] [ "throws" [ IDENT ] ]
               | "(" type ")" ;
```

In phase 3 `expression` is `postfix` directly; the Pratt loop of phase 4 sits
between them and uses the precedence table in SPEC.md section 3.

## Checks

**First tokens of each alternative.** Two alternatives of one rule must not
start with the same token, or the parser needs more than one token of lookahead.

| Rule | Alternative | First tokens |
|---|---|---|
| statement | let_stmt | `let`, `var` |
| statement | assign_stmt | IDENT |
| statement | expr_stmt | STRING, IDENT, INT, FLOAT, `(`, `-` |
| primary | each alternative | STRING / IDENT / INT / FLOAT / `(` |
| unary | `"-" unary` vs power | `-` vs first(primary) |
| primary_type | each alternative | IDENT / `fn` / `(` |

One collision: `assign_stmt` and `expr_stmt` both start with IDENT. Resolution
(phase 5): when a statement starts with IDENT, look at the second token; if it
is an `assign_op`, parse `assign_stmt`, otherwise `expr_stmt`. Two tokens of
lookahead, decided once, in `parse_statement`.

**Left recursion.** None. Repetition is written with `{ }`, which becomes a
loop, never a rule that starts with itself.

**Associativity and precedence.** `additive` and `multiplicative` loop to the
right, so `10 - 3 - 2` is `(10 - 3) - 2`: left-associative. `power` takes
`unary` on its right, so `2 ** 3 ** 2` is `2 ** (3 ** 2)` (right-associative)
and `2 ** -1` parses. `unary` is above `multiplicative` and below `power`, so
`-7 // 2` is `(-7) // 2` and `-2 ** 2` is `-(2 ** 2)`, matching SPEC.md and
`examples/pending/02_arithmetic.rt`.

**What ends an expression.** NEWLINE, `)`, `,`, EOF. None of these can start
an expression, so the Pratt loop stops without lookahead.

## Notes collected in phase 1

Lexer

- No negative number literals: `-7` is unary `-` applied to `7`. Otherwise `1 -7` and `a - -b` break.
- `1..3` lexes as `INT DOTDOT INT` because `1.` is not a float.
- The lexer never produces `>>`. When the type parser expects `>` and sees `>=`, it splits off one `>`.
- `!=` is one token (longest match); a bare `!` after an identifier is reserved.
- Inside `( [ {` the lexer emits no `NEWLINE`, `INDENT`, `DEDENT`. This is why a block lambda inside parentheses needs a decision in phase 7.
- String interpolation (phase 11): inside `{ ... }` the normal tokenizer runs with a brace-depth counter and a nested-string state, so any expression, including strings of either quote, is allowed. `{{` and `}}` are escapes only in `STRING_PART` state; a lone `}` there is an error.

Expressions

- Pratt parser with the precedence table from SPEC.md section 3.
- Comparison is non-associative: `a < b < c` is the error `chained comparison is not allowed`.
- `Ternary = Or [ "if" Or "else" Expr ]`: right-associative; the middle operand cannot be a bare ternary; `x if c` without `else` is an error.
- `Lambda = "fn" "(" [ params ] ")" [ "->" Type ] "=>" Expr`: the body has the lowest precedence and stops at `, ) ] } NEWLINE ->`. Whether the `-> Type` part exists is decided in phase 7.
- At statement start, `fn` followed by `IDENT` is a declaration; `fn` followed by `(` is an expression.
- `is` switches to the type parser for its right operand.
- Assignment is a statement: `AssignStmt = Target ( "=" | "+=" | ... ) Expr NEWLINE`. Inside an argument list, `IDENT "="` (not `==`) is a named argument.

Statements

- `ForStmt = "for" Target "in" Expr ":" Block ; Target = IDENT | "_" | "(" Target { "," Target } ")"`. The target is parsed by this pattern rule, never by the expression parser, so `in` the operator and `in` the separator never meet.
- `when` as a value: `LetStmt = ("let" | "var") IDENT [ ":" Type ] "=" ( WhenExpr | Expr NEWLINE )`, same for `ReturnStmt` and `AssignStmt`. A statement whose right-hand side is a `when` ends on `DEDENT`, not `NEWLINE`. A `when` inside brackets or as an operand is an error.
- `WhenBranch = Pattern "->" Expr NEWLINE` as the placeholder; branch bodies that are statements, and block branches `-> :`, are decided in phase 9.
- Class header shapes: `ClassDecl = [ "open" | "data" ] "class" IDENT [ TypeParams ] [ "(" Params ")" ] [ ":" Super { "," Super } ] ( ":" Block | NEWLINE )`. After `:`, peek one token: `NEWLINE` means a body follows, anything else means a supertype list. Inside `interface`, a `FnDecl` may end at `NEWLINE` without a body.
- The SPEC sentence "a line ending in `:` opens a block" is a lexer-level description; it is false inside brackets and for `let word = when x:`, where the block belongs to an expression.

Types

- The result of a `fn` type is greedy; `throws` binds to the innermost `fn`; `null` is not a type name; `->` is in no FOLLOW set of `Type`, so `is fn() -> int -> print("f")` in a `when` branch is unambiguous.
- Explicit type arguments at a call (`json<User>()`) are the one genuine ambiguity with one token of lookahead. Options for phase 9: turbofish `json::<User>()`, or speculative lookahead (a balanced `<...>` containing only type tokens and followed by `(` or `.` is a generic call). Until then no example needs the form except where marked `OPEN(9)`.
