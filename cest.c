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
      assert(regerror(err, &reg, errbuf, sizeof(errbuf)) < sizeof(errbuf));   \
      fprintf(stderr, "Error compiling regex `%s`: %s\n", regex, errbuf);     \
      exit(1);                                                                \
    }                                                                         \
  } while(0);

#define IDENT_RE "[A-Za-z_][A-Za-z0-9_]*"
// TODO: This does not consider typedef`s without body (i.e. forward defs)
#define STRUCT_RE "(typedef\\s+)?struct\\s*(\\s(" IDENT_RE "))?\\s*" \
  "\\{([^}]*)\\}\\s*(" IDENT_RE ")?\\s*;"
#define INHERIT_RE "(typedef\\s+)?INHERIT_STRUCT"                             \
  "\\(\\s*((struct\\s+)?" IDENT_RE ")(\\s*,\\s*(" IDENT_RE "))?\\s*\\)\\s*"   \
  "\\{([^}]*)\\}\\s*(" IDENT_RE ")?\\s*;"



typedef struct {
  String_View defn;
  String_View strt;
  String_View tdef;
  bool hasParent;
  size_t parent;
  MAKE_ARRAY(size_t) // inherits
} StructDef;
typedef struct {
  MAKE_ARRAY(StructDef)
  const char *orig;
} StructArr;

typedef struct {
  MAKE_ARRAY(char)
} FileArr;

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
  if (fread(ptr, size, 1, f) < 1) { // read one entire buffer or fail
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

void collect_inherits(StructArr *structs) {
  regex_t reg;
  REG_COMPILE(&reg, INHERIT_RE, REG_EXTENDED);
  regmatch_t matches[8];
  char *p = (char *)structs->orig;
  while ((regexec(&reg, p, sizeof(matches)/sizeof(matches[0]), matches, 0)) == 0) {
    StructDef new = {0};
    String_View who = (String_View) {
      .count = matches[2].rm_eo - matches[2].rm_so,
      .data = p + matches[2].rm_so,
    };
    new.defn = (String_View) {
      .count = matches[6].rm_eo - matches[6].rm_so,
      .data = p + matches[6].rm_so,
    };
    new.strt = (String_View) {
      .count = matches[5].rm_eo - matches[5].rm_so,
      .data = p + matches[5].rm_so,
    };
    new.tdef = (String_View) {
      .count = matches[7].rm_eo - matches[7].rm_so,
      .data = p + matches[7].rm_so,
    };

    // if exactly one of these is true, it's an error
    // i.e. typedef but no name, or no typedef but name
    if ((matches[1].rm_eo - matches[1].rm_so > 0) ^ (new.tdef.count > 0)) {
      fprintf(stderr, "Error: syntax error (typedef requries name) for child of `" SV_Fmt "`\n", SV_Arg(who));
      p += matches[0].rm_eo;
      continue;
    }
    if (!new.strt.count && !new.tdef.count) {
      fprintf(stderr, "Error: neither struct name nor typedef given for child of `" SV_Fmt "`\n", SV_Arg(who));
      p += matches[0].rm_eo;
      continue;
    }
    
    for (size_t i = 0; i < structs->items_count; ++i) {
      if ((sv_starts_with(who, SV("struct ")) &&
            sv_eq(sv_left(who, sizeof("struct ") - 1), structs->items[i].strt)) ||
          sv_eq(who, structs->items[i].tdef)) {
        new.parent = i;
        new.hasParent = true;
        ARRAY_PUSH(*structs, new);
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
}

FileArr replace_inherits(String_View file) {
  FileArr result = (FileArr) {
    .items = (char *)file.data,
    .items_count = file.count + 1,
    .items_cap = file.count + 1,
  };
  regex_t reg;
  REG_COMPILE(&reg, INHERIT_RE, REG_EXTENDED);
  regmatch_t matches[8];
  char *p = result.items;
  while ((regexec(&reg, p, sizeof(matches)/sizeof(matches[0]), matches, 0)) == 0) {
    StructDef new = {0};
    String_View who = (String_View) {
      .count = matches[2].rm_eo - matches[2].rm_so,
      .data = p + matches[2].rm_so,
    };
    (void)who;
    new.defn = (String_View) {
      .count = matches[6].rm_eo - matches[6].rm_so,
      .data = p + matches[6].rm_so,
    };
    new.strt = (String_View) {
      .count = matches[5].rm_eo - matches[5].rm_so,
      .data = p + matches[5].rm_so,
    };
    new.tdef = (String_View) {
      .count = matches[7].rm_eo - matches[7].rm_so,
      .data = p + matches[7].rm_so,
    };
    size_t size = matches[0].rm_eo - matches[0].rm_so;

    // remove erroneous definitions
    if (((matches[1].rm_eo - matches[1].rm_so > 0) ^ (new.tdef.count > 0)) ||
        (!new.strt.count && !new.tdef.count)) {
      static char err[] = "/* replaced error */";
      ssize_t diff = sizeof(err) - 1 - size;
      if (diff > 0) {
        // reallocations can shift own pointer
        size_t offset_in_file = p - result.items;
        ARRAY_EXTEND(result, diff);
        p = result.items + offset_in_file;
      }
      // move everything behind such that exactly error message is here
      // and adjust length of file
      memmove(p + matches[0].rm_so + sizeof(err) - 1, p + matches[0].rm_eo,
          (size_t) ((result.items + result.items_count) - (p + matches[0].rm_eo)));
      memcpy(p + matches[0].rm_so, err, sizeof(err) - 1);
      p += matches[0].rm_so + sizeof(err) - 1;
      result.items_count += diff;
      continue;
    }
    
    // TODO: dump struct definition, static asserts here
    p += matches[0].rm_eo;
  }
  return result;
}

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
  fprintf(stream, "%s <in file> <out file>\n", program);
}


int main(int argc, char *argv[]) {
  if (argc > 1 && strcmp(argv[1], "-h") == 0) {
    usage(stdout, argv[0]);
    exit(0);
  }
  if (argc < 3) {
    fprintf(stderr, "too few arguments provided!\n");
    usage(stderr, argv[0]);
    exit(1);
  }
  StructArr strts = collect_structs(argv[1]);
  printf("Originally known structs:\n");
  for (size_t i = 0; i < strts.items_count; i++) {
    print_struct_def(strts, strts.items[i], 0);
  }
  collect_inherits(&strts);
  printf("-------------------------\n");
  printf("Structs after inheritance:\n");
  for (size_t i = 0; i < strts.items_count; i++) {
    print_struct_def(strts, strts.items[i], 0);
  }
  // TODO: collect anonymous typedefs
  // will require making tdef an array
  free((void *)strts.orig);
  free((void *)strts.items);
  FileArr f = replace_inherits(load_file(argv[1]));
  printf("-------------------------\n");
  printf("New file contents:\n");
  printf(SV_Fmt, (int) f.items_count, f.items);
}
