"""Measure real Tk interactions in a temporary library, without starting runtime.

Run with PYTHONPATH=python. Times include dispatch and root.update(), rather
than claiming to measure compositor frame time. --profile reproduces the
instrumentation used while diagnosing the original page reconstruction.
"""
import argparse
import cProfile
import json
from pathlib import Path
import platform
import shutil
import statistics
import tempfile
import time
import tkinter as tk
import os
import sys
from desktop_app.test_desktop import require_isolated_gui, run_isolated

from desktop_app.gui import AssistantWindow
from desktop_app.workspace import ProfileRepository
from project_paths import PROJECT_ROOT


def main():
    if not os.environ.get('AIM_GUI_TEST_DESKTOP'):
        raise SystemExit(run_isolated([str(Path(__file__).resolve()), *sys.argv[1:]], cwd=PROJECT_ROOT))
    require_isolated_gui()
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--profile',action='store_true')
    args=parser.parse_args()
    report={'python':platform.python_version(),'platform':platform.platform(),
            'window':'900x740','profiled':args.profile,
            'measurement':'UI dispatch plus Tk update, isolated library, no runtime output',
            'interactions':{}}
    with tempfile.TemporaryDirectory(prefix='desktop-ui-timing-') as directory:
        project=Path(directory)
        header=project/'native/controller_native/aim_response_curve_plugin.h'
        header.parent.mkdir(parents=True)
        shutil.copyfile(PROJECT_ROOT/'native/controller_native/aim_response_curve_plugin.h',header)
        repo=ProfileRepository(project)
        a=repo.create('Apex A','apex');b=repo.duplicate(a,'Apex B')
        host=tk.Tk();host.withdraw()
        root=tk.Toplevel(host);app=AssistantWindow(root,project)
        try:
            app.select_profile(a['id']);root.geometry('900x740+20+20');root.update()
            root.after_cancel(app.poll_id)
            profiler=cProfile.Profile() if args.profile else None
            if profiler:profiler.enable()
            def measure(name,action,count):
                samples=[]
                for i in range(count):
                    start=time.perf_counter();action(i);root.update()
                    samples.append((time.perf_counter()-start)*1000)
                ordered=sorted(samples)
                report['interactions'][name]={'samples_ms':samples,
                    'median_ms':statistics.median(samples),'max_ms':max(samples),
                    'p95_ms':ordered[min(len(ordered)-1,int(len(ordered)*.95))]}
            pages=('device','feedback','curve','assist')
            measure('navigation_including_first_visits',lambda i:app.show_page(pages[i%4]),16)
            app.show_page('assist');root.update()
            measure('parameter_edit',lambda i:app.variables['gamepad.ads.output_limit_x'].set(str(.71+i*.001)),60)
            app.show_page('curve');root.update()
            measure('curve_edit',lambda i:app.curve_editor.move_point(3,.3,.2+i*.001),60)
            measure('warm_navigation',lambda i:app.show_page(pages[i%4]),40)
            measure('profile_switch',lambda i:app.select_profile((a['id'],b['id'])[i%2]),20)
            if profiler:profiler.disable()
            report['retained_pages']=len(app.views)
            report['curve_canvas_items']=len(app.views['curve']['curve_editor'].find_all())
        finally:
            app.closed=True;root.after_cancel(app.poll_id);root.destroy();host.destroy()
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps({name:{key:round(value,2) for key,value in result.items() if key!='samples_ms'}
                      for name,result in report['interactions'].items()},indent=2))


if __name__=='__main__':main()
