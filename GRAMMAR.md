# Arete grammar (EBNF)

The EBNF itself is written in phase 2. Every rule will correspond to one
function in the parser. Below are the parsing decisions already collected in
phase 1 while checking the 20 example programs; phase 2 turns them into rules.

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
- `WhenBranch = Pattern "->" Expr NEWLINE` as the phase-2 placeholder; branch bodies that are statements, and block branches `-> :`, are decided in phase 9.
- Class header shapes: `ClassDecl = [ "open" | "data" ] "class" IDENT [ TypeParams ] [ "(" Params ")" ] [ ":" Super { "," Super } ] ( ":" Block | NEWLINE )`. After `:`, peek one token: `NEWLINE` means a body follows, anything else means a supertype list. Inside `interface`, a `FnDecl` may end at `NEWLINE` without a body.
- The SPEC sentence "a line ending in `:` opens a block" is a lexer-level description; it is false inside brackets and for `let word = when x:`, where the block belongs to an expression.

Types

- `Type` grammar as in SPEC.md section 5. The result of a `fn` type is greedy; `throws` binds to the innermost `fn`; `null` is not a type name; `->` is in no FOLLOW set of `Type`, so `is fn() -> int -> print("f")` in a `when` branch is unambiguous.
- Explicit type arguments at a call (`json<User>()`) are the one genuine ambiguity with one token of lookahead. Options for phase 9: turbofish `json::<User>()`, or speculative lookahead (a balanced `<...>` containing only type tokens and followed by `(` or `.` is a generic call). Until then no example needs the form except where marked `OPEN(9)`.
