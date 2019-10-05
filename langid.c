/* 
 * Command-line driver for liblangid
 *
 * Hiroyuki Deguchi <deguchi@ai.cs.ehime-u.ac.jp>, October 2019
 * Marco Lui <saffsd@gmail.com>, September 2014
 */
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "liblangid.h"

const char* no_file = "NOSUCHFILE";
const char* not_file = "NOTAFILE";

void rstrip_ln(char *str);

int main(int argc, char **argv){
    const char* lang;
    size_t path_size = 4096, text_size=4096;
    ssize_t pathlen, textlen;
    char *path = NULL, *text = NULL; /* NULL init required for use with getline/getdelim*/
    LanguageIdentifier *lid;

    /* for use while accessing files through mmap*/
    int fd;

    /* for use with getopt */
    char *model_path = NULL;
    int c, l_flag = 0, b_flag = 0, f_flag = 0;
    opterr = 0;

#ifdef DEBUG
    fprintf(stderr,"DEBUG MODE ENABLED\n");
#endif

    /* valid options are:
     * l: line-mode
     * b: batch-mode
     * m: load a model file
     */

    while ((c = getopt (argc, argv, "lbmf:")) != -1)
      switch (c) {
        case 'l':
          l_flag = 1;
          break;
        case 'b':
          b_flag = 1;
          break;
        case 'm':
          model_path = optarg;
          break;
        case 'f':
          f_flag = 1;
          break;
        case '?':
          if (optopt == 'm')
            fprintf (stderr, "Option -%c requires an argument.\n", optopt);
          else if (isprint (optopt))
            fprintf (stderr, "Unknown option `-%c'.\n", optopt);
          else
            fprintf (stderr,
                    "Unknown option character `\\x%x'.\n",
                    optopt);
          return 1;
        default:
          abort();
      }

    /* validate getopt options */
    if (l_flag && b_flag && f_flag) {
      fprintf(stderr, "Cannot specify both -l and -b and -f.\n");
      exit(-1);
    }
    
    /* load an identifier */
    lid = model_path ? load_identifier(model_path) : get_default_identifier();

    /* enter appropriate operating mode.
     * we have an interactive mode determined by isatty, and then
     * the three modes are file-mode (default), line-mode and batch-mode
     */

    if (f_flag) { /*filter mode*/
      printf("langid.c filtering mode.\n");
      const char *src_lang, *tgt_lang;
      FILE *fp_src_file, *fp_tgt_file, *fp_src_dest, *fp_tgt_dest, *fp_src_lid, *fp_tgt_lid;
      size_t src_size = 16 * 1024, tgt_size = 16 * 1024;
      char src_file[path_size], tgt_file[path_size];
      char src_dest[path_size], tgt_dest[path_size];
      char src_lid[path_size], tgt_lid[path_size];
      ssize_t src_len, tgt_len;
      char *src_text = NULL, *tgt_text = NULL, *src_l = NULL, *tgt_l = NULL; /* NULL init required for use with getline/getdelim*/
      char *prefix = argv[2];
      char *src = argv[3];
      char *tgt = argv[4];
      char *dest_prefix = argv[5];

      sprintf(src_file, "%s.%s", prefix, src);
      sprintf(tgt_file, "%s.%s", prefix, tgt);
      sprintf(src_dest, "%s.%s", dest_prefix, src);
      sprintf(tgt_dest, "%s.%s", dest_prefix, tgt);
      sprintf(src_lid, "%s.lid.%s", prefix, src);
      sprintf(tgt_lid, "%s.lid.%s", prefix, tgt);

      fp_src_file = fopen(src_file, "r");
      fp_tgt_file = fopen(tgt_file, "r");
      fp_src_dest = fopen(src_dest, "w");
      fp_tgt_dest = fopen(tgt_dest, "w");
      fp_src_lid = fopen(src_lid, "w+");
      fp_tgt_lid = fopen(tgt_lid, "w+");

      if (fp_src_file == NULL || fp_tgt_file == NULL ||
          fp_src_dest == NULL || fp_tgt_dest == NULL ||
          fp_src_lid == NULL || fp_tgt_lid == NULL) {
        fprintf(stderr, "file open error.");
        goto cleanup;
        return -1;
      }
      else {
        int status = 0;
        pid_t wait_pid;
        pid_t pid = fork();
        if (pid < 0) {
          fprintf(stderr, "fork error.");
          return -1;
        }
        else if (pid == 0) {
          while ((src_len = getline(&src_text, &src_size, fp_src_file)) != -1) {
            src_lang = identify(lid, src_text, src_len);
            fprintf(fp_src_lid, "%s\n", src_lang);
          }
          exit(0);
        }
        while ((tgt_len = getline(&tgt_text, &tgt_size, fp_tgt_file)) != -1){
          tgt_lang = identify(lid, tgt_text, tgt_len);
          fprintf(fp_tgt_lid, "%s\n", tgt_lang);
        }
        wait_pid = wait(&status);
        if (wait_pid < 0) {
          fprintf(stderr, "wait error.");
          return -1;
        }
        fseek(fp_src_file, 0L, SEEK_SET);
        fseek(fp_tgt_file, 0L, SEEK_SET);
        fseek(fp_src_lid, 0L, SEEK_SET);
        fseek(fp_tgt_lid, 0L, SEEK_SET);
        while ((src_len = getline(&src_text, &src_size, fp_src_file)) != -1 &&
               (tgt_len = getline(&tgt_text, &tgt_size, fp_tgt_file)) != -1 &&
               (src_len = getline(&src_l, &path_size, fp_src_lid)) != -1 &&
               (tgt_len = getline(&tgt_l, &path_size, fp_tgt_lid)) != -1) {
          rstrip_ln(src_l);
          rstrip_ln(tgt_l);
          if ((strcmp(src_l, src) == 0) && (strcmp(tgt_l, tgt) == 0)) {
            fprintf(fp_src_dest, "%s", src_text);
            fprintf(fp_tgt_dest, "%s", tgt_text);
          }
        }
        goto cleanup;
        free(src_text);
        free(tgt_text);
        free(src_l);
        free(tgt_l);
        unlink(src_lid);
        unlink(tgt_lid);
        return 0;
      }

    cleanup:
      if (fp_src_file != NULL) fclose(fp_src_file);
      if (fp_tgt_file != NULL) fclose(fp_tgt_file);
      if (fp_src_dest != NULL) fclose(fp_src_dest);
      if (fp_tgt_dest != NULL) fclose(fp_tgt_dest);
      if (fp_src_lid != NULL) fclose(fp_src_lid);
      if (fp_tgt_lid != NULL) fclose(fp_tgt_lid);

    }
    else if (isatty(fileno(stdin))){
      printf("langid.c interactive mode.\n");

      for(;;) {
        printf(">>> ");
        textlen = getline(&text, &text_size, stdin);
        if (textlen == 1 || textlen == -1) break; /* -1 for EOF and 1 for only newline */
        lang = identify(lid, text, textlen);
        printf("%s,%zd\n", lang, textlen);
      } 

      printf("Bye!\n");

    }
    else if (l_flag) { /*line mode*/

      while ((textlen = getline(&text, &text_size, stdin)) != -1){
        lang = identify(lid, text, textlen);
        printf("%s,%zd\n", lang, textlen);
      }

    }
    else if (b_flag) { /*batch mode*/

      /* loop on stdin, interpreting each line as a path */
      while ((pathlen = getline(&path, &path_size, stdin)) != -1){
        path[pathlen-1] = '\0';
        /* TODO: ensure that path is a real file. 
         * the main issue is with directories I think, no problem reading from a pipe or socket
         * presumably. Anything that returns data should be fair game.*/
        if ((fd = open(path, O_RDONLY))==-1) {
          lang = no_file;
        }
        else {
          textlen = lseek(fd, 0, SEEK_END);
          text = (char *) mmap(NULL, textlen, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
          lang = identify(lid, text, textlen);

          /* no need to munmap if textlen is 0 */
          if (textlen && (munmap(text, textlen) == -1)) {
            fprintf(stderr, "failed to munmap %s of length %zd \n", path, textlen);
            exit(-1);
          }

          close(fd);
        }
        printf("%s,%zd,%s\n", path, textlen, lang);
      }

    }
    else { /*file mode*/

      /* read all of stdin and process as a single file */
      textlen = getdelim(&text, &text_size, EOF, stdin);
      lang = identify(lid, text, textlen);
      printf("%s,%zd\n", lang, textlen);
      free(text);

    }

    destroy_identifier(lid);
    return 0;
}

void rstrip_ln(char *str)
{
    char *p;
    p = strchr(str, '\n');
    if (p != NULL) {
        *p = '\0';
    }
}
