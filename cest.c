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

// #define DEBUG

#define SV_IMPLEMENTATION
#include "./sv.h"
#include "./array.h"

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

#define REG_COMPILE(preg, regex, cflags) do                                   \
  {                                                                           \
    int err;                                                                  \
    if ((err = regcomp(preg, regex, cflags))) {                               \
      static char errbuf[200];                                                \
      assert(regerror(err, preg, errbuf, sizeof(errbuf)) < sizeof(errbuf));   \
      fprintf(stderr, "Error compiling regex `%s`: %s\n", regex, errbuf);     \
      exit(1);                                                                \
    }                                                                         \
  } while(0);

#define IDENT_RE "[A-Za-z_][A-Za-z0-9_]*"
// TODO: This does not consider typedef`s without body (i.e. forward defs)
#define STRUCT_RE "(typedef\\s+)?struct\\s*(\\s(" IDENT_RE "))?\\s*" \
  "\\{([^}]*)\\}\\s*(" IDENT_RE ")?\\s*;"
// TODO: implement multiple inheritance
#define INHERIT_RE "(typedef\\s+)?struct\\s*(\\s(" IDENT_RE "))?\\s*"   \
  "\\(\\s*(struct\\s+)?(" IDENT_RE ")\\s*\\)\\s*"                       \
  "\\{([^}]*)\\}\\s*(" IDENT_RE ")?\\s*;"
#define STRUCT_NAME_RE "(" IDENT_RE ")\\s*;"
#define INSERT_STR "CEST_MACROS_HERE"



typedef struct {
  String_View defn;
  String_View strt;
  String_View tdef;
  bool hasParent;
  size_t parent;
  MAKE_ARRAY(size_t) // inherits
  char *loc_start;
  char *loc_end;
} StructDef;
typedef struct {
  MAKE_ARRAY(StructDef)
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

ssize_t next_struct(regex_t *reg, char *file, String_View *def, String_View *strt, String_View *tpdef) {
  regmatch_t matches[6];
  if (regexec(reg, file, sizeof(matches)/sizeof(matches[0]), matches, 0) != 0) return -1;
  if (def) {
    *def = (String_View) {
      .count = matches[4].rm_eo - matches[4].rm_so,
      .data = file + matches[4].rm_so,
    };
  }
  if (strt) {
    *strt = (String_View) {
      .count = matches[3].rm_eo - matches[3].rm_so,
      .data = file + matches[3].rm_so,
    };
  }
  if (tpdef) {
    *tpdef = (String_View) {
      .count = matches[5].rm_eo - matches[5].rm_so,
      .data = file + matches[5].rm_so,
    };
  }
  return matches[0].rm_eo;
}

StructArr collect_structs(const char *filename) {
  regex_t reg;
  REG_COMPILE(&reg, STRUCT_RE, REG_EXTENDED);
  String_View file = preprocess_file(filename);
  assert(file.data[file.count] == 0);

  char *p = (char *)file.data;
  ssize_t err;
  size_t n, i;
  for (n = 0; (err = next_struct(&reg, p, NULL, NULL, NULL)) >= 0; ++n, p += err);
  StructDef *structs = malloc(n * sizeof(*structs));
  memset(structs, 0, n * sizeof(*structs));
  if (structs == NULL) {
    perror("malloc");
    exit(1);
  }
  p = (char *)file.data;
  for (i = 0; (err = next_struct(&reg, p, &structs[i].defn, &structs[i].strt, &structs[i].tdef)) >= 0; ++i, p += err);
  assert(i == n);
  regfree(&reg);
  return (StructArr) {
    .items = structs,
    .items_count = n,
    .items_cap = n,
    .orig = file.data,
  };
}

void collect_inherits(StructArr *structs, String_View file) {
  regex_t reg;
  REG_COMPILE(&reg, INHERIT_RE, REG_EXTENDED);
  regmatch_t matches[8];
  char *p = (char *)file.data;
  while ((regexec(&reg, p, sizeof(matches)/sizeof(matches[0]), matches, 0)) == 0) {
    StructDef new = {0};
    String_View who = (String_View) {
      .count = matches[5].rm_eo - matches[5].rm_so,
      .data = p + matches[5].rm_so,
    };
    bool is_struct = (matches[4].rm_eo - matches[4].rm_so) > 0;
    new.defn = (String_View) {
      .count = matches[6].rm_eo - matches[6].rm_so,
      .data = p + matches[6].rm_so,
    };
    new.strt = (String_View) {
      .count = matches[3].rm_eo - matches[3].rm_so,
      .data = p + matches[3].rm_so,
    };
    new.tdef = (String_View) {
      .count = matches[7].rm_eo - matches[7].rm_so,
      .data = p + matches[7].rm_so,
    };
    new.loc_start = p + matches[0].rm_so;
    new.loc_end = p + matches[0].rm_eo;

    // typdef given but no name -> skip it
    if ((matches[1].rm_eo - matches[1].rm_so > 0) && (new.tdef.count == 0)) {
      fprintf(stderr, "Warning: typedef but no name for child of `" SV_Fmt "`\n", SV_Arg(who));
      new.loc_start = p + matches[1].rm_eo;
    }
    // no typedef but name -> clear 
    if ((matches[1].rm_eo - matches[1].rm_so == 0) && (new.tdef.count > 0)) {
      fprintf(stderr, "Warning: ignoring name without typedef for child of `" SV_Fmt "`\n", SV_Arg(who));
      new.tdef.count = 0;
    }
    if (!new.strt.count && !new.tdef.count) {
      fprintf(stderr, "Warning: neither struct name nor typedef given for child of `" SV_Fmt "`\n", SV_Arg(who));
    }
    
    for (size_t i = 0; i < structs->items_count; ++i) {
      if ((is_struct && sv_eq(who, structs->items[i].strt)) ||
          sv_eq(who, structs->items[i].tdef)) {
        new.parent = i;
        new.hasParent = true;
        ARRAY_PUSH(*structs, new);
        if (new.strt.count || new.tdef.count) // TODO: is this good? parent-child broken...
          ARRAY_PUSH(structs->items[i], structs->items_count - 1);
        break;
      }
    }
    if (!new.hasParent) {
      fprintf(stderr, "Error: no parent `" SV_Fmt "` known in definition of ", SV_Arg(who));
      if (new.strt.count) fprintf(stderr, "`struct " SV_Fmt "`", SV_Arg(new.strt));
      if (new.strt.count && new.tdef.count) fprintf(stderr, " / ");
      if (new.tdef.count) fprintf(stderr, "`" SV_Fmt "`", SV_Arg(new.tdef));
      fprintf(stderr, "\n");
    }
    p += matches[0].rm_eo;
  }
  regfree(&reg);
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

void write_type_name(StructDef def, FILE *outfile) {
  static char strut[] = "struct ";
  if (def.strt.count) {
    WRITE(strut, sizeof(strut) - 1);
    WRITE(def.strt.data, def.strt.count);
  } else {
    assert(def.tdef.count);
    WRITE(def.tdef.data, def.tdef.count);
  }
}

void dump_asserts(StructArr data, StructDef def, StructDef curparent, StructDef parent, regex_t *reg, FILE *outfile) {
  // dump asserts for all fields in parent chain, but with actual parent name
  if (parent.hasParent) dump_asserts(data, def, curparent, data.items[parent.parent], reg, outfile);
  regmatch_t matches[2];
  char *p = (char *)parent.defn.data;
  while ((regexec(reg, p, sizeof(matches)/sizeof(matches[0]), matches, 0)) == 0) {
    if (p + matches[0].rm_so >= parent.defn.data + parent.defn.count) break;
    static char assrt1[] = "_Static_assert(offsetof(";
    static char assrt2[] = ") == offsetof(";
    static char assrt3[] = "), \"Offsets don't match\");\n";
    WRITE(assrt1, sizeof(assrt1) - 1);
    write_type_name(def, outfile);
    WRITE(", ", 2);
    WRITE(p + matches[1].rm_so, matches[1].rm_eo - matches[1].rm_so);
    WRITE(assrt2, sizeof(assrt2) - 1);
    write_type_name(curparent, outfile);
    WRITE(", ", 2);
    WRITE(p + matches[1].rm_so, matches[1].rm_eo - matches[1].rm_so);
    WRITE(assrt3, sizeof(assrt3) - 1);
    p += matches[0].rm_eo;
  }
}

void out_cast(StructArr data, StructDef def, String_View name, bool is_struct, FILE *outfile) {
  if (!def.items_count) return;
  static char defc[] = "#define CEST_AS_";
  static char strt[] = "struct_";
  static char gene[] = "(T) _Generic((T)";
  static char stut[] = "struct ";
  WRITE(defc, sizeof(defc) - 1);
  if (is_struct) WRITE(strt, sizeof(strt) - 1);
  WRITE(name.data, name.count);
  WRITE(gene, sizeof(gene) - 1);
  WRITE(", ", 2);
  if (is_struct) WRITE(stut, sizeof(stut) - 1);
  WRITE(name.data, name.count);
  WRITE(": (T)", 5);
  for (size_t i = 0; i < def.items_count; ++i) {
    StructDef in = data.items[def.items[i]];
    if (in.strt.count) {
      WRITE(", ", 2);
      WRITE(stut, sizeof(stut) - 1);
      WRITE(in.strt.data, in.strt.count);
      WRITE(": ", 2);
      WRITE("*(", 2);
      if (is_struct) WRITE(stut, sizeof(stut) - 1);
      WRITE(name.data, name.count);
      WRITE("*)&(T)", 6);
    } else if (in.tdef.count) {
      WRITE(", ", 2);
      WRITE(in.tdef.data, in.tdef.count);
      WRITE(": ", 2);
      WRITE("*(", 2);
      if (is_struct) WRITE(stut, sizeof(stut) - 1);
      WRITE(name.data, name.count);
      WRITE("*)&(T)", 6);
    }
  }
  WRITE(")\n", 2);
}
void out_cast_ptr(StructArr data, StructDef def, String_View name, bool is_struct, FILE *outfile) {
  if (!def.items_count) return;
  static char defc[] = "#define CEST_AS_";
  static char strt[] = "struct_";
  static char gene[] = "S(T) _Generic((T)";
  static char stut[] = "struct ";
  WRITE(defc, sizeof(defc) - 1);
  if (is_struct) WRITE(strt, sizeof(strt) - 1);
  WRITE(name.data, name.count);
  WRITE(gene, sizeof(gene) - 1);
  WRITE(", ", 2);
  if (is_struct) WRITE(stut, sizeof(stut) - 1);
  WRITE(name.data, name.count);
  WRITE("*: (T)", 6);
  for (size_t i = 0; i < def.items_count; ++i) {
    StructDef in = data.items[def.items[i]];
    if (in.strt.count) {
      WRITE(", ", 2);
      WRITE(stut, sizeof(stut) - 1);
      WRITE(in.strt.data, in.strt.count);
      WRITE("*: ", 3);
      WRITE("(", 1);
      if (is_struct) WRITE(stut, sizeof(stut) - 1);
      WRITE(name.data, name.count);
      WRITE("*)(T)", 5);
    } else if (in.tdef.count) {
      WRITE(", ", 2);
      WRITE(in.tdef.data, in.tdef.count);
      WRITE("*: ", 3);
      WRITE("(", 1);
      if (is_struct) WRITE(stut, sizeof(stut) - 1);
      WRITE(name.data, name.count);
      WRITE("*)(T)", 5);
    }
  }
  WRITE(")\n", 2);
}

void output_casts(StructArr data, FILE *outfile) {
  for (size_t i = 0; i < data.items_count; ++i) {
    StructDef def = data.items[i];
    if (def.strt.count) out_cast(data, def, def.strt, true, outfile);
    if (def.strt.count) out_cast_ptr(data, def, def.strt, true, outfile);
    if (def.tdef.count) out_cast(data, def, def.tdef, false, outfile);
    if (def.tdef.count) out_cast_ptr(data, def, def.tdef, false, outfile);
  }
}

void replace_inherits(StructArr data, String_View file, FILE *outfile) {
  regex_t namereg;
  REG_COMPILE(&namereg, STRUCT_NAME_RE, REG_EXTENDED);
  char *ins = NULL;
  char *last = (char *)file.data;
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
    WRITE(def.tdef.data, def.tdef.count);
    WRITE(";\n", 2);
    if (def.strt.count || def.tdef.count)
      dump_asserts(data, def, data.items[def.parent], data.items[def.parent], &namereg, outfile);
    last = def.loc_end;
  }
  size_t rest = (file.data + file.count) - last;
  if ((ins = strstr(last, INSERT_STR)) != NULL && ins < last + rest) {
    WRITE(last, ins - last);
    output_casts(data, outfile);
    WRITE(ins + sizeof(INSERT_STR) - 1, file.data + file.count - (ins + sizeof(INSERT_STR) - 1));
  } else {
    WRITE(last, rest);
  }
  regfree(&namereg);
}
#undef WRITE

void print_struct_def(StructArr arr, StructDef def, int level) {
  printf("%*.s", level * 2, "");
  if (def.strt.count) printf("struct " SV_Fmt, SV_Arg(def.strt));
  if (def.strt.count && def.tdef.count) printf(" / ");
  if (def.tdef.count) printf(SV_Fmt, SV_Arg(def.tdef));
  if (def.hasParent) printf(" (parent: %zu)", def.parent);
  printf("\n");
  if (def.items_count) {
    printf("%*.sChildren:\n", level * 2, "");
    for (size_t i = 0; i < def.items_count; ++i) {
      print_struct_def(arr, arr.items[def.items[i]], level + 1);
    }
  }
}

void usage(FILE *stream, const char *program) {
  fprintf(stream, "%s <in file> [<out file>]\n", program);
  fprintf(stream, "   <in file>   File to resolve inheritance in\n");
  fprintf(stream, "   <out file>  File to place results in, may be - for stdout\n");
}


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
  collect_inherits(&strts, file);
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
  for (size_t i = 0; i < strts.items_count; ++i) free((void *)strts.items[i].items);
  free((void *)strts.items);
  free((void *)file.data);
}
