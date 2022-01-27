from collections import defaultdict
import json
broadcastersFile = open('broadcas.txt',encoding="ISO-8859-1")
frequenciesFile = open('B21all00.TXT')

broadcasters = {}
for broadcaster in broadcastersFile:
    if broadcaster[0] != ';':
        k, v = broadcaster.rstrip().split(" ", 1)
        broadcasters[k] = v

frequencies = defaultdict(set)
for frequency in frequenciesFile:
    if frequency[0] != ';':
        f = int(frequency[:5])
        b = frequency[117:120]
        frequencies[f].add(b)

for k in frequencies.keys():
    frequencies[k] = "\n".join(sorted(broadcasters[x] for x in frequencies[k]))

frequenciesSorted = []
for k, v in sorted(frequencies.items()):
    frequenciesSorted.append({"f": k*1000, "d": v, "m": "AM"})

json.dump(frequenciesSorted, open('shortwavestations.json','w'), indent=4)