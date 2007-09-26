#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct Exp Exp;

struct Exp
{
  int ref;
  int old;
  int idx;
  char* op;
  int width;
  int childs;
  int child[3];
  char* name;
};

static int verbose;

static const char* input_name;
static const char* output_name;
static const char* run_name;

static FILE* input;
static int lineno;
static int saved;

static char* tmp;
static char* cmd;

static Exp* exps;
static int sexps;
static int nexps;

static int iexps;
static int rexps;

static char* buf;
static int sbuf;
static int nbuf;

static int maxwidth;
static int runs;

static void
msg (int level, const char* fmt, ...)
{
  va_list ap;
  if (verbose < level) return;
  fflush (stdout);
  fputs ("[deltabtor] ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
}

static void
die (const char* fmt, ...)
{
  va_list ap;
  fflush (stdout);
  fputs ("*** deltabtor: ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
  exit (1);
}

static int
next (void)
{
  int res;

  if (saved != EOF)
  {
    res   = saved;
    saved = EOF;
  }
  else
    res = getc (input);

  if (res == ';')
  {
  SKIP_COMMENTS:
    while ((res = getc (input)) != '\n' && res != EOF)
      ;
  }
  else if (res == ' ' || res == '\t')
  {
    while ((res = getc (input)) == ' ' || res == '\t')
      ;

    if (res == ';') goto SKIP_COMMENTS;

    if (res != '\n')
    {
      saved = res;
      res   = ' ';
    }
  }

  if (res == '\n') lineno++;

  return res;
}

static void
perr (const char* fmt, ...)
{
  va_list ap;
  fflush (stdout);
  fprintf (stderr, "%s:%d: ", input_name, lineno);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
  exit (1);
}

static void
parse (void)
{
  int ch, idx, lit, sign, width, child[3], childs;
  char *op, *name;

  lineno = 1;
  saved  = EOF;

EXP:

  name = 0;
  op   = 0;

  while ((ch = next ()) == '\n')
    ;

  if (ch == EOF)
  {
    msg (1, "parsed %d expressions", iexps);
    return;
  }

  if (!isdigit (ch)) perr ("expected digit but got '0x%02x'", ch);

  idx = ch - '0';
  while (isdigit (ch = next ())) idx = 10 * idx + (ch - '0');

  if (ch != ' ') perr ("expected space after index %d", idx);

  assert (!nbuf);

  while ('a' <= (ch = next ()) && (ch <= 'z'))
  {
    if (nbuf == sbuf) buf = realloc (buf, sbuf = sbuf ? 2 * sbuf : 1);

    buf[nbuf++] = ch;
  }

  if (nbuf == sbuf) buf = realloc (buf, sbuf = sbuf ? 2 * sbuf : 1);
  buf[nbuf] = 0;

  if (ch != ' ') perr ("expected space after '%s'", buf);

  nbuf = 0;
  op   = strdup (buf);

  width    = 0;
  childs   = 0;
  child[0] = child[1] = child[2] = 0;

LIT:

  ch = next ();

  if (width && !childs)
  {
    if (!strcmp (op, "const") || !strcmp (op, "consth")
        || !strcmp (op, "constd") || !strcmp (op, "var"))
    {
      assert (!nbuf);

      while (!isspace (ch))
      {
        if (nbuf == sbuf) buf = realloc (buf, sbuf = sbuf ? 2 * sbuf : 1);

        buf[nbuf++] = ch;
        ch          = next ();
      }

      if (nbuf == sbuf) buf = realloc (buf, sbuf = sbuf ? 2 * sbuf : 1);
      buf[nbuf] = 0;
      nbuf      = 0;

      name = strdup (buf);

      goto INSERT;
    }
  }

  if (isdigit (ch) || ch == '-')
  {
    if (ch == '-')
    {
      sign = -1;
      ch   = next ();
      if (!isdigit (ch)) perr ("expected digit after '-' but got '0x%02x'", ch);
    }
    else
      sign = 1;

    lit = ch - '0';
    while (isdigit (ch = next ())) lit = 10 * lit + (ch - '0');

    lit *= sign;

    if (childs == 3)
      perr ("more than three childs");
    else if (width)
      child[childs++] = lit;
    else if (lit <= 0)
      perr ("expected positive width");
    else
      width = lit;

    if (ch == ' ') goto LIT;

    if (ch != '\n') perr ("expected space or new line");

    if (!width)
    WIDTH_MISSING:
      perr ("width missing");

  INSERT:
    assert (idx >= 1);

    while (nexps <= idx)
    {
      if (nexps == sexps) exps = realloc (exps, (sexps *= 2) * sizeof *exps);

      exps[nexps++].ref = 0;
    }

    if (exps[idx].ref) perr ("expression %d defined twice", idx);

    exps[idx].ref      = idx;
    exps[idx].idx      = 0;
    exps[idx].old      = 0;
    exps[idx].op       = op;
    exps[idx].width    = width;
    exps[idx].childs   = childs;
    exps[idx].child[0] = child[0];
    exps[idx].child[1] = child[1];
    exps[idx].child[2] = child[2];
    exps[idx].name     = name;

    iexps++;

    if (width > maxwidth) maxwidth = width;

    goto EXP;
  }

  if (!width) goto WIDTH_MISSING;

  perr ("expected digit but got '%c'", ch);
}

static void
save (void)
{
  Exp* e;
  int i;

  for (i = 1; i < nexps; i++)
  {
    e      = exps + i;
    e->old = e->ref;
  }
}

static int
deref (int start)
{
  int res, sign, tmp;

  sign = (start < 0);
  if (sign) start = -start;

  assert (0 < start);
  assert (start < nexps);

  tmp = exps[start].ref;

  assert (tmp);

  if (tmp != start)
    res = deref (tmp);
  else
    res = start;

  exps[start].ref = res;

  if (sign) res = -res;

  return res;
}

static int
deidx (int start)
{
  int res, sign, tmp;

  tmp  = deref (start);
  sign = (tmp < 0);
  if (sign) tmp = -tmp;

  res = exps[tmp].idx;
  assert (res);

  if (sign) res = -res;

  return res;
}

static void
simp (void)
{
  int i;
  rexps = 0;
  for (i = 1; i < nexps; i++)
    if (exps[i].ref) (void) deref (i);
}

static int
ischild (Exp* e, int child)
{
  assert (e->ref);

  if (child >= e->childs) return 0;

  assert (child >= 0);

  if (!strcmp (e->op, "array")) return 0;
  if (!strcmp (e->op, "const")) return 0;
  if (!strcmp (e->op, "consth")) return 0;
  if (!strcmp (e->op, "constd")) return 0;
  if (!strcmp (e->op, "root") && child != 0) return 0;
  if (!strcmp (e->op, "sext") && child != 0) return 0;
  if (!strcmp (e->op, "slice") && child != 0) return 0;
  if (!strcmp (e->op, "uext") && child != 0) return 0;
  if (!strcmp (e->op, "var")) return 0;
  if (!strcmp (e->op, "zero")) return 0;

  return 1;
}

static void
dfs (int idx)
{
  Exp* e;
  int i;

  idx = abs (idx);

  assert (0 < idx);
  assert (idx < nexps);

  e = exps + idx;

  if (e->idx) return;

  for (i = 0; i < 3; i++)
    if (ischild (e, i)) dfs (deref (e->child[i]));

  e->idx = ++rexps;
}

static void
cone (void)
{
  int i;
  rexps = 0;
  for (i = 1; i < nexps; i++)
    if (exps[i].ref && !strcmp (exps[i].op, "root")) dfs (i);
}

static void
clean (void)
{
  int i;
  for (i = 1; i < nexps; i++) exps[i].idx = 0;
}

static void
reset (void)
{
  Exp* e;
  int i;

  for (i = 1; i < nexps; i++)
  {
    e      = exps + i;
    e->ref = e->old;
  }
}

static void
print (void)
{
  FILE* file = fopen (tmp, "w");
  int i, j, lit;
  Exp* e;

  if (!file) die ("can not write to '%s'", tmp);

  for (i = 1; i < nexps; i++)
  {
    e = exps + i;

    if (!e->ref) continue;

    if (!e->idx) continue;

    fprintf (file, "%d %s %d", e->idx, e->op, e->width);

    for (j = 0; j < e->childs; j++)
    {
      lit = e->child[j];
      fprintf (file, " %d", ischild (e, j) ? deidx (lit) : lit);
    }

    if (e->name) fprintf (file, " %s", e->name);

    fputc ('\n', file);
  }

  fclose (file);
}

static void
expand (void)
{
  Exp *e, *old;
  int idx, j;

  nexps += maxwidth;

  old   = exps;
  sexps = nexps;
  exps  = calloc (nexps, sizeof *exps);

  for (idx = 1; idx <= maxwidth; idx++)
  {
    exps[idx].ref    = idx;
    exps[idx].idx    = 0;
    exps[idx].old    = 0;
    exps[idx].op     = strdup ("zero");
    exps[idx].width  = idx;
    exps[idx].childs = 0;
    exps[idx].name   = 0;
  }

  memcpy (exps + maxwidth + 1, old + 1, (nexps - maxwidth - 1) * sizeof *exps);

  for (idx = maxwidth + 1; idx < nexps; idx++)
  {
    e = exps + idx;
    if (!e->ref) continue;

    assert (e->ref == idx - maxwidth);
    e->ref += maxwidth;

    for (j = 0; j < e->childs; j++)
      if (ischild (e, j))
      {
        if (e->child[j] < 0)
          e->child[j] -= maxwidth;
        else
          e->child[j] += maxwidth;
      }
  }

  free (old);
}

static int
run (void)
{
  runs++;
  return system (cmd);
}

static int
min (int a, int b)
{
  return a < b ? a : b;
}

int
main (int argc, char** argv)
{
  int i, j, golden, res, changed, rounds, interval, fixed, sign, overwritten;
  Exp* e;

  for (i = 1; i < argc; i++)
  {
    if (!strcmp (argv[i], "-h"))
    {
      printf ("usage: deltabtor [-h][-v] <input> <output> <run>\n");
      exit (0);
    }
    else if (!strcmp (argv[i], "-v"))
      verbose++;
    else if (run_name)
      die ("more than three file names given");
    else if (output_name)
      run_name = argv[i];
    else if (input_name)
      output_name = argv[i];
    else
      input_name = argv[i];
  }

  if (!input_name) die ("<input> missing");

  if (!output_name) die ("<output> missing");

  if (!run_name) die ("<run> missing");

  if (!strcmp (input_name, output_name))
    die ("<input> and <output> are the same");

  if (!strcmp (input_name, run_name)) die ("<input> and <run> are the same");

  if (!strcmp (output_name, run_name)) die ("<output> and <run> are the same");

  nexps = sexps = 1;
  exps          = calloc (sexps, sizeof *exps);

  if (!(input = fopen (input_name, "r"))) die ("can not read '%s'", input_name);

  parse ();

  fclose (input);

  tmp = malloc (100);
  sprintf (tmp, "/tmp/deltabtor%u", (unsigned) getpid ());

  cmd = malloc (strlen (run_name) + strlen (tmp) + 100);
  sprintf (cmd, "%s %s >/dev/null 2>/dev/null", run_name, tmp);

  expand ();

  save ();
  simp ();
  cone ();
  print ();
  clean ();
  reset ();

  golden = run ();
  msg (1, "golden exit code %d", golden);

  rename (tmp, output_name);

  rounds = 0;
  fixed  = 0;

  interval = nexps - maxwidth;

  do
  {
    msg (1, "interval %d", interval);

    do
    {
      changed = 0;
      rounds++;

      msg (2, "round %d", rounds);

      for (i = maxwidth + 1; i < nexps; i += interval)
      {
        for (sign = 1; sign >= -1; sign -= 2)
        {
          overwritten = 0;

          for (j = i; j < i + interval && j < nexps; j++)
          {
            e = exps + j;

            if (!e->ref) continue;

            if (e->ref != j) continue;

            if (!strcmp (e->op, "root")) continue;

            if (!strcmp (e->op, "array")) continue;

            overwritten++;
          }

          if (!overwritten) continue;

          save ();

          for (j = i; j < i + interval && j < nexps; j++)
          {
            e = exps + j;

            if (!e->ref) continue;

            if (e->ref != j) continue;

            if (!strcmp (e->op, "root")) continue;

            if (!strcmp (e->op, "array")) continue;

            e->ref = sign * e->width;
          }

          msg (3,
               "trying to set %d expressions %d .. %d to %s",
               overwritten,
               j - maxwidth,
               min (j - maxwidth + interval, nexps) - 1,
               (sign < 0) ? "all one" : "zero");

          simp ();
          cone ();
          print ();
          clean ();

          res = run ();

          if (res == golden)
          {
            changed = 1;
            fixed += overwritten;

            msg (2, "fixed %d expressions", overwritten);
            rename (tmp, output_name);
            msg (2, "saved %d expressions in '%s'", rexps, output_name);
          }
          else
          {
            msg (3, "restored %d expressions", overwritten);
            reset ();
          }
        }
      }

    } while (changed);

    if (3 < interval && interval < 8)
      interval = 3;
    else if (interval == 3)
      interval = 2;
    else if (interval == 2)
      interval = 1;
    else if (interval == 1)
      interval = 0;
    else
      interval = (interval + 1) / 2;

  } while (interval);

  unlink (tmp);

  msg (2, "%d rounds", rounds);
  msg (2, "%d runs", runs);

  free (tmp);
  free (cmd);

  for (i = 1; i < nexps; i++)
  {
    if (!exps[i].ref) continue;

    free (exps[i].op);
    free (exps[i].name);
  }

  free (exps);
  free (buf);

  msg (1, "fixed %d expressions", fixed);

  return 0;
}
