import csv, os

CSV_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'CSVs')

def load_gesture(path):
    gx, gy, gz = [], [], []
    with open(path, newline='') as f:
        for row in csv.DictReader(f):
            gx.append(float(row['Gx']))
            gy.append(float(row['Gy']))
            gz.append(float(row['Gz']))
    energy = lambda v: sum(x**2 for x in v) / len(v)
    return energy(gx), energy(gy), energy(gz)

results = {}
for fname in sorted(os.listdir(CSV_DIR)):
    if not fname.endswith('.csv'):
        continue
    label = '_'.join(fname.replace('.csv','').split('_')[:-1])
    if label not in ('horizontal_shake', 'vertical_shake'):
        continue
    ex, ey, ez = load_gesture(os.path.join(CSV_DIR, fname))
    total = ex + ey + ez
    results.setdefault(label, []).append((fname, ex/total, ey/total, ez/total))

for label in ('horizontal_shake', 'vertical_shake'):
    print(f'=== {label} ===')
    print(f'  {"File":<30} {"Gx%":>6} {"Gy%":>6} {"Gz%":>6}  dominant')
    for fname, px, py, pz in results[label]:
        dom = max([(px,'Gx'),(py,'Gy'),(pz,'Gz')], key=lambda x: x[0])[1]
        if label == 'vertical_shake' and dom == 'Gz':
            flag = '  ANOMALY'
        elif label == 'horizontal_shake' and dom != 'Gz':
            flag = '  ANOMALY'
        else:
            flag = ''
        print(f'  {fname:<30} {px*100:5.1f}% {py*100:5.1f}% {pz*100:5.1f}%  {dom}{flag}')
    print()
