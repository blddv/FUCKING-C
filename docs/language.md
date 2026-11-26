# Fucking C language reference

[Русская семантика](language.ru.md) · [README](../README.md)

## Lexical rules

Source is UTF-8. Every token is separated by whitespace, including punctuation and operators. Newlines are only separators; statements end in `ċ`. There are no source comments. A BOM is accepted.

Cyrillic `С/с` are accepted as Latin `C/c`. Recognized decomposed forms of acute, circumflex, dot above, caron, and cedilla normalize to `Ć/ć`, `Ĉ/ĉ`, `Ċ/ċ`, `Č/č`, and `Ç/ç`. This is normalization of the language's finite alphabet, not a general Unicode NFC service. Unknown lookalikes or decorations are errors.

## Tokens

### Statements and punctuation

| Token | Meaning |
|---|---|
| `C` | `let`: declare a variable |
| `c` | `fn`: named or anonymous function |
| `CC` / `cc` | `if` / `else` |
| `Cc` | `while` |
| `CCC` / `CCc` | `for` / `in` |
| `cC` | `return` |
| `CcC` / `Ccc` | `break` / `continue` |
| `Ć` / `ć` | `(` / `)` |
| `Ĉ` / `ĉ` | `{` / `}` |
| `Č` / `č` | `[` / `]` |
| `Ċ` | comma |
| `ċ` | statement terminator |
| `Ç` | assignment |

### Operators and constants

| Token | Meaning |
|---|---|
| `C̄` / `c̄` | `+` / `-`, including unary signs |
| `C̈` / `c̈` | `*` / `/` |
| `c̃` / `C̃` | floor division `//` / remainder `%` |
| `C̈C` | exponentiation `**` |
| `ÇÇ` / `çç` | `==` / `!=` |
| `C̲` / `C̲C` | `<` / `<=` |
| `c̲` / `c̲c` | `>` / `>=` |
| `cCC` / `cCc` / `ccC` | `and` / `or` / `not` |
| `C̆` / `c̆` / `C̊` | `true` / `false` / `null` |

### Names and literals

- **Identifier:** `ƈ[cC]+`. Names are case-sensitive and the entire normalized token identifies the binding.
- **Integer:** `Ƈ[cC]+`. Binary, with `c=0`, `C=1`, most significant bit first. A negative sign is a separate `c̄` token.
- **String:** `Ȼ[cC]*`. Eight bits per UTF-8 byte, most significant bit first. Byte count must be integral and the decoded UTF-8 valid. `Ȼ` is the empty string.
- **Float:** produced by `/` or the `float` builtin. `fc literal float VALUE` emits a rational expression representing a finite binary64 value exactly.

Examples: `ƇCcCcCc` is 42; `c̄ ƇCC` is -3; `ȻcCccccCC` is the string `C`.

Encoding helpers:

```sh
./fc literal int 42
./fc literal float 2.5
./fc literal str 'Hello, C!'
./fc literal name counter
```

The helper for names encodes ordinary UTF-8 text into a C-only identifier. Short names such as `ƈC` may also be chosen directly. Runtime strings and I/O are not restricted to C characters.

### Combining marks

| Form | Mark |
|---|---|
| `C̄` / `c̄` | U+0304 macron |
| `C̈` / `c̈` | U+0308 diaeresis |
| `C̃` / `c̃` | U+0303 tilde |
| `C̲` / `c̲` | U+0332 low line |
| `C̶` / `c̶` | U+0336 long stroke overlay |
| `C̆` / `c̆` | U+0306 breve |
| `C̊` | U+030A ring above |

`Ƈ`, `ƈ`, and `Ȼ` are U+0187, U+0188, and U+023B. Copy exact tokens from the reference or `./fc syntax` instead of choosing a visually similar Unicode character.

## Grammar

The uppercase labels below name token kinds, not alternative source keywords.

```text
program       = statement* EOF
statement     = LET IDENT ASSIGN expression SEMI
              | FN IDENT parameters block
              | IF condition block (ELSE (if-statement | block))?
              | WHILE condition block
              | FOR LPAREN IDENT IN expression RPAREN block
              | RETURN expression? SEMI
              | BREAK SEMI | CONTINUE SEMI
              | block
              | expression (ASSIGN expression)? SEMI
block         = LBRACE statement* RBRACE
condition     = LPAREN expression RPAREN
parameters    = LPAREN (IDENT (COMMA IDENT)* COMMA?)? RPAREN
primary       = NUMBER | STRING | TRUE | FALSE | NULL | IDENT | BUILTIN
              | LPAREN expression RPAREN
              | LBRACKET (expression (COMMA expression)* COMMA?)? RBRACKET
              | FN parameters block
postfix       = primary (call | index)*
call          = LPAREN (expression (COMMA expression)* COMMA?)? RPAREN
index         = LBRACKET expression RBRACKET
```

Assignment is a statement, not an expression. Its target must be a name or a list index. The right-hand value is evaluated before the target's container and index. Function parameters must be unique. Trailing commas are accepted.

`return` is valid only inside a function. `break/continue` require a loop in that same function; a function defined inside another loop cannot control the outer loop. The parser checks these conditions before execution.

## Precedence

Lowest to highest:

1. `or`
2. `and`
3. unary `not`
4. chained `== != < <= > >=`
5. `+ -`
6. `* / // %`
7. unary `+ -`
8. `**`, right-associative
9. calls and indexing, left-to-right

Thus `2 ** 3 ** 2 = 512`, `-2 ** 2 = -4`, and `(-2) ** 2 = 4`. Comparisons are chains: each needed operand is evaluated once, and evaluation stops at the first false comparison. `not a == b` means `not (a == b)`.

False values are false, null, numeric zero, an empty string, and an empty list. `and/or` return an operand and short-circuit; `not` returns a bool.

## Values

- **int:** signed integer up to 4096 significant bits, implemented with Boost.Multiprecision. No byte or int64 wraparound.
- **float:** finite IEEE 754 binary64. `/` returns float, including for integer operands. Large integer division rounds directly to binary64 and supports subnormals. NaN and infinity are rejected.
- **bool:** not an arithmetic integer; `true == 1` is false and bool is not a valid index.
- **null:** absence of a result, implicit return, or input EOF.
- **str:** immutable Unicode code points stored in UTF-8. Length/indexing do not count bytes or grapheme clusters.
- **list:** mutable heterogeneous values, shared by reference.
- **function:** a callable user function, closure, or builtin.

Integer floor division rounds down; remainder is consistent with it. Negative powers can yield floats. Complex results are unsupported. Mixed int/float arithmetic requires a representable int-to-float conversion. Comparison of int and float instead uses the float's exact rational value, so large neighboring integers are not accidentally equal after rounding.

`+` concatenates two strings or two lists. `*` can repeat a string/list by an integer on either side; nonpositive repetition produces an empty value. List repetition and concatenation are shallow. A string and number do not concatenate implicitly.

Lists compare structurally, with cyclic lists supported. Functions compare by identity. Ordering compares numbers or two strings. Negative indices count from the end. Out-of-range indices are errors. Strings cannot be assigned through an index, and slicing is unsupported.

## Variables, functions, and loops

Declarations execute sequentially, after evaluating their initializer. There is no hoisting. Duplicate declarations in the current scope fail; nested scopes may shadow a name.

Assignment changes the nearest existing binding. A function captures its definition environment, not the caller's environment. Captured environments remain live: later mutations and declarations in them are visible to later calls. There is no separate static resolver or temporal dead zone.

Arguments are evaluated left-to-right and must match the parameter count. Calls create local parameter environments. Functions can be passed, returned, and stored in lists. `return` exits a function through nested loops; falling off the body or returning without an expression yields null.

Each while iteration has a fresh block environment. For iterates a snapshot of a string/list and creates a distinct loop binding per iteration, so closures retain distinct iteration values. Mutation of the original list does not extend the iteration. The loop variable does not escape its scope.

## Builtins

| Token | Signature | Result / behavior |
|---|---|---|
| `C̶` | `print(...)` | Space-separated values plus LF; returns null |
| `c̶` | `input([prompt])` | Reads one UTF-8 line; EOF → null |
| `C̶C` | `len(value)` | String code points or list elements |
| `C̶c` | `append(list, value)` | Mutates list; returns null |
| `C̶CC` | `pop(list[, index])` | Removes and returns an element; default index -1 |
| `C̶Cc` | `str(value)` | Formatted string representation |
| `C̶cC` | `int(value)` | Decimal string, number, or bool → int; truncates float toward zero |
| `C̶cc` | `float(value)` | String, number, or bool → finite float |
| `C̶CCC` | `range(stop)` / `range(start, stop[, step])` | Integer list; excludes stop; nonzero step |
| `C̶CCc` | `type(value)` | `int`, `float`, `bool`, `null`, `str`, `list`, or `function` |
| `C̶CcC` | `assert(condition[, message])` | Runtime error on a false condition; otherwise null |

`input` removes one terminating LF/CRLF but preserves other whitespace. An optional prompt is printed without a newline first. Invalid UTF-8 input is a runtime error.

`print` does not quote top-level strings, but quotes/escapes strings inside lists. Booleans/null print as `true/false/null`; cycles print as `[...]`. Float formatting is a short round-trippable binary64 representation, with `.0` retained for integral floats.

## Runtime ownership and failures

The C++ runtime owns environments, lists, and user functions. A tracing mark-and-sweep pass runs after each completed or failed program/REPL command, rooted at globals and the last result. It reclaims unreachable cycles. Collection does not run while temporary evaluation values are on the C++ stack. AST/source ownership allows REPL functions to outlive the temporary parsed command.

A syntax error prevents the entire file from executing. A runtime error stops execution but preserves earlier output. A print call constructs a bounded representation before writing it. REPL errors do not roll back mutations. User-defined exception handlers are not supported.

Coordinates count Unicode code points starting at 1. Diagnostics limit displayed line length and account for the language's combining marks. Up to 12 call sites are shown. Invalid source UTF-8 may only have an approximate source position plus a byte/error description.

## Resource limits

| Resource | Limit |
|---|---|
| Source | 1,000,000 code points; file at most 4,000,004 bytes |
| Integer / integer literal | 4096 bits, including leading literal bits |
| String | 1,000,000 code points |
| List / range result | 100,000 elements |
| Parameters / arguments | 255 |
| AST / parser depth | 256 / 512 |
| Active evaluator depth | 1024 |
| Formatting/comparison value nesting | 256 |
| Default execution steps | 1,000,000 |
| Default nested user calls | 128 |
| Default output | 1,048,576 UTF-8 bytes |

Steps count visited statements/expressions and for iterations, not source characters or CPU instructions. `--max-depth` changes the user-call limit, not the other depth bounds. REPL step/output budgets reset for each command.

These bounds are convenience/resource guards, **not a security sandbox for untrusted code**.
