# Arete grammar (EBNF)

Written in phase 2, grown since. Each rule below is one function in
`src/parser.c`, or one loop inside its parent's function where the rule is
only ever used in one place (`arguments` lives inside `parse_call`; binary
operators become rows of the `INFIX` table of the Pratt loop). Terminals in `UPPER_CASE` are tokens from
`src/lexer.c`. `{ }` means zero or more, `[ ]` means optional, `|` means or.

The right-hand column says which phase implements the rule; rules marked 3
to 7 are in the code now. `%` and `%=` are missing on purpose: they are the
phase 4 exercise.

## Rules

```
program        = HEADER { statement } EOF ;                                   (3)
repl_line      = { statement } EOF ;                     what the REPL parses  (3)
HEADER         = "arete" INT "." INT NEWLINE ;      checked before tokenizing  (3)

statement      = let_stmt | assign_stmt | if_stmt | while_stmt | for_stmt
               | fn_decl | return_stmt | break_stmt | continue_stmt
               | expr_stmt ;                                                  (7)
if_stmt        = "if" expression ":" block
                 { "elif" expression ":" block } [ "else" ":" block ] ;    (6)
while_stmt     = "while" expression ":" block ;                              (7)
for_stmt       = "for" IDENT "in" expression ":" block ;                     (7)
fn_decl        = "fn" IDENT "(" [ params ] ")" [ "->" type ] ":" block ;     (7)
params         = param { "," param } ;                                       (7)
param          = IDENT ":" type [ "=" expression ] ;                         (7)
return_stmt    = "return" [ expression ] NEWLINE ;                           (7)
break_stmt     = "break" NEWLINE ;                                           (7)
continue_stmt  = "continue" NEWLINE ;                                        (7)
block          = NEWLINE INDENT statement { statement } DEDENT ;            (6)
let_stmt       = ( "let" | "var" ) IDENT [ ":" type ] "=" expression NEWLINE ; (5)
assign_stmt    = IDENT assign_op expression NEWLINE ;                        (5)
assign_op      = "=" | "+=" | "-=" | "*=" | "/=" | "//=" ;          (5; `%=` pending)
expr_stmt      = expression NEWLINE ;                                         (3)

expression     = ternary ;                                                    (6)
ternary        = or [ "if" or "else" expression ] ;                            (6)
or             = and { "or" and } ;                                            (6)
and            = not { "and" not } ;                                           (6)
not            = "not" not | comparison ;                                      (6)
comparison     = range [ ( "==" | "!=" | "<" | "<=" | ">" | ">=" ) range ] ;  (6)
range          = additive [ ( ".." | "..=" ) additive ] ;                    (7)
additive       = multiplicative { ( "+" | "-" ) multiplicative } ;            (4)
multiplicative = unary { ( "*" | "/" | "//" ) unary } ;               (4; `%` pending)
unary          = "-" unary | power ;                                          (4)
power          = postfix [ "**" unary ] ;                                     (4)
postfix        = primary { call } ;                        (3; any primary from 7)
call           = "(" [ arguments ] ")" ;                                      (3)
arguments      = argument { "," argument } ;                                  (7)
argument       = [ IDENT "=" ] expression ;              (7: named arguments last)
primary        = STRING | IDENT                                               (3)
               | INT | FLOAT | "(" expression ")"                             (4)
               | "true" | "false"                                             (6)
               | lambda ;                                                     (7)
lambda         = "fn" "(" [ lambda_params ] ")" [ "->" type ] "=>" expression ; (7)
lambda_params  = lambda_param { "," lambda_param } ;                          (7)
lambda_param   = IDENT [ ":" type ] ;                                         (7)

type           = union_type ;                                                 (5)
union_type     = postfix_type { "|" postfix_type } ;                          (5)
postfix_type   = primary_type { "?" } ;                                       (5)
primary_type   = IDENT [ "<" type { "," type } ">" ]                          (5)
               | "fn" "(" [ type { "," type } ] ")" [ "->" type ] [ "throws" [ IDENT ] ]
               | "(" type ")" ;
```

The levels from `or` down to `power` are one Pratt loop in `parse_binary`,
driven by the table `INFIX` in `src/parser.c`, which mirrors the precedence
table in SPEC.md section 3; `not` and unary `-` are prefix operators in
`parse_prefix`; `ternary` is `parse_expression`. `range` is a row of that
table at level 8 (the `??` of phase 16 goes between `comparison` and `range`);
it has no repetition, a second range operator is the error `a range cannot be
an end of another range`. The lambda body is `expression`, the lowest level:
it extends to the next `,`, `)` or end of line. `comparison` has no
repetition on purpose: a second comparison operator in a row is the error
`chained comparison is not allowed`. `not` is reachable only from `and`, so
the parser refuses it as an operand of any higher level (`1 == not x`). The
parser also refuses expressions more than 1000 operators deep, nesting and
chains both counted, so later recursive passes never overflow the C stack.

## Checks

**First tokens of each alternative.** Two alternatives of one rule must not
start with the same token, or the parser needs more than one token of lookahead.

| Rule | Alternative | First tokens |
|---|---|---|
| statement | let_stmt | `let`, `var` |
| statement | assign_stmt | IDENT |
| statement | if_stmt | `if` |
| statement | while_stmt, for_stmt | `while`, `for` |
| statement | fn_decl | `fn` IDENT |
| statement | return_stmt, break_stmt, continue_stmt | `return`, `break`, `continue` |
| statement | expr_stmt | STRING, IDENT, INT, FLOAT, `(`, `-`, `not`, `true`, `false`, `fn` `(` |
| primary | each alternative | STRING / IDENT / INT / FLOAT / `(` / `true` / `false` / `fn` |
| argument | named vs positional | IDENT `=` vs anything else |
| not | `"not" not` vs comparison | `not` vs first(comparison), which never contains `not` |
| unary | `"-" unary` vs power | `-` vs first(primary) |
| primary_type | each alternative | IDENT / `fn` / `(` |

Three collisions, each resolved with a second token of lookahead, decided
once. `assign_stmt` and `expr_stmt` both start with IDENT (phase 5): an
`assign_op` as the second token means `assign_stmt`. `fn_decl` and a lambda
`expr_stmt` both start with `fn` (phase 7): IDENT as the second token means
`fn_decl`, `(` means a lambda. A named `argument` and a positional one that
starts with a name (phase 7): `=` as the second token means named.

**Left recursion.** None. Repetition is written with `{ }`, which becomes a
loop, never a rule that starts with itself.

**Associativity and precedence.** `additive` and `multiplicative` loop to the
right, so `10 - 3 - 2` is `(10 - 3) - 2`: left-associative. `power` takes
`unary` on its right, so `2 ** 3 ** 2` is `2 ** (3 ** 2)` (right-associative)
and `2 ** -1` parses. `unary` is above `multiplicative` and below `power`, so
`-7 // 2` is `(-7) // 2` and `-2 ** 2` is `-(2 ** 2)`, matching SPEC.md and
`examples/pending/02_arithmetic.rt`.

**What ends an expression.** NEWLINE, `)`, `,`, `:`, `else`, EOF. None of
these can start an expression, so the Pratt loop stops without lookahead.
`in` never meets the expression parser: the `for` target is a single IDENT
read by `parse_for` itself.
`if` after an expression starts the ternary form, which is why `x if c`
without `else` is an error rather than two statements.

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
- `Lambda = "fn" "(" [ params ] ")" [ "->" Type ] "=>" Expr`: the body has the lowest precedence and stops at `, ) ] } NEWLINE ->`. Phase 7 kept the optional `-> Type` and decided against block lambdas: a multi-line body is a nested named `fn`.
- At statement start, `fn` followed by `IDENT` is a declaration; `fn` followed by `(` is an expression.
- `is` switches to the type parser for its right operand.
- Assignment is a statement: `AssignStmt = Target ( "=" | "+=" | ... ) Expr NEWLINE`. Inside an argument list, `IDENT "="` (not `==`) is a named argument.

Statements

- `ForStmt = "for" Target "in" Expr ":" Block ; Target = IDENT | "_" | "(" Target { "," Target } ")"`. The target is parsed by this pattern rule, never by the expression parser, so `in` the operator and `in` the separator never meet. Phase 7 implements `IDENT` (which includes `_`); the tuple target waits for destructuring in phase 9.
- `when` as a value: `LetStmt = ("let" | "var") IDENT [ ":" Type ] "=" ( WhenExpr | Expr NEWLINE )`, same for `ReturnStmt` and `AssignStmt`. A statement whose right-hand side is a `when` ends on `DEDENT`, not `NEWLINE`. A `when` inside brackets or as an operand is an error.
- `WhenBranch = Pattern "->" Expr NEWLINE` as the placeholder; branch bodies that are statements, and block branches `-> :`, are decided in phase 9.
- Class header shapes: `ClassDecl = [ "open" | "data" ] "class" IDENT [ TypeParams ] [ "(" Params ")" ] [ ":" Super { "," Super } ] ( ":" Block | NEWLINE )`. After `:`, peek one token: `NEWLINE` means a body follows, anything else means a supertype list. Inside `interface`, a `FnDecl` may end at `NEWLINE` without a body.
- The SPEC sentence "a line ending in `:` opens a block" is a lexer-level description; it is false inside brackets and for `let word = when x:`, where the block belongs to an expression.

Types

- The result of a `fn` type is greedy; `throws` binds to the innermost `fn`; `null` is not a type name; `->` is in no FOLLOW set of `Type`, so `is fn() -> int -> print("f")` in a `when` branch is unambiguous.
- Explicit type arguments at a call (`json<User>()`) are the one genuine ambiguity with one token of lookahead. Options for phase 9: turbofish `json::<User>()`, or speculative lookahead (a balanced `<...>` containing only type tokens and followed by `(` or `.` is a generic call). Until then no example needs the form except where marked `OPEN(9)`.

## Notes collected in phase 9 (stage C syntax, decided; rules are sketches until their phase)

- Member access and method calls: `postfix = primary { call | "[" expression "]" | "." IDENT [ call ] }`. `s.len()` is a call on a member; there are no free functions over strings and collections. `print` and `str` are the only built-in functions.
- Explicit type arguments at a call: after IDENT, `<` starts type arguments only when the tokens up to the matching `>` parse as types and `(` follows (`json<User>()`); otherwise `<` is the comparison. `a < b > (c)` is refused, which `chained comparison is not allowed` already does.
- Literals (phase 12): `list_literal = "[" [ expression { "," expression } ] "]"`, `map_literal = "{" [ entry { "," entry } ] "}"`, `entry = expression ":" expression`. `{}` is an empty Map. Inside `[` and `{` the lexer suppresses NEWLINE, as inside `(`. A literal takes the expected type (`MutableList<int>` from an annotation), `List`/`Map` otherwise.
- Raw strings (phase 11): `r"..."` lexes as a STRING with no escapes and no interpolation parts.
- `when` (phase 13): `when_stmt = "when" [ expression ] ":" NEWLINE INDENT branch { branch } DEDENT`, `branch = pattern { "," pattern } "->" ( simple_stmt NEWLINE | expression NEWLINE | NEWLINE block )`, `pattern = expression | "in" range | "is" type | "null" | "else"`. A `when` in value position (`let x = when ...`) is parsed by the statement parser from `let_stmt`'s right-hand side and requires an expression branch body.
- Errors (phase 14): `throw_stmt = "throw" expression NEWLINE`, `try_stmt = "try" ":" block { "catch" IDENT [ "is" type ] ":" block } [ "finally" ":" block ]`, at least one `catch` or a `finally`. `fn_decl` gains `[ "throws" ]` after the result type (the token already exists for types).
- Classes (phase 15): `class_decl = [ "open" | "data" ] "class" IDENT [ type_params ] [ "(" params ")" ] [ ":" parent { "," parent } ] ":" block`, `parent = IDENT [ "(" arguments ")" ]`; after the first `:`, NEWLINE means "no parents, body follows". A method is a `fn_decl` inside the block, optionally prefixed by `override`. `interface_decl = "interface" IDENT ":" NEWLINE INDENT fn_signature { fn_signature } DEDENT`. `enum_decl = "enum" IDENT ":" NEWLINE INDENT IDENT NEWLINE { IDENT NEWLINE } DEDENT`. `data`, `open`, `override` are contextual: identifiers elsewhere.
- Destructuring (phases 12 and 15): `let_stmt` and `for_stmt` accept `"(" target { "," target } ")"` where each target is IDENT or `_`; a data class destructures by field order, a Map entry as (key, value).
- Modules (phase 18): `use_stmt = "use" STRING [ "as" IDENT | "{" IDENT { "," IDENT } "}" ] [ "allow" "[" [ IDENT { "," IDENT } ] "]" ] NEWLINE`. Only at the top of a file, before other statements.
- Generics (phase 17): `type_params = "<" IDENT [ ":" type ] { "," IDENT [ ":" type ] } ">"` after the name of a `fn` or `class`.
