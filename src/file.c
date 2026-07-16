/*******************************************************************************
 * Copyright (c) 2013-2025, Andrés G. Martinelli <andmarti@gmail.com>          *
 * All rights reserved.                                                        *
 *                                                                             *
 * This file is a part of sc-im                                                *
 *                                                                             *
 * sc-im is a spreadsheet program that is based on sc. The original authors    *
 * of sc are James Gosling and Mark Weiser, and mods were later added by       *
 * Chuck Martin.                                                               *
 *                                                                             *
 * Redistribution and use in source and binary forms, with or without          *
 * modification, are permitted provided that the following conditions are met: *
 * 1. Redistributions of source code must retain the above copyright           *
 *    notice, this list of conditions and the following disclaimer.            *
 * 2. Redistributions in binary form must reproduce the above copyright        *
 *    notice, this list of conditions and the following disclaimer in the      *
 *    documentation and/or other materials provided with the distribution.     *
 * 3. All advertising materials mentioning features or use of this software    *
 *    must display the following acknowledgement:                              *
 *    This product includes software developed by Andrés G. Martinelli         *
 *    <andmarti@gmail.com>.                                                    *
 * 4. Neither the name of the Andrés G. Martinelli nor the                     *
 *   names of other contributors may be used to endorse or promote products    *
 *   derived from this software without specific prior written permission.     *
 *                                                                             *
 * THIS SOFTWARE IS PROVIDED BY ANDRÉS G. MARTINELLI ''AS IS'' AND ANY         *
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED   *
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE      *
 * DISCLAIMED. IN NO EVENT SHALL ANDRÉS G. MARTINELLI BE LIABLE FOR ANY        *
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES  *
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;*
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND *
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT  *
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE       *
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.           *
 *******************************************************************************/

/**
 * \file file.c
 * \author Andrés G. Martinelli <andmarti@gmail.com>
 * \date 2017-07-18
 * \brief TODO Write a brief file description.
 */

#include "sc.h"
#include <pwd.h>
#include <sys/stat.h>
#include <time.h>
#include <utime.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <sys/wait.h>

#ifndef NO_WORDEXP
#include <wordexp.h>
#endif

#include "conf.h"
#include "maps.h"
#include "yank.h"
#include "cmds/cmds.h"
#include "file.h"
#include "marks.h"
#include "lex.h"
#include "format.h"
#include "interp.h"
#include "utils/string.h"
#include "utils/dictionary.h"
#include "cmds/cmds_edit.h"
#include "xmalloc.h"
#include "y.tab.h"
#include "formats/xlsx.h"
#include "formats/xls.h"
#include "tui.h"
#include "trigger.h"
#include "sheet.h"
#include "vmtbl.h"


#ifdef HAVE_PTHREAD
#include <pthread.h>
extern pthread_t fthread;
extern int pthread_exists;
#endif

extern struct ent * freeents;
extern int yyparse(void);
extern int exit_app(int status);
extern struct session * session;

/**
 * \brief erasedb()
 * \details Erase the database of a sheet (tbl) and set it default values.
 * if _free is set we also free the memory of the ents alloc'ed in the tbl
 * \return none
 */
void erasedb(struct sheet * sheet, int _free) {
    int  r, c;

    for (c = 0; c < sheet->maxcols; c++) {
        sheet->fwidth[c] = DEFWIDTH;
        sheet->precision[c] = DEFPREC;
        sheet->realfmt[c] = DEFREFMT;
    }

    for (r = 0; r < sheet->maxrows; r++) {
        sheet->row_format[r] = 1;
        struct ent ** pp = ATBL(sheet, sheet->tbl, r, 0);
        for (c = 0; c++ < sheet->maxcols; pp++)
            if (*pp != NULL) {
                clearent(*pp);
                if (_free) {
                    free(*pp);
                    *pp = NULL;
                } else {
                  (*pp)->next = freeents;    /* save [struct ent] for reuse */
                  freeents = *pp;
                }
            }
    }

    sheet->maxrow = 0;
    sheet->maxcol = 0;

    clean_range();

    sheet->currow = sheet->curcol = 0;
}

/**
 * \brief load rc config file
 * \return none
 */
void load_rc(void) {
    char rcpath[PATHLEN];
    char * home;

    if ((home = getenv("XDG_CONFIG_HOME"))) {
        char config_dir[PATHLEN-(sizeof CONFIG_FILE)];
        snprintf(config_dir, PATHLEN-(sizeof CONFIG_FILE), "%s/sc-im", home);
        mkdir(config_dir,0777);
        snprintf(rcpath, PATHLEN, "%s/%s", config_dir, CONFIG_FILE);
        (void) readfile(rcpath, 0);
    }
    /* Default to compile time if XDG_CONFIG_HOME not found */
    else if ((home = getenv("HOME"))) {
        char config_dir[PATHLEN];
        sprintf(config_dir, "%s/%s", home, CONFIG_DIR);
        mkdir(config_dir,0777);
        snprintf(rcpath, PATHLEN, "%s/%s/%s", home, CONFIG_DIR, CONFIG_FILE);
        (void) readfile(rcpath, 0);
    }
}


/**
 * \brief Check if a file exists
 * \details Check if a file exists.
 * \param[in] fname file name
 * \return 1 if file exists; 0 otherwise
 */
int file_exists(const char * fname) {
    FILE * file;
    if ((file = fopen(fname, "r"))) {
        fclose(file);
        return 1;
    }
    return 0;
}


/**
 * \brief Check if file has been modified since last save.
 * \details This function checks if a file suffered mods since it was open.
 * \return 0 if not modified; 1 if modified
 */
int modcheck() {
    struct roman * roman = session->cur_doc;
    if (roman->modflg && ! get_conf_int("nocurses")) {
        sc_error("File not saved since last change. Add '!' to force");
        return(1);
    }
    return 0;
}


/**
 * \brief Handle the save file process
 * This function handles the save file process
 * \return 0 on OK; -1 on error
 */
int savefile() {
    struct roman * doc = session->cur_doc;
    char * curfile = doc->name;
    int force_rewrite = 0;
    char name[BUFFERSIZE];

#ifndef NO_WORDEXP
    size_t len;
    wordexp_t p;
#endif

    if ((curfile == NULL || ! curfile[0]) && wcslen(inputline) < 3) { // casos ":w" ":w!" ":x" ":x!"
        sc_error("There is no filename");
        return -1;
    }

    if (inputline[1] == L'!') force_rewrite = 1;

    wcstombs(name, inputline, BUFFERSIZE);
    del_range_chars(name, 0, 1 + force_rewrite);

#ifndef NO_WORDEXP
    int rc;
    if((rc=wordexp(name, &p, 0))!=0){;
        sc_error("Failed expanding filepath: %s",worderror(rc));
        return -1;
    }
    if ((len = strlen(p.we_wordv[0])) >= sizeof(name)) {
        sc_error("File path too long");
        wordfree(&p);
        return -1;
    }
    memcpy(name, p.we_wordv[0], len+1);
    wordfree(&p);
#endif

    if (! force_rewrite && file_exists(name)) {
        sc_error("File already exists. Use \"!\" to force rewrite.");
        return -1;
    }

#ifdef AUTOBACKUP
    // check if backup of curfile exists.
    // if it exists, remove it.
    if (curfile != NULL && strlen(curfile) && backup_exists(curfile)) remove_backup(curfile);

    // check if backup of newfilename exists.
    // if it exists and '!' is set, remove it.
    // if it exists and no '!' is set, return.
    if ((curfile == NULL || ! strlen(curfile)) && backup_exists(name)) {
        if (!force_rewrite) {
            sc_error("Backup file of %s exists. Use \"!\" to force the write process.", name);
            return -1;
        } else remove_backup(name);
    }
#endif

    if (doc->name == NULL) doc->name = malloc(sizeof(char)*PATHLEN);
    curfile = doc->name;
    // copy newfilename to curfile
    if (wcslen(inputline) > 2) {
        strcpy(doc->name, name);
    }

    // add sc extension if not present
    if (wcslen(inputline) > 2 && str_in_str(curfile, ".") == -1) {
        sprintf(curfile + strlen(curfile), ".sc");

    // treat xlsx format
    } else if (strlen(curfile) > 5 && ! strcasecmp( & curfile[strlen(curfile)-5], ".xlsx")) {
#ifndef XLSX_EXPORT
        sc_error("XLSX export support not compiled in. Please save file in other extension.");
        return -1;
#else
        if (export_xlsx(curfile) == 0) {
            sc_info("File \"%s\" written", curfile);
            doc->modflg = 0;
        } else
            sc_error("File could not be saved");
        return 0;
#endif
    // legacy binary xls cannot be written back (read-only import); ask for xlsx instead
    } else if (strlen(curfile) > 4 && ! strcasecmp( & curfile[strlen(curfile)-4], ".xls")) {
        sc_error("Cannot save \'%s\' file. XLS writing is not supported, use .xlsx instead.", curfile);
        return -1;
    }

    // save in sc format
    if (writefile(curfile, 1) < 0) {
        sc_error("File could not be saved");
        return -1;
    }
    doc->modflg = 0;
    return 0;
}


/**
 * \brief Write current Doc(roman) to file in (.sc) sc-im format
 * \details Write a file. Receives parameter range and file name.
 * \param[in] fname file name
 * \param[in] r0
 * \param[in] c0
 * \param[in] rn
 * \param[in] cn
 * \param[in] verbose
 *
 * \return 0 on success; -1 on error
 */
int writefile(char * fname, int verbose) {
    FILE * f;
    char save[PATHLEN];
    char tfname[PATHLEN];
    int pid;

    (void) strcpy(tfname, fname);

    (void) strcpy(save, tfname);

    if ((f = openfile(tfname, &pid, NULL)) == NULL) {
        if (verbose) sc_error("Can't create file \"%s\"", save);
        return -1;
    }

    if (verbose) sc_info("Writing file \"%s\"...", save);

    // traverse sheets of the current doc and save the data to file
    write_fd(f, session->cur_doc);

    closefile(f, pid, 0);

    if (! pid) {
        (void) strcpy(session->cur_doc->name, save);
        session->cur_doc->modflg = 0;
        if (verbose) sc_info("File \"%s\" written", session->cur_doc->name);
    }

    return 0;
}


/**
 * \brief write_fd()
 * \details traverse sheets of the given Doc and save the data to file
 *
 * \param[in] f file pointer
 * \param[in] r0
 * \param[in] c0
 * \param[in] rn
 * \param[in] cn
 *
 * \return none
 */
void write_fd(FILE * f, struct roman * doc) {
    struct ent **pp;
    int r, c;

    (void) fprintf(f, "# This data file was generated by the Spreadsheet Calculator Improvised (sc-im)\n");
    (void) fprintf(f, "# You almost certainly shouldn't edit it.\n\n");
    print_options(f);
    for (c = 0; c < COLFORMATS; c++)
        if (colformat[c])
            (void) fprintf (f, "format %d = \"%s\"\n", c, colformat[c]);

    //create sheets
    struct sheet * sh = doc->first_sh;
    while (sh != NULL) {
        fprintf (f, "newsheet \"%s\"\n", sh->name);
        sh = sh->next;
    }

    //traverse sheets
    sh = doc->first_sh;
    while (sh != NULL) {
        fprintf (f, "movetosheet \"%s\"\n", sh->name);

        // save off screen values
        fprintf (f, "offscr_sc_cols %d\n", sh->offscr_sc_cols);
        fprintf (f, "offscr_sc_rows %d\n", sh->offscr_sc_rows);
        fprintf (f, "nb_frozen_rows %d\n", sh->nb_frozen_rows);
        fprintf (f, "nb_frozen_cols %d\n", sh->nb_frozen_cols);
        fprintf (f, "nb_frozen_screenrows %d\n", sh->nb_frozen_screenrows);
        fprintf (f, "nb_frozen_screencols %d\n", sh->nb_frozen_screencols);

        for (c = 0; c <= sh->maxcol; c++)
            if (sh->fwidth[c] != DEFWIDTH || sh->precision[c] != DEFPREC || sh->realfmt[c] != DEFREFMT)
                (void) fprintf (f, "format %s %d %d %d\n", coltoa(c), sh->fwidth[c], sh->precision[c], sh->realfmt[c]);

        for (r = 0; r <= sh->maxrow; r++)
            if (sh->row_format[r] != 1)
                (void) fprintf (f, "format %d %d\n", r, sh->row_format[r]);

        // new implementation of hidecol. group by ranges
        for (c = 0; c <= sh->maxcol; c++) {
            int c_aux = c;
            if ( sh->col_hidden[c] && c <= sh->maxcol && ( c == 0 || ! sh->col_hidden[c-1] )) {
                while (c_aux <= sh->maxcol && sh->col_hidden[c_aux])
                    c_aux++;
                fprintf(f, "hidecol %s", coltoa(c));
                if (c_aux-1 != c) {
                    fprintf(f, ":%s\n", coltoa(c_aux-1));
                    c = c_aux-1;
                } else
                    fprintf(f, "\n");
            }
        }

        // new implementation of hiderow. group by ranges
        for (r = 0; r <= sh->maxrow; r++) {
            int r_aux = r;
            if ( sh->row_hidden[r] && r <= sh->maxrow && ( r == 0 || ! sh->row_hidden[r-1] )) {
                while (r_aux <= sh->maxrow && sh->row_hidden[r_aux])
                    r_aux++;
                fprintf(f, "hiderow %d", r);
                if (r_aux-1 != r) {
                    fprintf(f, ":%d\n", r_aux-1);
                    r = r_aux-1;
                } else
                    fprintf(f, "\n");
            }
        }

        // frozen cols. group by ranges
        for (c = 0; c <= sh->maxcol; c++) {
            int c_aux = c;
            if (sh->col_frozen[c] && c <= sh->maxcol && (c == 0 || ! sh->col_frozen[c-1])) {
                while (c_aux <= sh->maxcol && sh->col_frozen[c_aux]) c_aux++;
                fprintf(f, "freeze %s", coltoa(c));
                if (c_aux-1 != c) {
                    fprintf(f, ":%s\n", coltoa(c_aux-1));
                    c = c_aux-1;
                } else
                    fprintf(f, "\n");
            }
        }

        // frozen rows. group by ranges
        for (r = 0; r <= sh->maxrow; r++) {
            int r_aux = r;
            if (sh->row_frozen[r] && r <= sh->maxrow && (r == 0 || ! sh->row_frozen[r-1])) {
                while (r_aux <= sh->maxrow && sh->row_frozen[r_aux]) r_aux++;
                fprintf(f, "freeze %d", r);
                if (r_aux-1 != r) {
                    fprintf(f, ":%d\n", r_aux-1);
                    r = r_aux-1;
                } else
                    fprintf(f, "\n");
            }
        }

        write_cells(f, doc, sh, 0, 0, sh->maxrow, sh->maxcol, 0, 0);

        struct custom_color * cc;
        for (r = 0; r <= sh->maxrow; r++) {
            pp = ATBL(sh, sh->tbl, r, 0);
            for (c = 0; c <= sh->maxcol; c++, pp++)
                if (*pp) {
                    // Write ucolors
                    if ((*pp)->ucolor != NULL) {
                        char strcolorbuf[BUFFERSIZE];
                        char * strcolor = strcolorbuf;
                        strcolor[0] = 0;
                        strcolor[1] = 0;

                        // decompile int value of color to its string description
                        if ((*pp)->ucolor->fg != NONE_COLOR) {
                            if ((*pp)->ucolor->fg <= 8) {
                                linelim=0;
                                struct enode * e = new((*pp)->ucolor->fg, (struct enode *)0, (struct enode *)0);
                                decompile(e, 0);
                                uppercase(line);
                                del_char(line, 0);
                                sprintf(strcolor, " fg=%.*s", BUFFERSIZE-5, &line[0]);
                                free(e);
                            } else if ((cc = get_custom_color_by_number((*pp)->ucolor->fg - 7)) != NULL) {
                                sprintf(strcolor, " fg=%.*s", BUFFERSIZE-5, cc->name);
                            }
                        }

                        if ((*pp)->ucolor->bg != NONE_COLOR) {
                            if ((*pp)->ucolor->bg <= WHITE) {
                                linelim=0;
                                struct enode * e = new((*pp)->ucolor->bg, (struct enode *)0, (struct enode *)0);
                                decompile(e, 0);
                                uppercase(line);
                                del_char(line, 0);
                                sprintf(strcolor + strlen(strcolor), " bg=%.*s", BUFFERSIZE-5, &line[0]);
                                free(e);
                            } else if ((cc = get_custom_color_by_number((*pp)->ucolor->bg - 7)) != NULL) {
                                sprintf(strcolor + strlen(strcolor), " bg=%.*s", BUFFERSIZE-5, cc->name);
                            }
                        }

                        if ((*pp)->ucolor->bold)      sprintf(strcolor + strlen(strcolor), " bold=1");
                        if ((*pp)->ucolor->italic)    sprintf(strcolor + strlen(strcolor), " italic=1");
                        if ((*pp)->ucolor->dim)       sprintf(strcolor + strlen(strcolor), " dim=1");
                        if ((*pp)->ucolor->reverse)   sprintf(strcolor + strlen(strcolor), " reverse=1");
                        if ((*pp)->ucolor->standout)  sprintf(strcolor + strlen(strcolor), " standout=1");
                        if ((*pp)->ucolor->underline) sprintf(strcolor + strlen(strcolor), " underline=1");
                        if ((*pp)->ucolor->blink)     sprintf(strcolor + strlen(strcolor), " blink=1");

                        // Remove the leading space
                        strcolor++;

                        // previous implementation
                        //(void) fprintf(f, "cellcolor %s%d \"%s\"\n", coltoa((*pp)->col), (*pp)->row, strcolor);

                        // new implementation
                        // by row, store cellcolors grouped by ranges
                        int c_aux = c;
                        struct ucolor * u = (*pp)->ucolor;
                        struct ucolor * a = NULL;
                        if ( c > 0 && *ATBL(sh, sh->tbl, r, c-1) != NULL)
                            a = (*ATBL(sh, sh->tbl, r, c-1))->ucolor;

                        if ( *strcolor != '\0' && (u != NULL) && (c <= sh->maxcol) && ( c == 0 || ( a == NULL ) || ( a != NULL && ! same_ucolor( a, u ) ))) {
                            while (c_aux <= sh->maxcol && *ATBL(sh, sh->tbl, r, c_aux) != NULL && same_ucolor( (*ATBL(sh, sh->tbl, r, c_aux))->ucolor, (*pp)->ucolor ))
                                c_aux++;
                            fprintf(f, "cellcolor %s%d", coltoa((*pp)->col), (*pp)->row);
                            if (c_aux-1 != (*pp)->col)
                                fprintf(f, ":%s%d \"%s\"\n", coltoa(c_aux-1), (*pp)->row, strcolor);
                            else
                                fprintf(f, " \"%s\"\n", strcolor);
                        }

                    }

                    /* if ((*pp)->nrow >= 0) {
                       (void) fprintf(f, "addnote %s ", v_name((*pp)->row, (*pp)->col));
                       (void) fprintf(f, "%s\n", r_name((*pp)->nrow, (*pp)->ncol, (*pp)->nlastrow, (*pp)->nlastcol));
                       } */

                    // padding
                    // previous implementation
                    //if ((*pp)->pad)
                    //    (void) fprintf(f, "pad %d %s%d\n", (*pp)->pad, coltoa((*pp)->col), (*pp)->row);
                    // new implementation
                    int r_aux = r;
                    if ( (*pp)->pad  && r <= sh->maxrow && ( r == 0 || (*ATBL(sh, sh->tbl, r-1, c) == NULL) ||
                                (*ATBL(sh, sh->tbl, r-1, c) != NULL && ((*ATBL(sh, sh->tbl, r-1, c))->pad != (*pp)->pad)) )) {
                        while (r_aux <= sh->maxrow && *ATBL(sh, sh->tbl, r_aux, c) != NULL && (*pp)->pad == (*ATBL(sh, sh->tbl, r_aux, c))->pad )
                            r_aux++;
                        fprintf(f, "pad %d %s%d", (*pp)->pad, coltoa((*pp)->col), (*pp)->row);
                        if (r_aux-1 != (*pp)->row)
                            fprintf(f, ":%s%d\n", coltoa((*pp)->col), r_aux-1);
                        else
                            fprintf(f, "\n");
                    }
                }
        }

        // write locked cells
        // lock should be stored after any other command
        for (r = 0; r <= sh->maxrow; r++) {
            pp = ATBL(sh, sh->tbl, r, 0);
            for (c = 0; c <= sh->maxcol; c++, pp++)
                if (*pp) {
                    // previous implementation
                    //if ((*pp)->flags & is_locked)
                    //    (void) fprintf(f, "lock %s%d\n", coltoa((*pp)->col), (*pp)->row);
                    // new implementation
                    int c_aux = c;
                    if ( (*pp)->flags & is_locked && c <= sh->maxcol && ( c == 0 || ( *ATBL(sh, sh->tbl, r, c-1) != NULL && ! ((*ATBL(sh, sh->tbl, r, c-1))->flags & is_locked) ) )) {
                        while (c_aux <= sh->maxcol && *ATBL(sh, sh->tbl, r, c_aux) != NULL && (*ATBL(sh, sh->tbl, r, c_aux))->flags & is_locked )
                            c_aux++;
                        fprintf(f, "lock %s%d", coltoa((*pp)->col), (*pp)->row);
                        if (c_aux-1 != (*pp)->col)
                            fprintf(f, ":%s%d\n", coltoa(c_aux-1), (*pp)->row);
                        else
                            fprintf(f, "\n");
                    }
                }
        }

        /*
         * Don't try to combine these into a single fprintf().  v_name() has
         * a single buffer that is overwritten on each call, so the first part
         * needs to be written to the file before making the second call.
         */

        if((sh->next == NULL) && strcmp(sh->name,doc->cur_sh->name) != 0){
            fprintf(f, "movetosheet \"%s\"\n", doc->cur_sh->name);
            fprintf(f, "goto %s", v_name(doc->cur_sh->currow, doc->cur_sh->curcol));
        fprintf(f, "\n");
        } else {
        //fprintf(f, " %s\n", v_name(strow, stcol));
        fprintf(f, "goto %s", v_name(sh->currow, sh->curcol));
        //fprintf(f, " %s\n", v_name(strow, stcol));
        fprintf(f, "\n");
        }
        sh = sh->next;
    }
    // write marks of document
    write_marks(f);

}


/**
 * \brief write_marks()
 * \param[in] f file pointer
 * \return none
 */
void write_marks(FILE * f) {
    int i;
    struct mark * m;

    for ( i='a'; i<='z'; i++ ) {
        m = get_mark((char) i);

        // m->rng should never be NULL if both m->col and m->row are -1 !!
        if ( m->row == -1 && m->col == -1) {
            fprintf(f, "mark %c \"%s\" %s%d ", i, m->sheet->name, coltoa(m->rng->tlcol), m->rng->tlrow);
            fprintf(f, "%s%d\n", coltoa(m->rng->brcol), m->rng->brrow);
        } else if ( m->row != 0 && m->row != 0) {
            fprintf(f, "mark %c \"%s\" %s%d\n", i, m->sheet->name, coltoa(m->col), m->row);
        }
    }
    return;
}


/**
 * \brief write_cells()
 * \param[in] struct roman * doc
 * \param[in] struct sheet * sh
 * \param[in] r0
 * \param[in] f file pointer
 * \param[in] r0
 * \param[in] c0
 * \param[in] rn
 * \param[in] cn
 * \param[in] dr
 * \param[in[ dc
 * \return none
 */
void write_cells(FILE * f, struct roman * doc, struct sheet * sh, int r0, int c0, int rn, int cn, int dr, int dc) {
    struct ent ** pp;
    int r, c;
    char * dpointptr;

    int mf = doc->modflg;
    if (dr != r0 || dc != c0) {
        //yank_area(r0, c0, rn, cn);
        rn += dr - r0;
        cn += dc - c0;
        //rs = sh->currow;
        //cs = sh->curcol;
        sh->currow = dr;
        sh->curcol = dc;
    }
    //if (Vopt) valueize_area(dr, dc, rn, cn);
    for (r = dr; r <= rn; r++) {
        pp = ATBL(sh, sh->tbl, r, dc);
        for (c = dc; c <= cn; c++, pp++)
            if (*pp) {
                if ((*pp)->label || (*pp)->flags & is_strexpr) {
                    edits(sh, r, c, 1);
                    (void) fprintf(f, "%s\n", line);
                }
                if ((*pp)->flags & is_valid) {
                //if ((*pp)->flags & is_valid || (*pp)->expr) { // for #541
                    editv(sh, r, c);
                    dpointptr = strchr(line, dpoint);
                    if (dpointptr != NULL)
                        *dpointptr = '.';
                    (void) fprintf(f, "%s\n", line);
                }
                if ((*pp)->format) {
                    editfmt(sh, r, c);
                    (void) fprintf(f, "%s\n",line);
                }
                if ((*pp)->trigger != NULL) {
                    struct trigger * t = (*pp)->trigger;
                    char * mode = NULL;
                    if ((t->flag & (TRG_READ | TRG_WRITE)) == (TRG_READ | TRG_WRITE)) mode = "RW";
                    else if (t->flag & TRG_WRITE) mode = "W";
                    else if (t->flag & TRG_READ) mode = "R";
                    char * type = NULL;
                    if (t->flag & TRG_LUA) type = "LUA";
                    else type = "C";
                    fprintf(f, "trigger %s%d \"mode=%s type=%s file=%s function=%s\"\n", coltoa(c), r, mode, type, t->file, t->function);
                }
            }
    }
    // restore modflg just in case
    doc->modflg = mf;
}


/**
 * \brief Try to open a spreadsheet file.
 *
 * \param[in] fname file name
 * \param[in] eraseflg
 *
 * \return
 * SC_READFILE_SUCCESS if we could load the file,
 * SC_READFILE_ERROR if we failed,
 * SC_READFILE_DOESNTEXIST if the file doesn't exist.
 */
sc_readfile_result readfile(char * fname, int eraseflg) {
    struct roman * roman = session->cur_doc;
    char * curfile = roman->name;
    if (! strlen(fname)) return 0;
    roman->loading = 1;

#ifdef AUTOBACKUP
    // Check if curfile is set and backup exists..
    if (str_in_str(fname, CONFIG_FILE) == -1 && strlen(curfile) &&
    backup_exists(curfile) && strcmp(fname, curfile)) {
        if (roman->modflg) {
            // TODO - force load with '!' ??
            sc_error("There are changes unsaved. Cannot load file: %s", fname);
            roman->loading = 0;
            return SC_READFILE_ERROR;
        }
        remove_backup(curfile);
    }
    // Check if fname is set and backup exists..
    if (backup_exists(fname)) {
        wchar_t msg[BUFFERSIZE];
        swprintf(msg, BUFFERSIZE,
        // TODO - Open backup readonly ??
        L"Backup of %s file exists. Do you want to (E)dit the file and remove the backup, (R)ecover the backup or (Q)uit: ", fname);
        wchar_t t = ui_query_opt(msg, L"qerQER");
        switch (t) {
            case L'q':
            case L'Q':
                roman->loading = 0;
                extern int shall_quit;
                shall_quit = 1;
                return SC_READFILE_ERROR;
                break;
            case L'e':
            case L'E':
                remove_backup(fname);
                break;
            case L'r':
            case L'R':
                ;
                int len = strlen(fname);
                if (!len) return 0;
                char * pstr = strrchr(fname, '/');
                int pos = pstr == NULL ? -1 : pstr - fname;
                char bkpname[len+6];
                strcpy(bkpname, fname);
                add_char(bkpname, '.', pos+1);
                sprintf(bkpname + strlen(bkpname), ".bak");
                remove(fname);
                rename(bkpname, fname);
                break;
        }
    }
#endif

    // Check if file is a correct format
    int len = strlen(fname);
    if (! strcmp( & fname[len-3], ".sc") ||
        (len >= strlen(CONFIG_FILE) && ! strcasecmp( & fname[len-strlen(CONFIG_FILE)], CONFIG_FILE))) {
        // pass

    // If file is an xlsx file, we import it
    } else if (len > 5 && ! strcasecmp( & fname[len-5], ".xlsx")){
        #ifndef XLSX
        sc_error("XLSX import support not compiled in");
        #else
        open_xlsx(fname, "UTF-8");
        if (roman->name != NULL) free(roman->name);
        roman->name = malloc(sizeof(char)*PATHLEN);
        strcpy(roman->name, fname);
        roman->modflg = 0;
        #endif
        roman->loading = 0;
        //TODO we should EvalAll here
        return SC_READFILE_SUCCESS;

    // If file is an xls file, we import it
    } else if (len > 4 && ! strcasecmp( & fname[len-4], ".xls")){
        #ifndef XLS
        sc_error("XLS import support not compiled in");
        #else
        open_xls(fname, "UTF-8");
        roman->modflg = 0;
        if (roman->name != NULL) free(roman->name);
        roman->name = malloc(sizeof(char)*PATHLEN);
        strcpy(roman->name, fname);
        #endif
        roman->loading = 0;
        return SC_READFILE_SUCCESS;

    } else {
        sc_info("\"%s\" is not an Excel-compatible file (.xlsx / .xls)", fname);
        roman->loading = 0;
        return SC_READFILE_ERROR;
    }

    // We open an 'sc' format file
    // open fname for reading
    register FILE * f;
    char save[PATHLEN];
    if (*fname == '\0') fname = curfile;
    (void) strcpy(save, fname);
    f = fopen(save, "r");
    if (f == NULL) {
        roman->loading = 0;
        if (strstr(save, "scimrc") == NULL) {
            if (roman->name != NULL) free(roman->name);
            roman->name = malloc(sizeof(char)*PATHLEN);
            strcpy(roman->name, save);
        }
        return SC_READFILE_DOESNTEXIST;
    }

    if (eraseflg) erasedb(roman->cur_sh, 0); //TODO handle file

    while (! brokenpipe && fgets(line, sizeof(line), f)) {
        linelim = 0;
        if (line[0] != '#') (void) yyparse();
    }
    fclose(f);

    roman->loading = 0;
    linelim = -1;
    if (eraseflg) {
        cellassign = 0;
    }
    if (strstr(save, "scimrc") == NULL) {
        if (roman->name != NULL) free(roman->name);
        roman->name = malloc(sizeof(char)*PATHLEN);
        strcpy(roman->name, save);
    }
    EvalAll();
    roman->modflg = 0;
    return SC_READFILE_SUCCESS;
}


/**
 * \brief Expand a ~ in path to the user's home directory
 * \param[in] path
 * \return path
 */
char * findhome(char * path) {
    static char * HomeDir = NULL;

    if (* path == '~') {
        char * pathptr;
        char tmppath[PATHLEN];

        if (HomeDir == NULL) {
            HomeDir = getenv("HOME");
            if (HomeDir == NULL)
                HomeDir = "/";
        }
        pathptr = path + 1;
        if ((* pathptr == '/') || (* pathptr == '\0'))
            strcpy(tmppath, HomeDir);
        else {
            struct passwd * pwent;
            char * namep;
            char name[50];

            namep = name;
            while ((*pathptr != '\0') && (*pathptr != '/'))
                *(namep++) = *(pathptr++);
            *namep = '\0';
            if ((pwent = getpwnam(name)) == NULL) {
                (void) sprintf(path, "Can't find user %s", name);
                return (NULL);
            }
            strcpy(tmppath, pwent->pw_dir);
        }
        strcat(tmppath, pathptr);
        strcpy(path, tmppath);
    }
    return (path);
}


/**
 * \brief Open the input or output file
 * \details Open the input or output file, setting up a pipe if needed.
 * \param[in] fname file name
 * \param[in] rpid
 * \param[in] rfd
 * \return file pointer
 */
FILE * openfile(char *fname, int *rpid, int *rfd) {
    int pipefd[4];
    int pid;
    FILE *f;
    char *efname;

    while (*fname && (*fname == ' '))    // Skip leading blanks
        fname++;

    if (*fname != '|') {                 // Open file if not pipe
        *rpid = 0;
        if (rfd != NULL)
            *rfd = 1;                    // Set to stdout just in case

        efname = findhome(fname);
        return (fopen(efname, rfd == NULL ? "w" : "r"));
    }

    fname++;                             // Skip |
    efname = findhome(fname);
    if (pipe(pipefd) < 0 || (rfd != NULL && pipe(pipefd+2) < 0)) {
        sc_error("Can't make pipe to child");
        *rpid = 0;
        return (0);
    }

    //deraw(rfd==NULL);

    if ((pid=fork()) == 0) {             // if child
        (void) close(0);                 // close stdin
        (void) close(pipefd[1]);
        (void) dup(pipefd[0]);           // connect to first pipe
        if (rfd != NULL) {               // if opening for read
            (void) close(1);             // close stdout
            (void) close(pipefd[2]);
            (void) dup(pipefd[3]);       // connect to second pipe
        }
        (void) signal(SIGINT, SIG_DFL);  // reset
        execl("/bin/sh", "sh", "-c", efname, 0, (char *) NULL);
        exit (-127);
    } else {                             // else parent
        *rpid = pid;
        if ((f = fdopen(pipefd[(rfd==NULL?1:2)], rfd==NULL?"w":"r")) == NULL) {
            (void) kill(pid, 9);
            sc_error("Can't fdopen %sput", rfd==NULL?"out":"in");
            (void) close(pipefd[1]);
            if (rfd != NULL)
                (void) close(pipefd[3]);
            *rpid = 0;
            return (0);
        }
    }
    (void) close(pipefd[0]);
    if (rfd != NULL) {
        (void) close(pipefd[3]);
        *rfd = pipefd[1];
    }
    return (f);
}

/**
 * \brief Close a file opened by openfile()
 * \details Close a file opened by openfile(). If process, wait for return
 * \param[in] f file pointer
 * \param[in] pid
 * \param[in] rfd
 * \return none
 */
void closefile(FILE *f, int pid, int rfd) {
    int temp;
    wint_t wi;

    (void) fclose(f);
    if (pid) {
        while (pid != wait(&temp)) //;
        if (rfd==0) {
            printf("Press any key to continue ");
            fflush(stdout);
#ifdef NCURSES
            cbreak();
#endif
            ui_getch_b(&wi);
        } else {
            close(rfd);
#ifdef NCURSES
            if (! get_conf_int("nocurses")) {
                cbreak();
                nonl();
                noecho ();
            }
#endif
        }
    }
    if (brokenpipe) {
        sc_error("Broken pipe");
        brokenpipe = FALSE;
    }
}


/**
 * \brief TODO <brief function description>
 * \param[in] f file pointer
 * \return none
 */
void print_options(FILE *f) {
    if (
            ! optimize &&
            ! rndtoeven &&
            calc_order == BYROWS &&
            prescale == 1.0 &&
            ! get_conf_int("external_functions")
       )
        return; // No reason to do this

    (void) fprintf(f, "set");
    if (optimize)              (void) fprintf(f," optimize");
    if (rndtoeven)             (void) fprintf(f, " rndtoeven");
    if (calc_order != BYROWS ) (void) fprintf(f, " bycols");
    if (prescale != 1.0)       (void) fprintf(f, " prescale");
    if ( get_conf_int("external_functions") ) (void) fprintf(f, " external_functions");
    (void) fprintf(f, "\n");
}



void unspecial(FILE * f, char * str, int delim) {
    int backquote = 0;

    if (str_in_str(str, ",") != -1) backquote = 1;
    if (backquote) putc('\"', f);
    if (*str == '\\') str++; // delete wheeling string operator, OK?
    while (*str) {
        // for LATEX export
        if (delim == '&' && ( (*str == '&') || (*str == '$') ||
           (*str == '#') || (*str == '%') || (*str == '{') || (*str == '}') || (*str == '&')))
           putc('\\', f);
        putc(*str, f);
        str++;
    }
    if (backquote) putc('\"', f);
}


/**
 * \brief export_delim() - export current tbl to delimited format
 * \details Kept as an internal helper (used by the gnuplot "plot" feature
 * to write its temporary data file). It is no longer exposed as a
 * user-facing save/export file format now that this fork is Excel-only.
 * \param[in] fname full path of the file
 * \param[in] coldelim
 * \param[in] r0
 * \param[in] c0
 * \param[in] rn
 * \param[in] cn
 * \param[in] verbose
 * \return none
 */
void export_delim(char * fname, char coldelim, int r0, int c0, int rn, int cn, int verbose) {
    struct roman * roman = session->cur_doc;
    struct sheet * sh = roman->cur_sh;
    FILE * f;
    int row, col;
    struct ent ** pp;
    int pid;

    // to prevent empty lines at the end of the file
    struct ent * ent = go_end(sh);
    if (rn > ent->row) rn = ent->row;
    ent = goto_last_col(sh); // idem with columns
    if (cn > ent->col) cn = ent->col;

    if (verbose) sc_info("Writing file \"%s\"...", fname);

    if (fname == NULL)
        f = stdout;
    else {
        if ((f = openfile(fname, &pid, NULL)) == (FILE *)0) {
            if (verbose) sc_error ("Can't create file \"%s\"", fname);
            return;
        }
    }

    for (row = r0; row <= rn; row++) {
        for (pp = ATBL(sh, sh->tbl, row, col = c0); col <= cn; col++, pp++) {
            int last_valid_col = right_limit(sh, row)->col; // for issue #374
            if (col > last_valid_col) continue;
            if (*pp) {
                char field[FBUFLEN] = "";
                char * s;
                if ((*pp)->flags & is_valid) {
                    if ((*pp)->cellerror) {
                        (void) fprintf (f, "%*s", sh->fwidth[col], ((*pp)->cellerror == CELLERROR ? "ERROR" : "INVALID"));
                    } else if ((*pp)->format) {
                        if (*((*pp)->format) == 'd') {  // Date format
                            time_t v = (time_t) ((*pp)->v);
                            strftime(field, sizeof(field), ((*pp)->format)+1, localtime(&v));
                        } else {                        // Numeric format
                            format((*pp)->format, sh->precision[col], (*pp)->v, field, sizeof(field));
                        }
                        ltrim(field, ' ');
                        unspecial(f, field, coldelim);
                    } else { //eng number format
                        (void) engformat(sh->realfmt[col], sh->fwidth[col], sh->precision[col], (*pp)->v, field, sizeof(field));
                        ltrim(field, ' ');
                        unspecial(f, field, coldelim);
                    }
                }
                if (field[0] == '\0' && (s = (*pp)->label)) {
                    ltrim(s, ' ');
                    unspecial(f, s, coldelim);
                }
            }
            if (col < cn && col < last_valid_col)
                (void) fprintf(f,"%c", coldelim);
        }
        (void) fprintf(f,"\n");
    }
    if (fname != NULL)
        closefile(f, pid, 0);

    if (! pid && verbose) {
        sc_info("File \"%s\" written", fname);
    }
}


/**
 * \brief Check what is the max length of all the lines in a file
 * \details Check the maximum length of lines in a file. Note:
 * FILE * f shall be opened.
 * \param[in] f file pointer
 * \return file length + 1
 */
int max_length(FILE * f) {
    if (f == NULL) return -1;
    int count = 0, max = 0;
    int c = fgetc(f);

    while (c != EOF) {
        if (c != '\n') {
            count++;
        } else if (count > max) {
            max = count;
            count = 0;
        } else {
            count = 0;
        }
        c = fgetc(f);
    }
    return max + 1;
}


/**
 * \brief Check the number of lines of a file
 * \details Check the numbers of lines of a file. it count \n chars.
 * FILE * f shall be opened.
 * \param[in] f file pointer
 * \return number
 */
int count_lines(FILE * f) {
    int count = 0;
    if (f == NULL) return count;
    int c = fgetc(f);

    while (c != EOF) {
        if (c == '\n') count++;
        c = fgetc(f);
    }
    return count;
}


/**
 * \brief plugin_exists()
 * \param[in] name
 * \param[in] len
 * \param[in] path
 * \return none
 */
int plugin_exists(char * name, int len, char * path) {
    FILE * fp;
    static char * HomeDir;
    char cwd[1024];

    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        strcpy((char *) path, cwd);
        strcat((char *) path, "/");
        strncat((char *) path, name, len);
        if ((fp = fopen((char *) path, "r"))) {
            fclose(fp);
            return 1;
        }
    }
    /* Check XDG_CONFIG_HOME */
    if ((HomeDir = getenv("XDG_CONFIG_HOME"))) {
        sprintf((char *) path, "%s/sc-im/%s", HomeDir, name);
        if ((fp = fopen((char *) path, "r"))) {
            fclose(fp);
            return 1;
        }
    }
    /* Check compile time path (default ~/.config/sc-im) */
    if ((HomeDir = getenv("HOME"))) {
        sprintf((char *) path, "%s/%s/%s", HomeDir, CONFIG_DIR, name);
        if ((fp = fopen((char *) path, "r"))) {
            fclose(fp);
            return 1;
        }
    }
    /* LEGACY PATH */
    if ((HomeDir = getenv("HOME"))) {
        sprintf((char *) path, "%s/.scim/%s", HomeDir, name);
        if ((fp = fopen((char *) path, "r"))) {
            fclose(fp);
            return 1;
        }
    }
    strcpy((char *) path, HELP_PATH);
    strcat((char *) path, "/");
    strncat((char *) path, name, len);
    if ((fp = fopen((char *) path, "r"))) {
        fclose(fp);
        return 1;
    }
    return 0;
}


void * do_autobackup(void *arg) {
    char * curfile = session->cur_doc->name;
    int len;
    if (curfile == NULL || ! (len = strlen(curfile))) return (void *) -1;
    //if (session->cur_doc->loading || ! len) return (void *) -1;
    //if (! len || ! session->cur_doc->modflg) return (void *) -1;

    char * pstr = strrchr(curfile, '/');
    int pos = pstr == NULL ? -1 : pstr - curfile;
    char name[PATHLEN] = {'\0'};
    char namenew[PATHLEN] = {'\0'};
    strcpy(name, curfile);
    add_char(name, '.', pos+1);
    sprintf(name + strlen(name), ".bak");
    sprintf(namenew, "%.*s.new", PATHLEN-5, name);
    //if (get_conf_int("debug")) sc_info("doing autobackup of file:%s", name);

    // create new version
    if (! strcmp(&name[strlen(name)-7], ".sc.bak")) {
        register FILE * f;
        if ((f = fopen(namenew , "w")) == NULL) return (void *) -1;
        write_fd(f, session->cur_doc);
        fclose(f);
#ifdef XLSX_EXPORT
    } else if (! strcmp(&name[strlen(name)-9], ".xlsx.bak")) {
        export_xlsx(namenew);
#endif
    }

    // delete if exists name
    remove(name);

    // rename name.new to name
    rename(namenew, name);

    return (void *) 0;
}


/**
 * \brief Check if it is time to do an autobackup
 * \return none
 */
void handle_backup() {
    #ifdef AUTOBACKUP
    extern struct timeval lastbackup_tv; // last backup timer
    extern struct timeval current_tv; //runtime timer

    int autobackup = get_conf_int("autobackup");
    if (autobackup && autobackup > 0 && (current_tv.tv_sec - lastbackup_tv.tv_sec > autobackup || (lastbackup_tv.tv_sec == 0 && lastbackup_tv.tv_usec == 0))) {
        #ifdef HAVE_PTHREAD
            if (pthread_exists) pthread_join (fthread, NULL);
            pthread_exists = (pthread_create(&fthread, NULL, do_autobackup, NULL) == 0) ? 1 : 0;
        #else
            do_autobackup();
        #endif
        gettimeofday(&lastbackup_tv, NULL);
    }
    #endif
    return;
}


/**
 * \brief Remove autobackup file
 * \details Remove autobackup file. Used when quitting or when loading
 * a new file.
 * \param[in] file file pointer
 * \return none
 */
void remove_backup(char * file) {
    int len = strlen(file);
    if (!len) return;
    char * pstr = strrchr(file, '/');
    int pos = pstr == NULL ? -1 : pstr - file;
    char name[len+6];
    strcpy(name, file);
    add_char(name, '.', pos+1);
    sprintf(name + strlen(name), ".bak");
    remove(name);
    return;
}


/**
 * \brief TODO Document backup_exists()
 * \param[in] file file pointer
 * \return none
 */
int backup_exists(char * file) {
    int len = strlen(file);
    if (!len) return 0;
    char * pstr = strrchr(file, '/');
    int pos = pstr == NULL ? -1 : pstr - file;
    char name[len+6];
    strcpy(name, file);
    add_char(name, '.', pos+1);
    sprintf(name + strlen(name), ".bak");
    FILE * fp;
    if ((fp = fopen((char *) name, "r"))) {
        fclose(fp);
        return 1;
    }
    return 0;
}


/**
 * \brief open file nested
 * \param[in] file name string
 * \return none
 */
void openfile_nested(char * file) {
    char * cmd = get_conf_value("default_open_file_under_cursor_cmd");
    if (cmd == NULL || ! strlen(cmd)) return;
    char syscmd[PATHLEN + strlen(cmd)];
    sprintf(syscmd, "%s", cmd);
    sprintf(syscmd + strlen(syscmd), " %s", file);
    system(syscmd);
}


/**
 * \brief open file under cursor
 * \param[in] current row and column
 * \return none
 */
void openfile_under_cursor(int r, int c) {
    struct roman * roman = session->cur_doc;
    register struct ent ** pp;
    pp = ATBL(roman->cur_sh, roman->cur_sh->tbl, r, c);
    if (*pp && (*pp)->label) {
        char text[FBUFLEN] = "";
        strcpy(text, (*pp)->label);
        openfile_nested(text);
    }
}


/*
 * function that takes argv arguments and create a new
 * roman struct for each file and attach it to main session
 * DISABLED BY DESIGN
void readfile_argv(int argc, char ** argv) {
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--", 2) ) { // a file was passed as argv. try to handle it
            printf("%s\n", argv[i]);

            struct roman * roman = calloc(1, sizeof(struct roman));
            roman->name = argv[i];
            roman->first_sh = NULL;
            roman->cur_sh = NULL;

            // save roman inside session
            INSERT(roman, (session->first_doc), (session->last_doc), next, prev);
            session->cur_doc = roman; // important: set cur_doc!

            // malloc a sheet
            roman->cur_sh = roman->first_sh = new_sheet(roman, "Sheet1");

            // grow sheet tbl
            growtbl(roman->first_sh, GROWNEW, 0, 0);

            load_sc(argv[i]);
        }
    }
    return;
}
*/


/*
 * \brief load a file into a roman struct
 * the file may contain multiple sheets
 * \return none
 */
void load_file(char * file) {
    if (file == NULL || file[0] == '\0') return;
    // Do not calloc a new roman struct everytime. See 783 PR discussion.
    // use the one allocated when loading rc file.
    struct roman * roman = session->cur_doc;
    if (roman == NULL) {
        roman = calloc(1, sizeof(struct roman));
        // save roman inside session
        INSERT(roman, (session->first_doc), (session->last_doc), next, prev);
        session->cur_doc = roman; // important: set cur_doc!
    }
    // prevent memory leak 16/02/24
    if (roman->cur_sh != NULL) {
        delete_sheet(roman, roman->cur_sh, 0);
    }

    roman->name = ! strlen(file) ? NULL : strdup(file);
    roman->first_sh = NULL;
    roman->cur_sh = NULL;

    // malloc a clean sheet
    // to make old sc file loading backwards compatible, mark it as is_allocated
    roman->cur_sh = roman->first_sh = new_sheet(roman, "Sheet1");
    roman->cur_sh->flags |= is_allocated;

    // grow sheet tbl
    growtbl(roman->first_sh, GROWNEW, 0, 0);

    // load_tbl may open an sc file or a new sc-im file that can handle sheets.
    // new_sheet() would reuse Sheet1 if loading sc-im file.
    load_tbl(file);

    // now mark 'Sheet1' as empty, removing is_allocated mark.
    roman->first_sh->flags &= ~is_allocated;
    roman->first_sh->flags |= is_empty;
    return;
}


/**
 * \brief Attempt to load a tbl into a sheet
 * \return none
 */


void load_tbl(char * loading_file) {
    char name[PATHLEN];
    strcpy(name, ""); //force name to be empty
    #ifdef NO_WORDEXP
    size_t len;
    #else
    int c;
    wordexp_t p;
    #endif

    #ifdef NO_WORDEXP
    if ((len = strlen(loading_file)) >= sizeof(name)) {
        sc_info("File path too long: '%s'", loading_file);
        return;
    }
    memcpy(name, loading_file, len+1);
    #else
    int rc;
    if((rc = wordexp(loading_file, &p, 0))== 0){
        for (c=0; c < p.we_wordc; c++) {
            if (c) sprintf(name + strlen(name), " ");
            sprintf(name + strlen(name), "%s", p.we_wordv[c]);
        }
        wordfree(&p);
    } else{
        sc_error("%s", worderror(rc));
        return;
    }

    #endif

    if (strlen(name) != 0) {
        sc_readfile_result result = readfile(name, 0);
        if (! get_conf_int("nocurses")) {
            if (result == SC_READFILE_DOESNTEXIST) {
                // It's a new record!
                sc_info("New file: \"%s\"", name);
            } else if (result == SC_READFILE_ERROR) {
                sc_info("\"%s\" is not a sc-im compatible file", name);
            }
        }
    }
}

const char *worderror(int errnum) {
    switch (errnum)
    {
    case WRDE_BADCHAR:
        return "File name with <newline>, '|', '&', ';', '<', '>', '(', ')', '{', '}' not supported";
    case WRDE_BADVAL:
        return "Reference to undefined shell variable when WRDE_UNDEF was set in flags to wordexp()";
    case WRDE_CMDSUB:
        return "Command substitution requested when WRDE_NOCMD was set in flags to wordexp()";
    case WRDE_NOSPACE:
        return "Attempt to allocate memory in wordexp() failed";
    case WRDE_SYNTAX:
        return "Shell syntax error, such as unbalanced parentheses or unterminated string";
    default:
        return "Unknown error from wordexp() function";
    }
}

/**
 * \brief create an empty workbook and attach it to session
 * \return [int] -> return -1 (and quit app) if cannot alloc - return 0 on success
 */
int create_empty_wb() {
        struct roman * roman = calloc(1, sizeof(struct roman));
        roman->name = NULL;
        roman->first_sh = NULL;
        roman->cur_sh = NULL;

        // save roman inside session
        INSERT(roman, (session->first_doc), (session->last_doc), next, prev);
        session->cur_doc = roman; // important: set cur_doc!

        // malloc a sheet
        roman->cur_sh = roman->first_sh = new_sheet(roman, "Sheet1");

        // grow sheet tbl
        if (! growtbl(roman->first_sh, GROWNEW, 0, 0)) {
            exit_app(-1);
            return -1;
        }

        erasedb(roman->first_sh, 0);

        // mark 'Sheet1' as empty, removing is_allocated mark.
        roman->first_sh->flags |= is_empty;
        return 0;
}
