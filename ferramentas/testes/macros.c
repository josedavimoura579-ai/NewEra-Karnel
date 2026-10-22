/* Teste do nefs-cpp: os exemplos de macros do padrão C11 (6.10.3.5) e
 * casos difíceis de #if. O resultado tem que ser igual ao do cpp do GCC.
 * Veja ferramentas/testes/testa-cpp.sh. */
#define x 3
#define f(a) f(x * (a))
#undef x
#define x 2
#define g f
#define z z[0]
#define h g(~
#define m(a) a(w)
#define w 0,1
#define t(a) a
#define p() int
#define q(x) x
#define r(x,y) x ## y
#define str(x) # x
f(y+1) + f(f(z)) % t(t(g)(0) + t)(1);
g(x+(3,4)-w) | h 5) & m
(f)^m(m);
p() i[q()] = { q(1), r(2,3), r(4,), r(,5), r(,) };
char c[2][6] = { str(hello), str() };
/* exemplo 4 */
#undef h
#undef f
#undef x
#undef t
#undef p
#undef str
#define str(s) # s
#define xstr(s) str(s)
#define debug(s, t) printf("x" # s "= %d, x" # t "= %s", \
 x ## s, x ## t)
#define INCFILE(n) vers ## n
#define glue(a, b) a ## b
#define xglue(a, b) glue(a, b)
#define HIGHLOW "hello"
#define LOW LOW ", world"
debug(1, 2);
fputs(str(strncmp("abc\0d", "abc", '\4') // this goes away
 == 0) str(: @\n), s);
xstr(INCFILE(2).h)
glue(HIGH, LOW);
xglue(HIGH, LOW)
/* exemplo 5 */
#define hash_hash # ## #
#define mkstr(a) # a
#define in_between(a) mkstr(a)
#define join(c, d) in_between(c hash_hash d)
char p[] = join(x, y);
/* exemplo 6/7 */
#define t2(x,y,z) x ## y ## z
int j[] = { t2(1,2,3), t2(,4,5), t2(6,,7), t2(8,9,),
 t2(10,,), t2(,11,), t2(,,12), t2(,,) };
#define OBJ_LIKE (1-1)
#define OBJ_LIKE /* white space */ (1-1) /* other */
#define FUNC_LIKE(a) ( a )
#define FUNC_LIKE( a )( /* note the white space */ \
 a /* other stuff on this line
 */ )
#define debug2(...) fprintf(stderr, __VA_ARGS__)
#define showlist(...) puts(#__VA_ARGS__)
#define report(test, ...) ((test)?puts(#test):\
 printf(__VA_ARGS__))
debug2("Flag");
debug2("X = %d\n", x);
showlist(The first, second, and third items.);
report(x>y, "x is %d but y is %d", x, y);
/* recursão e outros */
#define foo foo bar
foo
#define AA BB
#define BB AA
AA BB
#define F(x) x + F(x)
F(F(1))
#define EMPTY
#define LPAREN (
#define RPAREN )
#define G(x, y) x + y
#define ELLIP_FUNC(...) __VA_ARGS__
ELLIP_FUNC(G, LPAREN, 'a', 'b', RPAREN);
#define e(fmt, ...) printf(fmt, ## __VA_ARGS__)
e("a"); e("b", 1, 2);
-EMPTY-x
#define NEG -1
-NEG
#define CAT(a,b) a##b
CAT(1,e) CAT(.,5) CAT(-,=) CAT(<,<=)
#if defined(x) && x == 2 && (1 ? 2 : 3) == 2 && -1 < 0 && -1 > 0u && 'a' == 97 && 0x10 == 16 && 010 == 8
ok1
#endif
#if (2 || 1/0) && !(0 && 1/0)
ok2
#elif 1
ruim
#else
ruim
#endif
#ifdef nao_existe
ruim
#elif !defined nao_existe
ok3
#endif
#if __STDC__ && __STDC_VERSION__ >= 201112L
ok4
#endif
#if (-1)/2 == 0 && (-7)%3 == -1 && (1<<63) < 0 && 0xffffffffffffffff == -1 && (0u - 1) > 0
ok5
#endif
__LINE__ __FILE__
#line 1000 "outro.c"
__LINE__ __FILE__
