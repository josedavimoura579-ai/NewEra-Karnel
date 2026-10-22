# Compara dois objetos ELF64 (o do GNU as e o do nefs-as): o conteúdo das
# seções, as relocações (com os destinos normalizados) e os símbolos globais.
# Uso: python3 compara_o.py gnu.o nefs.o   (código 0 = iguais)
import sys, struct
def le(nome):
    d = open(nome,'rb').read()
    shoff, = struct.unpack_from('<Q', d, 40); shnum, shstr = struct.unpack_from('<HH', d, 60)
    secs = []
    for i in range(shnum):
        nm,tp,fl,ad,off,sz,lk,inf,al,es = struct.unpack_from('<IIQQQQIIQQ', d, shoff+64*i)
        secs.append(dict(nm=nm,tp=tp,fl=fl,off=off,sz=sz,lk=lk,inf=inf,al=al))
    so = secs[shstr]['off']
    def cstr(o): return d[o:d.index(b'\0',o)].decode()
    for s in secs: s['nome'] = cstr(so+s['nm'])
    sts = [s for s in secs if s['tp']==2]
    syms = []
    st = sts[0] if sts else dict(sz=0, off=0); strt = secs[st['lk']]['off'] if sts else 0
    for i in range(st['sz']//24):
        nm,info,oth,shn,val,sz = struct.unpack_from('<IBBHQQ', d, st['off']+24*i)
        syms.append(dict(nome=cstr(strt+nm), bind=info>>4, tipo=info&15, shn=shn, val=val, sz=sz))
    def alvo(sym, ad):
        if sym['tipo']==3: return (secs[sym['shn']]['nome'], ad)
        if sym['bind']==0 and 0<sym['shn']<0xff00: return (secs[sym['shn']]['nome'], sym['val']+ad)
        return (sym['nome'], ad)
    r = {}
    for s in secs:
        if not (s['fl'] & 2): continue
        cont = d[s['off']:s['off']+s['sz']] if s['tp']!=8 else None
        r[s['nome']] = dict(tp=s['tp'], fl=s['fl'], al=s['al'], sz=s['sz'], cont=cont, rel=[])
    for s in secs:
        if s['tp']!=4: continue
        alvo_sec = secs[s['inf']]['nome']
        if alvo_sec not in r: continue
        for i in range(s['sz']//24):
            off, info, ad = struct.unpack_from('<QQq', d, s['off']+24*i)
            r[alvo_sec]['rel'].append((off, info&0xffffffff, alvo(syms[info>>32], ad)))
    for v in r.values(): v['rel'].sort()
    glob = sorted((x['nome'], secs[x['shn']]['nome'] if 0<x['shn']<0xff00 else x['shn'], x['val'], x['tipo'], x['sz'], x['bind']) for x in syms if x['bind']!=0)
    return r, glob
a, ga = le(sys.argv[1]); b, gb = le(sys.argv[2])
ok = True
for k in sorted(set(a)|set(b)):
    if k not in a or k not in b: print('  seção só num lado:', k); ok=False; continue
    x, y = a[k], b[k]
    for c in ('tp','fl','al','sz'):
        if x[c]!=y[c]: print(f'  {k}: {c} {x[c]} != {y[c]}'); ok=False
    if x['cont']!=y['cont'] and x['cont'] is not None and y['cont'] is not None:
        n = min(len(x['cont']),len(y['cont']))
        i = next((i for i in range(n) if x['cont'][i]!=y['cont'][i]), n)
        print(f'  {k}: bytes diferentes a partir de 0x{i:x}: {x["cont"][i:i+12].hex()} != {y["cont"][i:i+12].hex()}'); ok=False
    if x['rel']!=y['rel']:
        dx=[r for r in x['rel'] if r not in y['rel']][:3]; dy=[r for r in y['rel'] if r not in x['rel']][:3]
        print(f'  {k}: relocações diferentes: {dx} != {dy}'); ok=False
if ga!=gb:
    print('  globais:', [g for g in ga if g not in gb][:3], '!=', [g for g in gb if g not in ga][:3]); ok=False
sys.exit(0 if ok else 1)
