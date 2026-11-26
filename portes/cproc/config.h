/* config.h do cproc no NovaEra (o configure do cproc o gera para o Linux).
 *
 * O driver (cproc) chama, em sequência, ligados por pipes:
 *   nefs-cpp  ->  cproc-qbe  ->  qbe  ->  nefs-as     e depois  nefs-ld
 * Todos rodam dentro do próprio NovaEra. */

static const char target[] = "x86_64-novaera";

/* O começo do programa (_start, em crt0.s) está na libc.a: o nefs-ld o
 * puxa sozinho. Por isso não há "startfiles". */
static const char *const startfiles[] = {0};
static const char *const endfiles[]   = {"-l", "c"};

static const char *const preprocesscmd[] = {
	"nefs-cpp", "-undef",

	/* recursos opcionais do C11 que o cproc ainda não tem */
	"-D", "__STDC_NO_ATOMICS__",
	"-D", "__STDC_NO_COMPLEX__",

	/* marcas de extensão do GCC: ignoradas */
	"-D", "__extension__=",

	"-D", "__STDC_ENDIAN_LITTLE__=1234",
	"-D", "__STDC_ENDIAN_BIG__=4321",
	"-D", "__STDC_ENDIAN_NATIVE__=__STDC_ENDIAN_LITTLE__",
	"-D", "__ORDER_LITTLE_ENDIAN__=__STDC_ENDIAN_LITTLE__",
	"-D", "__ORDER_BIG_ENDIAN__=__STDC_ENDIAN_BIG__",
	"-D", "__BYTE_ORDER__=__STDC_ENDIAN_NATIVE__",
	"-D", "__FLT_EVAL_METHOD__=1",
	"-D", "__BITINT_MAXWIDTH__=64",

	"-D", "__x86_64__",
	"-D", "__amd64__",

	"-D", "__LP64__",
	"-D", "__SIZEOF_LONG__=8",
	"-D", "__SIZEOF_POINTER__=8",
	"-D", "__SIZEOF_SIZE_T__=8",
	"-D", "__SIZEOF_PTRDIFF_T__=8",

	/* o sistema: NovaEra (não é Linux nem Unix) */
	"-D", "__novaera__",
	"-D", "__INT_MAX__=0x7fffffff",
	"-D", "__LONG_LONG_MAX__=0x7fffffffffffffffL",
};

static const char *const codegencmd[]  = {"qbe"};
static const char *const assemblecmd[] = {"nefs-as"};
static const char *const linkcmd[]     = {"nefs-ld"};
