"""Plot original observations and check the carried-acquisition blend equation.

This is arithmetic reconstruction of an identified branch, not a closed-loop
simulation or a replay of all hidden native controller state.
"""
import json
import pathlib
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from scan import OUT

def load(path):
    samples={};observations={};delivered={};meta=None
    for line in path.open():
        r=json.loads(line)
        if r['type']=='session_metadata':meta=r
        elif r['type']=='controller_sample':samples[r['sample_ns']]=r
        elif r['type']=='committed_capture_observation':observations[r['frame_id']]=r
        elif r['type']=='delivered_control_sample':delivered[r['control']['applied_at_ns']]=r
    return meta,sorted(samples.values(),key=lambda r:r['sample_ns']),list(observations.values()),list(delivered.values())

def main():
    path=next((OUT/'windows').glob('*080531*867.7*'))
    meta,samples,observations,delivered=load(path)
    base=meta['clock_steady_ns']
    elapsed=lambda r:(r['sample_ns']-base)/1e9
    chosen=[r for r in samples if 868.504<=elapsed(r)<=868.519]
    delivery_by_time={r['control']['applied_at_ns']:r['control'] for r in delivered}
    checks=[]
    for r in chosen:
        m,a=r['physical_x'],r['ai_x']
        u=max(0,min(1,(abs(m)-.15)/.15));w=u*u*(3-2*u)
        predicted=a+(m-a)*w
        actual_delivery=delivery_by_time[r['sample_ns']]
        assert actual_delivery['output_delivered'] and not actual_delivery['output_disabled']
        assert actual_delivery['final_right'][0]==r['final_x']
        checks.append(dict(local_elapsed_s=elapsed(r),manual=m,ai=a,actual=r['final_x'],weight=w,predicted=predicted,error=predicted-r['final_x']))
    assert len(checks)==4
    assert max(abs(r['error']) for r in checks)<1e-5
    dt=(chosen[-1]['sample_ns']-chosen[0]['sample_ns'])/1e9
    summary=dict(source=str(path),branch='BodyLock carried acquisition, opposing manual; smooth 15%-30% activity',checks=checks,
        interval_ms=dt*1000,manual_delta=chosen[-1]['physical_x']-chosen[0]['physical_x'],
        ai_delta=chosen[-1]['ai_x']-chosen[0]['ai_x'],final_delta=chosen[-1]['final_x']-chosen[0]['final_x'],
        same_target=sorted(set(r['controller_target_track_id'] for r in chosen)),
        all_cue=all(r['enemy_cue_current'] for r in chosen),all_firing=all(r['final_fire_button'] for r in chosen),
        output_delivered=all(r['output_delivered'] for r in chosen),independent_delivery_records_matched=len(chosen),
        limitations=['No video correspondence or world/camera ground truth.', 'Recorded samples are not every controller tick.', 'Only the identified algebraic branch is reconstructed.'])
    scope=[r for r in samples if 868.5371<=elapsed(r)<=868.5418]
    assert len(scope)==2
    ratios=[r['ai_x']/r['requested_assist_x'] for r in scope]
    assert abs(ratios[0]-.7)<1e-5 and abs(ratios[1]-1)<1e-5
    summary['hipfire_to_ads']={
        'samples':[dict(elapsed_s=elapsed(r),manual=r['physical_x'],requested=r['requested_assist_x'],ai=r['ai_x'],final=r['final_x'],lt=r['left_trigger'],target=r['controller_target_track_id']) for r in scope],
        'interval_ms':(scope[1]['sample_ns']-scope[0]['sample_ns'])/1e6,
        'final_delta':scope[1]['final_x']-scope[0]['final_x'],
        'requested_delta':scope[1]['requested_assist_x']-scope[0]['requested_assist_x'],
        'output_to_requested_ratios':ratios,
    }
    (OUT/'handoff-reconstruction.json').write_text(json.dumps(summary,indent=2))
    print(json.dumps(summary,indent=2))
    plt.rcParams['font.sans-serif']=['Microsoft YaHei','DejaVu Sans']
    plt.rcParams['axes.unicode_minus']=False
    for label,pattern,start,end,origin in [('162000','*080531*867.7*',868.28,868.84,868.4),('144511','*055009*3301.5*',3301.9,3302.5,3302.0)]:
        m,rows,obs,delivery=load(next((OUT/'windows').glob(pattern)))
        clock=m['clock_steady_ns'];t=lambda r:(r['sample_ns']-clock)/1e9
        rows=[r for r in rows if start<=t(r)<=end]
        x=[(t(r)-origin)*1000 for r in rows]
        fig,axes=plt.subplots(3,1,figsize=(11,7),sharex=True,gridspec_kw={'height_ratios':[2,1.5,1]})
        for k,name,c in [('physical_x','实际手动 X','#78716c'),('ai_x','仲裁前 AI 提议','#0284c7'),('final_x','实际最终输出 X','#dc2626')]:
            axes[0].plot(x,[r[k] for r in rows],label=name,color=c,linewidth=1.8)
        axes[0].set_ylabel('摇杆 [-1, 1]');axes[0].legend(ncol=3,fontsize=9)
        axes[1].plot(x,[r['observed_error_x'] if r['has_target'] else float('nan') for r in rows],color='#7c3aed',label='目标误差 X（正值在准星右边）')
        axes[1].set_ylabel('像素');axes[1].legend(fontsize=9)
        axes[2].step(x,[r['left_trigger'] for r in rows],where='post',label='LT / ADS 输入',color='#059669')
        axes[2].step(x,[int(r['final_fire_button']) for r in rows],where='post',label='最终开火按钮',color='#d97706',linestyle='--')
        axes[2].set_ylabel('输入');axes[2].set_xlabel('相对参考采样时间（毫秒）');axes[2].legend(ncol=2,fontsize=9)
        for ax in axes:ax.axhline(0,color='#a8a29e',lw=.7);ax.grid(alpha=.2)
        if label=='162000':
            for ax in axes:ax.axvspan(104.632,118.567,color='#f59e0b',alpha=.17)
        fig.suptitle(f'2026-10-23 {label[:2]}:{label[2:4]}:{label[4:]} · 原始日志轨迹（去重排序）',fontsize=14)
        fig.tight_layout();fig.savefig(OUT/f'case-{label}.png',dpi=150);plt.close(fig)

if __name__=='__main__':main()
