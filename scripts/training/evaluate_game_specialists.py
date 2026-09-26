"""Select by validation only, then evaluate locked game candidates on holdout."""
import json
from pathlib import Path

from game_specialists import OUT, ROOT, evaluate, save_json, sha


def score(report):
    metrics=report['metrics']
    keys=[k for k in metrics if k.endswith('_unseen')] or list(metrics)
    values = [metrics[k]['f1'] for k in keys]
    return sum(values) / len(values)


def main():
    import torch
    output = OUT/'evaluation'
    output.mkdir(exist_ok=True)
    selections = {}
    paths = {
        'delta': {'current_start': OUT/'training/delta_current2/weights/best.pt',
                  'clean_start': OUT/'training/delta_clean/weights/best.pt'},
        'apex': {'current_start': OUT/'training/apex_current/weights/best.pt',
                 'clean_start': OUT/'training/apex_clean/weights/best.pt'},
    }
    # All checkpoint paths must be complete before evaluating any candidate.
    for choices in paths.values():
        for p in choices.values():
            if not p.is_file(): raise FileNotFoundError(p)
            checkpoint=torch.load(p,map_location='cpu',weights_only=False)
            if checkpoint.get('epoch') != -1 or checkpoint.get('optimizer') is not None:
                raise RuntimeError(f'Training has not finalized this checkpoint: {p}')
    for game, choices in paths.items():
        candidates = {'baseline': ROOT/'models/best.pt', 'clean_untrained': ROOT/'yolo26n.pt', **choices}
        reports = {}
        for label, p in candidates.items():
            dest=output/f'{game}_{label}_val.json'
            if dest.exists(): raise FileExistsError(dest)
            evaluate(game,str(p),str(dest),'val')
            reports[label]=json.loads(dest.read_text())
        chosen=max(choices,key=lambda k:score(reports[k]))
        baseline=reports['baseline']['metrics']; selected=reports[chosen]['metrics']
        primary=[k for k in selected if k.endswith('_unseen')] or list(selected)
        protected={split:{metric:selected[split][metric]>=baseline[split][metric]
                           for metric in ['precision','recall']} for split in primary}
        coverage_sufficient=all(selected[k]['objects']>=20 for k in primary)
        selections[game]=dict(chosen=chosen,path=str(choices[chosen]),sha256=sha(choices[chosen]),
                              validation_score=score(reports[chosen]),protected_validation=protected,
                              primary_validation_cohorts=primary,
                              coverage_sufficient=coverage_sufficient,
                              eligible_validation=coverage_sufficient and all(v for p in protected.values() for v in p.values()),
                              note='Selection is a research checkpoint; eligibility is distinct from ranking.')
    # This is written before any candidate test scores are computed.
    save_json(output/'locked-selections.json',selections)
    for game,selection in selections.items():
        for label,p in [('baseline',ROOT/'models/best.pt'),('selected',Path(selection['path']))]:
            dest=output/f'{game}_{label}_test.json'
            if dest.exists(): raise FileExistsError(dest)
            evaluate(game,str(p),str(dest),'test')
    print('validation selection frozen and holdout evaluated',flush=True)


if __name__=='__main__': main()
