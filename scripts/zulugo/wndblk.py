"""Helpers for .wnd blocks: find a WINDOW block (CHILD line .. matching END) by NAME or by rect."""
def lines(path):
    return open(path, 'rb').read().decode('latin-1').split('\r\n')

def block(ls, key):
    """(start, end) line indexes of the CHILD..END block whose text contains key; end inclusive."""
    for i, l in enumerate(ls):
        if key in l:
            j = i
            while ls[j].strip() != 'WINDOW':
                j -= 1
            ind = len(ls[j]) - len(ls[j].lstrip())
            start = j - 1 if ls[j - 1].strip() == 'CHILD' else j
            k = j + 1
            while not (ls[k].strip() == 'END' and len(ls[k]) - len(ls[k].lstrip()) == ind):
                k += 1
            return start, k
    raise KeyError(key)
