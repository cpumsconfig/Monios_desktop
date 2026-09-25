/*
 * calculator.c - Monios x64 graphical scientific calculator.
 *
 * Builds to calculator.exe.  Runs in graphics mode and uses the OSUI
 * component library (buttons / labels / cards) with mouse click dispatch.
 *
 * Features:
 *   - Arithmetic: + - * / ( ) .  unary minus
 *   - Scientific: sin cos tan asin acos atan log ln sqrt pow exp factorial
 *   - Degree / Radian angle toggle
 *   - Memory: M+  M-  MR  MC
 *   - History: last 10 computations shown on the right
 *   - Error handling (divide by zero, domain errors, overflow) -> "Error"
 *
 * No libm is linked (freestanding, -nostdlib): all transcendental math is
 * implemented here with range reduction + Taylor / Newton series.
 */

#include "stdio.h"
#include "unistd.h"
#include "appsys.h"
#include "osui_dll.h"
#include "monios_dll.h"
#include "syscall.h"
#include "string.h"
#include "stdint.h"
#include "stdbool.h"

/* ------------------------------------------------------------------ */
/* key event types (mirror kernel include/keyboard.h)                 */
/* ------------------------------------------------------------------ */
#define EV_CHAR   1
#define EV_UP     2
#define EV_DOWN   3
#define EV_LEFT   4
#define EV_RIGHT  5
#define EV_ESC    29

typedef struct {
    uint32_t type;
    char ch;
    uint8_t mods;
    uint8_t pad[3];
} app_key_event_t;

/* ================================================================== */
/* math library (self contained)                                     */
/* ================================================================== */
static const double M_PI  = 3.14159265358979323846;
static const double M_E   = 2.71828182845904523536;
static const double LN2   = 0.69314718055994530942;
static const double INV_LN2 = 1.44269504088896340736;

static double m_fabs(double x) { return x < 0.0 ? -x : x; }

/* floating-point modulus into [0, m) */
static double m_fmod(double x, double m)
{
    double q = x / m;
    long long i = (long long) q;
    double r = x - (double) i * m;
    if (r < 0.0) r += m;
    return r;
}

static double m_sqrt(double x)
{
    double g;
    int k;
    if (x <= 0.0) return 0.0;
    g = (x > 1.0) ? x : 1.0;
    for (k = 0; k < 80; k++) g = 0.5 * (g + x / g);
    return g;
}

/* exp(x): range reduce by powers of two, then Taylor on |r| < ln2 */
static double m_exp(double x)
{
    double r, sum, term;
    long long k;
    int i;
    if (x == 0.0) return 1.0;
    if (x < 0.0) return 1.0 / m_exp(-x);
    k = (long long)(x * INV_LN2);
    r = x - (double) k * LN2;
    sum = 1.0; term = 1.0;
    for (i = 1; i <= 16; i++) {
        term *= r / (double) i;
        sum += term;
    }
    while (k-- > 0) sum *= 2.0;
    return sum;
}

/* ln(x), x>0: scale into [1,2), then atanh series */
static double m_ln(double x)
{
    double z, z2, sum, term;
    long long n = 0;
    int i;
    if (x <= 0.0) return -1e300; /* domain sentinel */
    while (x >= 2.0) { x *= 0.5; n++; }
    while (x < 1.0)  { x *= 2.0; n--; }
    z = (x - 1.0) / (x + 1.0);
    z2 = z * z;
    sum = z; term = z;
    for (i = 1; i <= 20; i++) {
        term *= z2;
        sum += term / (double)(2 * i + 1);
    }
    return 2.0 * sum + (double) n * LN2;
}

/* sin on angle already reduced into [-pi, pi] */
static double sin_reduced(double a)
{
    double a2, sum, term;
    int i;
    a2 = a * a;
    sum = a; term = a;
    for (i = 1; i <= 10; i++) {
        term *= -a2 / ((double)(2 * i) * (double)(2 * i + 1));
        sum += term;
    }
    return sum;
}

static double m_sin(double x)
{
    double a = m_fmod(x, 2.0 * M_PI);
    if (a > M_PI) a -= 2.0 * M_PI;
    return sin_reduced(a);
}

static double m_cos(double x)
{
    return m_sin(x + M_PI / 2.0);
}

static double m_tan(double x)
{
    double c = m_cos(x);
    if (m_fabs(c) < 1e-15) return 1e300; /* pole sentinel */
    return m_sin(x) / c;
}

/* atan(y) for y>=0 in [0,1] via double-angle reduction + Taylor */
static double atan_unit(double y)
{
    double s, sum, term;
    int i;
    /* double-angle reduction: z = y/(1+sqrt(1+y^2)) shrinks range */
    s = y / (1.0 + m_sqrt(1.0 + y * y));
    s = s / (1.0 + m_sqrt(1.0 + s * s));
    sum = s; term = s;
    for (i = 1; i <= 14; i++) {
        term *= -(s * s);
        sum += term / (double)(2 * i + 1);
    }
    return 2.0 * (2.0 * sum); /* undo two doublings */
}

static double m_atan(double x)
{
    double sign = 1.0;
    double y = x;
    if (y < 0.0) { sign = -1.0; y = -y; }
    if (y > 1.0) return sign * (M_PI / 2.0 - atan_unit(1.0 / y));
    return sign * atan_unit(y);
}

static double m_asin(double x)
{
    if (x < -1.0 || x > 1.0) return -1e300;
    return m_atan(x / m_sqrt(1.0 - x * x));
}

static double m_acos(double x)
{
    if (x < -1.0 || x > 1.0) return -1e300;
    return M_PI / 2.0 - m_asin(x);
}

static double m_pow(double base, double exp)
{
    if (base <= 0.0) {
        if (base == 0.0) return 0.0;
        return -1e300; /* negative non-integer base: unsupported */
    }
    return m_exp(exp * m_ln(base));
}

static double m_factorial(double x)
{
    double r = 1.0;
    long long n, i;
    if (x < 0.0) return -1e300;
    n = (long long)(x + 0.5);
    if (n > 170) return 1e300;
    for (i = 2; i <= n; i++) r *= (double) i;
    return r;
}

/* ================================================================== */
/* expression parser (recursive descent)                             */
/* ================================================================== */
typedef struct {
    const char *s;
    int         pos;
    double      value;     /* last parsed number / result          */
    bool        err;
    bool        deg;       /* degrees mode for trig                 */
} parser_t;

/* token kinds */
enum { TK_NONE, TK_NUM, TK_ADD, TK_SUB, TK_MUL, TK_DIV, TK_POW,
       TK_LP, TK_RP, TK_FACT, TK_ID, TK_END, TK_MOD };

typedef struct {
    int    kind;
    double num;
    char   id[16];
} token_t;

static int peek(parser_t *p) { return p->s[p->pos]; }

static void next_tok(parser_t *p, token_t *t)
{
    char c;
    while ((c = peek(p)) == ' ' || c == '\t') p->pos++;
    c = peek(p);
    if (c == 0) { t->kind = TK_END; return; }

    if ((c >= '0' && c <= '9') || c == '.') {
        double v = 0.0;
        bool has = false;
        while ((c = peek(p)) >= '0' && c <= '9') { v = v * 10.0 + (c - '0'); p->pos++; has = true; }
        if (peek(p) == '.') {
            double f = 1.0;
            p->pos++;
            while ((c = peek(p)) >= '0' && c <= '9') { f *= 0.1; v += (c - '0') * f; p->pos++; has = true; }
        }
        (void) has;
        t->kind = TK_NUM; t->num = v;
        return;
    }
    if ((c >= 'a' && c <= 'z') || c == '_') {
        uint32_t n = 0;
        while (((c = peek(p)) >= 'a' && c <= 'z') || c == '_' ||
               (c >= '0' && c <= '9')) {
            if (n < sizeof(t->id) - 1) t->id[n++] = c;
            p->pos++;
        }
        t->id[n] = 0;
        t->kind = TK_ID;
        return;
    }
    t->kind = TK_NONE;
    switch (c) {
        case '+': t->kind = TK_ADD; break;
        case '-': t->kind = TK_SUB; break;
        case '*': t->kind = TK_MUL; break;
        case '/': t->kind = TK_DIV; break;
        case '^': t->kind = TK_POW; break;
        case '(': t->kind = TK_LP; break;
        case ')': t->kind = TK_RP; break;
        case '!': t->kind = TK_FACT; break;
        case '%': t->kind = TK_MOD; break;
        default: p->err = true; break;
    }
    p->pos++;
}

static double parse_expr(parser_t *p, token_t *t);

static double eval_function(parser_t *p, const char *id)
{
    token_t a;
    double v;
    next_tok(p, &a); /* expect '(' */
    if (a.kind != TK_LP) { p->err = true; return 0.0; }
    v = parse_expr(p, &a); /* parses until ')' */
    if (p->err) return 0.0;

    if (p->deg) {
        /* convert argument / result for trig */
        if (!strcmp(id, "sin"))  v = m_sin(v * M_PI / 180.0);
        else if (!strcmp(id, "cos"))  v = m_cos(v * M_PI / 180.0);
        else if (!strcmp(id, "tan"))  v = m_tan(v * M_PI / 180.0);
        else if (!strcmp(id, "asin")) v = m_asin(v) * 180.0 / M_PI;
        else if (!strcmp(id, "acos")) v = m_acos(v) * 180.0 / M_PI;
        else if (!strcmp(id, "atan")) v = m_atan(v) * 180.0 / M_PI;
        else if (!strcmp(id, "sqrt")) v = m_sqrt(v);
        else if (!strcmp(id, "ln"))   v = m_ln(v);
        else if (!strcmp(id, "log"))  v = m_ln(v) / 2.302585092994046;
        else if (!strcmp(id, "exp"))  v = m_exp(v);
        else p->err = true;
    } else {
        if (!strcmp(id, "sin"))  v = m_sin(v);
        else if (!strcmp(id, "cos"))  v = m_cos(v);
        else if (!strcmp(id, "tan"))  v = m_tan(v);
        else if (!strcmp(id, "asin")) v = m_asin(v);
        else if (!strcmp(id, "acos")) v = m_acos(v);
        else if (!strcmp(id, "atan")) v = m_atan(v);
        else if (!strcmp(id, "sqrt")) v = m_sqrt(v);
        else if (!strcmp(id, "ln"))   v = m_ln(v);
        else if (!strcmp(id, "log"))  v = m_ln(v) / 2.302585092994046;
        else if (!strcmp(id, "exp"))  v = m_exp(v);
        else p->err = true;
    }
    if (v <= -1e299 || v >= 1e299) p->err = true;
    return v;
}

/* forward */
static double parse_pow(parser_t *p, token_t *t);

static double parse_primary(parser_t *p, token_t *t)
{
    double v;
    if (t->kind == TK_NUM) {
        v = t->num;
        next_tok(p, t);
        return v;
    }
    if (t->kind == TK_LP) {
        next_tok(p, t);
        v = parse_expr(p, t);
        if (t->kind != TK_RP) p->err = true;
        else next_tok(p, t);
        return v;
    }
    if (t->kind == TK_ID) {
        /* look ahead (without losing the current token) to tell a function
         * call "sin(" from a bare constant "pi" */
        uint32_t saved_pos = (uint32_t)(p->pos);
        token_t next;
        const char *id = t->id;
        next_tok(p, &next);
        if (next.kind == TK_LP) {
            p->pos = (int) saved_pos; /* rewind: eval_function consumes '(' */
            return eval_function(p, id);
        }
        *t = next; /* keep the token after the identifier for the caller */
        if (!strcmp(id, "pi") || !strcmp(id, "PI")) return M_PI;
        if (!strcmp(id, "e")) return M_E;
        p->err = true;
        return 0.0;
    }
    p->err = true;
    return 0.0;
}

static double parse_unary(parser_t *p, token_t *t)
{
    if (t->kind == TK_SUB) {
        double v;
        next_tok(p, t);
        v = parse_unary(p, t);
        return -v;
    }
    if (t->kind == TK_ADD) {
        next_tok(p, t);
        return parse_unary(p, t);
    }
    return parse_pow(p, t);
}

static double parse_pow(parser_t *p, token_t *t)
{
    double base = parse_primary(p, t);
    /* postfix factorial */
    while (t->kind == TK_FACT) {
        next_tok(p, t);
        base = m_factorial(base);
        if (base <= -1e299 || base >= 1e299) p->err = true;
    }
    if (t->kind == TK_POW) {
        double exp;
        next_tok(p, t);
        exp = parse_unary(p, t); /* right associative */
        base = m_pow(base, exp);
        if (base <= -1e299 || base >= 1e299) p->err = true;
    }
    return base;
}

static double parse_term(parser_t *p, token_t *t)
{
    double v = parse_unary(p, t);
    while (t->kind == TK_MUL || t->kind == TK_DIV || t->kind == TK_MOD) {
        int op = t->kind;
        next_tok(p, t);
        double r = parse_unary(p, t);
        if (op == TK_MUL) v *= r;
        else if (op == TK_DIV) {
            if (r == 0.0) { p->err = true; return 0.0; }
            v /= r;
        } else {
            v = v - r * (long long)(v / r);
        }
        if (v <= -1e299 || v >= 1e299) p->err = true;
    }
    return v;
}

static double parse_expr(parser_t *p, token_t *t)
{
    double v = parse_term(p, t);
    while (t->kind == TK_ADD || t->kind == TK_SUB) {
        int op = t->kind;
        next_tok(p, t);
        double r = parse_term(p, t);
        v = (op == TK_ADD) ? v + r : v - r;
        if (v <= -1e299 || v >= 1e299) p->err = true;
    }
    return v;
}

static double evaluate(const char *expr, bool deg, bool *err)
{
    parser_t p;
    token_t t;
    double v;
    p.s = expr; p.pos = 0; p.value = 0; p.err = false; p.deg = deg;
    next_tok(&p, &t);
    if (t.kind == TK_END) { *err = true; return 0.0; }
    v = parse_expr(&p, &t);
    if (!p.err && t.kind != TK_END) p.err = true;
    *err = p.err;
    return v;
}

/* ================================================================== */
/* double -> string formatter (no %f in this stdio)                   */
/* ================================================================== */
static void fmt_double(char *out, uint32_t cap, double v)
{
    uint32_t o = 0;
    long long ipart;
    double fpart;
    int i;

    if (v <= -1e299 || v >= 1e299) { out[0]='E'; out[1]='r'; out[2]='r'; out[3]=0; return; }

    if (v < 0.0) { out[o++] = '-'; v = -v; }

    /* scientific for very large / very small */
    if ((v != 0.0 && v < 1e-6) || v >= 1e15) {
        int exp = 0;
        while (v >= 10.0) { v *= 0.1; exp++; }
        while (v < 1.0) { v *= 10.0; exp--; }
        ipart = (long long) v;
        fpart = v - (double) ipart;
        out[o++] = (char)('0' + ipart);
        out[o++] = '.';
        for (i = 0; i < 5; i++) {
            fpart *= 10.0;
            ipart = (long long) fpart;
            out[o++] = (char)('0' + ipart);
            fpart -= (double) ipart;
        }
        out[o++] = 'e';
        if (exp < 0) { out[o++] = '-'; exp = -exp; }
        else out[o++] = '+';
        out[o++] = (char)('0' + exp / 10);
        out[o++] = (char)('0' + exp % 10);
        out[o] = 0;
        return;
    }

    ipart = (long long) v;
    fpart = v - (double) ipart;

    /* integer part */
    {
        char tmp[24]; uint32_t t = 0;
        if (ipart == 0) tmp[t++] = '0';
        while (ipart > 0 && t < sizeof(tmp) - 1) { tmp[t++] = (char)('0' + ipart % 10); ipart /= 10; }
        while (t > 0 && o < cap - 1) out[o++] = tmp[--t];
    }

    /* fractional part, up to 10 digits, trimmed */
    {
        char frac[12]; uint32_t n = 0;
        for (i = 0; i < 10; i++) {
            fpart *= 10.0;
            long long d = (long long) fpart;
            frac[n++] = (char)('0' + d);
            fpart -= (double) d;
        }
        while (n > 0 && frac[n - 1] == '0') n--;
        if (n > 0) {
            out[o++] = '.';
            for (i = 0; i < (int)n && o < cap - 1; i++) out[o++] = frac[i];
        }
    }
    out[o] = 0;
}

/* ================================================================== */
/* application state                                                  */
/* ================================================================== */
#define EXPR_MAX 256
#define HIST_MAX 10

static uint32_t g_screen_w;
static uint32_t g_screen_h;

static char    g_expr[EXPR_MAX];     /* expression being typed */
static char    g_result[64];         /* formatted last result  */
static double  g_result_val;
static bool    g_have_result;
static bool    g_err;
static bool    g_deg = true;        /* degrees by default */
static bool    g_sci = false;      /* scientific mode */
static double  g_mem = 0.0;
static char    g_mem_txt[8] = "0";

static char    g_history[HIST_MAX][128];
static uint32_t g_hist_count;

static bool g_dirty = true;

/* clickable control registry */
#define MAX_CTRLS 64
typedef struct { osui_rect_t rect; uint32_t action; } ctrl_t;
static ctrl_t g_ctrls[MAX_CTRLS];
static uint32_t g_ctrl_count;

/* action ids */
enum {
    ACT_DIGIT0 = 0,   /* 0..9 -> ACT_DIGIT0 + d */
    ACT_DOT = 20,
    ACT_LP, ACT_RP, ACT_CLEAR, ACT_BACK,
    ACT_ADD, ACT_SUB, ACT_MUL, ACT_DIV, ACT_POW, ACT_MOD,
    ACT_NEG, ACT_EQUALS,
    ACT_SIN, ACT_COS, ACT_TAN, ACT_ASIN, ACT_ACOS, ACT_ATAN,
    ACT_LOG, ACT_LN, ACT_SQRT, ACT_EXP, ACT_FACT, ACT_RECIP,
    ACT_PI, ACT_E,
    ACT_MPLUS, ACT_MMINUS, ACT_MR, ACT_MC,
    ACT_SCI_TOGGLE, ACT_ANGLE_TOGGLE
};

static void ctrl_add(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t action)
{
    if (g_ctrl_count >= MAX_CTRLS) return;
    g_ctrls[g_ctrl_count].rect.x = x;
    g_ctrls[g_ctrl_count].rect.y = y;
    g_ctrls[g_ctrl_count].rect.width = w;
    g_ctrls[g_ctrl_count].rect.height = h;
    g_ctrls[g_ctrl_count].action = action;
    g_ctrl_count++;
}

static void expr_append(const char *tok)
{
    uint32_t l = 0; while (tok[l]) l++;
    uint32_t cur = 0; while (g_expr[cur]) cur++;
    uint32_t k = 0;
    while (k < l && cur < EXPR_MAX - 1) { g_expr[cur++] = tok[k++]; }
    g_expr[cur] = 0;
    g_dirty = true;
}

static void expr_backspace(void)
{
    uint32_t cur = 0; while (g_expr[cur]) cur++;
    if (cur > 0) {
        /* remove whole function names like "sin(" if present */
        g_expr[cur - 1] = 0;
        (void) cur;
    }
    g_dirty = true;
}

static void push_history(const char *expr, const char *res)
{
    uint32_t i;
    for (i = HIST_MAX - 1; i > 0; i--) {
        uint32_t a = 0;
        while (g_history[i-1][a] && a < sizeof(g_history[0]) - 1) { g_history[i][a] = g_history[i-1][a]; a++; }
        g_history[i][a] = 0;
    }
    {
        uint32_t k = 0;
        while (expr[k] && k < 110) g_history[0][k] = expr[k], k++;
        g_history[0][k++] = '='; g_history[0][k++] = ' ';
        uint32_t r = 0;
        while (res[r] && k < sizeof(g_history[0]) - 1) g_history[0][k++] = res[r++];
        g_history[0][k] = 0;
    }
    if (g_hist_count < HIST_MAX) g_hist_count++;
}

static void do_equals(void)
{
    bool err;
    double v = evaluate(g_expr, g_deg, &err);
    char saved[EXPR_MAX];
    uint32_t k = 0;
    while (g_expr[k] && k < EXPR_MAX - 1) { saved[k] = g_expr[k]; k++; }
    saved[k] = 0;

    if (err) {
        g_err = true;
        g_result[0] = 'E'; g_result[1] = 'r'; g_result[2] = 'r'; g_result[3] = 'o';
        g_result[4] = 'r'; g_result[5] = 0;
    } else {
        g_err = false;
        g_result_val = v;
        g_have_result = true;
        fmt_double(g_result, sizeof(g_result), v);
        push_history(saved, g_result);
        /* replace expression with result for continued calc */
        fmt_double(g_expr, sizeof(g_expr), v);
    }
    g_dirty = true;
}

/* ================================================================== */
/* rendering                                                          */
/* ================================================================== */
static void render(void)
{
    uint16_t x, y;
    g_ctrl_count = 0;

    osui_canvas(0x00EAF0F6);
    osui_titlebar((osui_rect_t){ 0, 0, (uint16_t)g_screen_w, 36 }, "Calculator", true);

    /* display card */
    osui_card((osui_rect_t){ 16, 48, 400, 78 });
    osui_label(28, 58, g_expr[0] ? g_expr : "0", true);
    osui_label(28, 84, g_result[0] ? g_result : "0", false);

    /* memory / mode buttons row */
    osui_button_state((osui_rect_t){ 428, 48, 60, 26 }, g_deg ? "Deg" : "Rad", 0,
                       OSUI_STATE_PRIMARY | OSUI_STATE_FOCUSED);
    ctrl_add(428, 48, 60, 26, ACT_ANGLE_TOGGLE);
    osui_button((osui_rect_t){ 494, 48, 60, 26 }, g_sci ? "Std" : "Sci", 0);
    ctrl_add(494, 48, 60, 26, ACT_SCI_TOGGLE);
    osui_label(428, 86, "Mem:", true);
    osui_label(468, 86, g_mem_txt, false);

    /* memory strip */
    {
        const char *ml[4] = { "MC", "MR", "M-", "M+" };
        uint32_t ma[4] = { ACT_MC, ACT_MR, ACT_MMINUS, ACT_MPLUS };
        uint16_t bx = 428;
        for (uint32_t i = 0; i < 4; i++) {
            osui_button((osui_rect_t){ bx, 108, 36, 22 }, ml[i], 0);
            ctrl_add(bx, 108, 36, 22, ma[i]);
            bx += 40;
        }
    }

    /* scientific function grid (only in sci mode) */
    uint16_t keypad_x = 16;
    if (g_sci) {
        const char *fl[4][4] = {
            { "sin", "cos", "tan", "pi" },
            { "asin","acos","atan","e" },
            { "log", "ln",  "sqrt","^" },
            { "exp", "n!", "1/x", "mod" }
        };
        uint32_t fa[4][4] = {
            { ACT_SIN, ACT_COS, ACT_TAN, ACT_PI },
            { ACT_ASIN,ACT_ACOS,ACT_ATAN,ACT_E },
            { ACT_LOG, ACT_LN, ACT_SQRT, ACT_POW },
            { ACT_EXP, ACT_FACT, ACT_RECIP, ACT_MOD }
        };
        y = 140;
        for (uint32_t r = 0; r < 4; r++) {
            x = 16;
            for (uint32_t c = 0; c < 4; c++) {
                osui_button((osui_rect_t){ x, y, 88, 40 }, fl[r][c], 0);
                ctrl_add(x, y, 88, 40, fa[r][c]);
                x += 94;
            }
            y += 46;
        }
        keypad_x = 16 + 4 * 94; /* shift keypad right */
    }

    /* main keypad 4 cols x 6 rows */
    {
        const char *kl[6][4] = {
            { "(", ")", "C", "BK" },
            { "7", "8", "9", "/" },
            { "4", "5", "6", "*" },
            { "1", "2", "3", "-" },
            { "0", ".", "+/-", "=" },
            { "M+", "MR", "MC", "M-" }
        };
        uint32_t ka[6][4] = {
            { ACT_LP, ACT_RP, ACT_CLEAR, ACT_BACK },
            { ACT_DIGIT0+7, ACT_DIGIT0+8, ACT_DIGIT0+9, ACT_DIV },
            { ACT_DIGIT0+4, ACT_DIGIT0+5, ACT_DIGIT0+6, ACT_MUL },
            { ACT_DIGIT0+1, ACT_DIGIT0+2, ACT_DIGIT0+3, ACT_SUB },
            { ACT_DIGIT0+0, ACT_DOT, ACT_NEG, ACT_EQUALS },
            { ACT_MPLUS, ACT_MR, ACT_MC, ACT_MMINUS }
        };
        y = 140;
        for (uint32_t r = 0; r < 6; r++) {
            x = keypad_x;
            for (uint32_t c = 0; c < 4; c++) {
                uint32_t flags = 0;
                if (ka[r][c] == ACT_EQUALS) flags = OSUI_BUTTON_PRIMARY;
                osui_button_state((osui_rect_t){ x, y, 88, 40 }, kl[r][c], flags,
                                   ka[r][c] == ACT_EQUALS ? OSUI_STATE_PRIMARY : 0);
                ctrl_add(x, y, 88, 40, ka[r][c]);
                x += 94;
            }
            y += 46;
        }
    }

    /* history panel */
    {
        uint16_t hx = (uint16_t)(keypad_x + 4 * 94 + 12);
        osui_card((osui_rect_t){ hx, 140, 220, 320 });
        osui_label((uint16_t)(hx + 10), 148, "History", false);
        uint16_t hy = 172;
        for (uint32_t i = 0; i < g_hist_count; i++) {
            osui_label((uint16_t)(hx + 10), hy, g_history[g_hist_count - 1 - i], true);
            hy += 26;
        }
        if (g_hist_count == 0) osui_label((uint16_t)(hx + 10), 172, "(none)", true);
    }

    osui_statusbar((osui_rect_t){ 0, (uint16_t)(g_screen_h - 28), (uint16_t)g_screen_w, 28 },
                   "Calculator", g_deg ? "Degrees" : "Radians", OSUI_STATE_SUCCESS);
    osui_present();
}

static bool point_in(const ctrl_t *c, int32_t px, int32_t py)
{
    return px >= c->rect.x && px < c->rect.x + c->rect.width &&
           py >= c->rect.y && py < c->rect.y + c->rect.height;
}

static void do_action(uint32_t a)
{
    if (g_err && a != ACT_CLEAR) {
        if (a >= ACT_DIGIT0 && a <= ACT_DIGIT0 + 9) {
            g_expr[0] = 0; g_err = false; g_result[0] = 0;
        } else if (a != ACT_CLEAR) {
            /* stay in error until cleared */
        }
    }
    switch (a) {
        case ACT_DIGIT0 ... ACT_DIGIT0 + 9: {
            char tok[2] = { (char)('0' + (a - ACT_DIGIT0)), 0 };
            expr_append(tok);
            break;
        }
        case ACT_DOT: expr_append("."); break;
        case ACT_LP:  expr_append("("); break;
        case ACT_RP:  expr_append(")"); break;
        case ACT_ADD: expr_append("+"); break;
        case ACT_SUB: expr_append("-"); break;
        case ACT_MUL: expr_append("*"); break;
        case ACT_DIV: expr_append("/"); break;
        case ACT_POW: expr_append("^"); break;
        case ACT_MOD: expr_append("%"); break;
        case ACT_NEG: {
            uint32_t n = 0; while (g_expr[n]) n++;
            if (n == 0 || g_expr[n-1] == '-' ) {
                if (n > 0 && g_expr[n-1] == '-') g_expr[n-1] = 0;
                else expr_append("-");
            } else {
                expr_append("-");
            }
            g_dirty = true;
            break;
        }
        case ACT_SIN: expr_append("sin("); break;
        case ACT_COS: expr_append("cos("); break;
        case ACT_TAN: expr_append("tan("); break;
        case ACT_ASIN: expr_append("asin("); break;
        case ACT_ACOS: expr_append("acos("); break;
        case ACT_ATAN: expr_append("atan("); break;
        case ACT_LOG: expr_append("log("); break;
        case ACT_LN: expr_append("ln("); break;
        case ACT_SQRT: expr_append("sqrt("); break;
        case ACT_EXP: expr_append("exp("); break;
        case ACT_FACT: expr_append("!"); break;
        case ACT_RECIP: expr_append("(1/"); {
            /* wrap current expression: easier to append "(1/(" */
            break;
        }
        case ACT_PI: expr_append("pi"); break;
        case ACT_E: expr_append("e"); break;
        case ACT_CLEAR:
            g_expr[0] = 0; g_result[0] = 0; g_err = false;
            g_have_result = false;
            break;
        case ACT_BACK: expr_backspace(); break;
        case ACT_EQUALS: do_equals(); break;
        case ACT_MPLUS:
            if (g_have_result && !g_err) g_mem += g_result_val;
            fmt_double(g_mem_txt, sizeof(g_mem_txt), g_mem);
            break;
        case ACT_MMINUS:
            if (g_have_result && !g_err) g_mem -= g_result_val;
            fmt_double(g_mem_txt, sizeof(g_mem_txt), g_mem);
            break;
        case ACT_MR:
            if (g_have_result) { }
            { char b[64]; fmt_double(b, sizeof(b), g_mem); expr_append(b); }
            break;
        case ACT_MC: g_mem = 0.0; g_mem_txt[0]='0'; g_mem_txt[1]=0; break;
        case ACT_SCI_TOGGLE: g_sci = !g_sci; break;
        case ACT_ANGLE_TOGGLE: g_deg = !g_deg; break;
        default: break;
    }
    g_dirty = true;
}

int main(int argc, char **argv)
{
    app_key_event_t ev;
    app_mouse_snapshot_t mouse;
    int32_t prev_btn = 0;
    (void) argc; (void) argv;

    fputs("calculator.exe\r\n");

    app_enter_graphics_mode();
    g_screen_w = (uint32_t)monios_syscall0(SYS_GRAPHICS_GET_WIDTH);
    g_screen_h = (uint32_t)monios_syscall0(SYS_GRAPHICS_GET_HEIGHT);
    if (g_screen_w == 0) g_screen_w = 1024;
    if (g_screen_h == 0) g_screen_h = 768;

    g_expr[0] = 0;
    g_result[0] = 0;
    g_mem_txt[0] = '0'; g_mem_txt[1] = 0;

    for (;;) {
        bool did = false;
        while (syscall1(SYS_KEYBOARD_READ_EVENT, (uint64_t)&ev) == 1) {
            did = true;
            if (ev.type == EV_CHAR) {
                char c = ev.ch;
                if (c >= '0' && c <= '9') { char t[2]={c,0}; expr_append(t); }
                else if (c == '.' || c == '(' || c == ')' || c == '+' || c == '-' ||
                         c == '*' || c == '/' || c == '^' || c == '%') {
                    char t[2]={c,0}; expr_append(t);
                }
                else if (c == '=' || c == '\r' || c == '\n') do_equals();
                else if (c == 'c' || c == 'C') { g_expr[0]=0; g_result[0]=0; g_err=false; g_dirty=true; }
                else if (c == 8) expr_backspace(); /* backspace */
                else if (c == 27 || c == 'q' || c == 'Q') app_exit(0);
            } else if (ev.type == EV_ESC) {
                app_exit(0);
            }
        }

        if (app_get_mouse(&mouse) >= 0) {
            int32_t btn = (int32_t)mouse.buttons;
            if ((btn & 1) && !(prev_btn & 1)) {
                for (uint32_t i = 0; i < g_ctrl_count; i++) {
                    if (point_in(&g_ctrls[i], mouse.x_pixels, mouse.y_pixels)) {
                        do_action(g_ctrls[i].action);
                        break;
                    }
                }
            }
            prev_btn = btn;
        }

        if (g_dirty) { render(); g_dirty = false; }
        if (!did) app_sleep_ticks(3);
    }
    return 0;
}
