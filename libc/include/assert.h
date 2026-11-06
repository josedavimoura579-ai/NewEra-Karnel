/* nlibc - assert.h (pode ser incluído mais de uma vez, com ou sem NDEBUG) */
#undef assert
#ifdef NDEBUG
#define assert(x) ((void)0)
#else
void __assert_falhou(const char *expr, const char *arquivo, int linha);
#define assert(x) ((x) ? (void)0 : __assert_falhou(#x, __FILE__, __LINE__))
#endif
