
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX 256
#define LEN 64
#define TAC_LEN 256

typedef enum {
    END, ID, NUM, PLUS, MINUS, MUL, DIV, MOD, POW,
    LP, RP, ASSIGN, BAD
} Type;

typedef struct {
    Type type;
    char text[LEN];
    int pos;
} Token;

typedef struct {
    char op[LEN], a1[LEN], a2[LEN], res[LEN];
} Quad;

typedef struct {
    char op[LEN], a1[LEN], a2[LEN];
} Triple;

typedef struct {
    char text[LEN];
    int ref;
} Value;

static Token ts[MAX];
static int nt, at;

static Quad q[MAX];
static Triple tr[MAX];

static char tac[MAX][TAC_LEN];
static char err[200];

static int count, tempno;

/* Error handling */
static void error(const char *s) {
    if (!err[0]) {
        snprintf(err, sizeof(err), "%s", s);
    }
}

/* Add a token */
static int add(Type t, const char *s, int n, int p) {
    if (nt >= MAX - 1 || n >= LEN) {
        error("Input contains too many or too-long tokens.");
        return 0;
    }

    ts[nt].type = t;
    memcpy(ts[nt].text, s, n);
    ts[nt].text[n] = '\0';
    ts[nt].pos = p;
    nt++;

    return 1;
}

/* Lexical Analysis */
static int lex(const char *s) {
    int i = 0;

    nt = 0;
    err[0] = '\0';

    while (s[i]) {

        if (isspace((unsigned char)s[i])) {
            i++;
            continue;
        }

        /* Identifier */
        if (isalpha((unsigned char)s[i]) || s[i] == '_') {
            int b = i++;

            while (isalnum((unsigned char)s[i]) || s[i] == '_') {
                i++;
            }

            if (!add(ID, s + b, i - b, b)) {
                return 0;
            }

            continue;
        }

        /* Number */
        if (isdigit((unsigned char)s[i]) ||
            (s[i] == '.' && isdigit((unsigned char)s[i + 1]))) {

            int b = i;
            int dots = 0;

            while (isdigit((unsigned char)s[i]) || s[i] == '.') {
                if (s[i] == '.') {
                    dots++;
                }
                i++;
            }

            if (dots > 1) {
                error("Invalid number: multiple decimal points.");
                return 0;
            }

            if (!add(NUM, s + b, i - b, b)) {
                return 0;
            }

            continue;
        }

        Type t;

        switch (s[i]) {
            case '+': t = PLUS; break;
            case '-': t = MINUS; break;
            case '*': t = MUL; break;
            case '/': t = DIV; break;
            case '%': t = MOD; break;
            case '^': t = POW; break;
            case '(': t = LP; break;
            case ')': t = RP; break;
            case '=': t = ASSIGN; break;

            default:
                snprintf(err, sizeof(err),
                         "Invalid character '%c' at position %d.",
                         s[i], i + 1);
                return 0;
        }

        if (!add(t, s + i, 1, i)) {
            return 0;
        }

        i++;
    }

    ts[nt].type = END;
    ts[nt].text[0] = '\0';
    ts[nt].pos = i;
    nt++;

    return 1;
}

/* Parser utilities */
static Token *peek(void) {
    return &ts[at];
}

static int take(Type t) {
    if (peek()->type == t) {
        at++;
        return 1;
    }

    return 0;
}

static int need(Type t, const char *msg) {
    if (take(t)) {
        return 1;
    }

    snprintf(err, sizeof(err),
             "%s near position %d.",
             msg, peek()->pos + 1);

    return 0;
}

/* Create a value */
static Value val(const char *s, int ref) {
    Value v;

    snprintf(v.text, LEN, "%s", s);
    v.ref = ref;

    return v;
}

/* Convert a value into a triple reference */
static void triple_arg(char *out, Value v) {
    if (v.ref >= 0) {
        snprintf(out, LEN, "(%d)", v.ref);
    } else {
        snprintf(out, LEN, "%s", v.text);
    }
}

/* Intermediate Code Generation */
static Value emit(const char *op, Value a, Value b, int unary) {

    if (count >= MAX) {
        error("Too many generated instructions.");
        return val("?", -1);
    }

    int k = count++;
    char temp[LEN];
    char x[LEN], y[LEN];

    snprintf(temp, LEN, "t%d", ++tempno);

    /* Three Address Code */
    if (unary) {
        snprintf(tac[k], TAC_LEN,
                 "%s = -%s", temp, a.text);
    } else {
        snprintf(tac[k], TAC_LEN,
                 "%s = %s %s %s",
                 temp, a.text, op, b.text);
    }

    /* Quadruple */
    snprintf(q[k].op, LEN, "%s", op);
    snprintf(q[k].a1, LEN, "%s", a.text);

    if (unary) {
        snprintf(q[k].a2, LEN, "-");
    } else {
        snprintf(q[k].a2, LEN, "%s", b.text);
    }

    snprintf(q[k].res, LEN, "%s", temp);

    /* Triple */
    triple_arg(x, a);

    if (unary) {
        snprintf(y, LEN, "-");
    } else {
        triple_arg(y, b);
    }

    snprintf(tr[k].op, LEN, "%s", op);
    snprintf(tr[k].a1, LEN, "%s", x);
    snprintf(tr[k].a2, LEN, "%s", y);

    return val(temp, k);
}

/* Forward declaration */
static Value expression(void);

/* Primary expression */
static Value primary(void) {

    if (peek()->type == ID || peek()->type == NUM) {
        Token t = ts[at++];
        return val(t.text, -1);
    }

    if (take(LP)) {
        Value v = expression();

        if (!err[0]) {
            need(RP, "Expected ')'");
        }

        return v;
    }

    snprintf(err, sizeof(err),
             "Expected identifier, number, or '(' near position %d.",
             peek()->pos + 1);

    return val("?", -1);
}

/* Unary expression */
static Value unary(void);

/* Power expression: right associative */
static Value power(void) {

    Value a = primary();

    if (!err[0] && take(POW)) {
        Value b = unary();

        if (!err[0]) {
            a = emit("^", a, b, 0);
        }
    }

    return a;
}

/* Unary minus */
static Value unary(void) {

    if (take(MINUS)) {
        Value a = unary();

        if (!err[0]) {
            a = emit("uminus", a, val("-", -1), 1);
        }

        return a;
    }

    return power();
}

/* Multiplication, division and modulo */
static Value term(void) {

    Value a = unary();

    while (!err[0]) {

        const char *op = NULL;

        if (take(MUL)) {
            op = "*";
        } else if (take(DIV)) {
            op = "/";
        } else if (take(MOD)) {
            op = "%";
        } else {
            break;
        }

        Value b = unary();

        if (!err[0]) {
            a = emit(op, a, b, 0);
        }
    }

    return a;
}

/* Addition and subtraction */
static Value expression(void) {

    Value a = term();

    while (!err[0]) {

        const char *op = NULL;

        if (take(PLUS)) {
            op = "+";
        } else if (take(MINUS)) {
            op = "-";
        } else {
            break;
        }

        Value b = term();

        if (!err[0]) {
            a = emit(op, a, b, 0);
        }
    }

    return a;
}

/* Generate intermediate code */
static int generate(const char *input) {

    Value result;
    char target[LEN] = "";

    int assignment = 0;

    count = 0;
    tempno = 0;
    at = 0;
    err[0] = '\0';

    if (!lex(input)) {
        return 0;
    }

    /* Check assignment */
    if (peek()->type == ID && ts[at + 1].type == ASSIGN) {

        snprintf(target, LEN, "%s", peek()->text);

        at += 2;
        assignment = 1;
    }

    result = expression();

    if (err[0]) {
        return 0;
    }

    /* Check remaining tokens */
    if (peek()->type != END) {

        snprintf(err, sizeof(err),
                 "Unexpected token '%s' at position %d.",
                 peek()->text, peek()->pos + 1);

        return 0;
    }

    /* Assignment statement */
    if (assignment) {

        int k = count;
        char rhs[LEN];

        if (k >= MAX) {
            error("Too many instructions.");
            return 0;
        }

        snprintf(tac[k], TAC_LEN,
                 "%s = %s", target, result.text);

        snprintf(q[k].op, LEN, "=");
        snprintf(q[k].a1, LEN, "%s", result.text);
        snprintf(q[k].a2, LEN, "-");
        snprintf(q[k].res, LEN, "%s", target);

        /* Triple: (=, target, RHS) */
        triple_arg(rhs, result);

        snprintf(tr[k].op, LEN, "=");
        snprintf(tr[k].a1, LEN, "%s", target);
        snprintf(tr[k].a2, LEN, "%s", rhs);

        count++;
    }

    /* Standalone expression */
    else {

        int k = count;
        char rhs[LEN];

        if (k >= MAX) {
            error("Too many instructions.");
            return 0;
        }

        snprintf(tac[k], TAC_LEN,
                 "Result = %s", result.text);

        snprintf(q[k].op, LEN, "result");
        snprintf(q[k].a1, LEN, "%s", result.text);
        snprintf(q[k].a2, LEN, "-");
        snprintf(q[k].res, LEN, "-");

        triple_arg(rhs, result);

        snprintf(tr[k].op, LEN, "result");
        snprintf(tr[k].a1, LEN, "%s", rhs);
        snprintf(tr[k].a2, LEN, "-");

        count++;
    }

    return 1;
}

/* Display generated code */
static void display(void) {

    int i;

    puts("\n--- THREE ADDRESS CODE ---");

    for (i = 0; i < count; i++) {
        printf("%2d: %s\n", i, tac[i]);
    }

    puts("\n--- QUADRUPLES ---");

    printf("%-6s %-10s %-12s %-12s %-12s\n",
           "Index", "Operator", "Arg1", "Arg2", "Result");

    for (i = 0; i < count; i++) {
        printf("%-6d %-10s %-12s %-12s %-12s\n",
               i, q[i].op, q[i].a1, q[i].a2, q[i].res);
    }

    puts("\n--- TRIPLES ---");

    printf("%-6s %-10s %-12s %-12s\n",
           "Index", "Operator", "Arg1", "Arg2");

    for (i = 0; i < count; i++) {
        printf("%-6d %-10s %-12s %-12s\n",
               i, tr[i].op, tr[i].a1, tr[i].a2);
    }
}

/* Main function */
int main(void) {

    char input[512];

    puts("======================================");
    puts("       INTERMEDIATE CODE GENERATOR");
    puts("======================================");

    puts("Operators: + - * / % ^");
    puts("Assignment: x = expression");
    puts("Type help for examples, or exit to quit.");

    while (1) {

        printf("\nICG> ");

        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }

        input[strcspn(input, "\n")] = '\0';

        if (!input[0]) {
            continue;
        }

        if (!strcmp(input, "exit") ||
            !strcmp(input, "quit")) {
            break;
        }

        if (!strcmp(input, "help")) {

            puts("Examples:");
            puts("a+b*c");
            puts("x=(a+b)*c");
            puts("p=a^b^c");
            puts("total=-price+tax");

            puts("\nPrecedence:");
            puts("Parentheses, ^, unary -, *, /, %, +, -");

            continue;
        }

        if (generate(input)) {
            display();
        } else {
            printf("Error: %s\n",
                   err[0] ? err : "Invalid expression.");
        }
    }

    puts("Goodbye!");

    return 0;
}
