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

#define POSIX_WORK(name, ...) do   \
  {                                \
    if (name(__VA_ARGS__) < 0) {   \
      perror(#name);               \
      exit(1);                     \
    }                              \
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


#define INITIAL_FILE_CAP 10
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
    POSIX_WORK(execlp, "cc", "cc", "-E", filename, NULL);
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
        perror("realloc");
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


typedef struct {
  String_View defn;
  String_View strt;
  String_View tdef;
} StructDef;
typedef struct {
  const StructDef *structs;
  size_t n;
  const char *orig;
} StructArr;

StructArr collect_structs(char *filename) {
  regex_t reg;
  REG_COMPILE(&reg, STRUCT_RE, REG_EXTENDED);
  String_View file = preprocess_file("test.c");
  assert(file.data[file.count] == 0);

  char *p = (char *)file.data;
  ssize_t err;
  size_t n, i;
  for (n = 0; (err = next_struct(&reg, p, NULL, NULL, NULL)) >= 0; ++n, p += err);
  StructDef *structs = malloc(n * sizeof(*structs));
  if (structs == NULL) {
    perror("malloc");
    exit(1);
  }
  p = (char *)file.data;
  for (i = 0; (err = next_struct(&reg, p, &structs[i].defn, &structs[i].strt, &structs[i].tdef)) >= 0; ++i, p += err);
  assert(i == n);
  regfree(&reg);
  return (StructArr) {
    .structs = structs,
    .n = n,
    .orig = file.data,
  };
}

void usage(FILE *stream, const char *program) {
  fprintf(stream, "%s <file>\n", program);
}


int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "too few arguments provided!\n");
    usage(stderr, argv[0]);
    exit(1);
  }
  StructArr strts = collect_structs(argv[1]);
  for (size_t i = 0; i < strts.n; i++) {
    if (strts.structs[i].strt.count) printf("struct " SV_Fmt "\n", SV_Arg(strts.structs[i].strt));
    if (strts.structs[i].tdef.count) printf(SV_Fmt "\n", SV_Arg(strts.structs[i].tdef));
  }
  free((void *)strts.orig);
  free((void *)strts.structs);
}
