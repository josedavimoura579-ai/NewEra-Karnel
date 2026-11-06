# Compara duas saídas de pré-processador token a token (os espaços não contam).
# Uso: python3 compara_tokens.py gcc.txt nefs.txt
import re, sys
def toks(f):
    s=open(f).read()
    return re.findall(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[A-Za-z_]\w*|\.?\d(?:[eEpP][+-]|[\w.])*|<<=|>>=|\.\.\.|->|\+\+|--|<<|>>|<=|>=|==|!=|&&|\|\||[*/%+\-&^|]=|##|\S', s)
a=toks(sys.argv[1]); b=toks(sys.argv[2])
for i,(x,y) in enumerate(zip(a,b)):
    if x!=y: print('DIFERENTE no token',i, a[max(0,i-6):i+6], '\n             nefs:', b[max(0,i-6):i+6]); sys.exit(1)
print('iguais' if len(a)==len(b) else f'tamanhos diferentes {len(a)} {len(b)}', len(a), 'tokens')
