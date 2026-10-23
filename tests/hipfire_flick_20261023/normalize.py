"""Sampler emits overlapping pre-event history; sort/deduplicate by sample_ns.

tick_id is the drain-time tick, not necessarily the sampled tick. Compare
duplicates excluding this envelope field; preserve originals separately.
"""
import csv
import json
import pathlib
from scan import OUT

def main():
    target=OUT/'ordered';target.mkdir(exist_ok=True)
    summaries=[]
    for p in sorted(OUT.glob('2026*.csv')):
        with p.open(newline='') as f:
            reader=csv.reader(f);header=next(reader);ns=header.index('sample_ns');tick=header.index('tick_id')
            records={};duplicates=conflicts=backwards=0;previous=0
            for row in reader:
                stamp=int(row[ns]);backwards+=stamp<previous;previous=stamp
                row[tick]=''
                packed=','.join(row)
                if stamp in records:
                    duplicates+=1;conflicts+=records[stamp]!=packed
                else:records[stamp]=packed
        with (target/p.name).open('w',newline='') as f:
            f.write(','.join(header)+'\n')
            for stamp in sorted(records):f.write(records[stamp]+'\n')
        summaries.append(dict(session=p.stem,unique=len(records),duplicates=duplicates,conflicting_duplicates=conflicts,backward_rows=backwards))
        print(summaries[-1])
    (OUT/'normalization.json').write_text(json.dumps(summaries,indent=2))

if __name__=='__main__':main()
