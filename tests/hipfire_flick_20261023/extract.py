"""Preserve bounded original telemetry windows before an environment reset."""
import json
import pathlib
from scan import ROOT, OUT

WINDOWS={
 '20261023T055009Z_28860_1':[(367.5,369.2),(850.3,851.9),(2420.3,2421.9),(3052.2,3053.8),(3301.5,3303.0),(3388.5,3390.0)],
 '20261023T080531Z_21112_1':[(819.9,821.3),(867.7,869.3)],
}

def main():
    target=OUT/'windows';target.mkdir(exist_ok=True)
    for session,windows in WINDOWS.items():
        handles=[(a,b,(target/f'{session}-{a:.1f}-{b:.1f}.jsonl').open('wb')) for a,b in windows]
        metadata=None;counts=[0]*len(handles)
        try:
            for path in sorted((ROOT/session).glob('*.jsonl'),key=lambda p:int(p.stem.rsplit('_',1)[1])):
                with path.open('rb') as f:
                    for line in f:
                        r=json.loads(line)
                        if r['type']=='session_metadata':
                            if metadata is None:
                                for _,_,out in handles:out.write(line)
                            metadata=r
                            continue
                        ns=next((r.get(k) for k in ['sample_ns','output_sent_ns','controller_consume_ns','inference_ready_ns','vision_capture_ns','physical_read_ns'] if r.get(k)),0)
                        t=(ns-metadata['clock_steady_ns'])/1e9
                        for i,(a,b,out) in enumerate(handles):
                            if a<=t<=b:out.write(line);counts[i]+=1
                print(path.name,flush=True)
        finally:
            for _,_,out in handles:out.close()
        print(session,counts,flush=True)
    (target/'index.json').write_text(json.dumps(WINDOWS,indent=2))

if __name__=='__main__':main()
