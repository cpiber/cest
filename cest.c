#define _GNU_SOURCE
#include <stdio.h>
#include <sys/types.h>
#include <regex.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>

#define DEBUG

#include "array.h"
#include "lexer.h"
#define SV_IMPLEMENTATION
#include "sv.h"

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define POSIX_WORK(name, ...) do                           \
  {                                                        \
    if (name(__VA_ARGS__) < 0) {                           \
      perror(__FILE__ ":" TOSTRING(__LINE__) ":" #name);   \
      exit(1);                                             \
    }                                                      \
  } while(0);
#define UNREACHABLE do { assert(0 && "unreachable"); exit(99); } while(0);

#define INSERT_STR "CEST_MACROS_HERE"

// TODO: does not consider typedef`s without body (i.e. forward defs)
// TODO: implement multiple inheritance


typedef struct {
  String_View defn;
  String_View strt;
  String_View tdef;
  bool hasParent;
  size_t parent;
  MAKE_ARRAY(size_t, inherits)
  const char *loc_start;
  const char *loc_end;
  const char *loc_after;
} StructDef;
typedef struct {
  MAKE_ARRAY(StructDef, items)
  const char *orig;
} StructArr;

#define INITIAL_FILE_CAP 1000
String_View preprocess_file(const char *filename) {
  int fd[2];
  POSIX_WORK(pipe, fd);
  pid_t child = fork();
  if (child < 0) {
    perror("fork");
    exit(1);
  } else if (child == 0) {
    // child process
    POSIX_WORK(close, fd[0]); // close read end
    POSIX_WORK(dup2, fd[1], STDOUT_FILENO); // use pipe as stdout to read in parent process
    POSIX_WORK(execlp, "cc", "cc", "-x", "c", "-fdirectives-only", "-w", "-E", filename, NULL);
    UNREACHABLE
  }
  // parent process
  POSIX_WORK(close, fd[1]); // close write end
  
  size_t size = 0;
  char *ptr = NULL;
  size_t total = 0;
  ssize_t nread = 0;
  do {
    total += nread;
    if (total >= size) {
      size = size == 0 ? INITIAL_FILE_CAP : size * 2; // new size double
      ptr = realloc(ptr, size);
      if (ptr == NULL) {
        perror("realloc preprocess_file");
        exit(1);
      }
    }
  } while ((nread = read(fd[0], ptr + total, size - total)) > 0);
  if (nread < 0) {
    perror("read");
    exit(1);
  }
  POSIX_WORK(close, fd[0]);
  int status;
  POSIX_WORK(waitpid, child, &status, 0);
  if (!WIFEXITED(status)) {
    fprintf(stderr, "child crashed\n");
    exit(1);
  } else if (WEXITSTATUS(status) != 0) {
    fprintf(stderr, "child did not exit normally\n");
    exit(1);
  }
  ptr[total] = 0;
  return (String_View) {
    .count = total,
    .data = ptr,
  };
}

String_View load_file(const char *filename) {
  FILE *f = fopen(filename, "r");
  if (f == NULL) {
    perror("fopen load_file");
    exit(1);
  }
  POSIX_WORK(fseek, f, 0, SEEK_END);
  long size = ftell(f);
  if (size < 0) {
    perror("ftell");
    exit(1);
  }
  POSIX_WORK(fseek, f, 0, SEEK_SET);

  char *ptr = malloc(size + 1);
  if (fread(ptr, size, 1, f) != 1 && ferror(f)) { // read one entire buffer or fail
    perror("fread");
    exit(1);
  }
  POSIX_WORK(fclose, f);
  ptr[size] = 0;
  return (String_View) {
    .count = size,
    .data = ptr,
  };
}

char *struct_to_name(StructDef def, bool include_struct_body) {
  const size_t n = def.strt.count ? sizeof("struct ") - 1 + def.strt.count : 0;
  const size_t m = n && def.tdef.count ? 3 : 0;
  char *fname = malloc(n + def.tdef.count + m + 1 + include_struct_body ? sizeof(" struct body") - 1 : 0);
  if (fname == NULL) {
    perror("malloc filename");
    exit(1);
  }
  if (n) {
    strcpy(fname, "struct ");
    strncat(fname, def.strt.data, def.strt.count);
  }
  if (m) strcat(fname, " / ");
  strncat(fname, def.tdef.data, def.tdef.count);
  if (include_struct_body) strcat(fname, " struct body");
  return fname;
}

bool parse_structdef(Lexer *lexer, String_View *strt, String_View *def, bool *is_struct, String_View *who) {
  bool is_inherit = false;
  TokenOrEnd nameOrParen = lexer_peek_token(lexer);
  if (nameOrParen.has_value && nameOrParen.token.kind == TK_NAME) {
    *strt = nameOrParen.token.content;
    lexer_expect_token(lexer);
  }
  Token paren = lexer_expect_token(lexer);
  if (paren.kind != TK_PAREN)
    lexer_exit_err(paren.loc, stderr, "Expected identifier or `{'");
  if (sv_eq(paren.content, SV("("))) {
    is_inherit = true;
    TokenOrEnd nameOrStruct = lexer_peek_token(lexer);
    if (nameOrStruct.has_value && nameOrStruct.token.kind == TK_STRUCT) {
      if (is_struct) *is_struct = true;
      lexer_expect_token(lexer);
    }
    Token name = lexer_expect_token(lexer);
    if (name.kind != TK_NAME)
      lexer_exit_err(paren.loc, stderr, "Expected identifier or `struct'");
    if (who) *who = name.content;
    Token closeParen = lexer_expect_token(lexer);
    if (closeParen.kind != TK_PAREN || !sv_eq(closeParen.content, SV(")")))
      lexer_exit_err(closeParen.loc, stderr, "Expected `)'");
    paren = lexer_expect_token(lexer);
  }
  if (paren.kind != TK_PAREN || !sv_eq(paren.content, SV("{")))
    lexer_exit_err(paren.loc, stderr, "Expected identifier or `{'");
  
  *def = lexer->content;
  TokenOrEnd ntoken = lexer_peek_token(lexer);
  while (ntoken.has_value) {
    // TODO: maybe error check definition contents
    if (ntoken.token.kind == TK_PAREN && sv_eq(ntoken.token.content, SV("}")))
      break;
    lexer_expect_token(lexer);
    ntoken = lexer_peek_token(lexer);
  }
  Token closeParen = lexer_expect_token(lexer);
  if (closeParen.kind != TK_PAREN || !sv_eq(closeParen.content, SV("}")))
    lexer_exit_err(closeParen.loc, stderr, "Expected `}'");
  def->count = closeParen.content.data - def->data;
  return is_inherit;
}

void parse_struct(StructArr *structs, Lexer *lexer) {
  String_View strt = {0}, def = {0};
  if (parse_structdef(lexer, &strt, &def, NULL, NULL)) return;
  
  StructDef item = (StructDef) {
    .defn = def,
    .strt = strt,
  };
  ARRAY_PUSH(*structs, items, item);
}

void parse_typedef(StructArr *structs, Lexer *lexer) {
  Token token = lexer_expect_token(lexer);
  if (token.kind != TK_STRUCT) return;
  String_View strt = {0}, def = {0};
  if (parse_structdef(lexer, &strt, &def, NULL, NULL)) return;
  
  TokenOrEnd nameOrSemi = lexer_peek_token(lexer);
  String_View tpdef;
  if (nameOrSemi.has_value && nameOrSemi.token.kind == TK_NAME) {
    tpdef = nameOrSemi.token.content;
    lexer_expect_token(lexer);
  }
  
  StructDef item = (StructDef) {
    .defn = def,
    .strt = strt,
    .tdef = tpdef,
  };
  ARRAY_PUSH(*structs, items, item);
}

StructArr collect_structs(const char *filename) {
  String_View file = preprocess_file(filename);
  assert(file.data[file.count] == 0);

  StructArr structs = {
    .orig = file.data,
  };
  char *fname = malloc(strlen(filename) + sizeof(" (preprocessed)"));
  if (fname == NULL) {
    perror("malloc filename");
    exit(1);
  }
  strcpy(fname, filename);
  strcat(fname, " (preprocessed)");
  Lexer lexer = lexer_create(sv_from_cstr(fname), file);
  size_t depth = 0;

  TokenOrEnd token = lexer_get_token(&lexer);
  for (; token.has_value; token = lexer_get_token(&lexer)) {
    Token t = token.token;
    if (t.kind == TK_PAREN && sv_eq(t.content, SV("}"))) {
      depth -= 1;
      continue;
    }
    if (t.kind == TK_PAREN && sv_eq(t.content, SV("{"))) {
      depth += 1;
      continue;
    }
    if (t.kind == TK_TYPEDF) {
      parse_typedef(&structs, &lexer);
      continue;
    }
    if (t.kind == TK_STRUCT) {
      parse_struct(&structs, &lexer);
      continue;
    }
    // ignore everything else
  }
  if (depth != 0)
    lexer_exit_err(lexer.loc, stderr, "Unclosed block");
  
  free(fname);
  return structs;
}

bool parse_struct_inherit(Lexer *lexer, String_View *strt, String_View *def, bool *is_struct, String_View *who) {
  return parse_structdef(lexer, strt, def, is_struct, who);
}

bool parse_typedef_inherit(Lexer *lexer, String_View *strt, String_View *def, String_View *tpdef, bool *is_struct, String_View *who) {
  Token token = lexer_expect_token(lexer);
  if (token.kind != TK_STRUCT) return false;
  if (!parse_structdef(lexer, strt, def, is_struct, who)) return false;
  
  TokenOrEnd nameOrSemi = lexer_peek_token(lexer);
  if (nameOrSemi.has_value && nameOrSemi.token.kind == TK_NAME) {
    *tpdef = nameOrSemi.token.content;
    lexer_expect_token(lexer);
  }
  return true;
}

void collect_inherits(StructArr *structs, String_View file, String_View filename) {
  Lexer lexer = lexer_create(filename, file);

  size_t depth = 0;
  TokenOrEnd token = lexer_get_token(&lexer);
  for (; token.has_value; token = lexer_get_token(&lexer)) {
    Token t = token.token;
    if (t.kind == TK_PAREN && sv_eq(t.content, SV("}"))) {
      depth -= 1;
      continue;
    }
    if (t.kind == TK_PAREN && sv_eq(t.content, SV("{"))) {
      depth += 1;
      continue;
    }

    if (t.kind != TK_TYPEDF && t.kind != TK_STRUCT) continue; // ignore everything else

    StructDef new = {0};
    String_View who = {0};
    bool is_struct = false;
    new.loc_start = t.content.data;
    
    if (t.kind == TK_TYPEDF) {
      if (!parse_typedef_inherit(&lexer, &new.strt, &new.defn, &new.tdef, &is_struct, &who))
        continue;
      // typdef given but no name -> skip it
      if (new.tdef.count == 0) {
        lexer_dump_warn(t.loc, stderr, "Warning: typedef but no name for child of `" SV_Fmt "`", SV_Arg(who));
        new.loc_start += t.content.count;
      }
    }
    if (t.kind == TK_STRUCT) {
      if (!parse_struct_inherit(&lexer, &new.strt, &new.defn, &is_struct, &who))
        continue;
    }
    
    new.loc_end = lexer.content.data - new.tdef.count;
    if (!new.strt.count && !new.tdef.count) {
      lexer_dump_warn(t.loc, stderr, "Warning: neither struct name nor typedef given for child of `" SV_Fmt "`", SV_Arg(who));
    }

    for (; token.has_value; token = lexer_get_token(&lexer)) {
      if (token.token.kind == TK_SEP && sv_eq(token.token.content, SV(";"))) {
        new.loc_after = lexer.content.data;
        break;
      }
    }

    for (size_t i = 0; i < structs->items_count; ++i) {
      if ((is_struct && sv_eq(who, structs->items[i].strt)) ||
          (!is_struct && sv_eq(who, structs->items[i].tdef))) {
        new.parent = i;
        new.hasParent = true;
        ARRAY_PUSH(*structs, items, new);
        if (new.strt.count || new.tdef.count) // TODO: is this good? parent-child broken...
          ARRAY_PUSH(structs->items[i], inherits, structs->items_count - 1);
        break;
      }
    }
    if (!new.hasParent)
      lexer_dump_err(t.loc, stderr, "Error: no parent `" SV_Fmt "` known in definition of %s", SV_Arg(who), struct_to_name(new, false));
  }
  if (depth != 0)
    lexer_exit_err(lexer.loc, stderr, "Unclosed block");
}

#define WRITE(ptr, size) do                                          \
    {                                                                \
      /* write one entire buffer or fail */                          \
      if (fwrite(ptr, size, 1, outfile) != 1 && ferror(outfile)) {   \
        perror("fwrite");                                            \
        exit(1);                                                     \
      }                                                              \
    } while (0);
void dump_def(StructArr data, StructDef def, FILE *outfile) {
  if (def.hasParent) dump_def(data, data.items[def.parent], outfile);
  WRITE(def.defn.data, def.defn.count);
}

void dump_type_name(StructDef def, FILE *outfile) {
  static char strut[] = "struct ";
  if (def.strt.count) {
    WRITE(strut, sizeof(strut) - 1);
    WRITE(def.strt.data, def.strt.count);
  } else {
    assert(def.tdef.count);
    WRITE(def.tdef.data, def.tdef.count);
  }
}

String_View extract_property(Lexer *lexer) {
  String_View last_name = {0};
  while (true) {
    Token token = lexer_expect_token(lexer);
    if (token.kind == TK_NAME) last_name = token.content;
    else if (token.kind == TK_SEP && sv_eq(token.content, SV(";"))) break;
  }
  return last_name;
}

void dump_asserts(StructArr data, StructDef def, StructDef curparent, StructDef parent, FILE *outfile) {
  // dump asserts for all fields in parent chain, but with actual parent name
  if (parent.hasParent) dump_asserts(data, def, curparent, data.items[parent.parent], outfile);

  char *fname = struct_to_name(parent, true);
  Lexer lexer = lexer_create(sv_from_cstr(fname), parent.defn);
  TokenOrEnd token = lexer_get_token(&lexer);
  for (; token.has_value; token = lexer_get_token(&lexer)) {
    Token t = token.token;
    if (t.kind != TK_NAME)
      lexer_exit_err(t.loc, stderr, "Expected a type");
    String_View property = extract_property(&lexer);
    if (!property.count)
      lexer_exit_err(t.loc, stderr, "Not a valid property");
    
    static char assrt1[] = "_Static_assert(offsetof(";
    static char assrt2[] = ") == offsetof(";
    static char assrt3[] = "), \"Offsets don't match\");\n";
    WRITE(assrt1, sizeof(assrt1) - 1);
    dump_type_name(def, outfile);
    WRITE(", ", 2);
    WRITE(property.data, property.count);
    WRITE(assrt2, sizeof(assrt2) - 1);
    dump_type_name(curparent, outfile);
    WRITE(", ", 2);
    WRITE(property.data, property.count);
    WRITE(assrt3, sizeof(assrt3) - 1);
  }
  free(fname);
}

void dump_child_cast(StructArr data, StructDef in, String_View name, bool is_struct, bool ptr, FILE *outfile) {
  static char stut[] = "struct ";
  // <typename>: *(<parent>*)&(T)
  // or
  // <typename>*: (<parent>*)(T)
  WRITE(", ", 2);
  if (in.strt.count) {
    WRITE(stut, sizeof(stut) - 1);
    WRITE(in.strt.data, in.strt.count);
  } else {
    assert(in.tdef.count);
    WRITE(in.tdef.data, in.tdef.count);
  }
  if (ptr) WRITE("*", 1);
  WRITE(": ", 2);
  if (!ptr) WRITE("*", 1);
  WRITE("(", 1);
  if (is_struct) WRITE(stut, sizeof(stut) - 1);
  WRITE(name.data, name.count);
  WRITE("*)", 2);
  if (!ptr) WRITE("&", 1);
  WRITE("(T)", 3);

  // recurse into children to allow casting up the chain
  for (size_t i = 0; i < in.inherits_count; ++i) {
    StructDef in2 = data.items[in.inherits[i]];
    dump_child_cast(data, in2, name, is_struct, ptr, outfile);
  }
}

void dump_cast(StructArr data, StructDef def, String_View name,
    bool is_struct, bool ptr, FILE *outfile) {
  if (!def.inherits_count) return;
  static char defc[] = "#define CEST_AS_";
  static char strt[] = "struct_";
  static char gene[] = "(T) _Generic((T)";
  static char stut[] = "struct ";
  WRITE(defc, sizeof(defc) - 1);
  if (is_struct) WRITE(strt, sizeof(strt) - 1);
  WRITE(name.data, name.count);
  if (ptr) WRITE("S", 1);
  WRITE(gene, sizeof(gene) - 1);
  WRITE(", ", 2);
  if (is_struct) WRITE(stut, sizeof(stut) - 1);
  WRITE(name.data, name.count);
  if (ptr) WRITE("*", 1);
  WRITE(": (T)", 5);
  for (size_t i = 0; i < def.inherits_count; ++i) {
    StructDef in = data.items[def.inherits[i]];
    dump_child_cast(data, in, name, is_struct, ptr, outfile);
  }
  WRITE(")\n", 2);
}

void output_casts(StructArr data, FILE *outfile) {
  for (size_t i = 0; i < data.items_count; ++i) {
    StructDef def = data.items[i];
    if (def.strt.count) dump_cast(data, def, def.strt, true, false, outfile);
    if (def.strt.count) dump_cast(data, def, def.strt, true, true, outfile);
    if (def.tdef.count) dump_cast(data, def, def.tdef, false, false, outfile);
    if (def.tdef.count) dump_cast(data, def, def.tdef, false, true, outfile);
  }
}

void replace_inherits(StructArr data, String_View file, FILE *outfile) {
  char *ins = NULL;
  const char *last = file.data;
  // items are guaranteed to be in order
  for (size_t i = 0; i < data.items_count; ++i) {
    StructDef def = data.items[i];
    if (!def.hasParent) continue;
    if ((ins = strstr(last, INSERT_STR)) != NULL && ins < def.loc_start) {
      WRITE(last, ins - last);
      output_casts(data, outfile);
      WRITE(ins + sizeof(INSERT_STR) - 1, def.loc_start - (ins + sizeof(INSERT_STR) - 1));
    } else {
      WRITE(last, def.loc_start - last);
    }
    
    static char tpdef[] = "typedef ";
    static char strut[] = "struct ";
    if (def.tdef.count) WRITE(tpdef, sizeof(tpdef) - 1);
    WRITE(strut, sizeof(strut) - 1);
    WRITE(def.strt.data, def.strt.count);
    WRITE("{", 1);
    dump_def(data, def, outfile);
    WRITE("}", 1);
    WRITE(def.loc_end, def.loc_after - def.loc_end);
    WRITE("\n", 1);
    if (def.strt.count || def.tdef.count)
      dump_asserts(data, def, data.items[def.parent], data.items[def.parent], outfile);
    last = def.loc_after;
  }
  size_t rest = (file.data + file.count) - last;
  if ((ins = strstr(last, INSERT_STR)) != NULL && ins < last + rest) {
    WRITE(last, ins - last);
    output_casts(data, outfile);
    WRITE(ins + sizeof(INSERT_STR) - 1, file.data + file.count - (ins + sizeof(INSERT_STR) - 1));
  } else {
    WRITE(last, rest);
  }
}
#undef WRITE

void print_struct_def(StructArr arr, StructDef def, int level) {
  printf("%*.s", level * 2, "");
  if (def.strt.count) printf("struct " SV_Fmt, SV_Arg(def.strt));
  if (def.strt.count && def.tdef.count) printf(" / ");
  if (def.tdef.count) printf(SV_Fmt, SV_Arg(def.tdef));
  if (def.hasParent) printf(" (parent: %zu)", def.parent);
  printf("\n");
  if (def.inherits_count) {
    printf("%*.sChildren:\n", level * 2, "");
    for (size_t i = 0; i < def.inherits_count; ++i) {
      print_struct_def(arr, arr.items[def.inherits[i]], level + 1);
    }
  }
}

void usage(FILE *stream, const char *program) {
  fprintf(stream, "%s <in file> [<out file>]\n", program);
  fprintf(stream, "   <in file>   File to resolve inheritance in\n");
  fprintf(stream, "   <out file>  File to place results in, may be - for stdout\n");
}


#ifndef NO_MAIN
int main(int argc, char *argv[]) {
  if (argc > 1 && strcmp(argv[1], "-h") == 0) {
    usage(stdout, argv[0]);
    exit(0);
  }
  if (argc < 2) {
    fprintf(stderr, "too few arguments provided!\n");
    usage(stderr, argv[0]);
    exit(1);
  }
  char *outstr = "-";
  if (argc >= 3) outstr = argv[2];
  StructArr strts = collect_structs(argv[1]);
#ifdef DEBUG
  printf("Originally known structs:\n");
  for (size_t i = 0; i < strts.items_count; i++) {
    print_struct_def(strts, strts.items[i], 0);
  }
#endif // DEBUG
  String_View file = load_file(argv[1]);
  collect_inherits(&strts, file, sv_from_cstr(argv[1]));
#ifdef DEBUG
  printf("-------------------------\n");
  printf("Structs after inheritance:\n");
  for (size_t i = 0; i < strts.items_count; i++) {
    print_struct_def(strts, strts.items[i], 0);
  }
  printf("-------------------------\n");
#endif // DEBUG
  // TODO: collect anonymous typedefs
  // will require making tdef an array
  FILE *outfile = stdout;
  if (strcmp(outstr, "-") != 0) outfile = fopen(outstr, "w");
  if (outfile == NULL) {
    fprintf(stderr, "Could not open file `%s` for writing: %s\n", outstr, strerror(errno));
    exit(1);
  }
  replace_inherits(strts, file, outfile); 
  if (strcmp(outstr, "-") != 0) POSIX_WORK(fclose, outfile);
  free((void *)strts.orig);
  for (size_t i = 0; i < strts.items_count; ++i) free((void *)strts.items[i].inherits);
  free((void *)strts.items);
  free((void *)file.data);
  return 0;
}
#endif // NO_MAIN
