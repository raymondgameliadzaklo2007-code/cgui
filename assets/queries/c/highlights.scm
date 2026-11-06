;; Tree-sitter query for C syntax highlighting (Helix & Neovim compatible)

;; Keywords
[
  "sizeof"
  "offsetof"
  "alignof"
  "_Alignof"
  "asm"
  "__asm__"
] @keyword

[
  "enum"
  "struct"
  "typedef"
  "union"
] @keyword.storage.type

[
  "const"
  "static"
  "extern"
  "inline"
  "volatile"
  "register"
  "auto"
  "restrict"
] @keyword.storage.modifier

[
  "goto"
  "break"
  "continue"
] @keyword.control

[
  "do"
  "for"
  "while"
] @keyword.control.repeat

[
  "if"
  "else"
  "switch"
  "case"
  "default"
] @keyword.control.conditional

"return" @keyword.control.return

;; Directives & Include
"#include" @include

[
  "defined"
  "#define"
  "#elif"
  "#else"
  "#endif"
  "#if"
  "#ifdef"
  "#ifndef"
  "#elifdef"
  "#elifndef"
  "#undef"
  "#pragma"
  "#error"
  "#warning"
  (preproc_directive)
] @keyword.directive

;; Operators
[
  "+"
  "-"
  "*"
  "/"
  "++"
  "--"
  "%"
  "=="
  "!="
  ">"
  "<"
  ">="
  "<="
  "&&"
  "||"
  "!"
  "&"
  "|"
  "^"
  "~"
  "<<"
  ">>"
  "="
  "+="
  "-="
  "*="
  "/="
  "%="
  "<<="
  ">>="
  "&="
  "^="
  "|="
  "?"
  "->"
] @operator

;; Brackets and Delimiters
[
  "{"
  "}"
  "("
  ")"
  "["
  "]"
] @punctuation.bracket

[
  ";"
  ","
  ":"
] @punctuation.delimiter

;; Literals & Types
(string_literal) @string
(system_lib_string) @string
(char_literal) @string
(escape_sequence) @string.escape
(number_literal) @constant.numeric
[(true) (false)] @constant.builtin
(null) @constant.builtin

(field_identifier) @variable.other.member
(type_identifier) @type
(primitive_type) @type.builtin
(sized_type_specifier) @type.builtin

;; Function calls & declarators
(call_expression
  function: (identifier) @function)
(call_expression
  function: (field_expression
    field: (field_identifier) @function))

(function_declarator
  declarator: (identifier) @function)

(enumerator name: (identifier) @constant)
(preproc_def name: (identifier) @constant)

(identifier) @variable

(comment) @comment
